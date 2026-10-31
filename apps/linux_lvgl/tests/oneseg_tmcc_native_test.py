"""Exercise actual native TMCC messages with zero/recorded IQ; no hardware."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def run(args):
    prefix = args.backend_prefix.resolve()
    environment = os.environ.copy()
    environment["PYTHONPATH"] = os.pathsep.join(str(p) for pattern in
        ("lib/python*/site-packages", "lib/python*/dist-packages") for p in prefix.glob(pattern))
    environment["LD_LIBRARY_PATH"] = os.pathsep.join(sorted({str(p.parent) for p in prefix.rglob("libgnuradio-isdbt.so*")}))
    command = [sys.executable, str(args.decoder.resolve()), "--profile", "onesegcast-mode3-gi16-qpsk23-ti4"]
    args.output.mkdir(parents=True, exist_ok=True)

    def decode(name, data, extra=()):
        result = subprocess.run(command + list(extra), input=data, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, env=environment, timeout=30)
        (args.output / (name + ".stderr.log")).write_bytes(result.stderr)
        assert result.returncode == 0, (name, result.returncode, result.stderr[-3000:])
        events = []
        for line in result.stderr.splitlines():
            try:
                event = json.loads(line)
                if isinstance(event, dict):
                    events.append(event)
            except (ValueError, UnicodeDecodeError):
                pass
        return result.stdout, events

    output, events = decode("backend", b"", ["--check-backend"])
    assert not output
    assert any(event.get("event") == "backend_ready" and event.get("parameter_observation") is True
               and event.get("parameter_autodetection") is False for event in events)
    output, events = decode("zero-iq", bytes(1152000))
    assert not output and not any(event.get("event") == "tmcc_parameters" for event in events)
    assert args.recorded_iq.stat().st_size < 64000000
    output, events = decode("recorded-iq", args.recorded_iq.read_bytes())
    assert output and len(output) % 188 == 0
    assert all(output[offset] == 0x47 for offset in range(0, len(output), 188))
    received = [event for event in events if event.get("event") == "tmcc_parameters"]
    assert received, ("No stable native TMCC parameters", events)
    for event in received:
        assert event["parity_validated"] is True and event["confirmation_frames"] == 2
        assert event["mode"] == 3 and event["guard_denominator"] == 16
        assert event["modulation"] == "QPSK" and event["code_rate"] == "2/3" and event["time_interleaving"] == 4
    report = dict(origin="offline recorded IQ; no LR2021 or audible output", events=received,
                  ts_packets=len(output) // 188, automatic_parameter_acquisition=False)
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decoder", type=Path, required=True)
    parser.add_argument("--backend-prefix", type=Path, required=True)
    parser.add_argument("--recorded-iq", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())
