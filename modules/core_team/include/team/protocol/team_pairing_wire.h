#pragma once

#include "../domain/team_types.h"
#include "team_mgmt.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace team::proto::pairing
{

constexpr uint8_t kPairingVersion = 1;
constexpr size_t kMaxTeamNameLen = 15;

enum class MessageType : uint8_t
{
    Beacon = 1,
    Join = 2,
    Key = 3
};

struct BeaconPacket
{
    TeamId team_id{};
    uint32_t key_id = 0;
    uint32_t leader_id = 0;
    uint32_t window_ms = 0;
    char team_name[kMaxTeamNameLen + 1] = {0};
    bool has_team_name = false;
};

struct JoinPacket
{
    TeamId team_id{};
    uint32_t member_id = 0;
    uint32_t nonce = 0;
};

struct KeyPacket
{
    TeamId team_id{};
    uint32_t key_id = 0;
    uint32_t nonce = 0;
    std::array<uint8_t, kTeamChannelPskSize> channel_psk{};
    uint8_t channel_psk_len = 0;
};

bool decodeType(const uint8_t* data, size_t len, MessageType* out_type);

bool encodeBeacon(const BeaconPacket& input, std::vector<uint8_t>& out);
bool decodeBeacon(const uint8_t* data, size_t len, BeaconPacket* out);

bool encodeJoin(const JoinPacket& input, std::vector<uint8_t>& out);
bool decodeJoin(const uint8_t* data, size_t len, JoinPacket* out);

bool encodeKey(const KeyPacket& input, std::vector<uint8_t>& out);
bool decodeKey(const uint8_t* data, size_t len, KeyPacket* out);

} // namespace team::proto::pairing
