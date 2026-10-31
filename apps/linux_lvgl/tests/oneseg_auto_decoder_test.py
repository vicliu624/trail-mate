"""Real automatic decoder process lifecycle with paced offline IQ, never RF."""
import argparse
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import threading
import time


def run(args):
    prefix = args.backend_prefix.resolve()
    environment = os.environ.copy()
    environment["PYTHONPATH"] = os.pathsep.join(str(p) for pattern in
        ("lib/python*/site-packages", "lib/python*/dist-packages") for p in prefix.glob(pattern))
    environment["LD_LIBRARY_PATH"] = os.pathsep.join(sorted({str(p.parent) for p in prefix.rglob("libgnuradio-isdbt.so*")}))
    command = [sys.executable, str(args.decoder.resolve()), "--profile", "auto"]
    args.output.mkdir(parents=True, exist_ok=True)
    report = dict(origin="paced offline IQ / real decoder subprocess; no RF", cases=[])

    def exercise(name, data=None, cancel=None, expected=0, extra=()):
        read_fd, write_fd = os.pipe()
        child = subprocess.Popen(command + ["--ready-fd", str(write_fd), *extra], stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment, pass_fds=(write_fd,))
        os.close(write_fd)
        transport, diagnostics, events = bytearray(), bytearray(), []
        probing = threading.Event()
        stopped = threading.Event()
        producer_failures = []

        def read_transport():
            for block in iter(lambda: child.stdout.read(4096), b""):
                transport.extend(block)

        def read_diagnostics():
            for line in iter(child.stderr.readline, b""):
                diagnostics.extend(line)
                try:
                    event = json.loads(line)
                    if isinstance(event, dict):
                        events.append(event)
                        if event.get("event") == "acquisition_probe":
                            probing.set()
                except (ValueError, UnicodeDecodeError):
                    pass

        readers = [threading.Thread(target=read_transport), threading.Thread(target=read_diagnostics)]
        for reader in readers:
            reader.start()
        producer = None
        started = time.monotonic()
        try:
            assert select.select([read_fd], [], [], 10)[0], (name, "No private ready byte", bytes(diagnostics[-2000:]))
            assert os.read(read_fd, 2) == b"R", (name, "Invalid ready handshake")
            assert not transport, "Ready was incorrectly treated as a decoded programme"
            cancel_started = None
            if cancel == "before-input":
                cancel_started = time.monotonic()
                child.send_signal(signal.SIGTERM)
            elif cancel in ("prefix", "probe"):
                # Keep stdin open; otherwise EOF would terminate acquisition.
                child.stdin.write(data)
                child.stdin.flush()
                if cancel == "probe":
                    assert probing.wait(5), (name, bytes(diagnostics[-2000:]))
                cancel_started = time.monotonic()
                child.send_signal(signal.SIGTERM)
            elif data is None:
                pass  # Explicit --seconds case: no input and pipe left open.
            else:
                def feed():
                    began = time.monotonic()
                    try:
                        # Odd block length exercises split IQ-pair boundaries.
                        for offset in range(0, len(data), 46081):
                            if stopped.is_set():
                                break
                            block = data[offset:offset + 46081]
                            child.stdin.write(block)
                            child.stdin.flush()
                            delay = began + (offset + len(block)) / 1152000 - time.monotonic()
                            if delay > 0:
                                stopped.wait(delay)
                    except (BrokenPipeError, OSError) as error:
                        producer_failures.append(str(error))
                    finally:
                        child.stdin.close()

                producer = threading.Thread(target=feed)
                producer.start()
            child.wait(timeout=20)
            stopped.set()
            if producer is not None:
                producer.join(timeout=2)
                assert not producer.is_alive()
            for reader in readers:
                reader.join(timeout=2)
                assert not reader.is_alive()
            (args.output / (name + ".stderr.log")).write_bytes(diagnostics)
            assert child.returncode == expected, (name, child.returncode, bytes(diagnostics[-4000:]))
            if cancel_started is not None:
                assert time.monotonic() - cancel_started < 2, (name, "Slow cancellation")
                assert not transport
            entry = dict(name=name, exit=child.returncode, elapsed_seconds=time.monotonic() - started,
                         ts_bytes=len(transport), producer_failures=producer_failures)
            report["cases"].append(entry)
            return bytes(transport), events, entry
        finally:
            stopped.set()
            if child.poll() is None:
                child.kill()
                child.wait()
            if producer is not None:
                producer.join(timeout=2)
            for reader in readers:
                reader.join(timeout=2)
            if not child.stdin.closed:
                child.stdin.close()
            child.stdout.close()
            child.stderr.close()
            os.close(read_fd)

    assert args.recorded_iq.stat().st_size <= 64000000
    recording = args.recorded_iq.read_bytes()
    exercise("cancel-before-input", cancel="before-input")
    exercise("cancel-prefix", recording[:46081], cancel="prefix")
    exercise("cancel-probe", recording[:1152000], cancel="probe")
    output, events, _ = exercise("duration-without-input", extra=["--seconds", ".15"])
    assert not output and not any(event.get("event") == "parameters_acquired" for event in events)
    exercise("empty-eof", b"", expected=3)
    exercise("odd-eof", b"\x00", expected=3)
    output, events, _ = exercise("zero-iq", bytes(1152000), expected=4)
    assert not output and not any(event.get("event") == "parameters_acquired" for event in events)
    output, events, entry = exercise("recorded-iq", recording)
    assert output and len(output) % 188 == 0 and not entry["producer_failures"]
    assert all(output[offset] == 0x47 for offset in range(0, len(output), 188))
    acquired = [event for event in events if event.get("event") == "parameters_acquired"]
    assert len(acquired) == 1 and acquired[0]["mode"] == 3 and acquired[0]["guard_denominator"] == 16
    assert acquired[0]["code_rate"] == "2/3" and acquired[0]["time_interleaving"] == 4
    terminal = [event for event in events if event.get("event") == "stopped"]
    assert len(terminal) == 1 and terminal[0]["iq_pairs"] * 2 == len(recording), terminal
    assert terminal[0]["errors"] == []
    inspected = subprocess.run([str(args.transport_inspector)], input=output, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
    assert inspected.returncode == 0, inspected.stdout
    report["received_programme"] = json.loads(inspected.stdout)
    report["parameters"] = acquired[0]
    report["physical_rf_acceptance"] = False
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decoder", type=Path, required=True)
    parser.add_argument("--backend-prefix", type=Path, required=True)
    parser.add_argument("--recorded-iq", type=Path, required=True)
    parser.add_argument("--transport-inspector", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())
