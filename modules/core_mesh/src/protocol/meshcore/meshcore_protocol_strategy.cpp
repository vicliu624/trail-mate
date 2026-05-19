#include "mesh/protocol/meshcore/meshcore_protocol_strategy.h"

#include "chat/domain/chat_types.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"

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
constexpr uint8_t kPayloadVer1 = 0x00;
constexpr uint8_t kDirectAppMagic0 = 0xDA;
constexpr uint8_t kDirectAppMagic1 = 0x7A;
constexpr uint8_t kDirectAppFlagWantAck = 0x01;
constexpr uint8_t kGroupDataMagic0 = 0x47;
constexpr uint8_t kGroupDataMagic1 = 0x44;
constexpr uint8_t kLoraSyncWordPrivate = 0x12;
constexpr size_t kCipherMacSize = 2;
constexpr size_t kCipherBlockSize = 16;
constexpr size_t kMeshCoreMaxFrameSize = 255;
constexpr size_t kMeshCoreMaxPayloadSize = 184;
constexpr size_t kMeshCorePubKeySize = 32;

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

    size_t body_len = command.payload.size;
    if (body_len + prefix_size > plain_capacity)
    {
        body_len = plain_capacity - prefix_size;
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
        constexpr size_t kDirectCipherBudget =
            ((kMeshCoreMaxPayloadSize - 2 - kCipherMacSize) / kCipherBlockSize) * kCipherBlockSize;
        uint8_t plain[kDirectCipherBudget] = {};
        size_t plain_len = 0;
        plain[0] = kDirectAppMagic0;
        plain[1] = kDirectAppMagic1;
        plain[2] = command.request_ack ? kDirectAppFlagWantAck : 0x00;
        std::memcpy(plain + 3, &command.application_port, sizeof(command.application_port));
        if (!copyPayload(command, plain, sizeof(plain), kDirectPlainPrefix, plain_len))
        {
            return ProtocolResult::fail(ProtocolFailure::EncodeFailed);
        }

        uint8_t payload[kMeshCoreMaxPayloadSize] = {};
        size_t payload_len = 0;
        const uint8_t dest_hash = lowNodeHash(command.to);
        const uint8_t src_hash = lowNodeHash(context.local_node);
        if (src_hash == 0 || !chat::meshcore::buildPeerDatagramPayload(dest_hash,
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
        if (!chat::meshcore::buildFrameNoTransport(route_type,
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

    ByteView group_key = context.channel_key.empty()
                             ? ByteView{group_key16_, sizeof(group_key16_)}
                             : context.channel_key;
    if (!has_group_key_ && context.channel_key.empty())
    {
        return ProtocolResult::fail(ProtocolFailure::MissingPeerKey);
    }
    if (!mapSecretToKeys(group_key, key16, key32))
    {
        return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
    }

    constexpr size_t kGroupPlainPrefix = 2 + sizeof(context.local_node.value) +
                                         sizeof(command.application_port);
    constexpr size_t kGroupCipherBudget =
        ((kMeshCoreMaxPayloadSize - 1 - kCipherMacSize) / kCipherBlockSize) * kCipherBlockSize;
    uint8_t plain[kGroupCipherBudget] = {};
    size_t plain_len = 0;
    plain[0] = kGroupDataMagic0;
    plain[1] = kGroupDataMagic1;
    std::memcpy(plain + 2, &context.local_node.value, sizeof(context.local_node.value));
    std::memcpy(plain + 2 + sizeof(context.local_node.value),
                &command.application_port,
                sizeof(command.application_port));
    if (!copyPayload(command, plain, sizeof(plain), kGroupPlainPrefix, plain_len))
    {
        return ProtocolResult::fail(ProtocolFailure::EncodeFailed);
    }

    uint8_t encrypted[kMeshCoreMaxPayloadSize] = {};
    const size_t encrypted_len = chat::meshcore::encryptThenMac(key16,
                                                                key32,
                                                                encrypted,
                                                                sizeof(encrypted),
                                                                plain,
                                                                plain_len);
    if (encrypted_len == 0 || encrypted_len > (kMeshCoreMaxPayloadSize - 1))
    {
        return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
    }

    uint8_t payload[kMeshCoreMaxPayloadSize] = {};
    size_t payload_len = 0;
    payload[payload_len++] = context.channel_hash != 0
                                 ? context.channel_hash
                                 : chat::meshcore::computeChannelHash(key16);
    std::memcpy(payload + payload_len, encrypted, encrypted_len);
    payload_len += encrypted_len;

    if (!chat::meshcore::buildFrameNoTransport(kRouteTypeFlood,
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
        parsed.payload_ver != kPayloadVer1 ||
        !parsed.payload || parsed.payload_len == 0)
    {
        return ProtocolResult::fail(ProtocolFailure::DecodeFailed);
    }

    if (parsed.payload_type == kPayloadTypeAdvert && parsed.payload_len >= kMeshCorePubKeySize)
    {
        out.kind = MeshProtocolEventKind::PeerKeyLearned;
        out.peer_key.node_id = NodeId{
            chat::meshcore::deriveNodeIdFromPubkey(parsed.payload, kMeshCorePubKeySize)};
        out.peer = out.peer_key.node_id;
        std::memcpy(out.peer_key.public_key, parsed.payload, kMeshCorePubKeySize);
        out.peer_key.updated_at_ms = packet.received_at_ms;
        return ProtocolResult::success();
    }

    uint8_t plain[kMeshCoreMaxPayloadSize] = {};
    size_t plain_len = 0;

    if (parsed.payload_type == kPayloadTypeDirectData)
    {
        if (!has_direct_secret_ || parsed.payload_len <= (2 + kCipherMacSize))
        {
            return ProtocolResult::fail(ProtocolFailure::MissingPeerKey);
        }
        const uint8_t dest_hash = parsed.payload[0];
        const uint8_t src_hash = parsed.payload[1];
        if (local_public_hash_ != 0 && dest_hash != local_public_hash_)
        {
            return ProtocolResult::fail(ProtocolFailure::InvalidInput);
        }
        if (!chat::meshcore::macThenDecrypt(direct_key16_,
                                            direct_key32_,
                                            parsed.payload + 2,
                                            parsed.payload_len - 2,
                                            plain,
                                            &plain_len))
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
        out.peer = NodeId{src_hash};
        out.packet_id = chat::meshcore::packetSignature(parsed.payload_type,
                                                        parsed.path_len,
                                                        parsed.payload,
                                                        parsed.payload_len);
        out.setPayload(decoded.payload, decoded.payload_len);
        return ProtocolResult::success();
    }

    if (parsed.payload_type == kPayloadTypeGrpData)
    {
        if (!has_group_key_ || parsed.payload_len <= (1 + kCipherMacSize))
        {
            return ProtocolResult::fail(ProtocolFailure::MissingPeerKey);
        }
        const uint8_t channel_hash = parsed.payload[0];
        if (group_channel_hash_ != 0 && channel_hash != group_channel_hash_)
        {
            return ProtocolResult::fail(ProtocolFailure::InvalidInput);
        }
        if (!chat::meshcore::macThenDecrypt(group_key16_,
                                            group_key32_,
                                            parsed.payload + 1,
                                            parsed.payload_len - 1,
                                            plain,
                                            &plain_len))
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
