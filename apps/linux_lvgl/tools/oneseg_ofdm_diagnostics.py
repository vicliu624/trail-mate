"""Versioned, bounded tracking records on stderr, separate from TS stdout."""
import math

FIELDS = ('prior', 'observed', 'corrected', 'correction', 'residual',
          'nco_phase', 'frequency_hz', 'sampling_ppm')
CONFIG_FIELDS = ('mode', 'fft_length', 'cp_length', 'carriers', 'sample_rate',
                 'alpha', 'beta', 'timing_alpha', 'timing_beta', 'integer_search',
                 'offset_confirmation', 'channel_average', 'equalizer_floor', 'sampling_ppm')


def fec_record(message):
    if isinstance(message.get('schema'), bool) or message.get('schema') != 1:
        raise ValueError('Invalid native FEC schema')
    values = [message[key] for key in ('words', 'clean', 'corrected', 'uncorrectable', 'symbols', 'maximum')]
    if any(isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xffffffffffffffff for value in values):
        raise ValueError('Invalid FEC counter')
    words, clean, corrected, uncorrectable, symbols, maximum = values
    if not words or words != clean+corrected+uncorrectable or maximum > 8 or (not corrected and (symbols or maximum)) or (corrected and (not maximum or not corrected <= symbols <= corrected*maximum or symbols < maximum)):
        raise ValueError('Inconsistent native FEC outcomes')
    return ('\x1eTM_FEC1 '+' '.join(str(value) for value in values)+'\x1f').encode('ascii')


def configuration_record(message):
    if isinstance(message.get('schema'), bool) or message.get('schema') != 1:
        raise ValueError('Invalid native configuration schema')
    values = [float(message[key]) for key in CONFIG_FIELDS]
    if any(not math.isfinite(value) or abs(value) > 1e9 for value in values):
        raise ValueError('Invalid native configuration value')
    for index in (0, 1, 2, 3, 9, 10, 11):
        if values[index] != int(values[index]): raise ValueError('Non-integer native configuration')
    mode, fft, cp, carriers, rate, alpha, beta, ta, tb, search, confirm, average, floor, ppm = values
    if mode not in (1, 2, 3) or not 16 <= fft <= 8192 or not 1 <= cp <= fft/4+1 or carriers != 108*2**(int(mode)-1) or not 0 < rate <= 2e6:
        raise ValueError('Invalid native waveform dimensions')
    if any(not 0 <= value <= 4 for value in (alpha, beta, ta, tb)) or not 1 <= search <= 32 or not 1 <= confirm <= 64 or not 1 <= average <= 32 or not 0 <= floor <= 1:
        raise ValueError('Native control readback outside bounds')
    data = ('\x1eTM_CONFIG1 '+' '.join(format(value, '.17g') for value in values)+'\x1f').encode('ascii')
    if len(data) >= 512: raise ValueError('Native configuration exceeds framing budget')
    return data


def phase_record(message):
    schema = message.get('schema')
    if isinstance(schema, bool) or schema not in (1, 2):
        raise ValueError('Unsupported native OFDM tracking schema')
    symbol = message.get('symbol')
    if isinstance(symbol, bool) or not isinstance(symbol, int) or not 0 <= symbol <= 0xffffffffffffffff:
        raise ValueError('Invalid OFDM symbol index')
    values = [float(message[key]) for key in FIELDS]
    if any(not math.isfinite(value) or abs(value) > 1e9 for value in values):
        raise ValueError('Invalid native OFDM tracking value')
    prior, observed, corrected, correction, residual, *_ = values
    if abs((observed-corrected)-residual) > 1e-4*(1+abs(observed)) or abs((corrected-prior)-correction) > 1e-4*(1+abs(prior)):
        raise ValueError('Inconsistent native OFDM tracking record')
    if schema == 2:
        prediction, integrator, alpha, beta = [float(message[key]) for key in ('prediction', 'integrator', 'alpha', 'beta')]
        extra = [prediction, integrator, alpha, beta]
        if any(not math.isfinite(value) or abs(value) > 1e9 for value in extra) or not 0 <= alpha <= 4 or not 0 <= beta <= 4:
            raise ValueError('Invalid native forecast state')
        tolerance = 5e-6*(1+abs(prior)+abs(prediction)+abs(corrected))
        if abs(prediction-prior-integrator) > tolerance or abs(corrected-prediction-(alpha+beta)*(observed-prior)) > tolerance:
            raise ValueError('Inconsistent native forecast update')
        values.extend(extra)
    data = ('\x1eTM_OFDM'+str(schema)+' '+str(symbol)+' '+' '.join(format(value, '.17g') for value in values)+'\x1f').encode('ascii')
    if len(data) >= 512:
        raise ValueError('OFDM tracking record exceeds framing budget')
    return data
