#pragma once

#include "../../chat/domain/chat_types.h"
#include "../protocol/team_chat.h"
#include "../protocol/team_mgmt.h"
#include "team_types.h"
#include <cstdint>
#include <vector>

namespace team
{

struct TeamEventContext
{
    TeamId team_id{};
    uint32_t key_id = 0;
    uint32_t from = 0;
    uint32_t timestamp = 0;
    chat::RxMeta rx_meta;
};

struct TeamKickEvent
{
    TeamEventContext ctx;
    team::proto::TeamKick msg;
};

struct TeamTransferLeaderEvent
{
    TeamEventContext ctx;
    team::proto::TeamTransferLeader msg;
};

struct TeamKeyDistEvent
{
    TeamEventContext ctx;
    team::proto::TeamKeyDist msg;
};

struct TeamStatusEvent
{
    TeamEventContext ctx;
    team::proto::TeamStatus msg;
};

struct TeamPositionEvent
{
    TeamEventContext ctx;
    std::vector<uint8_t> payload;
};

struct TeamWaypointEvent
{
    TeamEventContext ctx;
    std::vector<uint8_t> payload;
};

struct TeamTrackEvent
{
    TeamEventContext ctx;
    std::vector<uint8_t> payload;
};

struct TeamChatEvent
{
    TeamEventContext ctx;
    team::proto::TeamChatMessage msg;
};

struct TeamPairingEvent
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

enum class TeamProtocolError
{
    DecryptFail,
    DecodeFail,
    KeyMismatch,
    UnknownVersion
};

struct TeamErrorEvent
{
    TeamEventContext ctx;
    TeamProtocolError error;
    uint32_t portnum = 0;
};

} // namespace team
