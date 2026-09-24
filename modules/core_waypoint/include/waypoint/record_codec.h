#pragma once
#include "waypoint/waypoint.h"
#include <cstddef>

namespace waypoint
{
constexpr std::size_t kEncodedBytes = 64;
enum class SlotState : uint8_t
{
    Empty,
    Active,
    Deleted
};
struct Slot
{
    Record record;
    uint64_t generation = 0;
    SlotState state = SlotState::Empty;
};
// Exactly 64 bytes, little endian, versioned and CRC protected. Deleted slots
// retain their id; a store can recover the high-water mark by scanning slots.
bool encode(const Slot& slot, uint8_t* bytes, std::size_t capacity);
bool decode(const uint8_t* bytes, std::size_t size, Slot& out);
} // namespace waypoint
