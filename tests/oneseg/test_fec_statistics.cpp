#include "platform/linux/oneseg_ofdm_wire.h"
#include <cassert>
#include <string>
using namespace platform::linux_runtime;
int main()
{
    OneSegOfdmWire wire;
    OneSegPhaseObservation phase;
    OneSegFecStatistics stats;
    const auto feed = [&](const std::string& text)
    {
        auto result = OneSegOfdmWire::Result::Ordinary;
        for (uint8_t byte : text) result = wire.feed(byte, phase, nullptr, nullptr, &stats);
        return result;
    };
    assert(feed("\x1eTM_FEC1 64 16 32 16 144 8\x1f") == OneSegOfdmWire::Result::Fec);
    assert(stats.valid() && stats.words == 64 && stats.uncorrectable == 16);
    OneSegFecHistory history;
    assert(!history.append(stats, OneSegLabSource::Synthetic, 100));
    assert(history.append(stats, OneSegLabSource::RecordedIq, 100));
    assert(!history.append(stats, OneSegLabSource::RecordedIq, 101));
    stats.words = 128;
    stats.clean = 80;
    assert(history.append(stats, OneSegLabSource::RecordedIq, 200));
    assert(history.fresh(1700) && !history.fresh(1701));
    assert(feed("\x1eTM_FEC1 64 17 32 16 144 8\x1f") == OneSegOfdmWire::Result::Invalid);
    assert(feed("\x1eTM_FEC1 64 16 32 16 144 9\x1f") == OneSegOfdmWire::Result::Invalid);
    assert(feed("\x1eTM_FEC1 64 16 32 16 999 8\x1f") == OneSegOfdmWire::Result::Invalid);
    assert(feed("\x1eTM_FEC1 -1 0 0 0 0 0\x1f") == OneSegOfdmWire::Result::Invalid);
}
