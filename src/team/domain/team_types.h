#pragma once

#include "../protocol/team_wire.h"
#include <array>
#include <cstdint>

namespace team
{

constexpr size_t kTeamKeySize = 32;

using TeamId = std::array<uint8_t, team::proto::kTeamIdSize>;

struct TeamKeys
{
    TeamId team_id{};
    uint32_t key_id = 0;
    std::array<uint8_t, kTeamKeySize> mgmt_key{};
    std::array<uint8_t, kTeamKeySize> pos_key{};
    std::array<uint8_t, kTeamKeySize> wp_key{};
    std::array<uint8_t, kTeamKeySize> chat_key{};
    bool valid = false;
};

} // namespace team
