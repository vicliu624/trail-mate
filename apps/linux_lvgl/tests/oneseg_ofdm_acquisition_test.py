"""Blind mode/GI candidate tests, including optional offline IQ; no hardware."""
import argparse
import json
import os
from pathlib import Path
import runpy
import subprocess
import threading
import time

import numpy as np

decoder = runpy.run_path(str(Path(__file__).parents[1] / "tools/oneseg_decode.py"))
detect = decoder["ofdm_candidates"]


def waveform(mode, guard, seed):
    random = np.random.default_rng(seed)
    fft, carriers = 2 ** (7 + mode), 108 * 2 ** (mode - 1)
    bins = np.zeros((96, fft), dtype=np.complex128)
    qpsk = random.choice((-1, 1), (96, carriers)) + 1j * random.choice((-1, 1), (96, carriers))
    bins[:, (fft - carriers) // 2:(fft + carriers) // 2] = qpsk
    symbols = np.fft.ifft(np.fft.ifftshift(bins, axes=1), axis=1)
    iq = np.concatenate((symbols[:, -fft // guard:], symbols), axis=1).reshape(-1)
    iq /= np.sqrt(np.mean(np.abs(iq) ** 2))
    # Multipath, arbitrary starting offset, CFO and 8 dB additive noise.
    iq = iq + .3j * np.concatenate((np.zeros(3), iq[:-3]))
    iq *= np.exp(2j * np.pi * .003 * np.arange(len(iq)))
    noise = random.normal(size=len(iq)) + 1j * random.normal(size=len(iq))
    return (iq + noise * (10 ** (-8 / 20) / np.sqrt(2)))[17:]


def run(args):
    cases = []
    started = time.monotonic()
    for mode in (1, 2, 3):
        for guard in (4, 8, 16, 32):
            for seed in (1, 42, 123):
                candidates = detect(waveform(mode, guard, seed))
                assert candidates and candidates[0]["mode"] == mode and candidates[0]["guard_denominator"] == guard, (mode, guard, seed, candidates)
                cases.append(candidates[0])
    n = np.arange(131072)
    assert not detect(np.zeros(131072, dtype=np.complex64))
    assert not detect(np.ones(131072, dtype=np.complex64))
    assert not detect(np.exp(2j * np.pi * .031 * n))
    assert not detect(np.ones(2048, dtype=np.complex64))
    bad = np.ones(131072, dtype=np.complex64)
    bad[0] = np.nan
    assert not detect(bad)
    for seed in range(20):
        random = np.random.default_rng(seed)
        assert not detect(random.normal(size=len(n)) + 1j * random.normal(size=len(n))), seed
    report = dict(origin="synthetic OFDM acquisition tests; no RF", synthetic_cases=len(cases),
                  elapsed_seconds=time.monotonic() - started, service_discovery=False)
    if args.recorded_iq:
        from scipy.signal import resample_poly
        with args.recorded_iq.open("rb") as recording:
            raw = np.frombuffer(recording.read(1152000), dtype=np.int8)
        assert len(raw) % 2 == 0
        iq = raw[0::2].astype(np.float32) + 1j * raw[1::2].astype(np.float32)
        native = resample_poly(iq, 1000, 567)
        started = time.monotonic()
        candidates = detect(native)
        assert candidates and candidates[0]["mode"] == 3 and candidates[0]["guard_denominator"] == 16, candidates
        report["recorded_iq"] = dict(origin="offline recorded IQ; not LR2021 capture", candidates=candidates,
                                     detector_seconds=time.monotonic() - started)
        if args.backend_prefix:
            # Set the loader environment before launching this test (see CLI).
            started = time.monotonic()
            parameters = decoder["acquire_tmcc_parameters"](native, candidates)
            assert parameters and parameters["mode"] == 3 and parameters["guard_denominator"] == 16
            assert parameters["modulation"] == "QPSK" and parameters["code_rate"] == "2/3" and parameters["time_interleaving"] == 4
            report["recorded_iq"]["tmcc_confirmed_parameters"] = parameters
            report["recorded_iq"]["tmcc_probe_seconds"] = time.monotonic() - started
            cancelled = threading.Event()
            cancelled.set()
            assert decoder["acquire_tmcc_parameters"](native, candidates, cancelled) is None
            assert decoder["acquire_tmcc_parameters"](native[:4096], candidates) is None
            assert decoder["acquire_tmcc_parameters"](native, [{"mode": 1, "guard_denominator": 32}]) is None
            report["recorded_iq"]["negative_cases"] = ["pre-cancelled", "insufficient TMCC symbols", "wrong FFT mode"]
            from gnuradio import blocks, gr
            graph = gr.top_block("TEST FIXTURE - acquired-parameter decode; no RF")
            source = blocks.vector_source_c(native.astype(np.complex64), False)
            sink = blocks.vector_sink_b(188)
            chain, _ = decoder["build_decoder_chain"](source, sink, parameters, native_rate=True)
            graph.connect(*chain)
            graph.run()
            transport = bytes(sink.data())
            assert transport and len(transport) % 188 == 0
            assert all(transport[offset] == 0x47 and not transport[offset + 1] & 0x80
                       for offset in range(0, len(transport), 188))
            if not args.transport_inspector:
                raise RuntimeError("Native chain test requires the production PSI inspector")
            verified = subprocess.run([str(args.transport_inspector)], input=transport, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
            assert verified.returncode == 0, verified.stdout
            received_programme = json.loads(verified.stdout)
            inspected = subprocess.run(["ffprobe", "-v", "error", "-probesize", "32768", "-analyzeduration", "1000000", "-scan_all_pmts", "1", "-f", "mpegts", "-i", "pipe:0", "-show_programs", "-show_streams", "-of", "json"],
                                       input=transport, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20, check=True)
            media = json.loads(inspected.stdout)
            types = {stream.get("codec_type") for stream in media.get("streams", [])}
            assert "video" in types and "audio" in types, media
            report["recorded_iq"]["acquired_chain_ts_packets"] = len(transport) // 188
            report["recorded_iq"]["acquired_chain_programme"] = received_programme
            report["recorded_iq"]["acquired_chain_stream_types"] = sorted(types)
            if args.output:
                args.output.with_suffix(".ffprobe.stderr.log").write_bytes(inspected.stderr)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recorded-iq", type=Path)
    parser.add_argument("--backend-prefix", type=Path)
    parser.add_argument("--transport-inspector", type=Path)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    if arguments.backend_prefix and os.environ.get("TRAIL_MATE_ACQUISITION_TEST_ACTIVE") != "1":
        import sys
        environment = os.environ.copy()
        prefix = arguments.backend_prefix.resolve()
        environment["PYTHONPATH"] = os.pathsep.join(str(p) for pattern in
            ("lib/python*/site-packages", "lib/python*/dist-packages") for p in prefix.glob(pattern))
        environment["LD_LIBRARY_PATH"] = os.pathsep.join(sorted({str(p.parent) for p in prefix.rglob("libgnuradio-isdbt.so*")}))
        environment["TRAIL_MATE_ACQUISITION_TEST_ACTIVE"] = "1"
        os.execve(sys.executable, [sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]], environment)
    run(arguments)
