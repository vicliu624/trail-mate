#pragma once

#include "../domain/team_types.h"
#include <cstddef>
#include <cstdint>

namespace team
{

struct TeamPairingStatus
{
    TeamPairingRole role = TeamPairingRole::None;
    TeamPairingState state = TeamPairingState::Idle;
    TeamId team_id{};
    bool has_team_id = false;
    uint32_t key_id = 0;
    uint32_t peer_id = 0;
    char team_name[16] = {0};
    bool has_team_name = false;
};

class TeamPairingService
{
  public:
    virtual ~TeamPairingService() = default;

    virtual bool startLeader(const TeamId& team_id,
                             uint32_t key_id,
                             const uint8_t* psk,
                             size_t psk_len,
                             uint32_t leader_id,
                             const char* team_name) = 0;
    virtual bool startMember(uint32_t self_id) = 0;
    virtual void stop() = 0;
    virtual void update() = 0;

    virtual TeamPairingStatus getStatus() const = 0;
};

} // namespace team
