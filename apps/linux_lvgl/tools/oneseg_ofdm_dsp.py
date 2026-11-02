"""Validated OFDM experiment front end. Qualified defaults are an exact bypass."""
import math
import os


def read_settings(sample_rate, environment=None):
    environment = os.environ if environment is None else environment
    def number(key, default, low, high, integer=False):
        value = float(environment.get('TRAIL_MATE_OFDM_'+key, default))
        if not math.isfinite(value) or value < low or value > high or integer and value != int(value):
            raise ValueError('Invalid OFDM experiment parameter: '+key)
        return int(value) if integer else value
    result = dict(
        digital_gain=number('DIGITAL_GAIN', 1, .01, 16),
        iq_gain_db=number('IQ_GAIN_DB', 0, -12, 12),
        iq_phase_degrees=number('IQ_PHASE_DEGREES', 0, -45, 45),
        remove_dc=bool(number('REMOVE_DC', 0, 0, 1, True)),
        cutoff=number('FILTER_CUTOFF_HZ', 0, 0, sample_rate/2),
        transition=number('FILTER_TRANSITION_HZ', 0, 0, sample_rate/2),
        attenuation=number('FILTER_ATTENUATION_DB', 0, 0, 120),
        sample_clock_ppm=number('SAMPLE_CLOCK_PPM', 0, -500, 500),
        channel_average=number('CHANNEL_AVERAGE', 1, 1, 32, True),
        equalizer_floor=number('EQUALIZER_FLOOR', 0, 0, 1),
        evm_window=number('EVM_WINDOW', 128, 16, 1024, True),
        carrier_bandwidth=number('CARRIER_BW', 3.14159/100, .00001, .5),
        timing_bandwidth=number('TIMING_BW', 3.14159/10000, .000001, .1),
        damping=number('DAMPING', math.sqrt(2)/2, .1, 3),
        integer_search=number('INTEGER_SEARCH', 10, 1, 32, True),
        offset_confirmation=number('OFFSET_CONFIRMATION', 4, 1, 64, True),
        manual=bool(number('MANUAL', 0, 0, 1, True)),
        mode=number('MODE', 3, 1, 3, True),
        guard_denominator=number('GUARD', 16, 4, 32, True),
        constellation=number('CONSTELLATION', 4, 4, 64, True),
        code_rate_index=number('CODE_RATE', 1, 0, 4, True),
        time_interleaving=number('INTERLEAVING', 4, 0, 16, True),
        synthetic_snr_db=number('SYNTH_SNR', 30, -10, 100),
        synthetic_cfo_hz=number('SYNTH_CFO', 100, -20000, 20000),
        synthetic_drift_hz_s=number('SYNTH_DRIFT', 0, -2000, 2000),
        synthetic_iq_gain_db=number('SYNTH_IQ_GAIN', 0, -12, 12),
        synthetic_iq_phase_degrees=number('SYNTH_IQ_PHASE', 0, -45, 45),
        synthetic_echo_gain=number('SYNTH_ECHO_GAIN', 0, 0, 1.5),
        synthetic_echo_delay=number('SYNTH_ECHO_DELAY', 16, 0, 512),
        synthetic_echo_phase=number('SYNTH_ECHO_PHASE', 0, -180, 180),
        synthetic_timing_offset=number('SYNTH_TIMING', 0, -256, 256),
        synthetic_clock_ppm=number('SYNTH_CLOCK', 0, -500, 500),
        synthetic_phase_noise=number('SYNTH_PHASE_NOISE', 0, 0, .05),
        synthetic_seed=number('SYNTH_SEED', 1, 0, 65535, True))
    if result['guard_denominator'] not in (4, 8, 16, 32) or result['constellation'] not in (4, 16, 64):
        raise ValueError('Invalid manual OFDM waveform')
    factor = 2**(3-result['mode'])
    if result['manual'] and result['time_interleaving'] not in (0, factor, 2*factor, 4*factor):
        raise ValueError('Invalid OFDM interleaving for the selected mode')
    if any(result[key] != 0 for key in ('cutoff', 'transition', 'attenuation')):
        if result['cutoff'] <= 0 or result['transition'] <= 0 or result['attenuation'] < 20 or result['cutoff']+result['transition']/2 > sample_rate/2:
            raise ValueError('OFDM filter requires a valid passband and transition below Nyquist')
        # Bound design before asking firdes to allocate the tap vector.
        if sample_rate/result['transition']*result['attenuation'] > 8192*20:
            raise ValueError('OFDM filter exceeds the experiment tap budget')
    return result


