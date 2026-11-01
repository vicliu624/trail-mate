"""Exercise the real probe parser before --help exits; never open a radio."""
import subprocess
import sys

probe = sys.argv[1]
for rate in (450000, 500000):
    for clock in (8350000, 8700000, 16000000):
        for gain in (0, 9, 13):
            result = subprocess.run(
                [probe, "--sample-rate", str(rate), "--spi-clock", str(clock),
                 "--gain-step", str(gain), "--help"], capture_output=True, text=True, timeout=10)
            assert result.returncode == 0, (rate, clock, gain, result.stderr)
            assert "Engineering RX tool" in result.stdout
for option, value in (("--sample-rate", "500001"), ("--spi-clock", "16000001"),
                      ("--gain-step", "14"), ("--seconds", "3601"),
                      ("--sample-rate", "-1"), ("--spi-clock", "8.7MHz")):
    result = subprocess.run([probe, option, value, "--help"], capture_output=True, text=True, timeout=10)
    assert result.returncode != 0, (option, value)
print("Real capture-probe CLI ranges passed; no radio opened")
