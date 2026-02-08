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

enum class TeamPairingRole : uint8_t
{
    None = 0,
    Leader = 1,
    Member = 2
};

enum class TeamPairingState : uint8_t
{
    Idle = 0,
    LeaderBeacon = 1,
    MemberScanning = 2,
    JoinSent = 3,
    WaitingKey = 4,
    Completed = 5,
    Failed = 6
};

} // namespace team
