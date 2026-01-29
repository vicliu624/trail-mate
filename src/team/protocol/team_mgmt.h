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

enum class TeamMgmtType : uint8_t
{
    Advertise = 1,
    JoinRequest = 2,
    JoinAccept = 3,
    JoinConfirm = 4,
    Status = 5,
    Rotate = 6,
    Leave = 7,
    Disband = 8,
    JoinDecision = 9,
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

struct TeamAdvertise
{
    std::array<uint8_t, kTeamIdSize> team_id{};
    uint32_t join_hint = 0;
    uint8_t channel_index = 0;
    uint64_t expires_at = 0;
    uint64_t nonce = 0;
    bool has_join_hint = false;
    bool has_channel_index = false;
    bool has_expires_at = false;
};

struct TeamJoinRequest
{
    std::array<uint8_t, kTeamIdSize> team_id{};
    std::array<uint8_t, 32> member_pub{};
    uint8_t member_pub_len = 0;
    uint32_t capabilities = 0;
    uint64_t nonce = 0;
    bool has_member_pub = false;
    bool has_capabilities = false;
};

struct TeamJoinAccept
{
    std::array<uint8_t, kTeamIdSize> team_id{};
    uint8_t channel_index = 0;
    std::array<uint8_t, kTeamChannelPskSize> channel_psk{};
    uint8_t channel_psk_len = 0;
    uint32_t key_id = 0;
    TeamParams params;
    bool has_team_id = false;
};

struct TeamJoinConfirm
{
    bool ok = false;
    uint32_t capabilities = 0;
    uint8_t battery = 0;
    bool has_capabilities = false;
    bool has_battery = false;
};

struct TeamJoinDecision
{
    bool accept = false;
    uint32_t reason = 0;
    bool has_reason = false;
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
};

bool encodeTeamMgmtMessage(TeamMgmtType type,
                           const std::vector<uint8_t>& payload,
                           std::vector<uint8_t>& out);
bool decodeTeamMgmtMessage(const uint8_t* data, size_t len,
                           uint8_t* out_version,
                           TeamMgmtType* out_type,
                           std::vector<uint8_t>& out_payload);

bool encodeTeamAdvertise(const TeamAdvertise& input, std::vector<uint8_t>& out);
bool decodeTeamAdvertise(const uint8_t* data, size_t len, TeamAdvertise* out);

bool encodeTeamJoinRequest(const TeamJoinRequest& input, std::vector<uint8_t>& out);
bool decodeTeamJoinRequest(const uint8_t* data, size_t len, TeamJoinRequest* out);

bool encodeTeamJoinAccept(const TeamJoinAccept& input, std::vector<uint8_t>& out);
bool decodeTeamJoinAccept(const uint8_t* data, size_t len, TeamJoinAccept* out);

bool encodeTeamJoinConfirm(const TeamJoinConfirm& input, std::vector<uint8_t>& out);
bool decodeTeamJoinConfirm(const uint8_t* data, size_t len, TeamJoinConfirm* out);

bool encodeTeamJoinDecision(const TeamJoinDecision& input, std::vector<uint8_t>& out);
bool decodeTeamJoinDecision(const uint8_t* data, size_t len, TeamJoinDecision* out);

bool encodeTeamKick(const TeamKick& input, std::vector<uint8_t>& out);
bool decodeTeamKick(const uint8_t* data, size_t len, TeamKick* out);

bool encodeTeamTransferLeader(const TeamTransferLeader& input, std::vector<uint8_t>& out);
bool decodeTeamTransferLeader(const uint8_t* data, size_t len, TeamTransferLeader* out);

bool encodeTeamKeyDist(const TeamKeyDist& input, std::vector<uint8_t>& out);
bool decodeTeamKeyDist(const uint8_t* data, size_t len, TeamKeyDist* out);

bool encodeTeamStatus(const TeamStatus& input, std::vector<uint8_t>& out);
bool decodeTeamStatus(const uint8_t* data, size_t len, TeamStatus* out);

} // namespace team::proto
