#include "agenda/domain/event_codec.h"
#include "agenda/domain/recurrence.h"

#include <cstring>

namespace agenda
{
namespace
{
constexpr uint8_t kVersion = 1;
constexpr std::size_t kCrcOffset = kSlotBytes - 4;

void put32(uint8_t* out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        out[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t get32(const uint8_t* in)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(in[i]) << (8 * i);
    return value;
}

void put64(uint8_t* out, int64_t value)
{
    const auto bits = static_cast<uint64_t>(value);
    for (unsigned i = 0; i < 8; ++i)
        out[i] = static_cast<uint8_t>(bits >> (8 * i));
}

int64_t get64(const uint8_t* in)
{
    uint64_t bits = 0;
    for (unsigned i = 0; i < 8; ++i)
        bits |= static_cast<uint64_t>(in[i]) << (8 * i);
    // Avoid implementation-defined conversion of unsigned values above INT64_MAX.
    return (bits & (uint64_t{1} << 63)) ? -1 - static_cast<int64_t>(~bits) : static_cast<int64_t>(bits);
}

int32_t signed32(uint32_t bits)
{
    return (bits & 0x80000000U) ? -1 - static_cast<int32_t>(~bits) : static_cast<int32_t>(bits);
}

template <std::size_t N>
bool terminated(const char (&text)[N])
{
    return std::memchr(text, '\0', N) != nullptr;
}

bool hasTitle(const char* title)
{
    for (; *title; ++title)
        if (*title != ' ' && *title != '\t' && *title != '\r' && *title != '\n') return true;
    return false;
}

bool reminderPreset(uint32_t offset)
{
    return offset == 0 || offset == 600 || offset == 1800 || offset == 3600 || offset == 86400;
}
} // namespace

bool validEvent(const EventRecord& record)
{
    if (record.state > RecordState::Deleted) return false;
    if (record.state != RecordState::Active) return true;
    if (record.id == 0 || record.start_time < 0 || record.start_time > kLastCalendarSecond ||
        record.repeat > Repeat::Yearly || record.location_type > LocationType::Waypoint ||
        (record.flags & ~uint8_t{HasReminder | HasLocation | HasNote | HasEndTime}) != 0)
        return false;
    if (!terminated(record.title) || !terminated(record.location_name) ||
        !terminated(record.waypoint_id) || !terminated(record.note) || !hasTitle(record.title))
        return false;
    if ((record.flags & HasEndTime) &&
        (record.end_time <= record.start_time || record.end_time > kLastCalendarSecond)) return false;
    if ((record.flags & HasReminder) && !reminderPreset(record.reminder_offset_sec)) return false;
    if (record.flags & HasLocation)
    {
        if (record.location_type == LocationType::None ||
            record.latitude_e7 < -900000000 || record.latitude_e7 > 900000000 ||
            record.longitude_e7 < -1800000000 || record.longitude_e7 > 1800000000)
            return false;
        if (record.location_type == LocationType::Waypoint && record.waypoint_id[0] == '\0') return false;
    }
    return true;
}

uint32_t recordCrc32(const uint8_t* bytes, std::size_t size)
{
    uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool encodeEvent(const EventRecord& record, uint8_t (&out)[kSlotBytes])
{
    if (!validEvent(record)) return false;
    std::memset(out, 0, sizeof(out));
    out[0] = 'A';
    out[1] = 'G';
    out[2] = kVersion;
    out[3] = static_cast<uint8_t>(record.state);
    put32(out + 4, record.id);
    if (record.state == RecordState::Active)
    {
        put64(out + 8, record.start_time);
        put64(out + 16, record.end_time);
        put32(out + 24, record.reminder_offset_sec);
        put32(out + 28, static_cast<uint32_t>(record.latitude_e7));
        put32(out + 32, static_cast<uint32_t>(record.longitude_e7));
        out[36] = record.flags;
        out[37] = static_cast<uint8_t>(record.repeat);
        out[38] = static_cast<uint8_t>(record.location_type);
        std::memcpy(out + 40, record.title, sizeof(record.title));
        std::memcpy(out + 80, record.location_name, sizeof(record.location_name));
        std::memcpy(out + 112, record.waypoint_id, sizeof(record.waypoint_id));
        std::memcpy(out + 128, record.note, sizeof(record.note));
    }
    put32(out + kCrcOffset, recordCrc32(out, kCrcOffset));
    return true;
}

DecodeResult decodeEvent(const uint8_t (&bytes)[kSlotBytes], EventRecord& out)
{
    out = {};
    if (bytes[0] != 'A' || bytes[1] != 'G' ||
        recordCrc32(bytes, kCrcOffset) != get32(bytes + kCrcOffset))
        return DecodeResult::Corrupt;
    if (bytes[2] != kVersion) return DecodeResult::UnsupportedVersion;
    out.state = static_cast<RecordState>(bytes[3]);
    out.id = get32(bytes + 4);
    out.start_time = get64(bytes + 8);
    out.end_time = get64(bytes + 16);
    out.reminder_offset_sec = get32(bytes + 24);
    out.latitude_e7 = signed32(get32(bytes + 28));
    out.longitude_e7 = signed32(get32(bytes + 32));
    out.flags = bytes[36];
    out.repeat = static_cast<Repeat>(bytes[37]);
    out.location_type = static_cast<LocationType>(bytes[38]);
    std::memcpy(out.title, bytes + 40, sizeof(out.title));
    std::memcpy(out.location_name, bytes + 80, sizeof(out.location_name));
    std::memcpy(out.waypoint_id, bytes + 112, sizeof(out.waypoint_id));
    std::memcpy(out.note, bytes + 128, sizeof(out.note));
    if (validEvent(out)) return DecodeResult::Ok;
    out = {};
    return DecodeResult::InvalidRecord;
}

} // namespace agenda
