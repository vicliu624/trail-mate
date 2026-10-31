"""Actual GNU Radio decoder tests; optional offline RTL fixture, no hardware."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading


def run(args):
    root = args.artifact_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    prefix = args.backend_prefix.resolve()
    env = os.environ.copy()
    paths = [str(p) for pattern in ("lib/python*/site-packages", "lib/python*/dist-packages")
             for p in prefix.glob(pattern)]
    libraries = sorted({str(p.parent) for p in prefix.rglob("libgnuradio-isdbt.so*")})
    if not paths or not libraries:
        raise RuntimeError("Private backend is missing: " + str(prefix))
    env["PYTHONPATH"] = os.pathsep.join(paths + [env.get("PYTHONPATH", "")])
    env["LD_LIBRARY_PATH"] = os.pathsep.join(libraries + [env.get("LD_LIBRARY_PATH", "")])
    command = [sys.executable, str(args.decoder.resolve()), "--profile", "onesegcast-mode3-gi16-qpsk23-ti4"]
    report = {"origin": "host tests; no LR2021 hardware", "results": []}

    def check(name, data, expected=0, extra=()):
        result = subprocess.run(command + list(extra), input=data, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, env=env, timeout=20)
        (root / (name + ".stderr.log")).write_bytes(result.stderr)
        assert result.returncode == expected, (name, result.returncode, result.stderr[-2000:])
        report["results"].append(dict(name=name, exit=result.returncode, output_bytes=len(result.stdout)))
        return result

    assert check("backend", b"", extra=["--check-backend"]).stdout == b""
    assert check("empty", b"").stdout == b""
    assert check("odd", b"\x00", 3).stdout == b""
    assert check("zero", bytes(1152000)).stdout == b""
    assert check("invalid-duration", b"", 2, ["--seconds", "nan"]).stdout == b""
    child = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    started, errors = threading.Event(), bytearray()

    def read_status():
        for line in iter(child.stderr.readline, b""):
            errors.extend(line)
            try:
                if json.loads(line).get("event") == "started":
                    started.set()
            except (ValueError, AttributeError):
                pass

    reader = threading.Thread(target=read_status, daemon=True)
    reader.start()
    try:
        assert started.wait(10), "Decoder never published started state"
        assert child.poll() is None
        child.send_signal(signal.SIGTERM)
        child.wait(timeout=10)
        reader.join(timeout=1)
        assert not reader.is_alive()
        assert child.returncode == 0 and child.stdout.read() == b"", (child.returncode, errors[-2000:])
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        child.stdin.close()
        child.stdout.close()
        child.stderr.close()
        reader.join(timeout=1)
    (root / "empty-pipe-cancel.stderr.log").write_bytes(errors)
    report["results"].append(dict(name="empty_pipe_cancel", exit=child.returncode, output_bytes=0))

    if args.recorded_rtl:
        import numpy as np
        from scipy.signal import resample_poly
        capture = args.recorded_rtl.resolve()
        if capture.stat().st_size > 64000000 or capture.stat().st_size % 2:
            raise RuntimeError("Fixture must contain complete IQ8 pairs and be at most 64 MB")
        unsigned = np.fromfile(capture, dtype=np.uint8).reshape((-1, 2)).astype(np.float32)
        iq = (unsigned[:, 0] - 127.5 + 1j * (unsigned[:, 1] - 127.5)) / 128
        resampled = resample_poly(iq, 9, 16)  # Named fixture: 1.024 MHz -> 576 kHz.
        signed = np.empty((len(resampled), 2), dtype=np.int8)
        signed[:, 0] = np.clip(np.rint(resampled.real * 128), -128, 127)
        signed[:, 1] = np.clip(np.rint(resampled.imag * 128), -128, 127)
        data = signed.tobytes()
        (root / "recorded-rtl-derived-576k.si8").write_bytes(data)
        decoded = check("recorded-rtl-to-576k", data)
        assert decoded.stdout and len(decoded.stdout) % 188 == 0
        rows = np.frombuffer(decoded.stdout, dtype=np.uint8).reshape((-1, 188))
        assert np.all(rows[:, 0] == 0x47) and not np.any(rows[:, 1] & 0x80)
        (root / "decoded-research.ts").write_bytes(decoded.stdout)
        report["fixture"] = dict(source=str(capture), origin="offline RTL research recording",
                                 lr2021_capture=False, ts_packets=len(rows))
        for name, close_reader in (("closed-ts-pipe", True), ("blocked-ts-pipe", False)):
            read_fd, write_fd = os.pipe()
            if close_reader:
                os.close(read_fd)
            try:
                blocked = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=write_fd,
                                           stderr=subprocess.PIPE, env=env)
                os.close(write_fd)
                write_fd = -1
                try:
                    _, failure = blocked.communicate(data, timeout=20)
                    assert blocked.returncode == 3 and b'"event": "error"' in failure, (name, blocked.returncode, failure[-2000:])
                finally:
                    if blocked.poll() is None:
                        blocked.kill()
                        blocked.wait()
                (root / (name + ".stderr.log")).write_bytes(failure)
                report["results"].append(dict(name=name, exit=blocked.returncode))
            finally:
                if write_fd >= 0:
                    os.close(write_fd)
                if not close_reader:
                    os.close(read_fd)
    (root / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decoder", type=Path, required=True)
    parser.add_argument("--backend-prefix", type=Path, required=True)
    parser.add_argument("--artifact-dir", type=Path, required=True)
    parser.add_argument("--recorded-rtl", type=Path)
    run(parser.parse_args())
