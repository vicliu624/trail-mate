#include "waypoint/record_codec.h"
#include <cassert>
#include <cstring>

int main()
{
    waypoint::Slot slot;
    slot.state = waypoint::SlotState::Active;
    slot.generation = 0x123456789ABCDEF0ULL;
    slot.record.id = 42;
    slot.record.latitude_e7 = -900000000;
    slot.record.longitude_e7 = 1800000000;
    std::strcpy(slot.record.name, "Camp");
    uint8_t bytes[64];
    assert(waypoint::encode(slot, bytes, sizeof(bytes)));
    assert(bytes[4] == 0xF0 && bytes[11] == 0x12 && bytes[12] == 42);
    waypoint::Slot result;
    assert(waypoint::decode(bytes, sizeof(bytes), result));
    assert(result.record.latitude_e7 == slot.record.latitude_e7);
    assert(result.record.longitude_e7 == slot.record.longitude_e7);
    assert(result.generation == slot.generation && !std::strcmp(result.record.name, "Camp"));
    for (unsigned i = 0; i < 64; ++i)
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            bytes[i] ^= 1u << bit;
            assert(!waypoint::decode(bytes, sizeof(bytes), result) && !result.record.id);
            bytes[i] ^= 1u << bit;
        }
    for (unsigned size = 0; size < 64; ++size) assert(!waypoint::decode(bytes, size, result));
    slot.state = waypoint::SlotState::Deleted;
    assert(waypoint::encode(slot, bytes, sizeof(bytes)));
    assert(waypoint::decode(bytes, sizeof(bytes), result));
    assert(result.record.id == 42 && !result.record.name[0] && !result.record.latitude_e7);
    slot.state = waypoint::SlotState::Empty;
    assert(!waypoint::encode(slot, bytes, sizeof(bytes)));
    slot.record.id = 0;
    assert(waypoint::encode(slot, bytes, sizeof(bytes)));
    assert(waypoint::decode(bytes, sizeof(bytes), result) && result.state == waypoint::SlotState::Empty);
    assert(!waypoint::encode(slot, nullptr, 64));
    assert(!waypoint::encode(slot, bytes, 63));
}
