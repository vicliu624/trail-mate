import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('ota', Path(__file__).resolve().parents[1] / 'scripts/check_ota_slot_size.py')
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)


def entry(kind, subtype, size):
    return struct.pack('<HBBII16sI', 0x50AA, kind, subtype, 0x10000, size, b'app', 0)


class OtaSizeTest(unittest.TestCase):
    def test_smallest_app_slot(self):
        table = entry(1, 2, 0x1000) + entry(0, 0x10, 0x600000) + entry(0, 0x11, 0x400000)
        self.assertEqual(ota.ota_capacity(table), 0x400000)

    def test_six_mib_with_checksum(self):
        table = entry(0, 0x10, 0x600000) + b'\xeb\xeb' + bytes(30)
        self.assertEqual(ota.ota_capacity(table), 0x600000)

    def test_invalid_or_missing(self):
        for table in (b'', bytes(32), b'\xff' * 32, entry(0, 0, 0x600000), entry(0, 0x10, 0)):
            with self.assertRaises(ValueError):
                ota.ota_capacity(table)


if __name__ == '__main__':
    unittest.main()
