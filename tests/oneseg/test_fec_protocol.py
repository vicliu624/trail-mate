import importlib.util
from pathlib import Path
import unittest
path = Path(__file__).resolve().parents[2]/'apps/linux_lvgl/tools/oneseg_ofdm_diagnostics.py'
spec = importlib.util.spec_from_file_location('diagnostics', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
class Fec(unittest.TestCase):
    def sample(self):
        return dict(schema=1, words=64, clean=16, corrected=32, uncorrectable=16, symbols=144, maximum=8)
    def test_exact_native_outcomes(self):
        self.assertEqual(module.fec_record(self.sample()), b'\x1eTM_FEC1 64 16 32 16 144 8\x1f')
    def test_reject_inconsistent_and_overflow(self):
        for key, value in [('words', 65), ('symbols', 999), ('maximum', 9), ('clean', -1), ('corrected', True), ('words', 2**64)]:
            sample = self.sample(); sample[key] = value
            with self.assertRaises(ValueError): module.fec_record(sample)
if __name__ == '__main__': unittest.main()
