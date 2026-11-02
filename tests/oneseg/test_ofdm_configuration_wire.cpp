#include "platform/linux/oneseg_ofdm_wire.h"
#include <cassert>
#include <string>
using namespace platform::linux_runtime;
int main()
{
    OneSegOfdmWire wire;
    OneSegPhaseObservation phase;
    OneSegNativeConfiguration config;
    const auto feed = [&](const std::string& input)
    {
        auto result = OneSegOfdmWire::Result::Ordinary;
        for (uint8_t byte : input) result = wire.feed(byte, phase, nullptr, &config);
        return result;
    };
    assert(feed("\x1eTM_CONFIG1 3 1024 64 432 1015873 .08 .003 .0008 .000003 10 4 8 .01 2\x1f") == OneSegOfdmWire::Result::Configuration);
    assert(config.valid() && config.channel_average == 8 && config.fft_length == 1024 && config.equalizer_floor == .01);
    assert(feed("\x1eTM_CONFIG1 3 1024 64 431 1015873 .08 .003 .0008 .000003 10 4 8 .01 2\x1f") == OneSegOfdmWire::Result::Invalid);
    assert(feed("\x1eTM_CONFIG1 3 1024 64 432 nan .08 .003 .0008 .000003 10 4 8 .01 2\x1f") == OneSegOfdmWire::Result::Invalid);
    assert(feed("\x1eTM_CONFIG1 3 1024 64 432 1015873 .08 .003 .0008 .000003 10 4 8\x1f") == OneSegOfdmWire::Result::Invalid);
}
