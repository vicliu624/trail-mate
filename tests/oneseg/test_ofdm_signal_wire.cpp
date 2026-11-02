#include "platform/linux/oneseg_ofdm_wire.h"
#include <cassert>
#include <string>
using namespace platform::linux_runtime;
int main()
{
    OneSegOfdmWire wire;
    OneSegPhaseObservation phase;
    OneSegSignalMeasurement signal;
    auto feed = [&](const std::string& text)
    {
        auto result = OneSegOfdmWire::Result::Ordinary;
        for (unsigned char byte : text) result = wire.feed(byte, phase, &signal);
        return result;
    };
    assert(feed("\x1eTM_SIGNAL1 1 7 1000000 0.5 3 -100 -40 0 -10 100 -40\x1f") == OneSegOfdmWire::Result::Signal);
    assert(signal.kind == 1 && signal.sequence == 7 && signal.count == 3 && signal.points[1].y == -10);
    assert(feed("\x1eTM_SIGNAL1 4 7 1000000 0 2 0 1 10 1.1\x1f") == OneSegOfdmWire::Result::Invalid);
    assert(feed("\x1eTM_SIGNAL1 2 7 1000000 nan 1 1 1\x1f") == OneSegOfdmWire::Result::Invalid);
    assert(feed("\x1eTM_SIGNAL1 1 7 1000000 0 65\x1f") == OneSegOfdmWire::Result::Invalid);
    assert(feed("\x1eTM_SIGNAL1 1 7 1000000 0 2 0 1 0 1\x1f") == OneSegOfdmWire::Result::Invalid);
    assert(feed("\x1e" + std::string(4100, 'x') + "\x1f") == OneSegOfdmWire::Result::Buffered);
    assert(!wire.incomplete());
    assert(feed("\x1eTM_SIGNAL1 2 8 1000000 0.1 2 1 -1 -1 1\x1f") == OneSegOfdmWire::Result::Signal);
}
