#pragma once

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
};

struct TeamAdvertiseEvent
{
    TeamEventContext ctx;
    team::proto::TeamAdvertise msg;
};

struct TeamJoinRequestEvent
{
    TeamEventContext ctx;
    team::proto::TeamJoinRequest msg;
};

struct TeamJoinAcceptEvent
{
    TeamEventContext ctx;
    team::proto::TeamJoinAccept msg;
};

struct TeamJoinConfirmEvent
{
    TeamEventContext ctx;
    team::proto::TeamJoinConfirm msg;
};

struct TeamJoinDecisionEvent
{
    TeamEventContext ctx;
    team::proto::TeamJoinDecision msg;
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
