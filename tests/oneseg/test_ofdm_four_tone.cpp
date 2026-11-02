#include "platform/linux/oneseg_ofdm_carriers.h"
#include <cassert>

using namespace platform::linux_runtime;
int main()
{
    OneSegFourToneProbe p;
    p.geometry = OneSegCarrierGeometry::centerSegment(3, 16, true);
    p.selected_bin = 7;
    assert(std::abs(p.receivedBin() - std::complex<double>(1, 0)) < 1e-11);
    p.epsilon = .25;
    std::complex<double> expected{};
    for (int tone = -1; tone <= 2; ++tone) expected += OneSegFourToneProbe::weight(0, tone) * oneseg_carrier_projection(tone + p.epsilon, p.geometry.fft_length);
    assert(std::abs(p.receivedBin() - expected) < 1e-11);
    p.epsilon = 1;
    assert(std::abs(p.receivedBin() - OneSegFourToneProbe::weight(0, -1)) < 1e-11);
    assert(std::abs(p.receivedBin() - OneSegFourToneProbe::weight(0, 0)) > 1);
    p.epsilon = 0;
    p.echo_gain = .6;
    for (double delay : {0., 18., 36.})
    {
        p.delay = delay;
        assert(std::abs(p.receivedBin() - p.diagonalChannel()) < 1e-11);
    }
    p.delay = 60;
    assert(std::abs(p.receivedBin() - p.diagonalChannel()) > 1e-3);
    p.echo_gain = 0;
    p.window_shift = -18;
    assert(std::abs(p.receivedBin() - p.diagonalChannel()) < 1e-11);
    p.window_shift = 18;
    assert(std::abs(p.receivedBin() - p.diagonalChannel()) > 1e-3);
    p.window_shift = 0;
    for (int symbol = 0; symbol < 4; ++symbol)
    {
        p.symbol_index = symbol;
        assert(std::abs(p.receivedBin() - OneSegFourToneProbe::weight(symbol, 0)) < 1e-11);
    }
}
