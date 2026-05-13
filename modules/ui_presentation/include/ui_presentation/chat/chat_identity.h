#pragma once

#include <stdint.h>

namespace ui::chat
{

enum class ConversationKind : uint8_t
{
    None = 0,
    DirectPeer,
    Channel,
    Team,
    Broadcast,
    System,
};

enum class ChatProtocolKind : uint8_t
{
    None = 0,
    Meshtastic,
    MeshCore,
    RNode,
    LXMF,
    TrailMate,
    Mixed,
};

struct ConversationId
{
    ConversationKind kind = ConversationKind::None;
    ChatProtocolKind protocol = ChatProtocolKind::None;

    // DirectPeer: primary = node id, secondary = channel id / slot.
    // Channel: primary = channel id / slot, secondary = 0.
    // Team: primary = team id / slot, secondary = optional member/group scope.
    // Broadcast: primary = channel id / slot, secondary = 0.
    // System: primary = system row id, secondary = 0.
    uint32_t primary = 0;
    uint32_t secondary = 0;

    bool isValid() const
    {
        return kind != ConversationKind::None;
    }

    bool operator==(const ConversationId& other) const
    {
        return kind == other.kind &&
               protocol == other.protocol &&
               primary == other.primary &&
               secondary == other.secondary;
    }

    bool operator!=(const ConversationId& other) const
    {
        return !(*this == other);
    }
};

} // namespace ui::chat
