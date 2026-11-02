#include "platform/linux/oneseg_ofdm_wire.h"
#include <cassert>
#include <string>
using namespace platform::linux_runtime;
int main()
{
    OneSegOfdmWire decoder;
    OneSegPhaseObservation value;
    unsigned samples = 0, invalid = 0;
    const auto feed = [&](const std::string& input)
    {
        for (uint8_t byte : input)
        {
            const auto result = decoder.feed(byte, value);
            if (result == OneSegOfdmWire::Result::Sample) ++samples;
            if (result == OneSegOfdmWire::Result::Invalid) ++invalid;
        }
    };
    const std::string good = "\x1e"
                             "TM_OFDM1 32 1 1.1 1.05 0.05 0.05 0.2 150 3"
                             "\x1f";
    feed("ordinary native log\n" + good + "next log\n");
    assert(samples == 1 && invalid == 0 && value.symbol == 32 && value.frequency_hz == 150);
    feed("\x1e"
         "TM_OFDM1 32 nan 1.1 1.05 0.05 0.05 0.2 150 3"
         "\x1f");
    feed("\x1e"
         "TM_OFDM1 -1 1 1.1 1.05 0.05 0.05 0.2 150 3"
         "\x1f");
    feed("\x1e"
         "TM_OFDM1 32 1 1.1 1.05 0.05 0.05 0.2 150 3 junk"
         "\x1f");
    feed("\x1e" + std::string(600, 'x') + "\x1f");
    assert(samples == 1 && invalid == 4 && !decoder.incomplete());
    feed("\x1e"
         "truncated");
    assert(decoder.incomplete());
    feed(good);
    assert(samples == 2 && invalid == 5 && !decoder.incomplete());
}
