#include "chat_presentation_adapters/chat_conversation_mapper.h"

namespace chat_presentation_adapters
{

ui::chat::ChatProtocolKind mapProtocol(chat::MeshProtocol protocol)
{
    switch (protocol)
    {
    case chat::MeshProtocol::Meshtastic:
        return ui::chat::ChatProtocolKind::Meshtastic;
    case chat::MeshProtocol::MeshCore:
        return ui::chat::ChatProtocolKind::MeshCore;
    case chat::MeshProtocol::RNode:
        return ui::chat::ChatProtocolKind::RNode;
    case chat::MeshProtocol::LXMF:
        return ui::chat::ChatProtocolKind::LXMF;
    }
    return ui::chat::ChatProtocolKind::None;
}

ui::chat::ConversationId toUiConversationId(const chat::ConversationId& id)
{
    ui::chat::ConversationId out;
    out.protocol = mapProtocol(id.protocol);
    const uint32_t channel = static_cast<uint32_t>(id.channel);

    if (id.peer != 0)
    {
        out.kind = ui::chat::ConversationKind::DirectPeer;
        out.primary = id.peer;
        out.secondary = channel;
        return out;
    }

    out.kind = ui::chat::ConversationKind::Channel;
    out.primary = channel;
    out.secondary = 0;
    return out;
}

} // namespace chat_presentation_adapters
