"""Real CLI composition rejects missing hardware and never emits fake IQ."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

executable, source_profile = map(Path, sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="oneseg-probe-", dir=Path.cwd()) as temporary:
    root = Path(temporary)
    profile = root / "cm0.conf"
    profile.write_text(
        source_profile.read_text() +
        f"\nradio_spi={root / 'absent-spi'}\nradio_gpiochip={root / 'absent-gpio'}\n"
    )
    environment = os.environ.copy()
    environment["TRAIL_MATE_RUNTIME_MODE"] = "mesh"
    environment.pop("TRAIL_MATE_LORA_SIMULATED", None)
    result = subprocess.run(
        [str(executable), "--profile", str(profile), "--channel", "13", "--seconds", "1"],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        env=environment, timeout=5,
    )
    assert result.returncode == 3, (result.returncode, result.stderr)
    assert result.stdout == b"", "Missing hardware produced IQ bytes"
    assert b"state=error" in result.stderr and b"backend is unavailable" in result.stderr
    marker = root / "decoder-started"
    environment["TRAIL_MATE_ONESEG_TEST_START_MARKER"] = str(marker)
    fixture = Path(__file__).resolve().parents[3] / "tests/oneseg/decoder_child_fixture.py"
    integrated = subprocess.run(
        [str(executable), "--profile", str(profile), "--decoder", str(fixture), "--seconds", "1"],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        env=environment, timeout=5,
    )
    assert integrated.returncode == 3 and not integrated.stdout and not marker.exists()
    assert b"backend is unavailable" in integrated.stderr
    if os.environ.get("TRAIL_MATE_TEST_ONESEG_MEDIA") == "1":
        media = subprocess.run(
            [str(executable), "--profile", str(profile), "--decoder", str(fixture), "--media", "--seconds", "1"],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=environment, timeout=5,
        )
        assert media.returncode == 3 and not media.stdout and not marker.exists()
        assert b"backend is unavailable" in media.stderr
    environment["TRAIL_MATE_RUNTIME_MODE"] = "demo"
    rejected = subprocess.run(
        [str(executable), "--profile", str(profile)], stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment, timeout=5,
    )
    assert rejected.returncode == 2 and rejected.stdout == b""
    assert b"real CM0 LR2021 composition is required" in rejected.stderr
print("1seg CLI: missing hardware and simulated composition rejected without IQ output")
