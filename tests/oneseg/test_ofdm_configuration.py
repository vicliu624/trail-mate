import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[2]/'apps/linux_lvgl/tools/oneseg_ofdm_diagnostics.py'
spec = importlib.util.spec_from_file_location('diagnostics', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class Configuration(unittest.TestCase):
    def sample(self):
        return dict(schema=1, mode=3, fft_length=1024, cp_length=64, carriers=432,
                    sample_rate=64000000/63, alpha=.08, beta=.003, timing_alpha=.0008,
                    timing_beta=.000003, integer_search=10, offset_confirmation=4,
                    channel_average=8, equalizer_floor=.01, sampling_ppm=2)

    def test_actual_native_configuration_frame(self):
        frame = module.configuration_record(self.sample())
        self.assertTrue(frame.startswith(b'\x1eTM_CONFIG1 3 1024 64 432 '))
        self.assertTrue(frame.endswith(b'\x1f'))
        self.assertLess(len(frame), 512)

    def test_reject_invalid_dimensions_and_readback(self):
        for key, value in [('schema', True), ('carriers', 431), ('mode', 4), ('cp_length', 300),
                           ('channel_average', 2.5), ('sample_rate', 0), ('alpha', float('nan'))]:
            sample = self.sample()
            sample[key] = value
            with self.assertRaises(ValueError): module.configuration_record(sample)

if __name__ == '__main__': unittest.main()
