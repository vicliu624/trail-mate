#include "chat/infra/lxmf/lxmf_announce_defaults.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_announce_scheduler.h"

#include <cassert>
#include <cstring>

int main()
{
    chat::lxmf::runtime::AnnounceScheduler scheduler;

    scheduler.resetAfterConfig(1000, false);
    assert(!scheduler.next(2000, false, 120000, 1500, 30000).should_send);
    assert(scheduler.next(2500, false, 120000, 1500, 30000).should_send);

    scheduler.completeAttempt(2500, true, false);
    assert(!scheduler.next(32000, false, 120000, 1500, 30000).should_send);
    assert(scheduler.next(32500, false, 120000, 1500, 30000).should_send);

    scheduler.completeAttempt(32500, true, true);
    assert(!scheduler.next(150000, false, 120000, 1500, 30000).should_send);
    assert(scheduler.next(153000, false, 120000, 1500, 30000).should_send);

    scheduler.resetAfterConfig(200000, true);
    assert(!scheduler.next(400000, true, 120000, 1500, 30000).should_send);
    assert(!scheduler.beginManualBroadcast(true));

    scheduler.resetAfterConfig(400000, false);
    scheduler.markIdentityChanged();
    assert(!scheduler.next(400500, false, 120000, 1500, 30000).should_send);
    assert(scheduler.next(401500, false, 120000, 1500, 30000).should_send);

    assert(scheduler.rebroadcastDue(500000, 60000));
    assert(!scheduler.rebroadcastDue(550000, 60000));
    assert(scheduler.rebroadcastDue(560000, 60000));

    // Production defaults, including unsigned millisecond wraparound. A failed
    // initial attempt retries promptly instead of waiting four hours.
    constexpr uint32_t interval = chat::lxmf::kDefaultAnnounceIntervalMs;
    static_assert(interval >= 3U * 60U * 60U * 1000U && interval <= 6U * 60U * 60U * 1000U);
    constexpr uint32_t start = UINT32_MAX - 2000U;
    scheduler.resetAfterConfig(start, false);
    assert(scheduler.next(start + 1500U, false, interval, 1500, 30000).should_send);
    scheduler.completeAttempt(start + 1500U, false, false);
    assert(!scheduler.next(start + 31499U, false, interval, 1500, 30000).should_send);
    assert(scheduler.next(start + 31500U, false, interval, 1500, 30000).should_send);
    scheduler.completeAttempt(start + 31500U, true, true);
    assert(!scheduler.next(start + 31500U + interval - 1U, false, interval, 1500, 30000).should_send);
    assert(scheduler.next(start + 31500U + interval, false, interval, 1500, 30000).should_send);
    assert(!scheduler.next(start + 31500U + interval, true, interval, 1500, 30000).should_send);
    assert(std::strcmp(chat::lxmf::announceDisplayName(nullptr), "Anonymous Peer") == 0);
    assert(std::strcmp(chat::lxmf::announceDisplayName("", ""), "Anonymous Peer") == 0);
    assert(std::strcmp(chat::lxmf::announceDisplayName("", "ABCD"), "ABCD") == 0);
    assert(std::strcmp(chat::lxmf::announceDisplayName("My Pager", "ABCD"), "My Pager") == 0);
    assert(std::strcmp(chat::lxmf::announceDisplayName("Cardputer Zero"), "Cardputer Zero") == 0);

    return 0;
}
