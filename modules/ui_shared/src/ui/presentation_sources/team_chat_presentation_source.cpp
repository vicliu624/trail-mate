#include "ui/presentation_sources/team_chat_presentation_source.h"

#include "platform/ui/team_ui_store_runtime.h"
#include "team/protocol/team_chat.h"
#include "team/protocol/team_location_marker.h"
#include "ui_presentation/common/fixed_text.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace ui::presentation_sources
{
namespace
{

constexpr std::size_t kMaxMessageRows = 24;

uint32_t foldTeamId(const ::team::TeamId& team_id)
{
    uint32_t hash = 2166136261UL;
    for (const uint8_t byte : team_id)
    {
        hash ^= byte;
        hash *= 16777619UL;
    }
    return hash == 0 ? 1U : hash;
}

ui::chat::ConversationId teamConversationId(const ::team::ui::TeamUiSnapshot& snap)
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::Team;
    id.protocol = ui::chat::ChatProtocolKind::TrailMate;
    id.primary = snap.has_team_id ? foldTeamId(snap.team_id) : 1U;
    id.secondary = 0;
    return id;
}

std::string truncateText(const std::string& text, std::size_t max_len)
{
    if (text.size() <= max_len)
    {
        return text;
    }
    if (max_len <= 3)
    {
        return text.substr(0, max_len);
    }
    return text.substr(0, max_len - 3) + "...";
}

const char* teamCommandName(team::proto::TeamCommandType type)
{
    switch (type)
    {
    case team::proto::TeamCommandType::RallyTo:
        return "RallyTo";
    case team::proto::TeamCommandType::MoveTo:
        return "MoveTo";
    case team::proto::TeamCommandType::Hold:
        return "Hold";
    }
    return "Command";
}

std::string formatCoordinate(int32_t lat_e7, int32_t lon_e7)
{
    char buffer[64]{};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%.5f, %.5f",
                  static_cast<double>(lat_e7) / 10000000.0,
                  static_cast<double>(lon_e7) / 10000000.0);
    return std::string(buffer);
}

std::string formatTeamChatEntry(const ::team::ui::TeamChatLogEntry& entry)
{
    if (entry.type == team::proto::TeamChatType::Text)
    {
        return truncateText(std::string(entry.payload.begin(), entry.payload.end()),
                            160);
    }

    if (entry.type == team::proto::TeamChatType::Location)
    {
        team::proto::TeamChatLocation loc;
        if (team::proto::decodeTeamChatLocation(entry.payload.data(),
                                                entry.payload.size(),
                                                &loc))
        {
            const std::string coord = formatCoordinate(loc.lat_e7, loc.lon_e7);
            const bool marker =
                team::proto::team_location_marker_icon_is_valid(loc.source);
            const char* marker_name =
                team::proto::team_location_marker_icon_name(loc.source);
            char buffer[160]{};
            if (marker)
            {
                std::snprintf(buffer,
                              sizeof(buffer),
                              "%s: %s",
                              marker_name,
                              coord.c_str());
            }
            else if (!loc.label.empty())
            {
                std::snprintf(buffer,
                              sizeof(buffer),
                              "Location: %s %s",
                              loc.label.c_str(),
                              coord.c_str());
            }
            else
            {
                std::snprintf(buffer,
                              sizeof(buffer),
                              "Location: %s",
                              coord.c_str());
            }
            return std::string(buffer);
        }
        return "Location";
    }

    if (entry.type == team::proto::TeamChatType::Command)
    {
        team::proto::TeamChatCommand command;
        if (team::proto::decodeTeamChatCommand(entry.payload.data(),
                                               entry.payload.size(),
                                               &command))
        {
            const char* name = teamCommandName(command.cmd_type);
            char buffer[160]{};
            if (command.lat_e7 != 0 || command.lon_e7 != 0)
            {
                const std::string coord =
                    formatCoordinate(command.lat_e7, command.lon_e7);
                if (!command.note.empty())
                {
                    std::snprintf(buffer,
                                  sizeof(buffer),
                                  "Command: %s %s %s",
                                  name,
                                  coord.c_str(),
                                  command.note.c_str());
                }
                else
                {
                    std::snprintf(buffer,
                                  sizeof(buffer),
                                  "Command: %s %s",
                                  name,
                                  coord.c_str());
                }
            }
            else if (!command.note.empty())
            {
                std::snprintf(buffer,
                              sizeof(buffer),
                              "Command: %s %s",
                              name,
                              command.note.c_str());
            }
            else
            {
                std::snprintf(buffer, sizeof(buffer), "Command: %s", name);
            }
            return std::string(buffer);
        }
        return "Command";
    }

    return "Message";
}

std::string teamTitle(const ::team::ui::TeamUiSnapshot& snap)
{
    return snap.team_name.empty() ? "Team" : snap.team_name;
}

