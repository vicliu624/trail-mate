#include "ui/presentation_sources/team_chat_presentation_source.h"

#include "platform/ui/team_ui_store_runtime.h"
#include "ui/team_presentation/team_rich_payload_projector.h"
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

    ui::chat::resetChatWorkspaceSnapshot(out);
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
        ::ui::team_presentation::TeamRichPayloadProjector projector;
        ::ui::team_presentation::TeamRichPayloadDisplay display;
        (void)projector.project(entries.back(), display);
        ui::copyText(conversation.subtitle,
                     display.summary.c_str());
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

    ::ui::team_presentation::TeamRichPayloadProjector projector;
    for (std::size_t i = 0; i < out.message_count; ++i)
    {
        const ::team::ui::TeamChatLogEntry& entry = entries[i];
        ::ui::team_presentation::TeamRichPayloadDisplay display;
        (void)projector.project(entry, display);
        ui::chat::MessageRow& row = out.messages[i];
        row.conversation = team_id;
        row.outgoing = !entry.incoming;
        row.sender_node_id = entry.incoming ? entry.peer_id : 0;
        row.delivery = entry.incoming ? ui::chat::MessageDeliveryState::Received
                                      : ui::chat::MessageDeliveryState::Sent;
        row.failure = ui::chat::MessageFailureKind::None;
        row.ref.origin = entry.incoming ? ui::chat::MessageOrigin::RemoteStored
                                        : ui::chat::MessageOrigin::LocalStored;
        row.ref.local_id = messageLocalId(entry, i);
        ui::copyText(row.text, display.summary.c_str());
        copyTimeLabel(row.time_label, entry.ts);
        copySenderLabel(row.sender_label, snap, entry);
    }

    return true;
}

} // namespace ui::presentation_sources
