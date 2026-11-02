#include "platform/linux/oneseg_ofdm_wire.h"
#include <cassert>
#include <string>
using namespace platform::linux_runtime;
int main()
{
    OneSegOfdmWire wire;
    OneSegPhaseObservation value;
    const std::string record = "\x1eTM_SYNTH1 1 1 1.2 1.08 .08 .12 0 150 3 1.02 .02 .2 .1 1.19 .03 .01 1000 10 .001 30 20\x1f";
    auto result = OneSegOfdmWire::Result::Ordinary;
    for (uint8_t byte : record) result = wire.feed(byte, value);
    assert(result == OneSegOfdmWire::Result::Sample && value.has_reference && value.valid());
    assert(value.reference_ber == .01 && value.reference_errors == 10 && value.reference_bits == 1000);
    OneSegPhaseHistory history;
    assert(!history.append(value, OneSegLabSource::LiveReceiver, 0));
    assert(!history.append(value, OneSegLabSource::RecordedIq, 0));
    assert(history.append(value, OneSegLabSource::Synthetic, 0));
    value.reference_errors = 11;
    assert(!value.valid());
}
