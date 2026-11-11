#include "chat/linux_appdata_channel_access.h"
#include <cassert>
#include <limits>

int main()
{
    using trailmate::linux_app::AppDataChannelAccess;
    AppDataChannelAccess channel;
    assert(channel.ready(0));
    assert(!channel.mayTransmit(0));
    channel.observe(0, 0, 0, 0, -120, 0);
    assert(channel.ready(0));
    // A completed frame is followed by a peer's delayed relay. Do not put a
    // local application message into that exchange (the observed live failure).
    channel.observe(100, 1, 0, 0, -120, 0);
    assert(!channel.ready(1000));
    channel.observe(1800, 1, 1, 0, -120, 0);
    assert(!channel.ready(4100));
    assert(channel.ready(5800));
    // Detect a preamble before RX_DONE, and keep listening without consuming
    // any packet. Independent peers receive different contention delays.
    channel.observe(6000, 1, 1, 0, -40, 499);
    assert(!channel.ready(10498));
    assert(channel.ready(10499));
    channel.observe(10500, 1, 1, 1, -120, 0);
    assert(!channel.ready(14499));
    assert(channel.ready(14500));
    channel.observe(14500, 1, 1, 1, std::numeric_limits<float>::quiet_NaN(), 0);
    assert(channel.ready(14500));
    channel.reset();
    channel.observe(0xfffffff0, 0, 0, 0, -120, 0);
    channel.observe(0xfffffff1, 1, 0, 0, -120, 0);
    assert(!channel.ready(100));
    assert(channel.ready(3985));
    // LongFast at the current SF/BW needs about four seconds for the largest
    // peer packet plus the forwarding delay, and slower presets need more.
    const auto longfast = AppDataChannelAccess::guardForLoRa(250, 11, 5, 16);
    assert(longfast >= 3900 && longfast <= 4100);
    assert(AppDataChannelAccess::guardForLoRa(125, 12, 5, 16) > longfast);
    assert(AppDataChannelAccess::guardForLoRa(0, 0, 0, 0) == 4000);

    channel.reset();
    channel.observe(100, 0, 0, 0, -120, 0);
    channel.beginAttempt(100, 1000);
    assert(!channel.ready(1349));
    channel.beginAttempt(900, 0); // Polling must not postpone an existing attempt.
    assert(channel.ready(1350));
    assert(channel.mayTransmit(1350));
    channel.completeAttempt();
    assert(!channel.mayTransmit(1400));
    channel.beginAttempt(1400, 0);
    assert(!channel.ready(1649));
    assert(channel.ready(1650));
    channel.completeAttempt();
    channel.observe(1700, 1, 0, 0, -120, 0);
    channel.beginAttempt(1800, 0); // Starting a frame cannot shorten a relay guard.
    assert(!channel.ready(5699));
    assert(channel.ready(5700));
    channel.reset();
    channel.beginAttempt(0xfffffff0, 0);
    assert(!channel.ready(100));
    assert(channel.ready(234));
}
