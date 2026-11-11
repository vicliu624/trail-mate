"""Recovery failure, reboot and wire compatibility regression tests (no RF)."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name, REPO / "scripts" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class WireTests(unittest.TestCase):
    def test_known_standard_vector_and_bounds(self):
        probe = load("linux_espnow_probe")
        vector = bytes.fromhex("7f18fe3401020304dd0818fe340401616263")
        self.assertEqual(probe.encode_body(b"abc", bytes((1, 2, 3, 4))), vector)
        self.assertEqual(probe.decode_body(vector), b"abc")
        self.assertEqual(len(probe.encode_body(bytes(250), bytes(4))), 265)
        with self.assertRaises(ValueError):
            probe.encode_body(bytes(251), bytes(4))
        with self.assertRaises(ValueError):
            probe.decode_body(vector[:-1])
        with self.assertRaises(ValueError):
            probe.encode_body(b"abc", bytes(3))


@unittest.skipUnless(sys.platform.startswith("linux"), "Linux recovery uses flock and CLOCK_BOOTTIME")
class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.guard = load("linux_espnow_guard")
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.guard.ROOT = root
        self.guard.STATE = root / "armed.json"
        self.guard.BOOT = root / "boot"
        self.guard.BOOT.write_text("original-boot")
        self.now = self.guard.time.clock_gettime(self.guard.time.CLOCK_BOOTTIME)
        self.state = {"uuid": "saved-connection", "interface": "wlan0", "boot_id": "original-boot",
                      "deadline_boottime": self.now + 1000}
        self.guard.STATE.write_text(json.dumps(self.state))
        self.commands = []

    def tearDown(self):
        self.temp.cleanup()

    def command(self, *argv, **options):
        self.commands.append(argv)
        if argv[:2] == ("iw", "dev"):
            return "type managed"
        if "GENERAL.CON-UUID" in argv:
            return "saved-connection"
        if argv[:2] == ("ip", "-4"):
            return "default via 192.0.2.1 dev wlan0"
        return ""

    def test_deadline_does_not_interrupt_a_live_session(self):
        with patch.object(self.guard, "command", self.command):
            self.guard.recover()
        self.assertEqual(self.commands, [])
        self.assertTrue(self.guard.STATE.exists())

    def test_reboot_ignores_the_previous_monotonic_deadline(self):
        self.guard.BOOT.write_text("new-boot")
        with patch.object(self.guard, "command", self.command):
            self.guard.recover()
        self.assertFalse(self.guard.STATE.exists())
        self.assertTrue((self.guard.ROOT / "last-recovery.json").exists())
        self.assertIn(("systemctl", "stop", "trailmate-espnow-helper.service"), self.commands)

    def test_failed_activation_retains_state_for_independent_retry(self):
        def fail(*argv, **options):
            if "connection" in argv:
                raise RuntimeError("NetworkManager not ready yet")
            return self.command(*argv, **options)
        with patch.object(self.guard, "command", fail):
            with self.assertRaises(RuntimeError):
                self.guard.recover(force=True)
        self.assertTrue(self.guard.STATE.exists())
        with patch.object(self.guard, "command", self.command):
            self.guard.recover(force=True)
        self.assertFalse(self.guard.STATE.exists())

    def test_route_failure_never_disarms_recovery(self):
        def no_route(*argv, **options):
            return "" if argv[:2] == ("ip", "-4") else self.command(*argv, **options)
        with patch.object(self.guard, "command", no_route):
            with self.assertRaises(RuntimeError):
                self.guard.recover(force=True)
        self.assertTrue(self.guard.STATE.exists())

    def test_original_power_is_durable_before_any_power_change(self):
        class BrokenFirmware:
            closed = False
            def power_policy(self):
                return {"mpc": 1, "pm": 2}
            def set_power_policy(self, value):
                saved = json.loads(self_state.read_text())
                self_test.assertEqual(saved["radio_power"], {"mpc": 1, "pm": 2})
                raise RuntimeError("Firmware setter failed")
            def close(self):
                self.closed = True
        self_test, self_state = self, self.guard.STATE
        firmware = BrokenFirmware()
        with patch.object(self.guard, "radio", return_value=firmware):
            with self.assertRaises(RuntimeError):
                self.guard.prepare_radio()
        self.assertTrue(firmware.closed)
        self.assertEqual(json.loads(self.guard.STATE.read_text())["radio_power"], {"mpc": 1, "pm": 2})


if __name__ == "__main__":
    unittest.main()
