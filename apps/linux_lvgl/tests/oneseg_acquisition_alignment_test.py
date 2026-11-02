"""Native TMCC acquisition regression using a supplied offline 3 MHz IQ16 fixture.

The fixture must carry Mode 3 / GI 1/16 / QPSK 2/3 / TI 4. No radio is opened.
"""
import argparse
import importlib.util
from pathlib import Path
import threading
import numpy as np
from gnuradio import gr, blocks, filter


def run(args):
    spec = importlib.util.spec_from_file_location('alignment_decoder', args.decoder)
    decoder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(decoder)
    for start in (0, 1858679):
        raw = np.fromfile(args.iq16, dtype='<i2', count=4000000, offset=start*4).reshape(-1, 2)
        assert len(raw) == 2000000, 'Fixture is too short'
        iq = (raw[:, 0].astype(np.float32)+1j*raw[:, 1].astype(np.float32))/32768
        source = decoder._complex_capture_source(iq)
        converter = filter.rational_resampler_ccc(interpolation=64, decimation=189)
        sink = blocks.vector_sink_c()
        graph = gr.top_block()
        graph.connect(source, converter, sink)
        graph.run()
        samples = np.asarray(sink.data(), dtype=np.complex64)
        candidates = decoder.ofdm_candidates(samples)
        assert candidates
        parameters = decoder.acquire_tmcc_parameters(samples, candidates)
        assert parameters is not None, (start, candidates)
        assert (parameters['mode'], parameters['guard_denominator'], parameters['constellation'],
                parameters['code_rate_index'], parameters['time_interleaving']) == (3, 16, 4, 1, 4)
        for offset in (-1, 1088, True, .5):
            bad = dict(candidates[0], prefix_offset=offset)
            try:
                decoder.acquire_tmcc_parameters(samples, [bad])
            except ValueError:
                pass
            else:
                raise AssertionError('Invalid CP offset accepted: '+repr(offset))
        cancelled = threading.Event()
        cancelled.set()
        assert decoder.acquire_tmcc_parameters(samples, candidates, cancelled) is None
        print('Native TMCC alignment passed at IQ16 source offset', start, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decoder', type=Path, required=True)
    parser.add_argument('--iq16', type=Path, required=True)
    run(parser.parse_args())
