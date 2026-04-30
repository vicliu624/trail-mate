#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace team
{
namespace ui
{

constexpr size_t kTeamMaxMembers = 4;
constexpr uint8_t kTeamColorUnassigned = 0xFF;
static constexpr uint32_t kTeamMemberColors[kTeamMaxMembers] = {
    0xFF3B30, // red
    0x34C759, // green
    0x007AFF, // blue
    0xFFCC00  // yellow
};

inline uint32_t team_color_from_index(uint8_t index)
{
    if (index >= kTeamMaxMembers)
    {
        return kTeamMemberColors[0];
    }
    return kTeamMemberColors[index];
}

inline uint8_t team_color_index_from_node_id(uint32_t node_id)
{
    uint32_t h = node_id;
    h ^= h >> 16;
    h *= 0x7feb352d;
    h ^= h >> 15;
    h *= 0x846ca68b;
    h ^= h >> 16;
    return static_cast<uint8_t>(h % kTeamMaxMembers);
}

struct TeamMemberUi
{
    uint32_t node_id = 0;
    std::string name;
    bool online = false;
    bool leader = false;
    uint32_t last_seen_s = 0;
    uint8_t color_index = kTeamColorUnassigned;
};

} // namespace ui
} // namespace team
