import importlib.util
from pathlib import Path
import sys
import math
import numpy as np

spec = importlib.util.spec_from_file_location('ofdm_dsp', Path(sys.argv[1]))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
baseline = module.read_settings(450000, {})
assert baseline['digital_gain'] == 1 and baseline['cutoff'] == 0 and not baseline['remove_dc']
assert module.manual_parameters(baseline) is None
unused = module.read_settings(450000, {'TRAIL_MATE_OFDM_INTERLEAVING':'16'})
assert module.manual_parameters(unused) is None
try:
    module.read_settings(450000, {'TRAIL_MATE_OFDM_MANUAL':'1', 'TRAIL_MATE_OFDM_INTERLEAVING':'16'})
except ValueError:
    pass
else:
    raise AssertionError('Manual Mode3 cannot use interleaving16')
valid_manual = module.read_settings(450000, {'TRAIL_MATE_OFDM_MANUAL':'1', 'TRAIL_MATE_OFDM_MODE':'1', 'TRAIL_MATE_OFDM_INTERLEAVING':'16'})
assert module.manual_parameters(valid_manual)['time_interleaving'] == 16
original = np.array([1+2j, -1+.5j, .1-.4j], dtype=np.complex64)
gain_db, angle = 3, math.radians(12)
corrupted = original.real+1j*(10**(gain_db/20))*(original.real*math.sin(angle)+original.imag*math.cos(angle))
assert np.allclose(module.correct_iq(corrupted, gain_db, 12), original, atol=1e-6)
assert np.allclose(module.correct_iq(original, digital_gain=2), original*2)
for key, value in [('DIGITAL_GAIN', 'nan'), ('CHANNEL_AVERAGE', '1.5'), ('GUARD', '7'), ('IQ_PHASE_DEGREES', '90')]:
    try:
        module.read_settings(450000, {'TRAIL_MATE_OFDM_'+key:value})
    except ValueError:
        pass
    else:
        raise AssertionError((key, value))
manual = module.read_settings(450000, {'TRAIL_MATE_OFDM_MANUAL':'1', 'TRAIL_MATE_OFDM_MODE':'2', 'TRAIL_MATE_OFDM_INTERLEAVING':'8'})
assert module.manual_parameters(manual)['time_interleaving'] == 8
from gnuradio import blocks, gr
assert module.front_end_blocks(baseline, 450000) == []
settings = module.read_settings(450000, {'TRAIL_MATE_OFDM_IQ_GAIN_DB':'3', 'TRAIL_MATE_OFDM_IQ_PHASE_DEGREES':'12'})
graph = gr.top_block()
source, sink = blocks.vector_source_c(corrupted, False), blocks.vector_sink_c()
correction_chain = module.front_end_blocks(settings, 450000)
graph.connect(source, *correction_chain, sink)
graph.run()
assert np.allclose(np.asarray(sink.data()), original, atol=1e-6)
settings = module.read_settings(450000, {'TRAIL_MATE_OFDM_FILTER_CUTOFF_HZ':'80000',
    'TRAIL_MATE_OFDM_FILTER_TRANSITION_HZ':'20000', 'TRAIL_MATE_OFDM_FILTER_ATTENUATION_DB':'60'})
fir = module.front_end_blocks(settings, 450000)[0]
taps = np.asarray(fir.taps())
passband = abs(np.sum(taps*np.exp(-2j*np.pi*40000/450000*np.arange(len(taps)))))
stopband = abs(np.sum(taps*np.exp(-2j*np.pi*150000/450000*np.arange(len(taps)))))
assert .98 < passband < 1.02 and stopband < .002 and len(taps) <= 8192
print('OFDM front-end validation, actual IQ correction and manual waveform selection passed')
