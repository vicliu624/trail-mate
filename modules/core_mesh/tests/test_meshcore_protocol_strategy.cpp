#include "mesh/protocol/meshcore/meshcore_protocol_strategy.h"

#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "mesh/protocol/meshcore/mc_identity_flow.h"

#include <array>
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

    uint8_t v2_dest_hash[2] = {0x44, 0x33};
    uint8_t v2_src_hash[2] = {0x22, 0x11};
    context.meshcore_payload_ver = chat::meshcore::kMeshCorePayloadVer2;
    context.meshcore_peer_hash = mesh::ByteView{v2_dest_hash, sizeof(v2_dest_hash)};
    context.meshcore_local_hash = mesh::ByteView{v2_src_hash, sizeof(v2_src_hash)};
    auto v2_direct_built = strategy.buildDirectMessage(context, command, encoded);
    assert(v2_direct_built.ok);
    chat::meshcore::ParsedPacket parsed_v2_direct{};
    assert(chat::meshcore::parsePacket(encoded.bytes, encoded.size, &parsed_v2_direct));
    assert(parsed_v2_direct.payload_ver == chat::meshcore::kMeshCorePayloadVer2);
    assert(parsed_v2_direct.payload_len > 4);
    assert(std::memcmp(parsed_v2_direct.payload, v2_dest_hash, sizeof(v2_dest_hash)) == 0);
    assert(std::memcmp(parsed_v2_direct.payload + sizeof(v2_dest_hash),
                       v2_src_hash,
                       sizeof(v2_src_hash)) == 0);

    {
        constexpr uint8_t route_type_direct = 0x02;
        const uint8_t v2_path[] = {0x44, 0x33, 0x55, 0x66};
        const uint8_t payload[] = {0xA5, 0x5A};
        uint8_t frame[16] = {};
        size_t frame_len = 0;
        assert(chat::meshcore::buildFrameNoTransport(chat::meshcore::PayloadProfile::V2,
                                                     route_type_direct,
                                                     chat::meshcore::kMeshCorePayloadTypeControl,
                                                     v2_path,
                                                     sizeof(v2_path),
                                                     payload,
                                                     sizeof(payload),
                                                     frame,
                                                     sizeof(frame),
                                                     &frame_len));
        assert(frame_len == 2 + sizeof(v2_path) + sizeof(payload));
        assert(frame[1] == 0x42);

        chat::meshcore::ParsedPacket parsed_path{};
        assert(chat::meshcore::parsePacket(frame, frame_len, &parsed_path));
        assert(parsed_path.payload_ver == chat::meshcore::kMeshCorePayloadVer2);
        assert(parsed_path.path_descriptor == 0x42);
        assert(parsed_path.path_hash_bytes == 2);
        assert(parsed_path.path_hop_count == 2);
        assert(parsed_path.path_len == sizeof(v2_path));

        uint8_t wrong_profile_path[] = {
            chat::meshcore::buildHeader(route_type_direct,
                                        chat::meshcore::kMeshCorePayloadTypeControl,
                                        chat::meshcore::kMeshCorePayloadVer2),
            0x02,
            0x44,
            0x33,
            0xA5};
        assert(!chat::meshcore::parsePacket(wrong_profile_path,
                                            sizeof(wrong_profile_path),
                                            &parsed_path));

        uint8_t reserved_path[] = {
            chat::meshcore::buildHeader(route_type_direct,
                                        chat::meshcore::kMeshCorePayloadTypeControl,
                                        chat::meshcore::kMeshCorePayloadVer2),
            0xC1,
            0x44,
            0xA5};
        assert(!chat::meshcore::parsePacket(reserved_path, sizeof(reserved_path), &parsed_path));
    }

    packet = mesh::RadioRxPacket{};
    std::memcpy(packet.bytes, encoded.bytes, encoded.size);
    packet.size = encoded.size;
    event = mesh::MeshProtocolEvent{};
    auto v2_direct_parsed = receiver.parseRadioPacket(packet, event);
    assert(v2_direct_parsed.ok);
    assert(event.kind == mesh::MeshProtocolEventKind::MessageReceived);
    assert(event.peer == mesh::NodeId{0x22});
    assert(event.payload.size == sizeof(text) - 1);
    assert(std::memcmp(event.payload.data, text, sizeof(text) - 1) == 0);

    context.meshcore_payload_ver = chat::meshcore::kMeshCorePayloadVer1;
    context.meshcore_peer_hash = mesh::ByteView{};
    context.meshcore_local_hash = mesh::ByteView{};

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

    uint8_t invalid_group_key[3] = {1, 2, 3};
    context.channel_key = mesh::ByteView{invalid_group_key, sizeof(invalid_group_key)};
    auto invalid_group_key_result = strategy.buildDirectMessage(context, group_command, encoded);
    assert(!invalid_group_key_result.ok);
    assert(invalid_group_key_result.failure == mesh::ProtocolFailure::MissingChannelKey);

    context.channel_key = mesh::ByteView{};
    auto public_group_built = strategy.buildDirectMessage(context, group_command, encoded);
    assert(public_group_built.ok);

    packet = mesh::RadioRxPacket{};
    std::memcpy(packet.bytes, encoded.bytes, encoded.size);
    packet.size = encoded.size;

    mesh::meshcore::MeshCoreProtocolStrategy public_receiver;
    event = mesh::MeshProtocolEvent{};
    auto public_group_parsed = public_receiver.parseRadioPacket(packet, event);
    assert(public_group_parsed.ok);
    assert(event.kind == mesh::MeshProtocolEventKind::MessageReceived);
    assert(event.peer == context.local_node);
    assert(event.payload.size == sizeof(text) - 1);
    assert(std::memcmp(event.payload.data, text, sizeof(text) - 1) == 0);

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

    context.meshcore_payload_ver = chat::meshcore::kMeshCorePayloadVer2;
    context.meshcore_channel_hash = mesh::ByteView{};
    auto v2_group_built = strategy.buildDirectMessage(context, group_command, encoded);
    assert(v2_group_built.ok);
    chat::meshcore::ParsedPacket parsed_v2_group{};
    assert(chat::meshcore::parsePacket(encoded.bytes, encoded.size, &parsed_v2_group));
    assert(parsed_v2_group.payload_ver == chat::meshcore::kMeshCorePayloadVer2);
    assert(parsed_v2_group.payload_len > (chat::meshcore::kMeshCoreV2HashBytes +
                                          chat::meshcore::kMeshCoreV2CipherMacSize));
    uint8_t expected_group_hash[2] = {};
    assert(chat::meshcore::computeChannelHashBytes(group_key,
                                                   expected_group_hash,
                                                   sizeof(expected_group_hash)));
    assert(std::memcmp(parsed_v2_group.payload,
                       expected_group_hash,
                       sizeof(expected_group_hash)) == 0);

    packet = mesh::RadioRxPacket{};
    std::memcpy(packet.bytes, encoded.bytes, encoded.size);
    packet.size = encoded.size;
    event = mesh::MeshProtocolEvent{};
    auto v2_group_parsed = receiver.parseRadioPacket(packet, event);
    assert(v2_group_parsed.ok);
    assert(event.kind == mesh::MeshProtocolEventKind::MessageReceived);
    assert(event.peer == context.local_node);
    assert(event.payload.size == sizeof(text) - 1);
    assert(std::memcmp(event.payload.data, text, sizeof(text) - 1) == 0);

    context.meshcore_payload_ver = chat::meshcore::kMeshCorePayloadVer1;
    context.meshcore_channel_hash = mesh::ByteView{};

    mesh::meshcore::McIdentityFlow identity;
    uint8_t seed[mesh::meshcore::kMeshCoreSeedSize] = {};
    uint8_t public_key[mesh::meshcore::kMeshCorePublicKeySize] = {};
    uint8_t private_key[mesh::meshcore::kMeshCorePrivateKeySize] = {};
    bool created_key = false;
    for (uint8_t attempt = 1; attempt < 32 && !created_key; ++attempt)
    {
        for (size_t index = 0; index < sizeof(seed); ++index)
        {
            seed[index] = static_cast<uint8_t>(attempt + index);
        }
        created_key = identity.createKeypair(mesh::ByteView{seed, sizeof(seed)},
                                             public_key,
                                             private_key)
                          .ok;
    }
    assert(created_key);

    constexpr uint8_t route_type_flood = 0x01;
    constexpr uint8_t payload_type_advert = 0x04;
    constexpr uint8_t advert_flag_has_location = 0x10;
    constexpr uint8_t advert_flag_has_name = 0x80;
    constexpr uint8_t advert_type_chat = 0x01;
    const int32_t lat_i6 = 39906123;
    const int32_t lon_i6 = 116391234;
    const char advert_name[] = "Alpha";
    uint8_t app_data[1 + sizeof(lat_i6) + sizeof(lon_i6) + sizeof(advert_name)] = {};
    size_t app_len = 0;
    app_data[app_len++] = static_cast<uint8_t>(advert_flag_has_location |
                                               advert_flag_has_name |
                                               advert_type_chat);
    std::memcpy(app_data + app_len, &lat_i6, sizeof(lat_i6));
    app_len += sizeof(lat_i6);
    std::memcpy(app_data + app_len, &lon_i6, sizeof(lon_i6));
    app_len += sizeof(lon_i6);
    std::memcpy(app_data + app_len, advert_name, sizeof(advert_name));
    app_len += sizeof(advert_name);

    const uint32_t advert_ts = 123456U;
    std::array<uint8_t, mesh::meshcore::kMeshCorePublicKeySize + sizeof(advert_ts) +
                            sizeof(app_data)>
        signed_message{};
    size_t signed_len = 0;
    std::memcpy(signed_message.data() + signed_len, public_key, sizeof(public_key));
    signed_len += sizeof(public_key);
    std::memcpy(signed_message.data() + signed_len, &advert_ts, sizeof(advert_ts));
    signed_len += sizeof(advert_ts);
    std::memcpy(signed_message.data() + signed_len, app_data, app_len);
    signed_len += app_len;

    uint8_t signature[mesh::meshcore::kMeshCoreSignatureSize] = {};
    mesh::meshcore::MeshCoreKeyPairView keypair{
        private_key,
        sizeof(private_key),
        public_key,
        sizeof(public_key)};
    assert(identity.sign(keypair,
                         mesh::ByteView{signed_message.data(), signed_len},
                         signature)
               .ok);

    uint8_t advert_payload[mesh::meshcore::kMeshCorePublicKeySize + sizeof(advert_ts) +
                           mesh::meshcore::kMeshCoreSignatureSize + sizeof(app_data)] = {};
    size_t advert_payload_len = 0;
    std::memcpy(advert_payload + advert_payload_len, public_key, sizeof(public_key));
    advert_payload_len += sizeof(public_key);
    std::memcpy(advert_payload + advert_payload_len, &advert_ts, sizeof(advert_ts));
    advert_payload_len += sizeof(advert_ts);
    std::memcpy(advert_payload + advert_payload_len, signature, sizeof(signature));
    advert_payload_len += sizeof(signature);
    std::memcpy(advert_payload + advert_payload_len, app_data, app_len);
    advert_payload_len += app_len;

    uint8_t advert_frame[256] = {};
    size_t advert_frame_len = 0;
    assert(chat::meshcore::buildFrameNoTransport(route_type_flood,
                                                 payload_type_advert,
                                                 nullptr,
                                                 0,
                                                 advert_payload,
                                                 advert_payload_len,
                                                 advert_frame,
                                                 sizeof(advert_frame),
                                                 &advert_frame_len));

    packet = mesh::RadioRxPacket{};
    std::memcpy(packet.bytes, advert_frame, advert_frame_len);
    packet.size = advert_frame_len;
    packet.received_at_ms = 777;

    event = mesh::MeshProtocolEvent{};
    auto advert_parsed = receiver.parseRadioPacket(packet, event);
    assert(advert_parsed.ok);
    assert(event.kind == mesh::MeshProtocolEventKind::PeerAdvertReceived);
    assert(event.peer_key.updated_at_ms == 777);
    assert(event.advert.has_name);
    assert(std::strcmp(event.advert.name, advert_name) == 0);
    assert(event.advert.has_location);
    assert(event.advert.latitude_i6 == lat_i6);
    assert(event.advert.longitude_i6 == lon_i6);
    assert(event.advert.node_type == advert_type_chat);
    assert(event.advert.timestamp == advert_ts);

    // Explicit identity hashes are authoritative in both profiles. In V1 a
    // valid public key can start with zero; neither that byte nor the peer's
    // hash may be reconstructed from unrelated application node IDs.
    mesh::ProtocolBuildContext explicit_v1{};
    explicit_v1.local_node = mesh::NodeId{0x123456AA};
    explicit_v1.channel_key = mesh::ByteView{direct_secret, sizeof(direct_secret)};
    const uint8_t explicit_peer[] = {0x17};
    const uint8_t explicit_self[] = {0};
    explicit_v1.meshcore_peer_hash = mesh::ByteView{explicit_peer, sizeof(explicit_peer)};
    explicit_v1.meshcore_local_hash = mesh::ByteView{explicit_self, sizeof(explicit_self)};
    assert(strategy.buildDirectMessage(explicit_v1, command, encoded).ok);
    chat::meshcore::ParsedPacket explicit_packet{};
    assert(chat::meshcore::parsePacket(encoded.bytes, encoded.size, &explicit_packet));
    assert(explicit_packet.payload[0] == 0x17 && explicit_packet.payload[1] == 0);
    const uint8_t too_large[180] = {1};
    auto oversized = command;
    oversized.payload = mesh::ByteView{too_large, sizeof(too_large)};
    assert(!strategy.buildDirectMessage(explicit_v1, oversized, encoded).ok && encoded.size == 0);
    oversized.to = mesh::NodeId{};
    assert(!strategy.buildDirectMessage(explicit_v1, oversized, encoded).ok && encoded.size == 0);
    return 0;
}
