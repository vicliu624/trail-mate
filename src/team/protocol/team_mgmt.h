#pragma once

#include "team_wire.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace team::proto
{

constexpr uint8_t kTeamMgmtVersion = 1;
constexpr size_t kTeamMemberListHashSize = 32;
constexpr size_t kTeamChannelPskSize = 16;
constexpr size_t kTeamStatusMaxMembers = 8;

enum class TeamMgmtType : uint8_t
{
    Status = 5,
    Rotate = 6,
    Leave = 7,
    Disband = 8,
    Kick = 10,
    TransferLeader = 11,
    KeyDist = 12
};

struct TeamParams
{
    uint32_t position_interval_ms = 0;
    uint8_t precision_level = 0;
    uint32_t flags = 0;
    bool has_params = false;
};

struct TeamKick
{
    uint32_t target = 0;
};

struct TeamTransferLeader
{
    uint32_t target = 0;
};

struct TeamKeyDist
{
    std::array<uint8_t, kTeamIdSize> team_id{};
    uint32_t key_id = 0;
    std::array<uint8_t, kTeamChannelPskSize> channel_psk{};
    uint8_t channel_psk_len = 0;
};

struct TeamStatus
{
    std::array<uint8_t, kTeamMemberListHashSize> member_list_hash{};
    uint32_t key_id = 0;
    TeamParams params;
    uint32_t leader_id = 0;
    std::vector<uint32_t> members;
    bool has_members = false;
};

bool encodeTeamMgmtMessage(TeamMgmtType type,
                           const std::vector<uint8_t>& payload,
                           std::vector<uint8_t>& out);
bool decodeTeamMgmtMessage(const uint8_t* data, size_t len,
                           uint8_t* out_version,
                           TeamMgmtType* out_type,
                           std::vector<uint8_t>& out_payload);

bool encodeTeamKick(const TeamKick& input, std::vector<uint8_t>& out);
bool decodeTeamKick(const uint8_t* data, size_t len, TeamKick* out);

bool encodeTeamTransferLeader(const TeamTransferLeader& input, std::vector<uint8_t>& out);
bool decodeTeamTransferLeader(const uint8_t* data, size_t len, TeamTransferLeader* out);

bool encodeTeamKeyDist(const TeamKeyDist& input, std::vector<uint8_t>& out);
bool decodeTeamKeyDist(const uint8_t* data, size_t len, TeamKeyDist* out);

bool encodeTeamStatus(const TeamStatus& input, std::vector<uint8_t>& out);
bool decodeTeamStatus(const uint8_t* data, size_t len, TeamStatus* out);

} // namespace team::proto
