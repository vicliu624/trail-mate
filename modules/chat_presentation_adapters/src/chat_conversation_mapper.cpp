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

chat::MeshProtocol mapProtocol(ui::chat::ChatProtocolKind protocol)
{
    switch (protocol)
    {
    case ui::chat::ChatProtocolKind::Meshtastic:
        return chat::MeshProtocol::Meshtastic;
    case ui::chat::ChatProtocolKind::MeshCore:
        return chat::MeshProtocol::MeshCore;
    case ui::chat::ChatProtocolKind::RNode:
        return chat::MeshProtocol::RNode;
    case ui::chat::ChatProtocolKind::LXMF:
        return chat::MeshProtocol::LXMF;
    case ui::chat::ChatProtocolKind::None:
    case ui::chat::ChatProtocolKind::TrailMate:
    case ui::chat::ChatProtocolKind::Mixed:
        break;
    }
    return chat::MeshProtocol::Meshtastic;
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

bool toCoreConversationId(const ui::chat::ConversationId& id,
                          chat::ConversationId& out)
{
    if (!id.isValid())
    {
        return false;
    }

    if (id.protocol == ui::chat::ChatProtocolKind::None ||
        id.protocol == ui::chat::ChatProtocolKind::TrailMate ||
        id.protocol == ui::chat::ChatProtocolKind::Mixed)
    {
        return false;
    }

    const chat::MeshProtocol protocol = mapProtocol(id.protocol);
    if (id.kind == ui::chat::ConversationKind::DirectPeer)
    {
        out = chat::ConversationId(static_cast<chat::ChannelId>(id.secondary),
                                   id.primary,
                                   protocol);
        return id.primary != 0;
    }

    if (id.kind == ui::chat::ConversationKind::Channel)
    {
        out = chat::ConversationId(static_cast<chat::ChannelId>(id.primary),
                                   0,
                                   protocol);
        return true;
    }

    return false;
}

} // namespace chat_presentation_adapters
