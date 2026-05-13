#include "chat_presentation_adapters/chat_conversation_mapper.h"

#include <cassert>

namespace
{

void peerConversationMapsToDirectPeer()
{
    chat::ConversationId core(
        chat::ChannelId::PRIMARY,
        1234,
        chat::MeshProtocol::Meshtastic);

    const ui::chat::ConversationId ui =
        chat_presentation_adapters::toUiConversationId(core);

    assert(ui.kind == ui::chat::ConversationKind::DirectPeer);
    assert(ui.protocol == ui::chat::ChatProtocolKind::Meshtastic);
    assert(ui.primary == 1234);
    assert(ui.secondary == 0);
}

void channelConversationMapsToChannel()
{
    chat::ConversationId core(
        chat::ChannelId::SECONDARY,
        0,
        chat::MeshProtocol::MeshCore);

    const ui::chat::ConversationId ui =
        chat_presentation_adapters::toUiConversationId(core);

    assert(ui.kind == ui::chat::ConversationKind::Channel);
    assert(ui.protocol == ui::chat::ChatProtocolKind::MeshCore);
    assert(ui.primary == static_cast<uint32_t>(chat::ChannelId::SECONDARY));
    assert(ui.secondary == 0);
}

void protocolsMapOneToOne()
{
    assert(chat_presentation_adapters::mapProtocol(chat::MeshProtocol::Meshtastic) ==
           ui::chat::ChatProtocolKind::Meshtastic);
    assert(chat_presentation_adapters::mapProtocol(chat::MeshProtocol::MeshCore) ==
           ui::chat::ChatProtocolKind::MeshCore);
    assert(chat_presentation_adapters::mapProtocol(chat::MeshProtocol::RNode) ==
           ui::chat::ChatProtocolKind::RNode);
    assert(chat_presentation_adapters::mapProtocol(chat::MeshProtocol::LXMF) ==
           ui::chat::ChatProtocolKind::LXMF);
}

} // namespace

int main()
{
    peerConversationMapsToDirectPeer();
    channelConversationMapsToChannel();
    protocolsMapOneToOne();
    return 0;
}
