"""Numerical reference checks for real-sample diagnostic observations."""
import importlib.util
from pathlib import Path
import unittest
import numpy as np

path = Path(__file__).resolve().parents[2]/'apps/linux_lvgl/tools/oneseg_ofdm_measurements.py'
spec = importlib.util.spec_from_file_location('measurements', path)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class Measurements(unittest.TestCase):
    def test_deferred_evm_matches_latest_complete_symbols_and_owns_samples(self):
        rng = np.random.default_rng(74)
        for order in (4, 16, 64):
            window = m.DeferredSymbolEvmWindow(32, order, 108)
            history = np.empty((0, 108), dtype=np.complex64)
            with self.assertRaises(ValueError):
                window.value()
            for count in (1, 2, 17, 31, 5, 100, 3):
                batch = (rng.normal(size=(count, 108))+1j*rng.normal(size=(count, 108))).astype(np.complex64)
                history = np.concatenate((history, batch))[-32:]
                reference = m.SymbolEvmWindow(32, order)
                reference.append(history)
                window.append(batch)
                batch[:] = np.nan  # GNU Radio may reuse its input buffer immediately.
                self.assertAlmostEqual(window.value(), reference.value(), places=12)
            self.assertEqual(window.buffer.nbytes, 32*108*8)
            window.append(np.full((1, 108), np.nan))
            with self.assertRaises(ValueError):
                window.value()
            window.append(np.ones((32, 108), dtype=np.complex64))
            self.assertTrue(np.isfinite(window.value()))

    def test_channel_phase_unwraps_before_decimation(self):
        phase = np.linspace(0, 8*np.pi, 432)
        measured = m.channel_phase(np.exp(1j*phase))
        np.testing.assert_allclose(measured[:, 1], phase[measured[:, 0].astype(int)], atol=1e-5)
        self.assertTrue(m.record(5, 1, 64000000/63, 1., measured).startswith(b'\x1eTM_SIGNAL1 5 '))
    def test_evm_window_counts_symbols_across_scheduler_batches(self):
        symbol = np.full((1, 432), (1+1j)/np.sqrt(2), dtype=np.complex64)
        short, long = m.SymbolEvmWindow(16, 4), m.SymbolEvmWindow(32, 4)
        for window in (short, long):
            window.append(np.repeat(symbol, 16, axis=0))
            window.append(np.repeat(symbol+.1, 16, axis=0))
        self.assertAlmostEqual(short.value(), .1, places=6)
        self.assertAlmostEqual(long.value(), .1/np.sqrt(2), places=6)
        short.append(np.repeat(symbol, 100, axis=0))
        self.assertEqual(len(short.moments), 16)
        self.assertLess(short.value(), 1e-6)
        with self.assertRaises(ValueError):
            short.append(np.full((1, 432), np.nan))
        self.assertEqual(len(short.moments), 0)
        with self.assertRaises(ValueError):
            short.value()

    def test_symbol_evm_matches_all_supported_constellations(self):
        rng = np.random.default_rng(4)
        for order in (4, 16, 64):
            side = int(np.sqrt(order))
            levels = np.arange(1-side, side, 2)/np.sqrt(2*(order-1)/3)
            values = rng.choice(levels, (32, 108))+1j*rng.choice(levels, (32, 108))+.015j
            window = m.SymbolEvmWindow(32, order)
            window.append(values[:7])
            window.append(values[7:])
            _, expected = m.constellation(values, order, values.size)
            self.assertAlmostEqual(window.value(), expected, places=6)

    def test_spectrum_detects_signed_frequency(self):
        rate = 1024000
        for frequency in (-128000, 128000):
            iq = np.exp(2j*np.pi*frequency*np.arange(2048)/rate)
            measured = m.spectrum(iq, rate)
            peak = measured[np.argmax(measured[:, 1]), 0]
            self.assertLess(abs(peak-frequency), rate/64)
        with self.assertRaises(ValueError):
            m.spectrum(np.full(2048, np.nan), rate)

    def test_cp_peak_has_actual_symbol_offset(self):
        rng = np.random.default_rng(42)
        n, cp = 1024, 64
        useful = rng.normal(size=n)+1j*rng.normal(size=n)
        symbol = np.concatenate((useful[-cp:], useful))
        measured = m.cp_correlation(np.tile(symbol, 3), n, 16)
        self.assertAlmostEqual(measured[0, 1], 1., places=6)
        self.assertLess(np.median(measured[1:, 1]), .3)

    def test_known_constellation_and_added_error(self):
        symbols = np.tile(np.array([1+1j, 1-1j, -1+1j, -1-1j])/np.sqrt(2), 32)
        pairs, evm = m.constellation(symbols, 4)
        self.assertLess(evm, 1e-6)
        _, perturbed = m.constellation(symbols+.05, 4)
        self.assertAlmostEqual(perturbed, .05, places=6)
        self.assertEqual(pairs.shape, (64, 2))

    def test_channel_and_atomic_record(self):
        points = m.channel(np.full(432, .1+0j))
        np.testing.assert_allclose(points[:, 1], -20, atol=1e-5)
        record = m.record(3, 1, 64000000/63, 0., points)
        self.assertTrue(record.startswith(b'\x1eTM_SIGNAL1 3 1 '))
        self.assertTrue(record.endswith(b'\x1f'))
        self.assertLess(len(record), 4096)
        with self.assertRaises(ValueError):
            m.record(2, 1, 1e6, float('nan'), points)


if __name__ == '__main__':
    unittest.main()
