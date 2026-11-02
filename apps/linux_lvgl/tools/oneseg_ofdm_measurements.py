"""Bounded observations of actual flowgraph samples; no generated display data.

All powers use complex full scale 1. Spectrum uses a Hann-window periodogram;
CP correlation is energy-normalized and is evidence of periodicity, not lock.
Decision-directed EVM includes native pilots/TMCC and is deliberately labelled
as such: it is not a calibrated BER or an independently known reference EVM.
"""
import math
from collections import deque
import os
import time

import numpy as np


def spectrum(samples, sample_rate, points=64):
    values = np.asarray(samples, dtype=np.complex64)
    if values.size < 256 or not np.isfinite(values).all():
        raise ValueError("Spectrum requires finite IQ samples")
    length = min(2048, 1 << (values.size.bit_length()-1))
    values = values[-length:]
    window = np.hanning(length)
    power = np.abs(np.fft.fftshift(np.fft.fft(values*window)))**2 / np.sum(window)**2
    # Average linear power before conversion to dB, retaining integrated power
    # within each displayed bin rather than taking the dB of a complex average.
    groups = np.array_split(power, points)
    centers = np.array_split(np.fft.fftshift(np.fft.fftfreq(length, 1/sample_rate)), points)
    return np.column_stack(([np.mean(x) for x in centers],
                            [10*np.log10(max(float(np.mean(x)), 1e-16)) for x in groups]))


def cp_correlation(samples, fft_length, guard, points=64):
    values = np.asarray(samples, dtype=np.complex64)
    cp = fft_length // guard
    # One entire symbol of candidate offsets: the peak is not assumed aligned.
    width = fft_length+cp
    if values.size < width+fft_length+cp or not np.isfinite(values).all():
        raise ValueError("Insufficient IQ for CP correlation")
    offsets = np.linspace(0, width-1, points, dtype=int)
    result = []
    for offset in offsets:
        a = values[offset:offset+cp]
        b = values[offset+fft_length:offset+fft_length+cp]
        energy = float(np.vdot(a, a).real*np.vdot(b, b).real)
        result.append((offset, min(1., float(abs(np.vdot(a, b))) / math.sqrt(energy)) if energy > 1e-30 else 0.))
    return np.asarray(result)


def constellation(samples, order, window=128, points=64):
    values = np.asarray(samples, dtype=np.complex64).reshape(-1)[-window:]
    if not values.size or not np.isfinite(values).all() or order not in (4, 16, 64):
        raise ValueError("Invalid equalized constellation")
    side = int(math.sqrt(order))
    scale = math.sqrt(2*(order-1)/3)
    levels = np.arange(1-side, side, 2)/scale
    real = levels[np.argmin(abs(values.real[:, None]-levels), axis=1)]
    imag = levels[np.argmin(abs(values.imag[:, None]-levels), axis=1)]
    decisions = real+1j*imag
    energy = float(np.mean(abs(decisions)**2))
    evm = math.sqrt(float(np.mean(abs(values-decisions)**2))/energy) if energy else 0.
    selected = values[np.linspace(0, len(values)-1, min(points, len(values)), dtype=int)]
    return np.column_stack((selected.real, selected.imag)), evm


class SymbolEvmWindow:
    """Energy-weighted DD EVM over the most recent N complete OFDM symbols.

    Stores two float64 moments per symbol, never the full constellation history.
    All native active carriers participate (including pilot/TMCC carriers).
    """
    def __init__(self, symbols, order):
        if isinstance(symbols, bool) or not isinstance(symbols, int) or not 16 <= symbols <= 1024 or order not in (4, 16, 64):
            raise ValueError('Invalid EVM symbol window')
        self.symbols, self.order = symbols, order
        self.moments = deque(maxlen=symbols)

    def append(self, samples):
        values = np.asarray(samples, dtype=np.complex64)
        if values.ndim != 2 or not values.shape[1] or not np.isfinite(values).all():
            self.moments.clear()
            raise ValueError('EVM requires finite complete symbol vectors')
        # A scheduler batch may exceed the requested window; earlier rows could
        # not contribute to the result and require no decision calculations.
        values = values[-self.symbols:]
        side = int(math.sqrt(self.order))
        scale = math.sqrt(2*(self.order-1)/3)
        real = (2*np.clip(np.rint((values.real*scale+side-1)/2), 0, side-1)-(side-1))/scale
        imag = (2*np.clip(np.rint((values.imag*scale+side-1)/2), 0, side-1)-(side-1))/scale
        error = np.sum((values.real-real)**2+(values.imag-imag)**2, axis=1, dtype=np.float64)
        reference = np.sum(real**2+imag**2, axis=1, dtype=np.float64)
        self.moments.extend(zip(error, reference))

    def value(self):
        if not self.moments:
            raise ValueError('EVM window is empty')
        error = sum(item[0] for item in self.moments)
        reference = sum(item[1] for item in self.moments)
        return math.sqrt(error/reference) if reference > 0 else 0.


