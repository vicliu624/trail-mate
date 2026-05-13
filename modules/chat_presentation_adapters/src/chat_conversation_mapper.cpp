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

bool tryMapProtocol(ui::chat::ChatProtocolKind protocol,
                    chat::MeshProtocol& out)
{
    switch (protocol)
    {
    case ui::chat::ChatProtocolKind::Meshtastic:
        out = chat::MeshProtocol::Meshtastic;
        return true;
    case ui::chat::ChatProtocolKind::MeshCore:
        out = chat::MeshProtocol::MeshCore;
        return true;
    case ui::chat::ChatProtocolKind::RNode:
        out = chat::MeshProtocol::RNode;
        return true;
    case ui::chat::ChatProtocolKind::LXMF:
        out = chat::MeshProtocol::LXMF;
        return true;
    case ui::chat::ChatProtocolKind::None:
    case ui::chat::ChatProtocolKind::TrailMate:
    case ui::chat::ChatProtocolKind::Mixed:
        break;
    }
    return false;
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

    chat::MeshProtocol protocol;
    if (!tryMapProtocol(id.protocol, protocol))
    {
        return false;
    }

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
