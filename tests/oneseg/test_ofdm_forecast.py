import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[2]/'apps/linux_lvgl/tools/oneseg_ofdm_diagnostics.py'
spec = importlib.util.spec_from_file_location('diagnostics', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class Forecast(unittest.TestCase):
    def sample(self):
        return dict(schema=2, symbol=64, prior=1., observed=1.2, corrected=1.08,
                    correction=.08, residual=.12, nco_phase=0., frequency_hz=150.,
                    sampling_ppm=3., prediction=1.02, integrator=.02, alpha=.2, beta=.1)

    def test_native_state_identities_and_legacy(self):
        sample = self.sample()
        self.assertTrue(module.phase_record(sample).startswith(b'\x1eTM_OFDM2 64 '))
        sample['schema'] = 1
        self.assertTrue(module.phase_record(sample).startswith(b'\x1eTM_OFDM1 64 '))

    def test_reject_inconsistent_or_nonfinite_projection(self):
        for key, value in [('prediction', 1.03), ('integrator', .03), ('alpha', -.1), ('beta', float('nan')), ('schema', True)]:
            sample = self.sample()
            sample[key] = value
            with self.assertRaises(ValueError): module.phase_record(sample)

if __name__ == '__main__': unittest.main()
