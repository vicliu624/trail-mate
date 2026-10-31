"""TEST FIXTURE - NO RF: deterministic child lifecycle and IPC faults only."""
import argparse
import fcntl
import os
import signal
import sys
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--profile")
parser.add_argument("--ready-fd", type=int, required=True)
args = parser.parse_args()
if args.profile != os.environ.get("TRAIL_MATE_ONESEG_TEST_EXPECT_PROFILE", "auto"):
    raise SystemExit(26)
if args.profile == "auto":
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
if os.environ.get("TRAIL_MATE_ONESEG_TEST_START_MARKER"):
    Path(os.environ["TRAIL_MATE_ONESEG_TEST_START_MARKER"]).write_text("TEST FIXTURE - NO RF\n")
mode = os.environ.get("TRAIL_MATE_ONESEG_TEST_MODE", "echo")
if os.getpgrp() != os.getpid():
    raise SystemExit(25)
if "TRAIL_MATE_ONESEG_TEST_FD" in os.environ:
    try:
        fcntl.fcntl(int(os.environ["TRAIL_MATE_ONESEG_TEST_FD"]), fcntl.F_GETFD)
    except OSError:
        pass
    else:
        raise SystemExit(23)
if os.environ.get("TRAIL_MATE_ONESEG_TEST_LITERAL", "literal;$(not-a-command)") != "literal;$(not-a-command)":
    raise SystemExit(24)
if mode == "early-exit":
    print("TEST FIXTURE - NO RF: backend unavailable", file=sys.stderr, flush=True)
    raise SystemExit(9)
if mode == "no-ready":
    time.sleep(10)
    raise SystemExit(10)
if mode == "ignore-term":
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
if mode == "flood":
    os.write(2, b"x" * 100000)
    os.write(2, b"TEST FIXTURE - NO RF: stderr drained\n")
os.write(args.ready_fd, b"R")
os.close(args.ready_fd)
if mode == "die-after-ready":
    time.sleep(.1)
    raise SystemExit(7)
if mode == "invalid-ts":
    os.write(1, bytes(188))
if mode == "partial-ts":
    os.write(1, b"\x47\x00\x00")
    raise SystemExit(0)
if mode == "ignore-term":
    while True:
        time.sleep(1)


def section(programme=24):
    # Received-PMT fixture: service 24, PMT 0x1fc8, H264 0x530, AAC 0x531.
    data = bytearray([2, 0xb0, 23, 0, programme, 0xc1, 0, 0, 0xe5, 0x30, 0xf0, 0,
                      0x1b, 0xe5, 0x30, 0xf0, 0, 0x0f, 0xe5, 0x31, 0xf0, 0])
    crc = 0xffffffff
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04c11db7 if crc & 0x80000000 else 0)) & 0xffffffff
    data.extend(crc.to_bytes(4, "big"))
    return bytes(data)


def metadata_section(data):
    data = bytearray(data)
    length = len(data) + 1
    data[1:3] = (0xf000 | length).to_bytes(2, "big")
    crc = 0xffffffff
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04c11db7 if crc & 0x80000000 else 0)) & 0xffffffff
    return bytes(data) + crc.to_bytes(4, "big")


def arib_graphics(text):
    # ISO-2022-JP's ASCII B designation is not the ARIB alphanumeric J set.
    return text.encode("iso2022_jp").replace(b"\x1b(B", b"\x1b(J")


