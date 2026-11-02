import importlib.util
from pathlib import Path
import unittest
import numpy as np

root = Path(__file__).resolve().parents[2]/'apps/linux_lvgl/tools'
def module(name):
    spec = importlib.util.spec_from_file_location(name, root/(name+'.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result
lab = module('oneseg_synthetic_ofdm')
dsp = module('oneseg_ofdm_dsp')
protocol = module('oneseg_ofdm_diagnostics')

def settings(**changes):
    result = dsp.read_settings(lab.OfdmReference.rate, {})
    result.update(synthetic_snr_db=100, synthetic_cfo_hz=0, evm_window=16)
    result.update(changes)
    return result

def settle(parameters, count=160):
    model = lab.OfdmReference(parameters)
    for _ in range(count): row = model.step()
    return row

class Reference(unittest.TestCase):
    def test_complete_carrier_bursts_keep_generic_mapping_and_known_reference(self):
        carrier_protocol = module('oneseg_carrier_protocol')
        for mode in (1, 2, 3):
            model = lab.OfdmReference(settings(mode=mode, equalizer_floor=.04))
            seen = []
            for _ in range(17):
                model.step()
                frame = model.carrier_frame
                if frame is None:
                    continue
                seen.append(frame['sequence'])
                count = 108*2**(mode-1)
                self.assertEqual(frame['raw'].shape, (count,))
                self.assertNotIn(0, model.offsets)
                self.assertEqual(int(model.pilot_mask.sum()), count//12)
                expected = model.symbol(frame['sequence']-1)[1]
                np.testing.assert_array_equal(frame['pilot_reference'], expected.astype(np.complex64))
                y, h = frame['raw'], frame['channel']
                np.testing.assert_allclose(frame['equalized'], y*h.conj()/(abs(h)**2+.04), rtol=2e-5, atol=2e-5)
                self.assertEqual(len(carrier_protocol.records(frame, layout=1)), (count+63)//64)
            self.assertEqual(seen, [1,2,3,4,13,14,15,16])

    def test_known_qam_roundtrip_and_determinism(self):
        for order in (4, 16, 64):
            row = settle(settings(constellation=order), 40)
            self.assertLess(row[1][1], .001)
            self.assertEqual(row[1][4], 0)
        a = lab.OfdmReference(settings()).step()
        b = lab.OfdmReference(settings()).step()
        self.assertEqual(a[0], b[0])
        self.assertEqual(a[1], b[1])

    def test_noise_increases_reference_error(self):
        low = settle(settings(synthetic_snr_db=40))[1][1]
        high = settle(settings(synthetic_snr_db=10))[1][1]
        self.assertGreater(high, low*4)

    def test_modes_and_guard_lengths(self):
        for mode in (1, 2, 3):
            for guard in (4, 8, 16, 32):
                _, truth, *_ = settle(settings(mode=mode, guard_denominator=guard), 24)
                self.assertLess(truth[1], .001)
                self.assertEqual(truth[4], 0)

    def test_integer_and_fractional_cfo_tracking(self):
        phase, truth, *_ = settle(settings(synthetic_cfo_hz=1400), 240)
        self.assertAlmostEqual(truth[0], 2*np.pi*1400*1024/lab.OfdmReference.rate, places=3)
        self.assertLess(abs(phase['corrected']-truth[0]), .1)
        self.assertTrue(lab.reference_record(protocol, phase, truth).startswith(b'\x1eTM_SYNTH1 '))

    def test_iq_calibration_has_measured_effect(self):
        corrupted = settle(settings(synthetic_iq_gain_db=3, synthetic_iq_phase_degrees=10))[1][1]
        corrected = settle(settings(synthetic_iq_gain_db=3, synthetic_iq_phase_degrees=10, iq_gain_db=3, iq_phase_degrees=10))[1][1]
        self.assertLess(corrected, corrupted/5)

    def test_echo_outside_guard_degrades_reference(self):
        short = settle(settings(synthetic_echo_gain=.3, synthetic_echo_delay=8))[1][1]
        long = settle(settings(synthetic_echo_gain=.3, synthetic_echo_delay=200))[1][1]
        self.assertGreater(long, short*3)

if __name__ == '__main__': unittest.main()
