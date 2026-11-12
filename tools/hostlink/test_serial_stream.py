"""Regression coverage for initial native USB CDC line state and write bounds."""
import importlib.util
import io
import json
from pathlib import Path
import sys
import time
import types
import unittest
from unittest.mock import patch


class SerialStreamTest(unittest.TestCase):
    def run_helper(self, commands):
        state = {"writes": [], "closed": False, "initial_signals": None}

        class Serial:
            def __init__(self, **_):
                self.dtr = self.rts = False
                self.is_open = False

            def open(self):
                state["initial_signals"] = (self.dtr, self.rts)
                if not (self.dtr and self.rts):
                    raise OSError("Native CDC driver needs both signals in the initial DCB")
                self.is_open = True

            def read(self, _):
                time.sleep(0.001)
                return b""

            def write(self, value):
                state["writes"].append(value)
                return len(value)

            def close(self):
                state["closed"] = True
                self.is_open = False

        helper = Path(__file__).with_name("serial_stream.py")
        spec = importlib.util.spec_from_file_location("stream_under_test", helper)
        module = importlib.util.module_from_spec(spec)
        output = io.StringIO()
        with patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=Serial)}):
            spec.loader.exec_module(module)
        with patch.object(sys, "argv", [str(helper), "COM_TEST", "115200"]), \
                patch.object(sys, "stdin", io.StringIO("".join(json.dumps(c) + "\n" for c in commands))), \
                patch.object(sys, "stdout", output):
            code = module.main()
        return code, state, [json.loads(line) for line in output.getvalue().splitlines()]

    def test_opens_connected_and_closes_after_bounded_duplex_commands(self):
        code, state, replies = self.run_helper([
            {"id": 1, "signals": {"dataTerminalReady": True, "requestToSend": True}},
            {"id": 2, "write": "484c0101000000000000"},
        ])
        self.assertEqual(code, 0)
        self.assertEqual(state["initial_signals"], (True, True))
        self.assertEqual(len(state["writes"]), 1)
        self.assertTrue(state["closed"])
        self.assertEqual(replies[-1], {"id": 2, "ok": True})

    def test_rejects_oversized_write_before_touching_device_and_closes(self):
        code, state, replies = self.run_helper([{"id": 1, "write": "00" * 523}])
        self.assertEqual(code, 1)
        self.assertEqual(state["writes"], [])
        self.assertTrue(state["closed"])
        self.assertIn("error", replies[-1])


if __name__ == "__main__":
    unittest.main()
