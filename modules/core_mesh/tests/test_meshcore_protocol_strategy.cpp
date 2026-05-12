#include "mesh/protocol/meshcore/meshcore_protocol_strategy.h"

#include <cassert>
#include <cstring>

namespace
{

bool hasCryptoBackend()
{
#if defined(ESP_PLATFORM) || defined(TRAIL_MATE_HAS_OPENSSL) || defined(ARDUINO)
    return true;
#else
    return false;
#endif
}

} // namespace

int main()
{
    mesh::meshcore::MeshCoreProtocolStrategy strategy;

    mesh::MeshRuntimeConfig runtime{};
    auto radio = strategy.deriveRadioConfig(runtime);
    assert(radio.frequency_hz != 0);
    assert(radio.bandwidth_hz != 0);
    assert(radio.sync_word == 0x12);

    const char text[] = "meshcore";
    mesh::DirectMessageCommand command{
        mesh::NodeId{0x33333344UL},
        mesh::ByteView{reinterpret_cast<const uint8_t*>(text), sizeof(text) - 1},
        true};
    command.application_port = 123;

    mesh::ProtocolBuildContext context{};
    context.local_node = mesh::NodeId{0x11111122UL};

    mesh::EncodedPacket encoded{};
    auto missing_key = strategy.buildDirectMessage(context, command, encoded);
    assert(!missing_key.ok);
    assert(missing_key.failure == mesh::ProtocolFailure::MissingPeerKey);

    if (!hasCryptoBackend())
    {
        return 0;
    }

    uint8_t direct_secret[32] = {};
    for (size_t index = 0; index < sizeof(direct_secret); ++index)
    {
        direct_secret[index] = static_cast<uint8_t>(index + 1);
    }
    context.channel_key = mesh::ByteView{direct_secret, sizeof(direct_secret)};

    auto direct_built = strategy.buildDirectMessage(context, command, encoded);
    assert(direct_built.ok);
    assert(encoded.size > 0);

    mesh::RadioRxPacket packet{};
    std::memcpy(packet.bytes, encoded.bytes, encoded.size);
    packet.size = encoded.size;
    packet.received_at_ms = 42;

    mesh::meshcore::MeshCoreProtocolStrategy receiver;
    receiver.setLocalPublicHash(0x44);
    receiver.setDirectSharedSecret(mesh::ByteView{direct_secret, sizeof(direct_secret)});
    mesh::MeshProtocolEvent event{};
    auto direct_parsed = receiver.parseRadioPacket(packet, event);
    assert(direct_parsed.ok);
    assert(event.kind == mesh::MeshProtocolEventKind::MessageReceived);
    assert(event.peer == mesh::NodeId{0x22});
    assert(event.payload.size == sizeof(text) - 1);
    assert(std::memcmp(event.payload.data, text, sizeof(text) - 1) == 0);

    uint8_t group_key[16] = {};
    for (size_t index = 0; index < sizeof(group_key); ++index)
    {
        group_key[index] = static_cast<uint8_t>(0xA0 + index);
    }

    mesh::DirectMessageCommand group_command{
        mesh::NodeId{},
        mesh::ByteView{reinterpret_cast<const uint8_t*>(text), sizeof(text) - 1},
        false};
    group_command.application_port = 321;
    context.channel_key = mesh::ByteView{group_key, sizeof(group_key)};

    auto group_built = strategy.buildDirectMessage(context, group_command, encoded);
    assert(group_built.ok);

    packet = mesh::RadioRxPacket{};
    std::memcpy(packet.bytes, encoded.bytes, encoded.size);
    packet.size = encoded.size;

    receiver.setGroupKey(mesh::ByteView{group_key, sizeof(group_key)});
    event = mesh::MeshProtocolEvent{};
    auto group_parsed = receiver.parseRadioPacket(packet, event);
    assert(group_parsed.ok);
    assert(event.kind == mesh::MeshProtocolEventKind::MessageReceived);
    assert(event.peer == context.local_node);
    assert(event.payload.size == sizeof(text) - 1);
    assert(std::memcmp(event.payload.data, text, sizeof(text) - 1) == 0);

    return 0;
}
