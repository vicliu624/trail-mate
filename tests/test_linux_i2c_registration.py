import importlib.util
from pathlib import Path
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / "platform/linux/common/tools/register_i2c_peripheral.py"
spec = importlib.util.spec_from_file_location("register_i2c_peripheral", SOURCE)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class RegistrationTest(unittest.TestCase):
    def test_selection_registration_and_idempotency(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tree = root / "tree"
            (tree / "soc/i2c").mkdir(parents=True)
            buses = root / "devices"
            adapter = buses / "i2c-19"
            adapter.mkdir(parents=True)
            (adapter / "of_node").symlink_to(tree / "soc/i2c", target_is_directory=True)
            self.assertEqual(module.find_adapter(buses, tree, "/soc/i2c"), adapter)
            calls = []

            def create(selected, name, address):
                calls.append((selected, name, address))
                client = buses / "19-0032"
                (client / "rtc/rtc8").mkdir(parents=True)
                (client / "name").write_text(name)
                (client / "driver").mkdir()
                (client / "rtc/rtc8/since_epoch").write_text("1792571555\n")

            result = module.register_peripheral(adapter, "rx8130", 0x32, create, 0)
            self.assertTrue(result["created"])
            self.assertEqual(result["rtc"], "rtc8")
            self.assertFalse(module.register_peripheral(adapter, "rx8130", 0x32, create, 0)["created"])
            self.assertEqual(len(calls), 1)
            with self.assertRaises(RuntimeError):
                module.register_peripheral(adapter, "different-chip", 0x32, create, 0)
            other = buses / "i2c-20"
            other.mkdir()
            (other / "of_node").symlink_to(tree / "soc/i2c", target_is_directory=True)
            with self.assertRaises(RuntimeError):
                module.find_adapter(buses, tree, "/soc/i2c")
            with self.assertRaises(ValueError):
                module.find_adapter(buses, tree, "/../escape")
            (buses / "19-0032/rtc/rtc8/since_epoch").write_text("invalid")
            with self.assertRaises(RuntimeError):
                module.register_peripheral(adapter, "rx8130", 0x32, create, 0)


if __name__ == "__main__":
    unittest.main()
