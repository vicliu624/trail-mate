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
import tempfile


def run(args):
    with tempfile.TemporaryDirectory(prefix="trailmate-oneseg-package-test-") as temporary:
        root = Path(temporary)
        subprocess.run(["dpkg-deb", "--extract", str(args.package.resolve()), str(root)], check=True)
        private = root / "usr/libexec/trailmate"
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
            assert os.read(read_fd, 1) == b"R", "Private ready descriptor did not survive exec"
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
