#pragma once

#include <cstddef>
#include <cstdint>

namespace agenda
{

constexpr uint16_t kMaxActiveEvents = 64;
constexpr std::size_t kSlotBytes = 256;

enum class RecordState : uint8_t
{
    Empty,
    Active,
    Deleted,
};

enum class Repeat : uint8_t
{
    None,
    Daily,
    Weekly,
    Monthly,
    Yearly,
};

enum class LocationType : uint8_t
{
    None,
    Coordinate,
    Waypoint,
};

enum EventFlags : uint8_t
{
    HasReminder = 1 << 0,
    HasLocation = 1 << 1,
    HasNote = 1 << 2,
    HasEndTime = 1 << 3,
};

// Value object, never a serialized C++ memory image. The codec supplies the
// on-disk byte order, version and CRC independently of ABI/padding.
struct EventRecord
{
    int64_t start_time = 0;
    int64_t end_time = 0;
    uint32_t id = 0;
    uint32_t reminder_offset_sec = 0;
    int32_t latitude_e7 = 0;
    int32_t longitude_e7 = 0;
    RecordState state = RecordState::Empty;
    uint8_t flags = 0;
    Repeat repeat = Repeat::None;
    LocationType location_type = LocationType::None;
    char title[40]{};
    char location_name[32]{};
    char waypoint_id[16]{};
    char note[80]{};
};

static_assert(sizeof(EventRecord) == 208, "Agenda record RAM budget changed");

bool validEvent(const EventRecord& record);

} // namespace agenda
