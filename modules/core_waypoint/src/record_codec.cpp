#include "waypoint/record_codec.h"
#include <cstring>

namespace waypoint
{
namespace
{
uint32_t crc(const uint8_t* bytes)
{
    uint32_t value = 0xFFFFFFFF;
    for (unsigned i = 0; i < 60; ++i)
    {
        value ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1)));
    }
    return ~value;
}
void put(uint8_t* bytes, uint64_t value, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(value >> (8 * i));
}
uint64_t get(const uint8_t* bytes, unsigned count)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= uint64_t{bytes[i]} << (8 * i);
    return value;
}
int32_t signedCoordinate(const uint8_t* bytes)
{
    const auto value = get(bytes, 4);
    return static_cast<int32_t>(value <= 0x7FFFFFFF ? static_cast<int64_t>(value)
                                                    : static_cast<int64_t>(value) - 0x100000000LL);
}
bool validSlot(const Slot& slot)
{
    if (!slot.generation) return false;
    if (slot.state == SlotState::Active) return valid(slot.record);
    if (slot.state == SlotState::Deleted) return slot.record.id != 0;
    return slot.state == SlotState::Empty && slot.record.id == 0;
}
} // namespace

bool encode(const Slot& slot, uint8_t* bytes, std::size_t capacity)
{
    if (!bytes || capacity < kEncodedBytes || !validSlot(slot)) return false;
    std::memset(bytes, 0, kEncodedBytes);
    bytes[0] = 'W';
    bytes[1] = 'P';
    bytes[2] = 1;
    bytes[3] = static_cast<uint8_t>(slot.state);
    put(bytes + 4, slot.generation, 8);
    put(bytes + 12, slot.record.id, 4);
    if (slot.state == SlotState::Active)
    {
        put(bytes + 16, static_cast<uint32_t>(slot.record.latitude_e7), 4);
        put(bytes + 20, static_cast<uint32_t>(slot.record.longitude_e7), 4);
        std::memcpy(bytes + 24, slot.record.name, std::strlen(slot.record.name));
    }
    put(bytes + 60, crc(bytes), 4);
    return true;
}

bool decode(const uint8_t* bytes, std::size_t size, Slot& out)
{
    out = {};
    if (!bytes || size != kEncodedBytes || bytes[0] != 'W' || bytes[1] != 'P' || bytes[2] != 1 ||
        bytes[3] > static_cast<uint8_t>(SlotState::Deleted) || get(bytes + 60, 4) != crc(bytes)) return false;
    for (unsigned i = 56; i < 60; ++i)
        if (bytes[i]) return false;
    Slot slot;
    slot.state = static_cast<SlotState>(bytes[3]);
    slot.generation = get(bytes + 4, 8);
    slot.record.id = static_cast<uint32_t>(get(bytes + 12, 4));
    if (slot.state == SlotState::Active)
    {
        slot.record.latitude_e7 = signedCoordinate(bytes + 16);
        slot.record.longitude_e7 = signedCoordinate(bytes + 20);
        std::memcpy(slot.record.name, bytes + 24, sizeof(slot.record.name));
    }
    else
        for (unsigned i = 16; i < 56; ++i)
            if (bytes[i]) return false;
    if (!validSlot(slot)) return false;
    out = slot;
    return true;
}
} // namespace waypoint
