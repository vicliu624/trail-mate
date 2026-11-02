"""Validate an extracted real native backend package without installing or RF."""
import argparse
import json
import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile


def run(args):
    with tempfile.TemporaryDirectory(prefix="trailmate-oneseg-package-test-") as temporary:
        root = Path(temporary)
        subprocess.run(["dpkg-deb", "--extract", str(args.package.resolve()), str(root)], check=True)
        private = root / "usr/libexec/trailmate"
        docs = root / "usr/share/doc/trailmate-oneseg-backend"
        manifest = json.loads((docs / "backend-manifest.json").read_text())
        with tarfile.open(docs / "corresponding-source.tar.gz") as source:
            names = set(source.getnames())
            for name in ("BUILD.txt", "source.tar.gz", "backend-manifest.json",
                         "tools/build_oneseg_backend.py", "tools/oneseg_sync_lab_backend.py",
                         "tools/oneseg_fec_backend.py", "tools/oneseg_cm0_backend.py",
                         "gr-isdbt-" + manifest["revision"] + "/COPYING"):
                assert name in names, "Missing corresponding source: " + name
            assert json.load(source.extractfile("backend-manifest.json")) == manifest
        version = subprocess.check_output(["dpkg-deb", "-f", str(args.package.resolve()), "Version"], text=True).strip()
        subprocess.run(["dpkg", "--compare-versions", version, "ge", "1.1.0"], check=True)
        provides = subprocess.check_output(["dpkg-deb", "-f", str(args.package.resolve()), "Provides"], text=True)
        assert "trailmate-oneseg-backend-auto-v1" in [item.strip() for item in provides.split(",")]
        decoder = private / "trailmate-linux-oneseg-decode"
        assert not decoder.exists(), "Backend package must not overwrite the application decoder"
        for path in root.rglob("*"):
            if not path.is_symlink():
                assert not path.stat().st_mode & 0o022, path
        shutil.copy2(args.decoder, decoder)
        # These sidecars belong to the application, just like its decoder entry.
        for name in ("oneseg_ofdm_dsp.py", "oneseg_ofdm_diagnostics.py",
                     "oneseg_ofdm_measurements.py", "oneseg_synthetic_ofdm.py", "oneseg_carrier_protocol.py"):
            shutil.copy2(args.decoder.resolve().parent / name, private / name)
        environment = os.environ.copy()
        for key in ("PYTHONPATH", "LD_LIBRARY_PATH", "TRAIL_MATE_ONESEG_BACKEND_ACTIVE"):
            environment.pop(key, None)
        command = [sys.executable, str(decoder), "--profile", "auto"]
        checked = subprocess.run(command + ["--check-backend"], env=environment,
                                 capture_output=True, timeout=30)
        assert checked.returncode == 0 and checked.stdout == b"", checked.stderr
        events = [json.loads(line) for line in checked.stderr.splitlines() if line.startswith(b"{")]
        ready = next(event for event in events if event["event"] == "backend_ready")
        assert ready["parameter_autodetection"] and ready["parameter_observation"], ready
        assert str(private / "oneseg-backend/python") in ready["backend_path"], ready
        read_fd, write_fd = os.pipe()
        child = subprocess.Popen(command + ["--ready-fd", str(write_fd)], env=environment,
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 pass_fds=(write_fd,))
        os.close(write_fd)
        try:
            assert select.select([read_fd], [], [], 15)[0], "Relocated decoder handshake timed out"
            if os.read(read_fd, 1) != b"R":
                output, diagnostics = child.communicate(timeout=15)
                raise AssertionError("Private ready descriptor did not survive exec: " + diagnostics.decode(errors="replace"))
            assert child.poll() is None
            child.send_signal(signal.SIGTERM)
            output, diagnostics = child.communicate(timeout=15)
            assert child.returncode == 0 and output == b"", diagnostics
            assert b'"event": "stopped"' in diagnostics, diagnostics
        finally:
            os.close(read_fd)
            if child.poll() is None:
                child.kill()
                child.communicate()
        print("Extracted package: private import, permissions, ready-FD preservation and cancellation passed; no RF")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--decoder", type=Path, required=True)
    run(parser.parse_args())
