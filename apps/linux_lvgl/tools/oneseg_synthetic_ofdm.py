"""Known-framing generic OFDM teaching receiver. No RF, TV, TMCC or FEC.

All observations are computed from generated IQ. The loop is a teaching model,
not gr-isdbt instrumentation. Virtual symbol time is distinct from wall time.
"""
from collections import deque
import importlib.util
import math
import os
from pathlib import Path
import signal
import sys
import threading
import time
import numpy as np


def sibling(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name+'.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class OfdmReference:
    rate = 64000000/63

    def __init__(self, settings):
        self.p = settings
        self.n = 2**(7+settings['mode'])
        self.cp = self.n//settings['guard_denominator']
        self.length = self.n+self.cp
        active = 108*2**(settings['mode']-1)
        self.offsets = np.r_[np.arange(-active//2, 0), np.arange(1, active//2+1)]
        self.bins = self.offsets % self.n
        self.pilot_mask = np.arange(active) % 12 == 0
        self.data_mask = ~self.pilot_mask
        self.pilot_offsets = self.offsets[self.pilot_mask]
        self.side = int(math.sqrt(settings['constellation']))
        self.scale = math.sqrt(2*(settings['constellation']-1)/3)
        self.cache = {}
        self.rng = np.random.default_rng(settings['synthetic_seed']+17)
        self.pilots = np.random.default_rng(settings['synthetic_seed']+71).choice([-1., 1.], self.pilot_mask.sum())
        self.phase_frequencies = self.rng.uniform(50, 5000, 8)
        self.phase_angles = self.rng.uniform(-math.pi, math.pi, 8)
        self.index = 0
        self.position = 0.
        self.estimate = self.integrator = self.nco_phase = 0.
        self.clock = settings['sample_clock_ppm']*1e-6
        self.clock_integrator = 0.
        self.channel_state = self.previous_pilots = None
        self.integer = self.candidate = self.confirmations = 0
        self.moments = deque(maxlen=settings['evm_window'])
        self.history = np.empty(0, np.complex128)
        self.dc_history = np.zeros(31, np.complex128)
        self.fir = None
        self.fir_history = np.empty(0, np.complex128)
        if settings['cutoff']:
            attenuation, transition = settings['attenuation'], settings['transition']
            size = int(math.ceil(max(1, (attenuation-8)/(2.285*2*math.pi*transition/self.rate))))+1
            size += (size+1)%2
            if size > 8192: raise ValueError('Synthetic FIR exceeds 8192 taps')
            beta = .1102*(attenuation-8.7) if attenuation > 50 else .5842*max(0, attenuation-21)**.4+.07886*max(0, attenuation-21)
            x = np.arange(size)-(size-1)/2
            self.fir = 2*settings['cutoff']/self.rate*np.sinc(2*settings['cutoff']/self.rate*x)*np.kaiser(size, beta)
            self.fir /= self.fir.sum()
            self.fir_history = np.zeros(size-1, np.complex128)
        self.alpha, self.beta = self.coefficients(settings['carrier_bandwidth'], settings['damping'])
        self.ta, self.tb = self.coefficients(settings['timing_bandwidth'], settings['damping'])

    @staticmethod
    def coefficients(bandwidth, damping):
        denominator = 1+2*damping*bandwidth+bandwidth**2
        return 4*damping*bandwidth/denominator, 4*bandwidth**2/denominator

    def symbol(self, index):
        if index not in self.cache:
            rng = np.random.default_rng(np.random.SeedSequence([self.p['synthetic_seed'], index % 2**32]))
            real = rng.integers(0, self.side, len(self.bins))
            imag = rng.integers(0, self.side, len(self.bins))
            values = ((2*real-(self.side-1))+1j*(2*imag-(self.side-1)))/self.scale
            values[self.pilot_mask] = self.pilots
            frequency = np.zeros(self.n, np.complex128)
            frequency[self.bins] = values
            useful = np.fft.ifft(frequency)*math.sqrt(self.n)
            self.cache[index] = (np.r_[useful[-self.cp:], useful], values, real, imag)
        return self.cache[index]

    def waveform(self, positions):
        indices = np.floor(positions/self.length).astype(np.int64)
        output = np.empty(len(positions), np.complex128)
        for index in np.unique(indices):
            selected = indices == index
            local = positions[selected]-index*self.length
            wave = np.r_[self.symbol(int(index))[0], self.symbol(int(index)+1)[0][0]]
            grid = np.arange(self.length+1)
            output[selected] = np.interp(local, grid, wave.real)+1j*np.interp(local, grid, wave.imag)
        return output

    def step(self):
        p, n, cp = self.p, self.n, self.cp
        adc = self.position+np.arange(self.length)*(1+self.clock)
        positions = adc/(1+p['synthetic_clock_ppm']*1e-6)
        t = positions/self.rate
        carrier_phase = 2*math.pi*(p['synthetic_cfo_hz']*t+.5*p['synthetic_drift_hz_s']*t*t)
        carrier_phase += p['synthetic_phase_noise']*math.sqrt(2/8)*np.cos(2*math.pi*t[:, None]*self.phase_frequencies+self.phase_angles).sum(axis=1)
        waveform = self.waveform(positions-p['synthetic_timing_offset'])
        echo = p['synthetic_echo_gain']*np.exp(1j*math.radians(p['synthetic_echo_phase']))
        if echo:
            waveform += echo*self.waveform(positions-p['synthetic_timing_offset']-p['synthetic_echo_delay'])
        noiseless = waveform*np.exp(1j*carrier_phase)
        power = max(float(np.mean(abs(noiseless)**2)), 1e-20)
        noise = math.sqrt(power/10**(p['synthetic_snr_db']/10)/2)*(self.rng.normal(size=self.length)+1j*self.rng.normal(size=self.length))
        received = noiseless+noise
        gain = 10**(p['synthetic_iq_gain_db']/20)
        theta = math.radians(p['synthetic_iq_phase_degrees'])
        received = received.real+1j*gain*(received.real*math.sin(theta)+received.imag*math.cos(theta))
        # Fixed known DC spur; the training DC control uses a 32-point mean.
        received += .02+.01j
        if p['remove_dc']:
            extended = np.r_[self.dc_history, received]
            mean = np.convolve(extended, np.ones(32)/32, mode='valid')
            self.dc_history = extended[-31:]
            received -= mean
        compensation = 10**(p['iq_gain_db']/20)
        angle = math.radians(p['iq_phase_degrees'])
        received = (received.real+1j*(received.imag/compensation-received.real*math.sin(angle))/math.cos(angle))*p['digital_gain']
        if self.fir is not None:
            extended = np.r_[self.fir_history, received]
            received = np.convolve(extended, self.fir, mode='valid')
            self.fir_history = extended[-len(self.fir)+1:]
        self.history = np.r_[self.history, received][-3*self.length:]
        fractional = float(np.angle(np.vdot(received[:cp], received[n:n+cp])))
        useful = received[cp:cp+n]
        fractional_fft = np.fft.fft(useful*np.exp(-1j*fractional*np.arange(n)/n))/math.sqrt(n)
        candidates = np.arange(-p['integer_search'], p['integer_search']+1)
        pilot_bins = self.bins[self.pilot_mask]
        scores = abs((fractional_fft[(pilot_bins[None, :]+candidates[:, None]) % n]*self.pilots.conj()).sum(axis=1))
        candidate = int(candidates[np.argmax(scores)])
        self.confirmations = self.confirmations+1 if candidate == self.candidate else 1
        self.candidate = candidate
        if self.confirmations >= p['offset_confirmation']: self.integer = candidate
        observed = fractional+2*math.pi*self.integer
        prior, previous_integrator = self.estimate, self.integrator
        prediction = prior+previous_integrator
        # Current FFT uses the previous control; the update feeds next symbol.
        frequency = np.fft.fft(useful*np.exp(-1j*(self.nco_phase+prior*(cp+np.arange(n))/n)))/math.sqrt(n)
        pilot_channel = frequency[pilot_bins]/self.pilots
        raw_channel = np.interp(self.offsets, self.pilot_offsets, pilot_channel.real)+1j*np.interp(self.offsets, self.pilot_offsets, pilot_channel.imag)
        average = p['channel_average']
        self.channel_state = raw_channel if self.channel_state is None else (raw_channel+(average-1)*self.channel_state)/average
        denominator = abs(self.channel_state)**2+p['equalizer_floor']
        equalized = np.divide(frequency[self.bins]*self.channel_state.conj(), denominator,
                              out=np.zeros(len(self.bins), np.complex128), where=denominator > 1e-12)
        if self.previous_pilots is not None:
            delta_phase = np.unwrap(np.angle(pilot_channel*self.previous_pilots.conj()))
            slope = float(np.polyfit(self.pilot_offsets, delta_phase, 1)[0])
            clock_error = -slope*n/(2*math.pi*self.length)
            self.clock_integrator += self.tb*clock_error
            self.clock = float(np.clip(self.clock+self.clock_integrator+self.ta*clock_error, -.001, .001))
        self.previous_pilots = pilot_channel
        error = observed-prior
        self.integrator += self.beta*error
        self.estimate = prior+self.integrator+self.alpha*error
        self.nco_phase = math.remainder(self.nco_phase+prior*self.length/n, 2*math.pi)
        reference = self.symbol(self.index)
        data = equalized[self.data_mask]
        ideal = reference[1][self.data_mask]
        indices_i = np.clip(np.rint((data.real*self.scale+self.side-1)/2), 0, self.side-1).astype(np.uint8)
        indices_q = np.clip(np.rint((data.imag*self.scale+self.side-1)/2), 0, self.side-1).astype(np.uint8)
        real = reference[2][self.data_mask].astype(np.uint8)
        imag = reference[3][self.data_mask].astype(np.uint8)
        gray = lambda value: value ^ (value >> 1)
        bit_errors = int(np.unpackbits(gray(real)^gray(indices_i)).sum()+np.unpackbits(gray(imag)^gray(indices_q)).sum())
        bits = len(data)*int(math.log2(p['constellation']))
        self.moments.append((float(np.sum(abs(data-ideal)**2)), float(np.sum(abs(ideal)**2)), bits, bit_errors))
        energy = sum(row[0] for row in self.moments)
        reference_energy = sum(row[1] for row in self.moments)
        total_bits = sum(row[2] for row in self.moments)
        total_errors = sum(row[3] for row in self.moments)
        self.index += 1
        self.position += self.length*(1+self.clock)
        for index in list(self.cache):
            if index < self.index-8 or index > self.index+8: del self.cache[index]
        phase = dict(schema=2, symbol=self.index, prior=prior, observed=observed, corrected=self.estimate,
                     correction=self.estimate-prior, residual=observed-self.estimate,
                     nco_phase=self.nco_phase, frequency_hz=self.estimate*self.rate/n/(2*math.pi), sampling_ppm=self.clock*1e6,
                     prediction=prediction, integrator=previous_integrator, alpha=self.alpha, beta=self.beta)
        truth = [float(np.mean(carrier_phase[n:n+cp]-carrier_phase[:cp])),
                 math.sqrt(energy/max(reference_energy, 1e-30)), total_errors/total_bits,
                 total_bits, total_errors, float(np.mean(t)), p['synthetic_snr_db'], p['synthetic_clock_ppm']]
        return phase, truth, data, self.channel_state, self.history


def reference_record(protocol, phase, truth):
    base = protocol.phase_record(phase).decode('ascii')
    values = [float(value) for value in truth]
    if len(values) != 8 or any(not math.isfinite(value) or abs(value) > 1e9 for value in values):
        raise ValueError('Invalid synthetic reference observation')
    if not 0 <= values[2] <= 1 or values[3] < 1 or not 0 <= values[4] <= values[3] or abs(values[2]-values[4]/values[3]) > 1e-9:
        raise ValueError('Inconsistent synthetic bit reference')
    record = (base.replace('TM_OFDM2 ', 'TM_SYNTH1 ', 1)[:-1]+' '+' '.join(format(value, '.17g') for value in values)+'\x1f').encode('ascii')
    if len(record) >= 1024: raise ValueError('Synthetic observation exceeds atomic budget')
    return record


def run(arguments):
    dsp = sibling('oneseg_ofdm_dsp')
    measurements = sibling('oneseg_ofdm_measurements')
    protocol = sibling('oneseg_ofdm_diagnostics')
    model = OfdmReference(dsp.read_settings(OfdmReference.rate))
    if arguments.check_backend: return 0
    stop = threading.Event()
    old_handlers = {value: signal.signal(value, lambda *_: stop.set()) for value in (signal.SIGTERM, signal.SIGINT)}
    if arguments.ready_fd is not None:
        os.write(arguments.ready_fd, b'R')
        os.close(arguments.ready_fd)
        arguments.ready_fd = None
        def eof():
            try:
                while sys.stdin.buffer.read(8192): pass
            finally: stop.set()
        threading.Thread(target=eof, daemon=True).start()
    began = time.monotonic()
    try:
        while not stop.is_set() and (not arguments.seconds or time.monotonic()-began < arguments.seconds):
            phase, truth, data, channel, history = model.step()
            os.write(2, reference_record(protocol, phase, truth))
            if model.index % 4 == 0:
                os.write(2, measurements.record(1, model.index, model.rate, float(np.mean(abs(history)**2)), measurements.spectrum(history, model.rate)))
                chosen = data[np.linspace(0, len(data)-1, min(64, len(data)), dtype=int)]
                os.write(2, measurements.record(2, model.index, model.rate, truth[1], np.column_stack((chosen.real, chosen.imag))))
                os.write(2, measurements.record(3, model.index, model.rate, 0., measurements.channel(channel)))
                os.write(2, measurements.record(5, model.index, model.rate, float(np.min(abs(channel))), measurements.channel_phase(channel)))
                if len(history) >= 2*model.length:
                    os.write(2, measurements.record(4, model.index, model.rate, 0., measurements.cp_correlation(history, model.n, model.p['guard_denominator'])))
            stop.wait(.05) # Virtual symbol time is deliberately independent.
    except BrokenPipeError:
        pass
    finally:
        for value, handler in old_handlers.items(): signal.signal(value, handler)
    return 0
