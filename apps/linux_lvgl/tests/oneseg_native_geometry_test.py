"""Verify real backend geometry, acquisition isolation and decoder rate metadata."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys


def run(args):
    from gnuradio import blocks, gr, isdbt
    spec = importlib.util.spec_from_file_location("geometry_decoder", args.decoder)
    decoder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(decoder)
    key = "TRAIL_MATE_ONESEG_COMPACT_FFT"
    for requested in (None, "576"):
        if requested is None:
            os.environ.pop(key, None)
        else:
            os.environ[key] = requested
        for mode in (1, 2, 3):
            full = 2**(7+mode)
            compact = requested == "576" and mode == 3
            expected = 576 if compact and args.compact_available else full
            block = decoder.synchronization_block(isdbt, mode, 16, compact=compact)
            assert abs(block.relative_rate()-1/(expected+expected//16)) < 1e-12
            acquisition = decoder.synchronization_block(isdbt, mode, 16, compact=False)
            assert abs(acquisition.relative_rate()-1/(full+full//16)) < 1e-12
            assert os.environ.get(key) == requested
            # The application enables compact geometry only with a matching backend.
            if compact and not args.compact_available:
                continue
            parameters = dict(mode=mode, guard_denominator=16, constellation=4,
                              code_rate_index=1, time_interleaving=2**(3-mode))
            for native in (False, True):
                chain, _ = decoder.build_decoder_chain(blocks.null_source(gr.sizeof_gr_complex),
                    blocks.null_sink(gr.sizeof_char*188), parameters, input_rate=450000, native_rate=native)
                assert isinstance(chain, list) and chain.fft_length == expected
                rate = 4000000/7 if compact else 64000000/63
                assert abs(chain.sample_rate-rate) < 1e-6
    print("Real native geometry: all modes, full acquisition, environment restoration and chain rates passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--decoder", type=Path, required=True)
    parser.add_argument("--compact-available", action="store_true")
    parser.add_argument("--child", action="store_true")
    args = parser.parse_args()
    if args.child:
        run(args)
    else:
        environment = os.environ.copy()
        python = [str(p) for p in args.prefix.rglob("*") if p.is_dir() and p.name in ("site-packages", "dist-packages")]
        libraries = sorted({str(p.parent) for p in args.prefix.rglob("libgnuradio-isdbt.so*")})
        environment["PYTHONPATH"] = ":".join(python+[environment.get("PYTHONPATH", "")])
        environment["LD_LIBRARY_PATH"] = ":".join(libraries+[environment.get("LD_LIBRARY_PATH", "")])
        subprocess.run([sys.executable, __file__, *sys.argv[1:], "--child"], env=environment, check=True)