cc = 0
time_control = os.environ.get("TRAIL_MATE_ONESEG_TEST_TIME_CONTROL")
time_mode, si_version, next_time_poll = "normal", 0, 0.0
while os.read(0, 1280):
    packet = bytes([0x47, 0x5f, 0xc8, 0x10 | cc, 0]) + section()
    packet += b"\xff" * (188 - len(packet))
    os.write(1, packet[:13])
    os.write(1, packet[13:])
    if mode == "metadata-japanese":
        # TEST FIXTURE - SYNTHETIC SI ONLY, NO RF/VIDEO/AUDIO.
        if time_control and time.monotonic() >= next_time_poll:
            next_time_poll = time.monotonic() + .1
            commands = Path(time_control).read_bytes().splitlines(keepends=True)
            requested = next((line.decode("ascii").strip() for line in reversed(commands) if line.endswith(b"\n")), time_mode)
            if requested not in {"normal", "undefined", "invalid", "paused"}:
                raise ValueError("Unknown TEST FIXTURE schedule mode")
            if requested != time_mode:
                time_mode, si_version = requested, (si_version + 1) & 31
        name = arib_graphics("試験放送")
        provider = arib_graphics("試験局")
        descriptor = bytes([0x48, 3 + len(provider) + len(name), 1, len(provider),
                            *provider, len(name), *name])
        sdt = metadata_section([0x42, 0, 0, 0, 1, 0xc1, 0, 0, 0, 2, 0xff,
                                0, 24, 0xff, 0xf0, len(descriptor), *descriptor])
        tables = [(0x11, sdt)]
        for index, title, description, clock, duration in (
            (0, "日本語ニュース", "現在の番組説明です。", [0x12, 0x45, 0], [1, 0x45, 0x30]),
            (1, "次の番組", "次の番組説明です。", [0x14, 0x30, 0x30], [255, 255, 255]),
        ):
            title_bytes = arib_graphics(title)
            description_bytes = arib_graphics(description)
            payload = b"jpn" + bytes([len(title_bytes)]) + title_bytes + bytes([len(description_bytes)]) + description_bytes
            short = bytes([0x4d, len(payload)]) + payload
            start = [0xc0, 0x79, *clock]
            running = 4 if index == 0 else 1
            if time_mode == "undefined":
                start = [255] * 5
            elif time_mode == "invalid":
                if index == 0:
                    start[2] = 0x24
                else:
                    duration = [1, 0x60, 0]
            elif time_mode == "paused":
                running = 3 if index == 0 else 2
            eit = metadata_section([0x4e, 0, 0, 0, 24, 0xc1 | (si_version << 1), index, 1, 0, 1, 0, 2, 1, 0x4e,
                                    0, 10 + index, *start, *duration, running << 5, len(short), *short])
            tables.append((0x27, eit))
        eit_cc = (cc * 2) & 15
        for pid, table in tables:
            counter = eit_cc if pid == 0x27 else cc
            if pid == 0x27:
                eit_cc = (eit_cc + 1) & 15
            metadata = bytes([0x47, 0x40 | (pid >> 8), pid & 255, 0x10 | counter, 0]) + table
            assert len(metadata) <= 188
            os.write(1, metadata + b"\xff" * (188 - len(metadata)))
    if mode in ("metadata", "metadata-multi"):
        sdt = metadata_section([0x42, 0, 0, 0, 1, 0xc1, 0, 0, 0, 2, 0xff,
                                0, 24, 0xff, 0xf0, 10, 0x48, 8, 1, 0, 5, 0x0e, *b"TEST"])
        eit = metadata_section([0x4e, 0, 0, 0, 24, 0xc1, 0, 1, 0, 1, 0, 2, 1, 0x4e,
                                0, 10, 0xeb, 0x96, 0x12, 0x30, 0, 1, 0, 0, 0x80, 12,
                                0x4d, 10, *b"jpn", 5, 0x0e, *b"NEWS", 0])
        for pid, table in ((0x11, sdt), (0x27, eit)):
            metadata = bytes([0x47, 0x40 | (pid >> 8), pid & 255, 0x10 | cc, 0]) + table
            os.write(1, metadata + b"\xff" * (188 - len(metadata)))
        if mode == "metadata-multi":
            other = bytes([0x47, 0x5f, 0xc9, 0x10 | cc, 0]) + section(25)
            os.write(1, other + b"\xff" * (188 - len(other)))
    cc = (cc + 1) % 16
