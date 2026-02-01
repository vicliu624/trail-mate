/**
 * @file team_chat.h
 * @brief Team chat protocol payloads
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace team::proto
{

constexpr uint8_t kTeamChatVersion = 1;

enum class TeamChatType : uint8_t
{
    Text = 1,
    Location = 2,
    Command = 3
};

struct TeamChatHeader
{
    uint8_t version = kTeamChatVersion;
    TeamChatType type = TeamChatType::Text;
    uint16_t flags = 0;
    uint32_t msg_id = 0;
    uint32_t ts = 0;
    uint32_t from = 0;
};

struct TeamChatMessage
{
    TeamChatHeader header{};
    std::vector<uint8_t> payload;
};

enum class TeamCommandType : uint8_t
{
    RallyTo = 1,
    MoveTo = 2,
    Hold = 3
};

struct TeamChatLocation
{
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    int16_t alt_m = 0;
    uint16_t acc_m = 0;
    uint32_t ts = 0;
    uint8_t source = 0;
    std::string label;
};

struct TeamChatCommand
{
    TeamCommandType cmd_type = TeamCommandType::RallyTo;
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    uint16_t radius_m = 0;
    uint8_t priority = 0;
    std::string note;
};

bool encodeTeamChatMessage(const TeamChatMessage& msg, std::vector<uint8_t>& out);
bool decodeTeamChatMessage(const uint8_t* data, size_t len, TeamChatMessage* out);

bool encodeTeamChatLocation(const TeamChatLocation& loc, std::vector<uint8_t>& out);
bool decodeTeamChatLocation(const uint8_t* data, size_t len, TeamChatLocation* out);

bool encodeTeamChatCommand(const TeamChatCommand& cmd, std::vector<uint8_t>& out);
bool decodeTeamChatCommand(const uint8_t* data, size_t len, TeamChatCommand* out);

} // namespace team::proto
