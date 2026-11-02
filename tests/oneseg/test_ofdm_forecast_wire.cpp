#include "platform/linux/oneseg_ofdm_wire.h"
#include <cassert>
#include <string>
using namespace platform::linux_runtime;
int main()
{
    OneSegOfdmWire wire;
    OneSegPhaseObservation output;
    const auto feed = [&](const std::string& text)
    {
        auto result = OneSegOfdmWire::Result::Ordinary;
        for (unsigned char byte : text) result = wire.feed(byte, output);
        return result;
    };
    const std::string frame = "\x1eTM_OFDM2 64 1 1.2 1.08 .08 .12 0 150 3 1.02 .02 .2 .1\x1f";
    assert(feed(frame) == OneSegOfdmWire::Result::Sample);
    assert(output.has_prediction && output.prediction == 1.02 && output.integrator == .02);
    assert(std::abs(output.corrected - output.prediction - (output.alpha + output.beta) * (output.observed - output.prior)) < 1e-12);
    assert(feed("\x1eTM_OFDM2 65 1 1.2 1.08 .08 .12 0 150 3 1.04 .02 .2 .1\x1f") == OneSegOfdmWire::Result::Invalid);
    assert(feed("\x1eTM_OFDM2 65 1 1.2 1.08 .08 .12 0 150 3 1.02 .02 nan .1\x1f") == OneSegOfdmWire::Result::Invalid);
    assert(feed("\x1eTM_OFDM2 65 1 1.2 1.08 .08 .12 0 150 3 1.02 .02 .2\x1f") == OneSegOfdmWire::Result::Invalid);
    assert(feed("\x1eTM_OFDM1 96 1 1.1 1.05 .05 .05 0 150 3\x1f") == OneSegOfdmWire::Result::Sample);
    assert(!output.has_prediction); // Never invent a forecast for legacy data.
}
