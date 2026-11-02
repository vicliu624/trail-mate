#include "platform/linux/oneseg_ofdm_carriers.h"
#include <cassert>

using namespace platform::linux_runtime;

int main()
{
    for (unsigned mode = 1; mode <= 3; ++mode)
    {
        const auto geometry = OneSegCarrierGeometry::centerSegment(mode, 16, true);
        const unsigned factor = 1u << (mode - 1);
        assert(geometry.valid());
        for (unsigned phase = 0; phase < 4; ++phase)
        {
            std::array<unsigned, 5> counts{};
            for (unsigned k = 0; k < geometry.active_carriers; ++k)
            {
                ++counts[static_cast<unsigned>(geometry.role(k, phase))];
                assert(static_cast<int>(geometry.shiftedBin(k)) - static_cast<int>(geometry.fft_length / 2) == geometry.signedBin(k));
            }
            assert(counts[0] == 0 && counts[1] == 96 * factor && counts[2] == 9 * factor && counts[3] == factor && counts[4] == 2 * factor);
        }
        assert(geometry.role(geometry.active_carriers, 0) == OneSegCarrierRole::Unused);
        for (unsigned guard : {4u, 8u, 16u, 32u})
        {
            const auto g = OneSegCarrierGeometry::centerSegment(mode, guard, true);
            assert(std::abs(g.guardSeconds() * guard - g.usefulSeconds()) < 1e-12);
        }
    }
    const auto full = OneSegCarrierGeometry::centerSegment(3, 16, false);
    const auto compact = OneSegCarrierGeometry::centerSegment(3, 16, true);
    assert(full.fft_length == 1024 && compact.fft_length == 576 && compact.cp_length == 36);
    assert(compact.broadcastFftLength() == 8192);
    assert(std::abs(full.spacingHz() - compact.spacingHz()) < 1e-10);
    assert(std::abs(compact.usefulSeconds() - .001008) < 1e-12);
    assert(std::abs(compact.guardSeconds() - .000063) < 1e-12);
    assert(compact.signedBin(216) == 0);
    assert(compact.genericSignedBin(215) == -1 && compact.genericSignedBin(216) == 1 && compact.genericSignedBin(431) == 216);
    assert(compact.genericRole(0) == OneSegCarrierRole::ReferencePilot);
    assert(compact.genericRole(1) == OneSegCarrierRole::Data);
    assert(compact.genericRole(432) == OneSegCarrierRole::Unused);
    assert(compact.role(0, 0) == OneSegCarrierRole::ScatteredPilot);
    assert(compact.role(0, 1) == OneSegCarrierRole::Data);
    assert(!OneSegCarrierGeometry::centerSegment(0, 16, false).valid());
    assert(!OneSegCarrierGeometry::centerSegment(3, 3, false).valid());

    // Compare the closed form with an independent direct DFT sum, including ICI.
    constexpr unsigned n = 576;
    for (double offset : {0., 1., -3., .25, -.7, 576.})
    {
        std::complex<double> sum{};
        for (unsigned t = 0; t < n; ++t)
            sum += std::polar(1., 6.28318530717958647692 * offset * t / n) / static_cast<double>(n);
        assert(std::abs(sum - oneseg_carrier_projection(offset, n)) < 1e-12);
    }
    double energy = 0;
    for (unsigned bin = 0; bin < n; ++bin) energy += std::norm(oneseg_carrier_projection(.3 - bin, n));
    assert(std::abs(energy - 1) < 1e-10);
    assert(std::abs(oneseg_carrier_projection(1, n)) < 1e-12);
    assert(std::abs(oneseg_carrier_wave(2, .25) - std::complex<double>(-1, 0)) < 1e-12);
    assert(std::abs(oneseg_carrier_echo(0, n, 1, 10, 0) - std::complex<double>(2, 0)) < 1e-12);
}
