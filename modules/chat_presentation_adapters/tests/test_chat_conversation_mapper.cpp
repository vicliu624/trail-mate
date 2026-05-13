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

    chat::MeshProtocol core_protocol = chat::MeshProtocol::LXMF;
    assert(chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::Meshtastic,
        core_protocol));
    assert(core_protocol == chat::MeshProtocol::Meshtastic);
    assert(chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::MeshCore,
        core_protocol));
    assert(core_protocol == chat::MeshProtocol::MeshCore);
    assert(chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::RNode,
        core_protocol));
    assert(core_protocol == chat::MeshProtocol::RNode);
    assert(chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::LXMF,
        core_protocol));
    assert(core_protocol == chat::MeshProtocol::LXMF);
}

void unsupportedPresentationProtocolsDoNotMapToCore()
{
    chat::MeshProtocol core_protocol = chat::MeshProtocol::Meshtastic;
    assert(!chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::None,
        core_protocol));
    assert(!chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::TrailMate,
        core_protocol));
    assert(!chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::Mixed,
        core_protocol));
}

void directPeerMapsBackToCoreConversation()
{
    ui::chat::ConversationId ui;
    ui.kind = ui::chat::ConversationKind::DirectPeer;
    ui.protocol = ui::chat::ChatProtocolKind::Meshtastic;
    ui.primary = 1234;
    ui.secondary = static_cast<uint32_t>(chat::ChannelId::SECONDARY);

    chat::ConversationId core;
    assert(chat_presentation_adapters::toCoreConversationId(ui, core));
    assert(core.protocol == chat::MeshProtocol::Meshtastic);
    assert(core.channel == chat::ChannelId::SECONDARY);
    assert(core.peer == 1234);
}

void teamDoesNotMapBackToCoreConversation()
{
    ui::chat::ConversationId ui;
    ui.kind = ui::chat::ConversationKind::Team;
    ui.protocol = ui::chat::ChatProtocolKind::TrailMate;
    ui.primary = 1234;

    chat::ConversationId core;
    assert(!chat_presentation_adapters::toCoreConversationId(ui, core));
}

void trailMateDirectPeerDoesNotMapBackToCoreConversation()
{
    ui::chat::ConversationId ui;
    ui.kind = ui::chat::ConversationKind::DirectPeer;
    ui.protocol = ui::chat::ChatProtocolKind::TrailMate;
    ui.primary = 1234;
    ui.secondary = static_cast<uint32_t>(chat::ChannelId::PRIMARY);

    chat::ConversationId core;
    assert(!chat_presentation_adapters::toCoreConversationId(ui, core));
}

} // namespace

int main()
{
    peerConversationMapsToDirectPeer();
    channelConversationMapsToChannel();
    protocolsMapOneToOne();
    unsupportedPresentationProtocolsDoNotMapToCore();
    directPeerMapsBackToCoreConversation();
    teamDoesNotMapBackToCoreConversation();
    trailMateDirectPeerDoesNotMapBackToCoreConversation();
    return 0;
}
