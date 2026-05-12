#include "mesh/protocol/meshtastic/meshtastic_protocol_strategy.h"

#include "meshtastic/portnums.pb.h"

#include <cassert>
#include <cstring>

int main()
{
    mesh::meshtastic::MeshtasticProtocolStrategy strategy;

    mesh::MeshRuntimeConfig runtime{};
    auto radio = strategy.deriveRadioConfig(runtime);
    assert(radio.frequency_hz != 0);
    assert(radio.bandwidth_hz != 0);
    assert(radio.sync_word == 0x2B);

    const char text[] = "hello";
    mesh::DirectMessageCommand command{
        mesh::NodeId{0x22222222UL},
        mesh::ByteView{reinterpret_cast<const uint8_t*>(text), sizeof(text) - 1},
        true};
    command.application_port = static_cast<uint32_t>(meshtastic_PortNum_TEXT_MESSAGE_APP);

    mesh::ProtocolBuildContext context{};
    context.local_node = mesh::NodeId{0x11111111UL};
    context.packet_id = 0xAABBCCDDUL;
    context.channel_hash = 0x01;
    context.hop_limit = 3;

    mesh::EncodedPacket encoded{};
    auto built = strategy.buildDirectMessage(context, command, encoded);
    assert(built.ok);
    assert(encoded.size > 0);

    mesh::RadioRxPacket packet{};
    std::memcpy(packet.bytes, encoded.bytes, encoded.size);
    packet.size = encoded.size;
    packet.received_at_ms = 42;

    mesh::MeshProtocolEvent event{};
    auto parsed = strategy.parseRadioPacket(packet, event);
    assert(parsed.ok);
    assert(event.kind == mesh::MeshProtocolEventKind::MessageReceived);
    assert(event.peer == mesh::NodeId{0x11111111UL});
    assert(event.packet_id == 0xAABBCCDDUL);
    assert(event.payload.size == sizeof(text) - 1);
    assert(std::memcmp(event.payload.data, text, sizeof(text) - 1) == 0);

    return 0;
}
