#include "agenda/domain/event_codec.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
    using namespace agenda;
    static_assert(kMaxActiveEvents == 64);
    uint8_t bytes[kSlotBytes]{};
    EventRecord event{};
    EventRecord decoded{};
    assert(encodeEvent(event, bytes));
    assert(decodeEvent(bytes, decoded) == DecodeResult::Ok);
    assert(decoded.state == RecordState::Empty);

    event.state = RecordState::Active;
    event.id = 42;
    event.start_time = 1790000000;
    std::strcpy(event.title, "Radio check");
    assert(validEvent(event)); // Point event: no location, reminder or note.
    assert(encodeEvent(event, bytes));
    assert(bytes[4] == 42 && bytes[5] == 0);
    assert(decodeEvent(bytes, decoded) == DecodeResult::Ok);
    assert(decoded.id == 42 && decoded.start_time == event.start_time);
    assert(std::strcmp(decoded.title, event.title) == 0 && decoded.flags == 0);

    event.flags = HasReminder | HasLocation | HasEndTime | HasNote;
    event.end_time = event.start_time + 3600;
    event.reminder_offset_sec = 600;
    event.location_type = LocationType::Coordinate;
    event.latitude_e7 = -900000000;
    event.longitude_e7 = -1800000000;
    std::strcpy(event.note, "Verify equipment");
    event.repeat = Repeat::Weekly;
    assert(encodeEvent(event, bytes));
    assert(decodeEvent(bytes, decoded) == DecodeResult::Ok);
    assert(decoded.latitude_e7 == event.latitude_e7);
    assert(decoded.longitude_e7 == event.longitude_e7);
    assert(decoded.end_time == event.end_time && decoded.repeat == Repeat::Weekly);
    for (std::size_t i = 0; i < kSlotBytes; ++i)
    {
        bytes[i] ^= 0x01;
        assert(decodeEvent(bytes, decoded) == DecodeResult::Corrupt);
        assert(decoded.state == RecordState::Empty);
        bytes[i] ^= 0x01;
    }

    event.end_time = event.start_time;
    assert(!validEvent(event));
    event.end_time += 3600;
    event.latitude_e7 = 900000001;
    assert(!validEvent(event));
    event.latitude_e7 = 0;
    event.reminder_offset_sec = 601;
    assert(!validEvent(event));
    event.reminder_offset_sec = 0;
    std::memset(event.title, 'a', sizeof(event.title));
    assert(!validEvent(event));
    std::strcpy(event.title, " \t");
    assert(!validEvent(event));
    std::strcpy(event.title, "Radio check");
    event.location_type = LocationType::Waypoint;
    assert(!validEvent(event));
    std::strcpy(event.waypoint_id, "camp");
    assert(validEvent(event));

    event.state = RecordState::Deleted;
    assert(encodeEvent(event, bytes));
    assert(decodeEvent(bytes, decoded) == DecodeResult::Ok);
    assert(decoded.state == RecordState::Deleted && decoded.id == 42);
    assert(decoded.title[0] == '\0');

    constexpr char known[] = "123456789";
    assert(recordCrc32(reinterpret_cast<const uint8_t*>(known), 9) == 0xcbf43926U);
    std::printf("EventRecord=%zu slot=%zu max_events=%u\n", sizeof(EventRecord), kSlotBytes, kMaxActiveEvents);
}
