#include "mesh/protocol/meshcore/meshcore_protocol_strategy.h"

#include "chat/domain/chat_types.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "mesh/protocol/meshcore/mc_identity_flow.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace mesh
{
namespace meshcore
{

namespace
{

constexpr uint8_t kRouteTypeFlood = 0x01;
constexpr uint8_t kPayloadTypeAdvert = 0x04;
constexpr uint8_t kPayloadTypeGrpData = 0x06;
constexpr uint8_t kPayloadTypeDirectData = 0x07;
constexpr uint8_t kDirectAppMagic0 = 0xDA;
constexpr uint8_t kDirectAppMagic1 = 0x7A;
constexpr uint8_t kDirectAppFlagWantAck = 0x01;
constexpr uint8_t kGroupDataMagic0 = 0x47;
constexpr uint8_t kGroupDataMagic1 = 0x44;
constexpr uint8_t kLoraSyncWordPrivate = 0x12;
constexpr size_t kCipherBlockSize = 16;
constexpr size_t kMeshCoreMaxFrameSize = 255;
constexpr size_t kMeshCoreMaxPayloadSize = 184;
constexpr size_t kMeshCorePubKeySize = 32;
constexpr size_t kAdvertSignatureSize = 64;
constexpr size_t kAdvertMinPayloadSize =
    kMeshCorePubKeySize + sizeof(uint32_t) + kAdvertSignatureSize;

bool isDirectSharedSecret(ByteView secret)
{
    return secret.data != nullptr && secret.size == 32;
}

bool isSupportedDirectSecret(ByteView secret)
{
    return secret.data != nullptr && (secret.size == 16 || secret.size == 32);
}

bool copyPayload(const DirectMessageCommand& command,
                 uint8_t* out_plain,
                 size_t plain_capacity,
                 size_t prefix_size,
                 size_t& out_plain_len)
{
    if (!out_plain || command.payload.empty() || prefix_size > plain_capacity)
    {
        return false;
    }

    const size_t body_len = command.payload.size;
    if (body_len > plain_capacity - prefix_size)
    {
        return false;
    }
    if (body_len == 0)
    {
        return false;
    }

    std::memcpy(out_plain + prefix_size, command.payload.data, body_len);
    out_plain_len = prefix_size + body_len;
    return true;
}

bool mapSecretToKeys(ByteView secret, uint8_t out_key16[16], uint8_t out_key32[32])
{
    if (!secret.data || !out_key16 || !out_key32)
    {
        return false;
    }

    if (secret.size == 32)
    {
        chat::meshcore::sharedSecretToKeys(secret.data, out_key16, out_key32);
        return true;
    }

    if (secret.size == 16)
    {
        std::memcpy(out_key16, secret.data, 16);
        chat::meshcore::toHmacKey32(out_key16, out_key32);
        return true;
    }

    return false;
}

uint8_t lowNodeHash(NodeId node)
{
    return static_cast<uint8_t>(node.value & 0xFFU);
}

uint32_t toFrequencyHz(float mhz)
{
    return static_cast<uint32_t>(mhz * 1000000.0f);
}

uint32_t toBandwidthHz(float khz)
{
    return static_cast<uint32_t>(khz * 1000.0f);
}

RadioConfig defaultMeshCoreRadioConfig()
{
    chat::MeshConfig defaults;
    if (const auto* preset =
            chat::meshcore::findRegionPresetById(defaults.meshcore_region_preset))
    {
        defaults.meshcore_freq_mhz = preset->freq_mhz;
        defaults.meshcore_bw_khz = preset->bw_khz;
        defaults.meshcore_sf = preset->sf;
        defaults.meshcore_cr = preset->cr;
        defaults.tx_power = preset->tx_power_dbm;
    }

    RadioConfig radio;
    radio.frequency_hz = toFrequencyHz(defaults.meshcore_freq_mhz);
    radio.bandwidth_hz = toBandwidthHz(defaults.meshcore_bw_khz);
    radio.spreading_factor = defaults.meshcore_sf;
    radio.coding_rate = defaults.meshcore_cr;
    radio.sync_word = kLoraSyncWordPrivate;
    radio.tx_power_dbm = defaults.tx_power;
    return radio;
}

} // namespace

MeshProtocolKind MeshCoreProtocolStrategy::kind() const
{
    return MeshProtocolKind::MeshCore;
}

RadioConfig MeshCoreProtocolStrategy::deriveRadioConfig(const MeshRuntimeConfig& config)
{
    if (config.radio.frequency_hz != 0)
    {
        return config.radio;
    }
    return defaultMeshCoreRadioConfig();
}

ProtocolResult MeshCoreProtocolStrategy::buildDirectMessage(
    const ProtocolBuildContext& context,
    const DirectMessageCommand& command,
    EncodedPacket& out)
{
    out = EncodedPacket{};
    if (command.payload.empty() || command.application_port == 0)
    {
        return ProtocolResult::fail(ProtocolFailure::InvalidInput);
    }

    const bool group_packet = command.to.value == 0 || command.to.isBroadcast();
    if (!group_packet && !command.to.isValidUnicast())
    {
        return ProtocolResult::fail(ProtocolFailure::InvalidInput);
    }

    uint8_t key16[16] = {};
    uint8_t key32[32] = {};
    const chat::meshcore::PayloadProfile profile =
        chat::meshcore::payloadProfileFromVersion(context.meshcore_payload_ver);
    const size_t hash_bytes = chat::meshcore::payloadHashBytes(profile);
    const size_t mac_len = chat::meshcore::payloadMacBytes(profile);

    if (!group_packet)
    {
        ByteView secret = isSupportedDirectSecret(context.channel_key)
                              ? context.channel_key
                              : ByteView{direct_key32_, sizeof(direct_key32_)};
        if (!has_direct_secret_ && !isSupportedDirectSecret(context.channel_key))
        {
            return ProtocolResult::fail(ProtocolFailure::MissingPeerKey);
        }
        if (!mapSecretToKeys(secret, key16, key32))
        {
            return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
        }

        constexpr size_t kDirectPlainPrefix = 2 + 1 + sizeof(command.application_port);
        const size_t kDirectCipherBudget =
            ((kMeshCoreMaxPayloadSize - (hash_bytes * 2U) - mac_len) / kCipherBlockSize) *
            kCipherBlockSize;
        uint8_t plain[kMeshCoreMaxPayloadSize] = {};
        size_t plain_len = 0;
        plain[0] = kDirectAppMagic0;
        plain[1] = kDirectAppMagic1;
        plain[2] = command.request_ack ? kDirectAppFlagWantAck : 0x00;
        std::memcpy(plain + 3, &command.application_port, sizeof(command.application_port));
        if (!copyPayload(command, plain, kDirectCipherBudget, kDirectPlainPrefix, plain_len))
        {
            return ProtocolResult::fail(ProtocolFailure::EncodeFailed);
        }

        uint8_t payload[kMeshCoreMaxPayloadSize] = {};
        size_t payload_len = 0;
        uint8_t dest_hash[chat::meshcore::kMeshCoreV2HashBytes] = {};
        uint8_t src_hash[chat::meshcore::kMeshCoreV2HashBytes] = {};
        if (context.meshcore_peer_hash.data &&
            context.meshcore_local_hash.data &&
            context.meshcore_peer_hash.size >= hash_bytes &&
            context.meshcore_local_hash.size >= hash_bytes)
        {
            std::memcpy(dest_hash, context.meshcore_peer_hash.data, hash_bytes);
            std::memcpy(src_hash, context.meshcore_local_hash.data, hash_bytes);
        }
        else if (profile == chat::meshcore::PayloadProfile::V1)
        {
            dest_hash[0] = lowNodeHash(command.to);
            src_hash[0] = lowNodeHash(context.local_node);
            if (src_hash[0] == 0)
            {
                return ProtocolResult::fail(ProtocolFailure::EncodeFailed);
            }
        }
        else
        {
            return ProtocolResult::fail(ProtocolFailure::EncodeFailed);
        }
        if (!chat::meshcore::buildPeerDatagramPayload(profile,
                                                      dest_hash,
                                                      src_hash,
                                                      key16,
                                                      key32,
                                                      plain,
                                                      plain_len,
                                                      payload,
                                                      sizeof(payload),
                                                      &payload_len))
        {
            return ProtocolResult::fail(ProtocolFailure::EncodeFailed);
        }

        const uint8_t route_type = context.route_type != 0 ? context.route_type : kRouteTypeFlood;
        const uint8_t* route_path = context.route_path.empty() ? nullptr : context.route_path.data;
        const size_t route_path_len = context.route_path.empty() ? 0 : context.route_path.size;
        if (!chat::meshcore::buildFrameNoTransport(profile,
                                                   route_type,
                                                   kPayloadTypeDirectData,
                                                   route_path,
                                                   route_path_len,
                                                   payload,
                                                   payload_len,
                                                   out.bytes,
                                                   sizeof(out.bytes),
                                                   &out.size))
        {
            return ProtocolResult::fail(ProtocolFailure::EncodeFailed);
        }
        return ProtocolResult::success();
    }

    ByteView group_key = context.channel_key;
    if (group_key.empty())
    {
        group_key = has_group_key_
                        ? ByteView{group_key16_, sizeof(group_key16_)}
                        : ByteView{chat::meshcore::publicGroupPsk(), chat::kMeshCoreChannelKeyLen};
    }
    if (!mapSecretToKeys(group_key, key16, key32))
    {
        return ProtocolResult::fail(ProtocolFailure::MissingChannelKey);
    }

    constexpr size_t kGroupPlainPrefix = 2 + sizeof(context.local_node.value) +
                                         sizeof(command.application_port);
    const size_t kGroupCipherBudget =
        ((kMeshCoreMaxPayloadSize - hash_bytes - mac_len) / kCipherBlockSize) * kCipherBlockSize;
    uint8_t plain[kMeshCoreMaxPayloadSize] = {};
    size_t plain_len = 0;
    plain[0] = kGroupDataMagic0;
    plain[1] = kGroupDataMagic1;
    std::memcpy(plain + 2, &context.local_node.value, sizeof(context.local_node.value));
    std::memcpy(plain + 2 + sizeof(context.local_node.value),
                &command.application_port,
                sizeof(command.application_port));
    if (!copyPayload(command, plain, kGroupCipherBudget, kGroupPlainPrefix, plain_len))
    {
        return ProtocolResult::fail(ProtocolFailure::EncodeFailed);
    }

    uint8_t encrypted[kMeshCoreMaxPayloadSize] = {};
    const size_t encrypted_len = chat::meshcore::encryptThenMac(key16,
                                                                key32,
                                                                encrypted,
                                                                sizeof(encrypted),
                                                                plain,
                                                                plain_len,
                                                                mac_len);
    if (encrypted_len == 0 || encrypted_len > (kMeshCoreMaxPayloadSize - hash_bytes))
    {
        return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
    }

    uint8_t payload[kMeshCoreMaxPayloadSize] = {};
    size_t payload_len = 0;
    uint8_t channel_hash[chat::meshcore::kMeshCoreV2HashBytes] = {};
    if (context.meshcore_channel_hash.data &&
        context.meshcore_channel_hash.size >= hash_bytes)
    {
        std::memcpy(channel_hash, context.meshcore_channel_hash.data, hash_bytes);
    }
    else if (profile == chat::meshcore::PayloadProfile::V1 && context.channel_hash != 0)
    {
        channel_hash[0] = context.channel_hash;
    }
    else if (!chat::meshcore::computeChannelHashBytes(key16, channel_hash, hash_bytes))
    {
        return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
    }
    std::memcpy(payload + payload_len, channel_hash, hash_bytes);
    payload_len += hash_bytes;
    std::memcpy(payload + payload_len, encrypted, encrypted_len);
    payload_len += encrypted_len;

    if (!chat::meshcore::buildFrameNoTransport(profile,
                                               kRouteTypeFlood,
                                               kPayloadTypeGrpData,
                                               nullptr,
                                               0,
                                               payload,
                                               payload_len,
                                               out.bytes,
                                               sizeof(out.bytes),
                                               &out.size))
    {
        return ProtocolResult::fail(ProtocolFailure::EncodeFailed);
    }

    return ProtocolResult::success();
}

ProtocolResult MeshCoreProtocolStrategy::parseRadioPacket(const RadioRxPacket& packet,
                                                          MeshProtocolEvent& out)
{
    out = MeshProtocolEvent{};
    if (packet.size == 0 || packet.size > kMeshCoreMaxFrameSize)
    {
        return ProtocolResult::fail(ProtocolFailure::InvalidInput);
    }

    chat::meshcore::ParsedPacket parsed;
    if (!chat::meshcore::parsePacket(packet.bytes, packet.size, &parsed) ||
        !chat::meshcore::isSupportedPayloadVersion(parsed.payload_ver) ||
        !parsed.payload || parsed.payload_len == 0)
    {
        return ProtocolResult::fail(ProtocolFailure::DecodeFailed);
    }
    const chat::meshcore::PayloadProfile profile =
        chat::meshcore::payloadProfileFromVersion(parsed.payload_ver);
    const size_t hash_bytes = chat::meshcore::payloadHashBytes(profile);
    const size_t mac_len = chat::meshcore::payloadMacBytes(profile);

    if (parsed.payload_type == kPayloadTypeAdvert && parsed.payload_len >= kMeshCorePubKeySize)
    {
        out.kind = MeshProtocolEventKind::PeerKeyLearned;
        out.peer_key.node_id = NodeId{
            chat::meshcore::deriveNodeIdFromPubkey(parsed.payload, kMeshCorePubKeySize)};
        out.peer = out.peer_key.node_id;
        std::memcpy(out.peer_key.public_key, parsed.payload, kMeshCorePubKeySize);
        out.peer_key.updated_at_ms = packet.received_at_ms;

        if (parsed.payload_len < kAdvertMinPayloadSize)
        {
            return ProtocolResult::success();
        }

        const uint8_t* pubkey = parsed.payload;
        const uint8_t* timestamp_bytes = parsed.payload + kMeshCorePubKeySize;
        const uint8_t* signature = timestamp_bytes + sizeof(uint32_t);
        const uint8_t* app_data = parsed.payload + kAdvertMinPayloadSize;
        const size_t app_data_len = parsed.payload_len - kAdvertMinPayloadSize;

        std::array<uint8_t, kMeshCorePubKeySize + sizeof(uint32_t) + kMeshCoreMaxPayloadSize>
            signed_message{};
        size_t signed_len = 0;
        std::memcpy(signed_message.data() + signed_len, pubkey, kMeshCorePubKeySize);
        signed_len += kMeshCorePubKeySize;
        std::memcpy(signed_message.data() + signed_len, timestamp_bytes, sizeof(uint32_t));
        signed_len += sizeof(uint32_t);
        if (app_data_len > 0)
        {
            std::memcpy(signed_message.data() + signed_len, app_data, app_data_len);
            signed_len += app_data_len;
        }

        McIdentityFlow identity;
        const ProtocolResult verified =
            identity.verify(ByteView{pubkey, kMeshCorePubKeySize},
                            ByteView{signature, kAdvertSignatureSize},
                            ByteView{signed_message.data(), signed_len});
        if (!verified.ok)
        {
            return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
        }

        chat::meshcore::DecodedAdvertAppData advert{};
        if (!chat::meshcore::decodeAdvertAppData(app_data, app_data_len, &advert))
        {
            return ProtocolResult::fail(ProtocolFailure::DecodeFailed);
        }

        uint32_t advert_ts = 0;
        std::memcpy(&advert_ts, timestamp_bytes, sizeof(advert_ts));
        out.kind = MeshProtocolEventKind::PeerAdvertReceived;
        out.advert.has_name = advert.has_name;
        if (advert.has_name)
        {
            std::memcpy(out.advert.name, advert.name, sizeof(out.advert.name));
        }
        out.advert.has_location = advert.has_location;
        out.advert.latitude_i6 = advert.latitude_i6;
        out.advert.longitude_i6 = advert.longitude_i6;
        out.advert.node_type = advert.node_type;
        out.advert.timestamp = advert_ts;
        out.advert.hops = static_cast<uint8_t>(std::min<size_t>(parsed.path_hop_count, 0xFFU));
        return ProtocolResult::success();
    }

    uint8_t plain[kMeshCoreMaxPayloadSize] = {};
    size_t plain_len = 0;

    if (parsed.payload_type == kPayloadTypeDirectData)
    {
        if (!has_direct_secret_ || parsed.payload_len <= ((hash_bytes * 2U) + mac_len))
        {
            return ProtocolResult::fail(ProtocolFailure::MissingPeerKey);
        }
        const uint8_t* dest_hash = parsed.payload;
        const uint8_t* src_hash = parsed.payload + hash_bytes;
        if (local_public_hash_ != 0 && dest_hash[0] != local_public_hash_)
        {
            return ProtocolResult::fail(ProtocolFailure::InvalidInput);
        }
        if (!chat::meshcore::macThenDecrypt(direct_key16_,
                                            direct_key32_,
                                            parsed.payload + (hash_bytes * 2U),
                                            parsed.payload_len - (hash_bytes * 2U),
                                            plain,
                                            &plain_len,
                                            mac_len))
        {
            return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
        }
        plain_len = chat::meshcore::trimTrailingZeros(plain, plain_len);

        chat::meshcore::DecodedDirectAppPayload decoded;
        if (!chat::meshcore::decodeDirectAppPayload(plain, plain_len, &decoded))
        {
            return ProtocolResult::fail(ProtocolFailure::DecodeFailed);
        }

        out.kind = MeshProtocolEventKind::MessageReceived;
        out.peer = NodeId{src_hash[0]};
        out.packet_id = chat::meshcore::packetSignature(parsed.payload_type,
                                                        parsed.path_len,
                                                        parsed.payload,
                                                        parsed.payload_len);
        out.setPayload(decoded.payload, decoded.payload_len);
        return ProtocolResult::success();
    }

    if (parsed.payload_type == kPayloadTypeGrpData)
    {
        if (parsed.payload_len <= (hash_bytes + mac_len))
        {
            return ProtocolResult::fail(ProtocolFailure::DecodeFailed);
        }
        const uint8_t* channel_hash = parsed.payload;
        uint8_t rx_key16[16] = {};
        uint8_t rx_key32[32] = {};
        const uint8_t* decrypt_key16 = group_key16_;
        const uint8_t* decrypt_key32 = group_key32_;
        uint8_t expected_hash[chat::meshcore::kMeshCoreV2HashBytes] = {};
        bool has_expected_hash = false;
        if (has_group_key_)
        {
            if (profile == chat::meshcore::PayloadProfile::V1 && group_channel_hash_ != 0)
            {
                expected_hash[0] = group_channel_hash_;
                has_expected_hash = true;
            }
            else
            {
                has_expected_hash = chat::meshcore::computeChannelHashBytes(group_key16_,
                                                                            expected_hash,
                                                                            hash_bytes);
            }
        }
        if (!has_group_key_)
        {
            if (!mapSecretToKeys(ByteView{chat::meshcore::publicGroupPsk(),
                                          chat::kMeshCoreChannelKeyLen},
                                 rx_key16,
                                 rx_key32))
            {
                return ProtocolResult::fail(ProtocolFailure::MissingChannelKey);
            }
            decrypt_key16 = rx_key16;
            decrypt_key32 = rx_key32;
            has_expected_hash = chat::meshcore::computeChannelHashBytes(rx_key16,
                                                                        expected_hash,
                                                                        hash_bytes);
        }
        if (has_expected_hash && memcmp(channel_hash, expected_hash, hash_bytes) != 0)
        {
            return ProtocolResult::fail(has_group_key_
                                            ? ProtocolFailure::InvalidInput
                                            : ProtocolFailure::MissingChannelKey);
        }
        if (!chat::meshcore::macThenDecrypt(decrypt_key16,
                                            decrypt_key32,
                                            parsed.payload + hash_bytes,
                                            parsed.payload_len - hash_bytes,
                                            plain,
                                            &plain_len,
                                            mac_len))
        {
            return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
        }
        plain_len = chat::meshcore::trimTrailingZeros(plain, plain_len);

        chat::meshcore::DecodedGroupAppPayload decoded;
        if (!chat::meshcore::decodeGroupAppPayload(plain, plain_len, &decoded))
        {
            return ProtocolResult::fail(ProtocolFailure::DecodeFailed);
        }

        out.kind = MeshProtocolEventKind::MessageReceived;
        out.peer = NodeId{decoded.sender};
        out.packet_id = chat::meshcore::packetSignature(parsed.payload_type,
                                                        parsed.path_len,
                                                        parsed.payload,
                                                        parsed.payload_len);
        out.setPayload(decoded.payload, decoded.payload_len);
        return ProtocolResult::success();
    }

    return ProtocolResult::fail(ProtocolFailure::Unsupported);
}

void MeshCoreProtocolStrategy::setLocalPublicHash(uint8_t public_hash)
{
    local_public_hash_ = public_hash;
}

void MeshCoreProtocolStrategy::setDirectSharedSecret(ByteView shared_secret)
{
    has_direct_secret_ = false;
    std::memset(direct_key16_, 0, sizeof(direct_key16_));
    std::memset(direct_key32_, 0, sizeof(direct_key32_));
    if (!isDirectSharedSecret(shared_secret))
    {
        return;
    }
    chat::meshcore::sharedSecretToKeys(shared_secret.data, direct_key16_, direct_key32_);
    has_direct_secret_ = true;
}

void MeshCoreProtocolStrategy::clearDirectSharedSecret()
{
    has_direct_secret_ = false;
    std::memset(direct_key16_, 0, sizeof(direct_key16_));
    std::memset(direct_key32_, 0, sizeof(direct_key32_));
}

void MeshCoreProtocolStrategy::setGroupKey(ByteView key, uint8_t channel_hash)
{
    has_group_key_ = false;
    std::memset(group_key16_, 0, sizeof(group_key16_));
    std::memset(group_key32_, 0, sizeof(group_key32_));
    group_channel_hash_ = 0;
    if (!mapSecretToKeys(key, group_key16_, group_key32_))
    {
        return;
    }
    group_channel_hash_ = channel_hash != 0 ? channel_hash
                                            : chat::meshcore::computeChannelHash(group_key16_);
    has_group_key_ = true;
}

void MeshCoreProtocolStrategy::clearGroupKey()
{
    has_group_key_ = false;
    std::memset(group_key16_, 0, sizeof(group_key16_));
    std::memset(group_key32_, 0, sizeof(group_key32_));
    group_channel_hash_ = 0;
}

} // namespace meshcore
} // namespace mesh