void copyTimeLabel(ui::FixedText<24>& out, uint32_t timestamp)
{
    if (timestamp == 0)
    {
        out.clear();
        return;
    }

    char buffer[24]{};
    std::snprintf(buffer, sizeof(buffer), "%lu",
                  static_cast<unsigned long>(timestamp));
    ui::copyText(out, buffer);
}

void copySenderLabel(ui::FixedText<32>& out,
                     const ::team::ui::TeamUiSnapshot& snap,
                     const ::team::ui::TeamChatLogEntry& entry)
{
    if (!entry.incoming)
    {
        ui::copyText(out, "Me");
        return;
    }

    for (const auto& member : snap.members)
    {
        if (member.node_id == entry.peer_id && !member.name.empty())
        {
            ui::copyText(out, member.name.c_str());
            return;
        }
    }

    char buffer[16]{};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%04lX",
                  static_cast<unsigned long>(entry.peer_id & 0xFFFFU));
    ui::copyText(out, buffer);
}

uint64_t messageLocalId(const ::team::ui::TeamChatLogEntry& entry,
                        std::size_t index)
{
    uint64_t id = 1469598103934665603ULL;
    id ^= entry.ts;
    id *= 1099511628211ULL;
    id ^= entry.peer_id;
    id *= 1099511628211ULL;
    id ^= static_cast<uint64_t>(index + 1U);
    id *= 1099511628211ULL;
    return id == 0 ? static_cast<uint64_t>(index + 1U) : id;
}

} // namespace

TeamChatPresentationSource::TeamChatPresentationSource(
    ::team::ui::ITeamUiStore& team_store)
    : team_store_(team_store)
{
}

bool TeamChatPresentationSource::buildChatWorkspaceSnapshot(
    const ui::chat::ChatWorkspaceRequest& request,
    ui::chat::ChatWorkspaceSnapshot& out) const
{
    (void)request.conversation_offset;
    (void)request.message_offset;

    out = ui::chat::ChatWorkspaceSnapshot{};
    out.header.valid = true;
    out.header.version = 1;
    ui::copyText(out.workspace_title, "Team");
    ui::copyText(out.composer_placeholder, "Team message");

    ::team::ui::TeamUiSnapshot snap;
    if (!team_store_.load(snap) || !snap.has_team_id)
    {
        return true;
    }

    const ui::chat::ConversationId team_id = teamConversationId(snap);
    out.selected_conversation = request.selected;

    out.conversation_count = 1;
    ui::chat::ConversationRow& conversation = out.conversations[0];
    conversation.id = team_id;
    conversation.kind = ui::chat::ConversationKind::Team;
    conversation.protocol = ui::chat::ChatProtocolKind::TrailMate;
    conversation.unread_count = static_cast<uint16_t>(
        snap.team_chat_unread > 0xFFFFU ? 0xFFFFU : snap.team_chat_unread);
    conversation.selected = request.selected == team_id;
    ui::copyText(conversation.title, teamTitle(snap).c_str());
    ui::copyText(conversation.subtitle, "No messages");

    std::vector<::team::ui::TeamChatLogEntry> entries;
    if (team::ui::team_ui_chatlog_load_recent(snap.team_id,
                                              kMaxMessageRows,
                                              entries) &&
        !entries.empty())
    {
        ui::copyText(conversation.subtitle,
                     formatTeamChatEntry(entries.back()).c_str());
        conversation.last_delivery = entries.back().incoming
                                         ? ui::chat::MessageDeliveryState::Received
                                         : ui::chat::MessageDeliveryState::Sent;
    }

    if (request.selected != team_id)
    {
        return true;
    }

    out.can_send = snap.has_team_psk;
    out.composer_enabled = out.can_send;
    out.message_count =
        entries.size() < kMaxMessageRows ? entries.size() : kMaxMessageRows;

    for (std::size_t i = 0; i < out.message_count; ++i)
    {
        const ::team::ui::TeamChatLogEntry& entry = entries[i];
        ui::chat::MessageRow& row = out.messages[i];
        row.conversation = team_id;
        row.outgoing = !entry.incoming;
        row.delivery = entry.incoming ? ui::chat::MessageDeliveryState::Received
                                      : ui::chat::MessageDeliveryState::Sent;
        row.failure = ui::chat::MessageFailureKind::None;
        row.ref.origin = entry.incoming ? ui::chat::MessageOrigin::RemoteStored
                                        : ui::chat::MessageOrigin::LocalStored;
        row.ref.local_id = messageLocalId(entry, i);
        ui::copyText(row.text, formatTeamChatEntry(entry).c_str());
        copyTimeLabel(row.time_label, entry.ts);
        copySenderLabel(row.sender_label, snap, entry);
    }

    return true;
}

} // namespace ui::presentation_sources
