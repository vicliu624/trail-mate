#include "platform/linux/oneseg_event_schedule.h"
#include <cassert>
#include <iostream>
using namespace platform::linux_runtime;
int main()
{
    using State = OneSegEventSchedule::State;
    OneSegServiceInformation::Event event;
    assert(oneseg_event_schedule(event).start == State::Undefined);
    event.valid = true;
    // STD-B10 example: 1993-10-13 12:45:00 JST; duration 01:45:30.
    event.start = {0xc0, 0x79, 0x12, 0x45, 0x00};
    event.duration = {0x01, 0x45, 0x30};
    const auto schedule = oneseg_event_schedule(event);
    assert(schedule.start == State::Known && schedule.duration == State::Known);
    assert(schedule.start_utc_seconds == 750483900);
    assert(schedule.duration_seconds == 6330 && schedule.scheduledEndUtc() == 750490230);
    event.duration.fill(255); // E.g. emergency news with an undefined end.
    assert(oneseg_event_schedule(event).duration == State::Undefined);
    assert(!oneseg_event_schedule(event).scheduledEndUtc());
    event.start.fill(255);
    assert(oneseg_event_schedule(event).start == State::Undefined);
    event.start = {0x9e, 0x8b, 0x09, 0x00, 0x00}; // Epoch date at 09:00 JST == 00:00 UTC.
    assert(oneseg_event_schedule(event).start_utc_seconds == 0);
    event.start[2] = 0x24;
    assert(oneseg_event_schedule(event).start == State::Invalid);
    event.start[2] = 0x09;
    event.start[3] = 0x1a;
    assert(oneseg_event_schedule(event).start == State::Invalid);
    event.start[3] = 0;
    event.start[4] = 0x60;
    assert(oneseg_event_schedule(event).start == State::Invalid);
    event.start = {255, 255, 0x23, 0x59, 0x59};
    assert(oneseg_event_schedule(event).start_utc_seconds == 2155561199LL); // Beyond signed 32-bit time.
    event.duration = {0x99, 0x59, 0x59};
    assert(oneseg_event_schedule(event).duration_seconds == 359999);
    event.duration[1] = 0x60;
    assert(oneseg_event_schedule(event).duration == State::Invalid);
    assert(!oneseg_event_schedule(event).scheduledEndUtc());
    event.duration = {0, 0, 0};
    assert(oneseg_event_schedule(event).duration == State::Known);
    assert(oneseg_event_schedule(event).scheduledEndUtc() == 2155561199LL);
    std::cout << "EIT JST/MJD, undefined values, BCD bounds and post-2038 arithmetic passed\n";
}