class DeferredSymbolEvmWindow:
    """Retain exactly N symbols; defer decision arithmetic to display updates.

    The owned ring is at most 1024 * 432 complex64 samples (3.375 MiB).
    Scheduler buffers may be reused as soon as append returns.
    """
    def __init__(self, symbols, order, carriers):
        self.calculator = SymbolEvmWindow(symbols, order)
        if isinstance(carriers, bool) or not isinstance(carriers, int) or not 1 <= carriers <= 432:
            raise ValueError('Invalid EVM carrier count')
        self.buffer = np.empty((symbols, carriers), dtype=np.complex64)
        self.position, self.count = 0, 0

    def append(self, samples):
        values = np.asarray(samples, dtype=np.complex64)
        if values.ndim != 2 or values.shape[1] != self.buffer.shape[1]:
            self.count = self.position = 0
            raise ValueError('EVM requires complete symbol vectors')
        values = values[-len(self.buffer):]
        first = min(len(values), len(self.buffer)-self.position)
        self.buffer[self.position:self.position+first] = values[:first]
        self.buffer[:len(values)-first] = values[first:]
        self.position = (self.position+len(values)) % len(self.buffer)
        self.count = min(len(self.buffer), self.count+len(values))

    def value(self):
        if not self.count:
            raise ValueError('EVM window is empty')
        self.calculator.moments.clear()
        # EVM is a sum of error/reference energies; ring order is immaterial.
        self.calculator.append(self.buffer[:self.count])
        return self.calculator.value()


def channel(samples, points=64):
    values = np.asarray(samples, dtype=np.complex64).reshape(-1)
    if not values.size or not np.isfinite(values).all():
        raise ValueError("Invalid native channel estimate")
    indices = np.linspace(0, len(values)-1, min(points, len(values)), dtype=int)
    return np.column_stack((indices, 20*np.log10(np.maximum(abs(values[indices]), 1e-8))))


def channel_phase(samples, points=64):
    values = np.asarray(samples, dtype=np.complex64).reshape(-1)
    if not values.size or not np.isfinite(values).all():
        raise ValueError('Invalid complex channel estimate')
    indices = np.linspace(0, len(values)-1, min(points, len(values)), dtype=int)
    # Unwrap before display decimation. Deep fades still make phase unreliable.
    return np.column_stack((indices, np.unwrap(np.angle(values))[indices]))


def record(kind, sequence, sample_rate, metric, pairs):
    values = np.asarray(pairs, dtype=float)
    if kind not in (1, 2, 3, 4, 5) or sequence < 0 or values.ndim != 2 or values.shape[1] != 2 or not 1 <= len(values) <= 64:
        raise ValueError("Invalid signal measurement record")
    scalars = [float(sample_rate), float(metric), *values.reshape(-1)]
    if not all(math.isfinite(x) and abs(x) <= 1e9 for x in scalars):
        raise ValueError("Non-finite signal measurement")
    data = ("\x1eTM_SIGNAL1 %d %d %s %s %d " % (kind, sequence, format(sample_rate, '.9g'), format(metric, '.9g'), len(values))
            + ' '.join(format(x, '.9g') for x in values.reshape(-1)) + '\x1f').encode('ascii')
    if len(data) > 4096:
        raise ValueError("Signal measurement exceeds atomic pipe budget")
    return data


def attach(graph, chain, synchronization, configuration, sample_rate, evm_window, fft_length=None):
    """Attach terminal observers, consuming every input with bounded 2 Hz math.

    Each observer keeps at most one IQ window and drops intermediate diagnostic
    windows. The receive chain itself does not drop or decimate these samples.
    Retain the returned blocks for the lifetime of the graph (Python callbacks).
    """
    from gnuradio import gr

    class Observer(gr.sync_block):
        def __init__(self, kind, length=1):
            gr.sync_block.__init__(self, 'OFDM measured signal %d' % kind,
                                   in_sig=[np.complex64 if length == 1 else (np.complex64, length)], out_sig=None)
            self.kind, self.next_update, self.sequence = kind, 0., 0
            # Bound Python scheduler crossings to batches (about 115--138 ms
            # at the qualified Mode 3 clock, below the 500 ms display cadence).
            # This affects only terminal diagnostic consumers, never decoding.
            self.set_output_multiple(65536 if length == 1 else 128)
            self.buffer = np.empty(0, dtype=np.complex64)
            self.evm = DeferredSymbolEvmWindow(evm_window, configuration['constellation'], length) if kind == 2 else None

        def work(self, input_items, output_items):
            data = input_items[0]
            now = time.monotonic()
            if self.kind == 1:
                self.buffer = np.concatenate((self.buffer, data[-4096:]))[-4096:]
            if self.evm is not None:
                try:
                    self.evm.append(data)
                except ValueError:
                    return len(data)
            if now >= self.next_update and len(data):
                self.next_update = now+.5
                self.sequence += 1
                try:
                    if self.kind == 1 and len(self.buffer) >= 256:
                        os.write(2, record(1, self.sequence, sample_rate, float(np.mean(abs(self.buffer)**2)), spectrum(self.buffer, sample_rate)))
                        n = fft_length or 2**(7+configuration['mode'])
                        if len(self.buffer) >= 2*(n+n//configuration['guard_denominator']):
                            os.write(2, record(4, self.sequence, sample_rate, 0., cp_correlation(self.buffer, n, configuration['guard_denominator'])))
                    elif self.kind == 2:
                        pairs, _ = constellation(data[-1], configuration['constellation'], len(data[-1]))
                        os.write(2, record(2, self.sequence, sample_rate, self.evm.value(), pairs))
                    elif self.kind == 3:
                        os.write(2, record(3, self.sequence, sample_rate, 0., channel(data[-1])))
                        os.write(2, record(5, self.sequence, sample_rate, float(np.min(abs(data[-1]))), channel_phase(data[-1])))
                except (ValueError, OSError):
                    pass
            return len(data)

    front = Observer(1)
    equalized = Observer(2, 108*2**(configuration['mode']-1))
    estimate = Observer(3, 108*2**(configuration['mode']-1))
    before = chain[chain.index(synchronization)-1]
    graph.connect(before, front)
    graph.connect((synchronization, 0), equalized)
    graph.connect((synchronization, 1), estimate)
    return [front, equalized, estimate]
