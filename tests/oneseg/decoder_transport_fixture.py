"""TEST FIXTURE - NO RF: paced synthetic TS; bypasses OFDM, not media decoding."""
import argparse
import os
import threading
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--profile")
parser.add_argument("--ready-fd", type=int, required=True)
args = parser.parse_args()
root = Path(os.environ["TRAIL_MATE_ONESEG_TRANSPORT_FIXTURES"])
stopped = threading.Event()


def drain_iq():
    try:
        while os.read(0, 32768):
            pass
    finally:
        stopped.set()


threading.Thread(target=drain_iq, daemon=True).start()
os.write(args.ready_fd, b"R")
os.close(args.ready_fd)
phase = None
data = b""
offset = 0
deadline = time.monotonic()
try:
    while not stopped.is_set():
        # Append-only commands avoid DrvFS rename-overwrite visibility gaps.
        # An incomplete last line is ignored until the parent finishes writing.
        commands = (root / "phase").read_bytes().splitlines(keepends=True)
        requested = next((line.decode("ascii").strip() for line in reversed(commands)
                          if line.endswith(b"\n")), phase)
        if requested not in {"a", "b", "c", "video", "jump"}:
            raise ValueError("Unknown TEST FIXTURE phase")
        if requested != phase:
            data = (root / (requested + ".ts")).read_bytes()
            if not data or len(data) % 188 or any(data[i] != 0x47 for i in range(0, len(data), 188)):
                raise ValueError("Unaligned TEST FIXTURE TS")
            phase, offset = requested, 0
        os.write(1, data[offset:offset + 188])
        offset = (offset + 188) % len(data)
        deadline += 188 * 8 / 500000
        if deadline < time.monotonic() - .1:
            deadline = time.monotonic()
        stopped.wait(max(0, deadline - time.monotonic()))
except BrokenPipeError:
    pass