def require_native_controls(settings, block):
    import pmt
    ports = block.message_ports_out()
    names = {pmt.symbol_to_string(pmt.vector_ref(ports, i)) for i in range(pmt.length(ports))}
    if 'ofdm_tracking' not in names:
        defaults = dict(carrier_bandwidth=3.14159/100, timing_bandwidth=3.14159/10000,
                        damping=math.sqrt(2)/2, integer_search=10, offset_confirmation=4)
        if any(not math.isclose(settings[key], value, rel_tol=1e-12, abs_tol=1e-12) for key, value in defaults.items()):
            raise ValueError('Native backend does not support OFDM tracking controls')
    if 'ofdm_controls_v1' not in names and (settings['channel_average'] != 1 or settings['equalizer_floor'] != 0 or settings['sample_clock_ppm'] != 0):
        raise ValueError('Native backend does not support channel/equalizer/clock experiment controls')


def correct_iq(samples, gain_db=0, phase_degrees=0, digital_gain=1):
    import numpy as np
    phase = math.radians(phase_degrees)
    ratio = 10**(gain_db/20)
    real = np.asarray(samples).real
    imag = (np.asarray(samples).imag/ratio-real*math.sin(phase))/math.cos(phase)
    return np.asarray((real+1j*imag)*digital_gain, dtype=np.complex64)


def front_end_blocks(settings, sample_rate):
    from gnuradio import gr, filter
    import numpy as np
    result = []
    if settings['remove_dc']:
        result.append(filter.dc_blocker_cc(32, True))
    if settings['digital_gain'] != 1 or settings['iq_gain_db'] != 0 or settings['iq_phase_degrees'] != 0:
        gain, imbalance = settings['digital_gain'], 10**(settings['iq_gain_db']/20)
        angle = math.radians(settings['iq_phase_degrees'])
        q_scale, cross = 1/(imbalance*math.cos(angle)), math.tan(angle)
        class Correction(gr.sync_block):
            def __init__(self):
                gr.sync_block.__init__(self, name='OFDM IQ calibration', in_sig=[np.complex64], out_sig=[np.complex64])
            def work(self, inputs, outputs):
                count = min(len(inputs[0]), len(outputs[0]))
                source, target = inputs[0][:count], outputs[0][:count]
                target.real = source.real*gain
                target.imag = (source.imag*q_scale-source.real*cross)*gain
                return count
        result.append(Correction())
    if settings['cutoff']:
        from gnuradio.filter import firdes
        from gnuradio.fft import window
        attenuation = settings['attenuation']
        if sample_rate/settings['transition']*attenuation > 8192*20:
            raise ValueError('OFDM filter exceeds the experiment tap budget')
        beta = .1102*(attenuation-8.7) if attenuation > 50 else .5842*max(0, attenuation-21)**.4+.07886*max(0, attenuation-21)
        taps = firdes.low_pass_2(1, sample_rate, settings['cutoff'], settings['transition'], attenuation, window.WIN_KAISER, beta)
        if len(taps) > 8192:
            raise ValueError('OFDM filter exceeds the experiment tap budget')
        result.append(filter.fir_filter_ccf(1, taps))
    return result


def manual_parameters(settings):
    if not settings['manual']:
        return None
    return {key: settings[key] for key in ('mode', 'guard_denominator', 'constellation', 'code_rate_index', 'time_interleaving')}
