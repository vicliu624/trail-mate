/**
 * @file lxmf_adapter.cpp
 * @brief Device-side LXMF adapter over the existing RNode raw carrier
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_adapter.h"

#include "../../internal/blob_store_io.h"
#include "chat/domain/contact_types.h"
#include "chat/time_utils.h"
#include "sys/event_bus.h"

#include <Arduino.h>
#include <Curve25519.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace chat::lxmf
{
namespace
{
constexpr size_t kMaxPacketLen = reticulum::kReticulumMtu;
constexpr size_t kMaxLxmfMessageLen = reticulum::kReticulumMtu;
constexpr size_t kSignedPartMaxLen = reticulum::kReticulumMtu;
constexpr size_t kMaxTokenPlaintextLen = reticulum::kReticulumMtu;
constexpr size_t kPathRequestTagSize = reticulum::kTruncatedHashSize;
constexpr uint32_t kPathRequestMinIntervalMs = 20000;
constexpr uint32_t kPathRefreshAgeS = 300;
constexpr size_t kMaxPersistedPeers = 64;
constexpr size_t kMaxPaths = 96;
constexpr size_t kMaxPacketFilter = 128;
constexpr size_t kMaxReverseEntries = 64;
constexpr size_t kMaxLinkRelays = 24;
constexpr uint32_t kPacketFilterTtlMs = 30000;
constexpr uint32_t kReverseEntryTtlMs = 60000;
constexpr uint32_t kLinkRelayTtlMs = 300000;
constexpr uint8_t kMaxTransportHops = 128;
constexpr const char* kPeersPrefsNs = "lxmf_peers";
constexpr const char* kPeersPrefsKey = "peers";
constexpr const char* kPeersPrefsVer = "ver";
constexpr const char* kPeersPrefsCrc = "crc";
constexpr uint8_t kPeersPrefsVersion = 1;

struct PersistedPeerRecord
{
    uint8_t destination_hash[reticulum::kTruncatedHashSize];
    uint8_t identity_hash[reticulum::kTruncatedHashSize];
    uint8_t enc_pub[LxmfIdentity::kEncPubKeySize];
    uint8_t sig_pub[LxmfIdentity::kSigPubKeySize];
    uint32_t last_seen_s;
    char display_name[32];
};

static_assert(sizeof(PersistedPeerRecord) == 132, "Unexpected LXMF peer record size");

void fillRandomBytes(uint8_t* out, size_t len)
{
    if (!out || len == 0)
    {
        return;
    }

    size_t offset = 0;
    while (offset < len)
    {
        const uint32_t rnd = static_cast<uint32_t>(esp_random());
        const size_t chunk = (len - offset >= sizeof(rnd)) ? sizeof(rnd) : (len - offset);
        memcpy(out + offset, &rnd, chunk);
        offset += chunk;
    }
}

bool isZeroBytes(const uint8_t* data, size_t len)
{
    if (!data)
    {
        return true;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (data[i] != 0)
        {
            return false;
        }
    }
    return true;
}

bool hashesEqual(const uint8_t* a, const uint8_t* b, size_t len)
{
    if ((!a || !b) && len != 0)
    {
        return false;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

void copyHash(uint8_t* out, const uint8_t* in, size_t len)
{
    if (!out || !in || len == 0)
    {
        return;
    }
    memcpy(out, in, len);
}

void copyCString(char* out, size_t out_len, const char* in)
{
    if (!out || out_len == 0)
    {
        return;
    }

    out[0] = '\0';
    if (!in)
    {
        return;
    }

    strncpy(out, in, out_len - 1);
    out[out_len - 1] = '\0';
}

uint32_t fnv1a32(const uint8_t* data, size_t len)
{
    uint32_t hash = 2166136261UL;
    if (!data)
    {
        return hash;
    }

    for (size_t i = 0; i < len; ++i)
    {
        hash ^= static_cast<uint32_t>(data[i]);
        hash *= 16777619UL;
    }
    return hash;
}

bool isLxmfDeliveryAnnounce(const reticulum::ParsedAnnounce& announce)
{
    if (!announce.valid || !announce.name_hash)
    {
        return false;
    }

    uint8_t expected_name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", "delivery", expected_name_hash);
    return hashesEqual(expected_name_hash, announce.name_hash, sizeof(expected_name_hash));
}

bool computeLinkIdFromLinkRequest(const uint8_t* raw_packet, size_t raw_len,
                                  const reticulum::ParsedPacket& packet,
                                  uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    if (!raw_packet || raw_len == 0 || !out_hash ||
        packet.packet_type != reticulum::PacketType::LinkRequest ||
        !packet.payload)
    {
        return false;
    }

    uint8_t scratch[kMaxPacketLen] = {};
    if (raw_len > sizeof(scratch))
    {
        return false;
    }

    size_t working_len = raw_len;
    memcpy(scratch, raw_packet, raw_len);

    if (packet.payload_len > 64)
    {
        const size_t trim = packet.payload_len - 64;
        if (trim >= working_len)
        {
            return false;
        }
        working_len -= trim;
    }

    reticulum::computeTruncatedPacketHash(scratch, working_len, out_hash);
    return true;
}

} // namespace

LxmfAdapter::LxmfAdapter(LoraBoard& board)
    : raw_(board)
{
}

MeshCapabilities LxmfAdapter::getCapabilities() const
{
    MeshCapabilities caps;
    caps.supports_unicast_text = true;
    caps.supports_node_info = true;
    return caps;
}

bool LxmfAdapter::sendText(ChannelId channel, const std::string& text,
                           MessageId* out_msg_id, NodeId peer)
{
    processRadioPackets();

    if (channel != ChannelId::PRIMARY || text.empty() || peer == 0 || !isReady())
    {
        if (out_msg_id)
        {
            *out_msg_id = 0;
        }
        return false;
    }

    PeerInfo* peer_info = findPeerByNodeId(peer);
    if (!peer_info)
    {
        if (out_msg_id)
        {
            *out_msg_id = 0;
        }
        return false;
    }

    if (shouldRequestPath(*peer_info))
    {
        (void)sendPathRequest(*peer_info);
    }

    uint8_t packed_payload[kMaxLxmfMessageLen] = {};
    size_t packed_payload_len = sizeof(packed_payload);
    if (!encodeTextPayload(static_cast<double>(currentTimestampSeconds()),
                           "",
                           text.c_str(),
                           packed_payload,
                           &packed_payload_len))
    {
        if (out_msg_id)
        {
            *out_msg_id = 0;
        }
        return false;
    }

    uint8_t signed_part[kSignedPartMaxLen] = {};
    size_t signed_part_len = sizeof(signed_part);
    uint8_t message_hash[reticulum::kFullHashSize] = {};
    if (!buildSignedPart(peer_info->destination_hash,
                         identity_.destinationHash(),
                         packed_payload,
                         packed_payload_len,
                         signed_part,
                         &signed_part_len,
                         message_hash))
    {
        if (out_msg_id)
        {
            *out_msg_id = 0;
        }
        return false;
    }

    uint8_t signature[reticulum::kSignatureSize] = {};
    if (!identity_.sign(signed_part, signed_part_len, signature))
    {
        if (out_msg_id)
        {
            *out_msg_id = 0;
        }
        return false;
    }

    uint8_t lxmf_message[kMaxLxmfMessageLen] = {};
    size_t lxmf_message_len = sizeof(lxmf_message);
    if (!packMessage(peer_info->destination_hash,
                     identity_.destinationHash(),
                     signature,
                     packed_payload,
                     packed_payload_len,
                     lxmf_message,
                     &lxmf_message_len))
    {
        if (out_msg_id)
        {
            *out_msg_id = 0;
        }
        return false;
    }

    uint8_t packet[kMaxPacketLen] = {};
    size_t packet_len = sizeof(packet);
    if (!buildEncryptedPacketForPeer(*peer_info, lxmf_message, lxmf_message_len, packet, &packet_len))
    {
        if (out_msg_id)
        {
            *out_msg_id = 0;
        }
        return false;
    }

    const MessageId message_id = messageIdFromHash(message_hash);
    const bool ok = routeAndSendPacket(packet, packet_len, true);
    if (out_msg_id)
    {
        *out_msg_id = message_id;
    }
    sys::EventBus::publish(new sys::ChatSendResultEvent(message_id, ok), 0);
    return ok;
}

bool LxmfAdapter::pollIncomingText(MeshIncomingText* out)
{
    processRadioPackets();
    maybeAnnounce();

    if (!out || text_receive_queue_.empty())
    {
        return false;
    }

    *out = std::move(text_receive_queue_.front());
    text_receive_queue_.pop();
    return true;
}

bool LxmfAdapter::sendAppData(ChannelId channel, uint32_t portnum,
                              const uint8_t* payload, size_t len,
                              NodeId dest, bool want_ack,
                              MessageId packet_id,
                              bool want_response)
{
    (void)channel;
    (void)portnum;
    (void)payload;
    (void)len;
    (void)dest;
    (void)want_ack;
    (void)packet_id;
    (void)want_response;
    return false;
}

bool LxmfAdapter::pollIncomingData(MeshIncomingData* out)
{
    (void)out;
    processRadioPackets();
    maybeAnnounce();
    return false;
}

bool LxmfAdapter::requestNodeInfo(NodeId dest, bool want_response)
{
    (void)want_response;

    processRadioPackets();
    if (dest != 0)
    {
        PeerInfo* peer = findPeerByNodeId(dest);
        if (!peer)
        {
            return false;
        }

        if (!shouldRequestPath(*peer))
        {
            return true;
        }
        return sendPathRequest(*peer);
    }

    announce_pending_ = true;
    return sendAnnounce();
}

NodeId LxmfAdapter::getNodeId() const
{
    return identity_.nodeId();
}

void LxmfAdapter::applyConfig(const MeshConfig& config)
{
    config_ = config;
    if (identity_.init() && !peers_loaded_)
    {
        loadPersistedPeers();
    }
    raw_.applyConfig(config_);
    last_announce_ms_ = millis();
    announce_pending_ = true;
}

void LxmfAdapter::setUserInfo(const char* long_name, const char* short_name)
{
    user_long_name_ = (long_name && long_name[0] != '\0') ? long_name : "";
    user_short_name_ = (short_name && short_name[0] != '\0') ? short_name : "";
    announce_pending_ = true;
}

bool LxmfAdapter::isReady() const
{
    return identity_.isReady() && raw_.isReady();
}

bool LxmfAdapter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    (void)out_data;
    (void)out_len;
    (void)max_len;
    return false;
}

void LxmfAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    raw_.handleRawPacket(data, size);
}

void LxmfAdapter::setLastRxStats(float rssi, float snr)
{
    raw_.setLastRxStats(rssi, snr);
}

void LxmfAdapter::processRadioPackets()
{
    cullTransportState();

    uint8_t packet[kMaxPacketLen] = {};
    size_t packet_len = 0;
    while (raw_.pollIncomingRawPacket(packet, packet_len, sizeof(packet)))
    {
        reticulum::ParsedPacket parsed{};
        if (reticulum::parsePacket(packet, packet_len, &parsed))
        {
            if (parsed.hops < 0xFF)
            {
                parsed.hops += 1;
            }

            uint8_t packet_hash[reticulum::kFullHashSize] = {};
            reticulum::computePacketHash(packet, packet_len, packet_hash);
            if (isDuplicatePacket(packet_hash))
            {
                continue;
            }
            rememberPacket(packet_hash);

            if (parsed.packet_type == reticulum::PacketType::Announce)
            {
                handleAnnouncePacket(packet, packet_len, parsed);
            }
            else if (parsed.packet_type == reticulum::PacketType::Proof)
            {
                handleProofPacket(packet, packet_len, parsed);
            }
            else if (parsed.packet_type == reticulum::PacketType::LinkRequest)
            {
                handleLinkRequestPacket(packet, packet_len, parsed);
            }
            else if (parsed.packet_type == reticulum::PacketType::Data)
            {
                if (!handlePathRequestPacket(parsed) &&
                    !handleCacheRequestPacket(parsed) &&
                    !maybeForwardLinkPacket(packet, packet_len, parsed) &&
                    !maybeForwardTransportPacket(packet, packet_len, parsed))
                {
                    handleDataPacket(packet, packet_len, parsed);
                }
            }
            else
            {
                (void)maybeForwardLinkPacket(packet, packet_len, parsed);
                (void)maybeForwardTransportPacket(packet, packet_len, parsed);
            }
        }

        MeshIncomingData discarded;
        while (raw_.pollIncomingData(&discarded))
        {
        }
    }
}

void LxmfAdapter::maybeAnnounce()
{
    if (!announce_pending_ && (millis() - last_announce_ms_) < kAnnounceIntervalMs)
    {
        return;
    }
    if (announce_pending_ && (millis() - last_announce_ms_) < kInitialAnnounceDelayMs)
    {
        return;
    }
    (void)sendAnnounce();
}

bool LxmfAdapter::sendAnnounce(reticulum::PacketContext context)
{
    if (!isReady())
    {
        return false;
    }

    uint8_t app_data[96] = {};
    size_t app_data_len = sizeof(app_data);
    if (!packPeerAnnounceAppData(effectiveDisplayName(),
                                 false,
                                 0,
                                 app_data,
                                 &app_data_len))
    {
        return false;
    }

    uint8_t combined_pub[reticulum::kCombinedPublicKeySize] = {};
    identity_.combinedPublicKey(combined_pub);

    uint8_t name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", "delivery", name_hash);

    uint8_t random_hash[10] = {};
    fillRandomBytes(random_hash, 5);
    const uint64_t now_s = currentTimestampSeconds();
    random_hash[5] = static_cast<uint8_t>((now_s >> 32) & 0xFFU);
    random_hash[6] = static_cast<uint8_t>((now_s >> 24) & 0xFFU);
    random_hash[7] = static_cast<uint8_t>((now_s >> 16) & 0xFFU);
    random_hash[8] = static_cast<uint8_t>((now_s >> 8) & 0xFFU);
    random_hash[9] = static_cast<uint8_t>(now_s & 0xFFU);

    uint8_t signed_data[kMaxPacketLen] = {};
    size_t signed_len = 0;
    memcpy(signed_data + signed_len, identity_.destinationHash(), reticulum::kTruncatedHashSize);
    signed_len += reticulum::kTruncatedHashSize;
    memcpy(signed_data + signed_len, combined_pub, sizeof(combined_pub));
    signed_len += sizeof(combined_pub);
    memcpy(signed_data + signed_len, name_hash, sizeof(name_hash));
    signed_len += sizeof(name_hash);
    memcpy(signed_data + signed_len, random_hash, sizeof(random_hash));
    signed_len += sizeof(random_hash);
    memcpy(signed_data + signed_len, app_data, app_data_len);
    signed_len += app_data_len;

    uint8_t signature[reticulum::kSignatureSize] = {};
    if (!identity_.sign(signed_data, signed_len, signature))
    {
        return false;
    }

    uint8_t announce_payload[kMaxPacketLen] = {};
    size_t announce_payload_len = 0;
    memcpy(announce_payload + announce_payload_len, combined_pub, sizeof(combined_pub));
    announce_payload_len += sizeof(combined_pub);
    memcpy(announce_payload + announce_payload_len, name_hash, sizeof(name_hash));
    announce_payload_len += sizeof(name_hash);
    memcpy(announce_payload + announce_payload_len, random_hash, sizeof(random_hash));
    announce_payload_len += sizeof(random_hash);
    memcpy(announce_payload + announce_payload_len, signature, sizeof(signature));
    announce_payload_len += sizeof(signature);
    memcpy(announce_payload + announce_payload_len, app_data, app_data_len);
    announce_payload_len += app_data_len;

    uint8_t packet[kMaxPacketLen] = {};
    size_t packet_len = sizeof(packet);
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::Announce,
                                       reticulum::DestinationType::Single,
                                       context,
                                       false,
                                       identity_.destinationHash(),
                                       announce_payload,
                                       announce_payload_len,
                                       packet,
                                       &packet_len))
    {
        return false;
    }

    if (!routeAndSendPacket(packet, packet_len, false))
    {
        return false;
    }

    last_announce_ms_ = millis();
    announce_pending_ = false;
    return true;
}

bool LxmfAdapter::handleAnnouncePacket(const uint8_t* raw_packet, size_t raw_len,
                                       const reticulum::ParsedPacket& packet)
{
    if (!raw_packet || raw_len == 0 || !packet.destination_hash)
    {
        return false;
    }
    if (packet.destination_type != reticulum::DestinationType::Single)
    {
        return false;
    }
    if (packet.context != static_cast<uint8_t>(reticulum::PacketContext::None) &&
        packet.context != static_cast<uint8_t>(reticulum::PacketContext::PathResponse))
    {
        return false;
    }

    reticulum::ParsedAnnounce announce{};
    if (!reticulum::parseAnnounce(packet, &announce) || !announce.valid)
    {
        return false;
    }

    uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::computeIdentityHash(announce.public_key, identity_hash);

    uint8_t expected_destination_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::computeDestinationHash(announce.name_hash, identity_hash, expected_destination_hash);
    if (!hashesEqual(expected_destination_hash, packet.destination_hash, reticulum::kTruncatedHashSize))
    {
        return false;
    }

    uint8_t signed_data[kMaxPacketLen] = {};
    size_t signed_len = 0;
    memcpy(signed_data + signed_len, packet.destination_hash, reticulum::kTruncatedHashSize);
    signed_len += reticulum::kTruncatedHashSize;
    memcpy(signed_data + signed_len, announce.public_key, reticulum::kCombinedPublicKeySize);
    signed_len += reticulum::kCombinedPublicKeySize;
    memcpy(signed_data + signed_len, announce.name_hash, reticulum::kNameHashSize);
    signed_len += reticulum::kNameHashSize;
    memcpy(signed_data + signed_len, announce.random_hash, 10);
    signed_len += 10;
    if (announce.app_data_len != 0)
    {
        memcpy(signed_data + signed_len, announce.app_data, announce.app_data_len);
        signed_len += announce.app_data_len;
    }

    const uint8_t* sig_pub = announce.public_key + reticulum::kEncryptionPublicKeySize;
    if (!LxmfIdentity::verify(sig_pub, announce.signature, signed_data, signed_len))
    {
        return false;
    }

    PathEntry& path = upsertPath(packet.destination_hash);
    const uint32_t now_s = currentTimestampSeconds();
    path.hops = packet.hops;
    path.last_seen_s = now_s;
    path.direct = (packet.transport_id == nullptr);
    if (packet.transport_id)
    {
        copyHash(path.next_hop_transport, packet.transport_id, sizeof(path.next_hop_transport));
    }
    else
    {
        copyHash(path.next_hop_transport, packet.destination_hash, sizeof(path.next_hop_transport));
    }

    if (raw_len <= sizeof(path.cached_announce))
    {
        memcpy(path.cached_announce, raw_packet, raw_len);
        path.cached_announce_len = raw_len;
        reticulum::computePacketHash(raw_packet, raw_len, path.cached_packet_hash);
    }

    if (shouldRebroadcastAnnounce(packet))
    {
        (void)rebroadcastAnnounce(path, packet);
    }

    if (!isLxmfDeliveryAnnounce(announce) ||
        hashesEqual(packet.destination_hash, identity_.destinationHash(), reticulum::kTruncatedHashSize))
    {
        return true;
    }

    PeerInfo& peer = upsertPeer(packet.destination_hash);
    copyHash(peer.identity_hash, identity_hash, sizeof(peer.identity_hash));
    memcpy(peer.enc_pub, announce.public_key, sizeof(peer.enc_pub));
    memcpy(peer.sig_pub, sig_pub, sizeof(peer.sig_pub));
    peer.last_seen_s = now_s;

    char display_name[sizeof(peer.display_name)] = {};
    bool has_stamp_cost = false;
    uint8_t stamp_cost = 0;
    if (announce.app_data && announce.app_data_len != 0 &&
        unpackPeerAnnounceAppData(announce.app_data, announce.app_data_len,
                                  display_name, sizeof(display_name),
                                  &has_stamp_cost, &stamp_cost))
    {
        (void)has_stamp_cost;
        (void)stamp_cost;
        copyCString(peer.display_name, sizeof(peer.display_name), display_name);
    }
    else if (peer.display_name[0] == '\0')
    {
        snprintf(peer.display_name, sizeof(peer.display_name),
                 "%08lX", static_cast<unsigned long>(peer.node_id));
    }

    (void)persistPeers();
    publishPeerUpdate(peer);
    return true;
}

bool LxmfAdapter::handleDataPacket(const uint8_t* raw_packet, size_t raw_len,
                                   const reticulum::ParsedPacket& packet)
{
    if (!raw_packet || raw_len == 0 || !packet.payload || !packet.destination_hash)
    {
        return false;
    }
    if (!hashesEqual(packet.destination_hash, identity_.destinationHash(), reticulum::kTruncatedHashSize))
    {
        return false;
    }
    if (packet.destination_type != reticulum::DestinationType::Single ||
        packet.payload_len <= (reticulum::kEncryptionPublicKeySize + reticulum::kTokenOverhead))
    {
        return false;
    }

    const uint8_t* peer_ephemeral_pub = packet.payload;
    const uint8_t* token = packet.payload + reticulum::kEncryptionPublicKeySize;
    const size_t token_len = packet.payload_len - reticulum::kEncryptionPublicKeySize;

    uint8_t shared_secret[LxmfIdentity::kEncPubKeySize] = {};
    if (!identity_.deriveSharedSecret(peer_ephemeral_pub, shared_secret))
    {
        return false;
    }

    uint8_t derived_key[reticulum::kDerivedTokenKeySize] = {};
    if (!reticulum::hkdfSha256(shared_secret, sizeof(shared_secret),
                               identity_.identityHash(), reticulum::kTruncatedHashSize,
                               nullptr, 0,
                               derived_key, sizeof(derived_key)))
    {
        return false;
    }

    uint8_t plaintext[kMaxTokenPlaintextLen] = {};
    size_t plaintext_len = sizeof(plaintext);
    if (!reticulum::tokenDecrypt(derived_key, token, token_len, plaintext, &plaintext_len))
    {
        return false;
    }

    DecodedMessage message{};
    if (!unpackMessage(plaintext, plaintext_len, &message))
    {
        return false;
    }
    if (!hashesEqual(message.destination_hash, identity_.destinationHash(), reticulum::kTruncatedHashSize))
    {
        return false;
    }

    const PeerInfo* peer = findPeerByDestinationHash(message.source_hash);
    if (!peer)
    {
        return false;
    }

    uint8_t signed_part[kSignedPartMaxLen] = {};
    size_t signed_part_len = sizeof(signed_part);
    uint8_t message_hash[reticulum::kFullHashSize] = {};
    if (!buildSignedPart(message.destination_hash,
                         message.source_hash,
                         message.packed_payload.data(),
                         message.packed_payload.size(),
                         signed_part,
                         &signed_part_len,
                         message_hash))
    {
        return false;
    }
    if (!LxmfIdentity::verify(peer->sig_pub, message.signature, signed_part, signed_part_len))
    {
        return false;
    }

    MeshIncomingText incoming;
    incoming.channel = ChannelId::PRIMARY;
    incoming.from = peer->node_id;
    incoming.to = identity_.nodeId();
    incoming.msg_id = messageIdFromHash(message_hash);
    incoming.timestamp = currentTimestampSeconds();
    incoming.text = message.content;
    incoming.hop_limit = 0xFF;
    incoming.encrypted = true;
    incoming.rx_meta.rx_timestamp_ms = millis();
    incoming.rx_meta.rx_timestamp_s = currentTimestampSeconds();
    incoming.rx_meta.time_source = is_valid_epoch(incoming.rx_meta.rx_timestamp_s)
                                       ? RxTimeSource::DeviceUtc
                                       : RxTimeSource::Uptime;
    incoming.rx_meta.origin = RxOrigin::Mesh;
    incoming.rx_meta.direct = true;
    incoming.rx_meta.from_is = false;
    incoming.rx_meta.rssi_dbm_x10 = static_cast<int16_t>(lround(raw_.lastRxRssi() * 10.0f));
    incoming.rx_meta.snr_db_x10 = static_cast<int16_t>(lround(raw_.lastRxSnr() * 10.0f));

    text_receive_queue_.push(std::move(incoming));
    (void)sendProofForPacket(raw_packet, raw_len);
    return true;
}

bool LxmfAdapter::handleProofPacket(const uint8_t* raw_packet, size_t raw_len,
                                    const reticulum::ParsedPacket& packet)
{
    (void)raw_len;
    if (!raw_packet || !packet.destination_hash || packet.packet_type != reticulum::PacketType::Proof)
    {
        return false;
    }

    if (packet.context == static_cast<uint8_t>(reticulum::PacketContext::LrProof) ||
        packet.destination_type == reticulum::DestinationType::Link)
    {
        return maybeForwardLinkPacket(raw_packet, raw_len, packet);
    }

    ReverseEntry* reverse = findReversePath(packet.destination_hash);
    if (!reverse)
    {
        return false;
    }

    uint8_t forward_packet[kMaxPacketLen] = {};
    size_t forward_len = sizeof(forward_packet);
    if (!reticulum::buildHeader1Packet(packet.packet_type,
                                       packet.destination_type,
                                       static_cast<reticulum::PacketContext>(packet.context),
                                       packet.context_flag != 0,
                                       packet.destination_hash,
                                       packet.payload,
                                       packet.payload_len,
                                       forward_packet,
                                       &forward_len,
                                       packet.hops,
                                       reticulum::TransportType::Broadcast))
    {
        return false;
    }

    reverse->created_ms = 0;
    return raw_.sendAppData(ChannelId::PRIMARY, 0, forward_packet, forward_len);
}

bool LxmfAdapter::handleLinkRequestPacket(const uint8_t* raw_packet, size_t raw_len,
                                          const reticulum::ParsedPacket& packet)
{
    if (!raw_packet || raw_len == 0 || !packet.destination_hash ||
        packet.packet_type != reticulum::PacketType::LinkRequest)
    {
        return false;
    }

    if (packet.transport_id &&
        !hashesEqual(packet.transport_id, identity_.identityHash(), reticulum::kTruncatedHashSize))
    {
        return false;
    }

    if (hashesEqual(packet.destination_hash, identity_.destinationHash(), reticulum::kTruncatedHashSize))
    {
        return false;
    }

    const PathEntry* path = findPath(packet.destination_hash);
    if (!path)
    {
        return false;
    }

    uint8_t link_id[reticulum::kTruncatedHashSize] = {};
    if (computeLinkIdFromLinkRequest(raw_packet, raw_len, packet, link_id))
    {
        LinkRelayEntry& relay = upsertLinkRelay(link_id);
        relay.initiator_hops = packet.hops;
        relay.responder_hops = path->hops;
        relay.last_seen_ms = millis();
    }

    if (path->hops <= 1 || path->direct)
    {
        uint8_t forward_packet[kMaxPacketLen] = {};
        size_t forward_len = sizeof(forward_packet);
        if (!reticulum::buildHeader1Packet(packet.packet_type,
                                           packet.destination_type,
                                           static_cast<reticulum::PacketContext>(packet.context),
                                           packet.context_flag != 0,
                                           packet.destination_hash,
                                           packet.payload,
                                           packet.payload_len,
                                           forward_packet,
                                           &forward_len,
                                           packet.hops,
                                           reticulum::TransportType::Broadcast))
        {
            return false;
        }

        return raw_.sendAppData(ChannelId::PRIMARY, 0, forward_packet, forward_len);
    }

    uint8_t forward_packet[kMaxPacketLen] = {};
    size_t forward_len = sizeof(forward_packet);
    if (!reticulum::buildHeader2Packet(packet.packet_type,
                                       packet.destination_type,
                                       static_cast<reticulum::PacketContext>(packet.context),
                                       packet.context_flag != 0,
                                       path->next_hop_transport,
                                       packet.destination_hash,
                                       packet.payload,
                                       packet.payload_len,
                                       forward_packet,
                                       &forward_len,
                                       packet.hops))
    {
        return false;
    }

    return raw_.sendAppData(ChannelId::PRIMARY, 0, forward_packet, forward_len);
}

bool LxmfAdapter::handlePathRequestPacket(const reticulum::ParsedPacket& packet)
{
    if (!packet.valid ||
        packet.packet_type != reticulum::PacketType::Data ||
        packet.destination_type != reticulum::DestinationType::Plain ||
        !packet.destination_hash ||
        !packet.payload ||
        packet.payload_len <= reticulum::kTruncatedHashSize)
    {
        return false;
    }

    uint8_t control_hash[reticulum::kTruncatedHashSize] = {};
    pathRequestDestinationHash(control_hash);
    if (!hashesEqual(packet.destination_hash, control_hash, sizeof(control_hash)))
    {
        return false;
    }

    const uint8_t* requested_hash = packet.payload;
    const uint8_t* tag = nullptr;
    size_t tag_len = 0;

    if (packet.payload_len > (reticulum::kTruncatedHashSize * 2))
    {
        tag = packet.payload + (reticulum::kTruncatedHashSize * 2);
        tag_len = packet.payload_len - (reticulum::kTruncatedHashSize * 2);
    }
    else
    {
        tag = packet.payload + reticulum::kTruncatedHashSize;
        tag_len = packet.payload_len - reticulum::kTruncatedHashSize;
    }

    if (tag_len > kPathRequestTagSize)
    {
        tag_len = kPathRequestTagSize;
    }
    if (tag_len == 0)
    {
        return true;
    }

    if (hashesEqual(requested_hash, identity_.destinationHash(), reticulum::kTruncatedHashSize))
    {
        (void)tag;
        return sendAnnounce(reticulum::PacketContext::PathResponse);
    }

    const PathEntry* path = findPath(requested_hash);
    if (!path || path->cached_announce_len == 0)
    {
        return true;
    }

    return sendCachedAnnounceResponse(*path, reticulum::PacketContext::PathResponse);
}

bool LxmfAdapter::handleCacheRequestPacket(const reticulum::ParsedPacket& packet)
{
    if (!packet.valid ||
        packet.packet_type != reticulum::PacketType::Data ||
        packet.context != static_cast<uint8_t>(reticulum::PacketContext::CacheRequest) ||
        !packet.payload ||
        packet.payload_len != reticulum::kFullHashSize)
    {
        return false;
    }

    return sendCachedPacketReplay(packet.payload);
}

bool LxmfAdapter::maybeForwardTransportPacket(const uint8_t* raw_packet, size_t raw_len,
                                              const reticulum::ParsedPacket& packet)
{
    if (!raw_packet || raw_len == 0 || !packet.destination_hash)
    {
        return false;
    }

    if (!packet.transport_id ||
        !hashesEqual(packet.transport_id, identity_.identityHash(), reticulum::kTruncatedHashSize))
    {
        return false;
    }

    if (hashesEqual(packet.destination_hash, identity_.destinationHash(), reticulum::kTruncatedHashSize))
    {
        return false;
    }

    const PathEntry* path = findPath(packet.destination_hash);
    if (!path)
    {
        return false;
    }

    if (packet.packet_type != reticulum::PacketType::Proof &&
        packet.packet_type != reticulum::PacketType::Announce)
    {
        uint8_t proof_hash[reticulum::kTruncatedHashSize] = {};
        reticulum::computeTruncatedPacketHash(raw_packet, raw_len, proof_hash);
        rememberReversePath(proof_hash);
    }

    if (path->hops <= 1 || path->direct)
    {
        uint8_t forward_packet[kMaxPacketLen] = {};
        size_t forward_len = sizeof(forward_packet);
        if (!reticulum::buildHeader1Packet(packet.packet_type,
                                           packet.destination_type,
                                           static_cast<reticulum::PacketContext>(packet.context),
                                           packet.context_flag != 0,
                                           packet.destination_hash,
                                           packet.payload,
                                           packet.payload_len,
                                           forward_packet,
                                           &forward_len,
                                           packet.hops,
                                           reticulum::TransportType::Broadcast))
        {
            return false;
        }

        return raw_.sendAppData(ChannelId::PRIMARY, 0, forward_packet, forward_len);
    }

    uint8_t forward_packet[kMaxPacketLen] = {};
    size_t forward_len = sizeof(forward_packet);
    if (!reticulum::buildHeader2Packet(packet.packet_type,
                                       packet.destination_type,
                                       static_cast<reticulum::PacketContext>(packet.context),
                                       packet.context_flag != 0,
                                       path->next_hop_transport,
                                       packet.destination_hash,
                                       packet.payload,
                                       packet.payload_len,
                                       forward_packet,
                                       &forward_len,
                                       packet.hops))
    {
        return false;
    }

    return raw_.sendAppData(ChannelId::PRIMARY, 0, forward_packet, forward_len);
}

bool LxmfAdapter::maybeForwardLinkPacket(const uint8_t* raw_packet, size_t raw_len,
                                         const reticulum::ParsedPacket& packet)
{
    (void)raw_packet;
    (void)raw_len;

    if (!packet.destination_hash)
    {
        return false;
    }

    if (packet.destination_type != reticulum::DestinationType::Link)
    {
        return false;
    }

    LinkRelayEntry* relay = findLinkRelay(packet.destination_hash);
    if (!relay)
    {
        return false;
    }

    const bool from_initiator = (packet.hops == relay->initiator_hops);
    const bool from_responder = (packet.hops == relay->responder_hops);
    if (!from_initiator && !from_responder)
    {
        return false;
    }

    uint8_t forward_packet[kMaxPacketLen] = {};
    size_t forward_len = sizeof(forward_packet);
    if (!reticulum::buildHeader1Packet(packet.packet_type,
                                       packet.destination_type,
                                       static_cast<reticulum::PacketContext>(packet.context),
                                       packet.context_flag != 0,
                                       packet.destination_hash,
                                       packet.payload,
                                       packet.payload_len,
                                       forward_packet,
                                       &forward_len,
                                       packet.hops,
                                       reticulum::TransportType::Broadcast))
    {
        return false;
    }

    relay->last_seen_ms = millis();
    return raw_.sendAppData(ChannelId::PRIMARY, 0, forward_packet, forward_len);
}

bool LxmfAdapter::sendProofForPacket(const uint8_t* raw_packet, size_t raw_len)
{
    if (!raw_packet || raw_len == 0 || !isReady())
    {
        return false;
    }

    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    reticulum::computePacketHash(raw_packet, raw_len, packet_hash);

    uint8_t signature[reticulum::kSignatureSize] = {};
    if (!identity_.sign(packet_hash, sizeof(packet_hash), signature))
    {
        return false;
    }

    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    memcpy(destination_hash, packet_hash, sizeof(destination_hash));

    uint8_t proof_packet[kMaxPacketLen] = {};
    size_t proof_len = sizeof(proof_packet);
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::Proof,
                                       reticulum::DestinationType::Single,
                                       reticulum::PacketContext::None,
                                       false,
                                       destination_hash,
                                       signature,
                                       sizeof(signature),
                                       proof_packet,
                                       &proof_len))
    {
        return false;
    }

    return routeAndSendPacket(proof_packet, proof_len, false);
}

bool LxmfAdapter::sendPathRequest(PeerInfo& peer)
{
    if (!isReady() || isZeroBytes(peer.destination_hash, sizeof(peer.destination_hash)))
    {
        return false;
    }

    const uint32_t now_ms = millis();
    if (peer.last_path_request_ms != 0 &&
        (now_ms - peer.last_path_request_ms) < kPathRequestMinIntervalMs)
    {
        return false;
    }

    uint8_t request_payload[reticulum::kTruncatedHashSize + kPathRequestTagSize] = {};
    memcpy(request_payload, peer.destination_hash, reticulum::kTruncatedHashSize);
    fillRandomBytes(request_payload + reticulum::kTruncatedHashSize, kPathRequestTagSize);

    uint8_t control_hash[reticulum::kTruncatedHashSize] = {};
    pathRequestDestinationHash(control_hash);

    uint8_t packet[kMaxPacketLen] = {};
    size_t packet_len = sizeof(packet);
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                       reticulum::DestinationType::Plain,
                                       reticulum::PacketContext::None,
                                       false,
                                       control_hash,
                                       request_payload,
                                       sizeof(request_payload),
                                       packet,
                                       &packet_len))
    {
        return false;
    }

    if (!routeAndSendPacket(packet, packet_len, false))
    {
        return false;
    }

    peer.last_path_request_ms = now_ms;
    return true;
}

bool LxmfAdapter::shouldRequestPath(const PeerInfo& peer) const
{
    const uint32_t now_ms = millis();
    if (peer.last_path_request_ms != 0 &&
        (now_ms - peer.last_path_request_ms) < kPathRequestMinIntervalMs)
    {
        return false;
    }

    if (peer.last_seen_s == 0)
    {
        return true;
    }

    const uint32_t now_s = currentTimestampSeconds();
    if (now_s < peer.last_seen_s)
    {
        return true;
    }

    return (now_s - peer.last_seen_s) >= kPathRefreshAgeS;
}

bool LxmfAdapter::buildEncryptedPacketForPeer(const PeerInfo& peer,
                                              const uint8_t* plaintext, size_t plaintext_len,
                                              uint8_t* out_packet, size_t* inout_len)
{
    if (!plaintext || plaintext_len == 0 || !out_packet || !inout_len)
    {
        return false;
    }

    uint8_t ephemeral_pub[LxmfIdentity::kEncPubKeySize] = {};
    uint8_t ephemeral_priv[LxmfIdentity::kEncPrivKeySize] = {};
    Curve25519::dh1(ephemeral_pub, ephemeral_priv);

    uint8_t shared_secret[LxmfIdentity::kEncPubKeySize] = {};
    memcpy(shared_secret, peer.enc_pub, sizeof(shared_secret));
    if (!Curve25519::dh2(shared_secret, ephemeral_priv))
    {
        return false;
    }

    uint8_t derived_key[reticulum::kDerivedTokenKeySize] = {};
    if (!reticulum::hkdfSha256(shared_secret, sizeof(shared_secret),
                               peer.identity_hash, sizeof(peer.identity_hash),
                               nullptr, 0,
                               derived_key, sizeof(derived_key)))
    {
        return false;
    }

    uint8_t iv[reticulum::kTokenIvSize] = {};
    fillRandomBytes(iv, sizeof(iv));

    uint8_t encrypted_token[kMaxPacketLen] = {};
    size_t encrypted_token_len = sizeof(encrypted_token);
    if (!reticulum::tokenEncrypt(derived_key, iv, plaintext, plaintext_len,
                                 encrypted_token, &encrypted_token_len))
    {
        return false;
    }

    uint8_t payload[kMaxPacketLen] = {};
    const size_t payload_len = sizeof(ephemeral_pub) + encrypted_token_len;
    if (payload_len > sizeof(payload))
    {
        return false;
    }
    memcpy(payload, ephemeral_pub, sizeof(ephemeral_pub));
    memcpy(payload + sizeof(ephemeral_pub), encrypted_token, encrypted_token_len);

    return reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                         reticulum::DestinationType::Single,
                                         reticulum::PacketContext::None,
                                         false,
                                         peer.destination_hash,
                                         payload,
                                         payload_len,
                                         out_packet,
                                         inout_len);
}

bool LxmfAdapter::routeAndSendPacket(const uint8_t* raw_packet, size_t raw_len,
                                     bool allow_transport)
{
    if (!raw_packet || raw_len == 0)
    {
        return false;
    }

    if (!allow_transport)
    {
        return raw_.sendAppData(ChannelId::PRIMARY, 0, raw_packet, raw_len);
    }

    reticulum::ParsedPacket parsed{};
    if (!reticulum::parsePacket(raw_packet, raw_len, &parsed) || !parsed.destination_hash)
    {
        return raw_.sendAppData(ChannelId::PRIMARY, 0, raw_packet, raw_len);
    }

    if (parsed.packet_type == reticulum::PacketType::Announce ||
        parsed.packet_type == reticulum::PacketType::Proof ||
        parsed.destination_type == reticulum::DestinationType::Plain ||
        parsed.destination_type == reticulum::DestinationType::Group)
    {
        return raw_.sendAppData(ChannelId::PRIMARY, 0, raw_packet, raw_len);
    }

    const PathEntry* path = findPath(parsed.destination_hash);
    if (!path || path->hops <= 1 || path->direct)
    {
        return raw_.sendAppData(ChannelId::PRIMARY, 0, raw_packet, raw_len);
    }

    uint8_t routed_packet[kMaxPacketLen] = {};
    size_t routed_len = sizeof(routed_packet);
    if (!reticulum::buildHeader2Packet(parsed.packet_type,
                                       parsed.destination_type,
                                       static_cast<reticulum::PacketContext>(parsed.context),
                                       parsed.context_flag != 0,
                                       path->next_hop_transport,
                                       parsed.destination_hash,
                                       parsed.payload,
                                       parsed.payload_len,
                                       routed_packet,
                                       &routed_len,
                                       raw_packet[1]))
    {
        return false;
    }

    return raw_.sendAppData(ChannelId::PRIMARY, 0, routed_packet, routed_len);
}

bool LxmfAdapter::sendCachedAnnounceResponse(const PathEntry& path,
                                             reticulum::PacketContext context)
{
    if (path.cached_announce_len == 0)
    {
        return false;
    }

    reticulum::ParsedPacket parsed{};
    if (!reticulum::parsePacket(path.cached_announce, path.cached_announce_len, &parsed) ||
        parsed.packet_type != reticulum::PacketType::Announce)
    {
        return false;
    }

    uint8_t packet[kMaxPacketLen] = {};
    size_t packet_len = sizeof(packet);
    if (!reticulum::buildHeader2Packet(reticulum::PacketType::Announce,
                                       reticulum::DestinationType::Single,
                                       context,
                                       parsed.context_flag != 0,
                                       identity_.identityHash(),
                                       parsed.destination_hash,
                                       parsed.payload,
                                       parsed.payload_len,
                                       packet,
                                       &packet_len,
                                       path.hops))
    {
        return false;
    }

    return raw_.sendAppData(ChannelId::PRIMARY, 0, packet, packet_len);
}

bool LxmfAdapter::sendCachedPacketReplay(const uint8_t packet_hash[reticulum::kFullHashSize])
{
    if (!packet_hash)
    {
        return false;
    }

    for (const auto& path : paths_)
    {
        if (path.cached_announce_len == 0)
        {
            continue;
        }
        if (hashesEqual(path.cached_packet_hash, packet_hash, reticulum::kFullHashSize))
        {
            return raw_.sendAppData(ChannelId::PRIMARY, 0, path.cached_announce, path.cached_announce_len);
        }
    }

    return false;
}

bool LxmfAdapter::shouldRebroadcastAnnounce(const reticulum::ParsedPacket& packet) const
{
    if (!packet.destination_hash)
    {
        return false;
    }
    if (packet.context == static_cast<uint8_t>(reticulum::PacketContext::PathResponse))
    {
        return false;
    }
    if (packet.hops >= kMaxTransportHops)
    {
        return false;
    }
    if (hashesEqual(packet.destination_hash, identity_.destinationHash(), reticulum::kTruncatedHashSize))
    {
        return false;
    }
    return true;
}

bool LxmfAdapter::rebroadcastAnnounce(const PathEntry& path, const reticulum::ParsedPacket& packet)
{
    if (!packet.destination_hash || path.cached_announce_len == 0)
    {
        return false;
    }

    uint8_t rebroadcast[kMaxPacketLen] = {};
    size_t rebroadcast_len = sizeof(rebroadcast);
    if (!reticulum::buildHeader2Packet(reticulum::PacketType::Announce,
                                       reticulum::DestinationType::Single,
                                       reticulum::PacketContext::None,
                                       packet.context_flag != 0,
                                       identity_.identityHash(),
                                       packet.destination_hash,
                                       packet.payload,
                                       packet.payload_len,
                                       rebroadcast,
                                       &rebroadcast_len,
                                       packet.hops))
    {
        return false;
    }

    return raw_.sendAppData(ChannelId::PRIMARY, 0, rebroadcast, rebroadcast_len);
}

bool LxmfAdapter::isDuplicatePacket(const uint8_t packet_hash[reticulum::kFullHashSize])
{
    if (!packet_hash)
    {
        return false;
    }
    for (const auto& entry : packet_filter_)
    {
        if (entry.seen_ms != 0 &&
            hashesEqual(entry.packet_hash, packet_hash, reticulum::kFullHashSize))
        {
            return true;
        }
    }
    return false;
}

void LxmfAdapter::rememberPacket(const uint8_t packet_hash[reticulum::kFullHashSize])
{
    if (!packet_hash)
    {
        return;
    }

    if (packet_filter_.size() >= kMaxPacketFilter)
    {
        packet_filter_.erase(packet_filter_.begin());
    }

    PacketFilterEntry entry{};
    copyHash(entry.packet_hash, packet_hash, sizeof(entry.packet_hash));
    entry.seen_ms = millis();
    packet_filter_.push_back(entry);
}

void LxmfAdapter::rememberReversePath(const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    if (!proof_hash)
    {
        return;
    }

    for (auto& entry : reverse_table_)
    {
        if (hashesEqual(entry.proof_hash, proof_hash, sizeof(entry.proof_hash)))
        {
            entry.created_ms = millis();
            return;
        }
    }

    if (reverse_table_.size() >= kMaxReverseEntries)
    {
        reverse_table_.erase(reverse_table_.begin());
    }

    ReverseEntry entry{};
    copyHash(entry.proof_hash, proof_hash, sizeof(entry.proof_hash));
    entry.created_ms = millis();
    reverse_table_.push_back(entry);
}

LxmfAdapter::ReverseEntry* LxmfAdapter::findReversePath(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    if (!proof_hash)
    {
        return nullptr;
    }
    for (auto& entry : reverse_table_)
    {
        if (entry.created_ms != 0 &&
            hashesEqual(entry.proof_hash, proof_hash, sizeof(entry.proof_hash)))
        {
            return &entry;
        }
    }
    return nullptr;
}

void LxmfAdapter::cullTransportState()
{
    const uint32_t now_ms = millis();

    packet_filter_.erase(
        std::remove_if(packet_filter_.begin(), packet_filter_.end(),
                       [now_ms](const PacketFilterEntry& entry)
                       {
                           return entry.seen_ms == 0 || (now_ms - entry.seen_ms) > kPacketFilterTtlMs;
                       }),
        packet_filter_.end());

    reverse_table_.erase(
        std::remove_if(reverse_table_.begin(), reverse_table_.end(),
                       [now_ms](const ReverseEntry& entry)
                       {
                           return entry.created_ms == 0 || (now_ms - entry.created_ms) > kReverseEntryTtlMs;
                       }),
        reverse_table_.end());

    link_relays_.erase(
        std::remove_if(link_relays_.begin(), link_relays_.end(),
                       [now_ms](const LinkRelayEntry& entry)
                       {
                           return entry.last_seen_ms == 0 || (now_ms - entry.last_seen_ms) > kLinkRelayTtlMs;
                       }),
        link_relays_.end());
}

LxmfAdapter::PathEntry& LxmfAdapter::upsertPath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    for (auto& path : paths_)
    {
        if (hashesEqual(path.destination_hash, destination_hash, reticulum::kTruncatedHashSize))
        {
            return path;
        }
    }

    if (paths_.size() >= kMaxPaths)
    {
        paths_.erase(paths_.begin());
    }

    paths_.push_back(PathEntry{});
    PathEntry& path = paths_.back();
    copyHash(path.destination_hash, destination_hash, sizeof(path.destination_hash));
    return path;
}

const LxmfAdapter::PathEntry* LxmfAdapter::findPath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const
{
    if (!destination_hash)
    {
        return nullptr;
    }
    for (const auto& path : paths_)
    {
        if (hashesEqual(path.destination_hash, destination_hash, sizeof(path.destination_hash)))
        {
            return &path;
        }
    }
    return nullptr;
}

LxmfAdapter::LinkRelayEntry& LxmfAdapter::upsertLinkRelay(
    const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    for (auto& relay : link_relays_)
    {
        if (hashesEqual(relay.link_id, link_id, sizeof(relay.link_id)))
        {
            return relay;
        }
    }

    if (link_relays_.size() >= kMaxLinkRelays)
    {
        link_relays_.erase(link_relays_.begin());
    }

    link_relays_.push_back(LinkRelayEntry{});
    LinkRelayEntry& relay = link_relays_.back();
    copyHash(relay.link_id, link_id, sizeof(relay.link_id));
    return relay;
}

LxmfAdapter::LinkRelayEntry* LxmfAdapter::findLinkRelay(
    const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    if (!link_id)
    {
        return nullptr;
    }
    for (auto& relay : link_relays_)
    {
        if (hashesEqual(relay.link_id, link_id, sizeof(relay.link_id)))
        {
            return &relay;
        }
    }
    return nullptr;
}

LxmfAdapter::PeerInfo* LxmfAdapter::findPeerByNodeId(NodeId node_id)
{
    for (auto& peer : peers_)
    {
        if (peer.node_id == node_id)
        {
            return &peer;
        }
    }
    return nullptr;
}

const LxmfAdapter::PeerInfo* LxmfAdapter::findPeerByDestinationHash(
    const uint8_t hash[reticulum::kTruncatedHashSize]) const
{
    if (!hash)
    {
        return nullptr;
    }
    for (const auto& peer : peers_)
    {
        if (hashesEqual(peer.destination_hash, hash, reticulum::kTruncatedHashSize))
        {
            return &peer;
        }
    }
    return nullptr;
}

LxmfAdapter::PeerInfo& LxmfAdapter::upsertPeer(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    for (auto& peer : peers_)
    {
        if (hashesEqual(peer.destination_hash, destination_hash, reticulum::kTruncatedHashSize))
        {
            return peer;
        }
    }

    peers_.push_back(PeerInfo{});
    PeerInfo& peer = peers_.back();
    copyHash(peer.destination_hash, destination_hash, reticulum::kTruncatedHashSize);
    peer.node_id = reticulum::nodeIdFromDestinationHash(destination_hash);
    return peer;
}

void LxmfAdapter::publishPeerUpdate(const PeerInfo& peer) const
{
    char short_name[10] = {};
    snprintf(short_name, sizeof(short_name), "%04lX",
             static_cast<unsigned long>(peer.node_id & 0xFFFFUL));

    sys::EventBus::publish(new sys::NodeProtocolUpdateEvent(
        peer.node_id,
        peer.last_seen_s,
        static_cast<uint8_t>(chat::contacts::NodeProtocolType::LXMF)), 0);

    sys::EventBus::publish(new sys::NodeInfoUpdateEvent(
        peer.node_id,
        short_name,
        peer.display_name[0] != '\0' ? peer.display_name : short_name,
        raw_.lastRxSnr(),
        raw_.lastRxRssi(),
        peer.last_seen_s,
        static_cast<uint8_t>(chat::contacts::NodeProtocolType::LXMF),
        static_cast<uint8_t>(chat::contacts::NodeRoleType::Client),
        0,
        0,
        0xFF), 0);
}

void LxmfAdapter::loadPersistedPeers()
{
    peers_loaded_ = true;

    std::vector<uint8_t> blob;
    chat::infra::PreferencesBlobMetadata meta;
    if (!chat::infra::loadRawBlobFromPreferencesWithMetadata(kPeersPrefsNs,
                                                             kPeersPrefsKey,
                                                             kPeersPrefsVer,
                                                             kPeersPrefsCrc,
                                                             blob,
                                                             &meta))
    {
        return;
    }

    if (meta.len == 0)
    {
        if (meta.has_version || meta.has_crc)
        {
            chat::infra::clearPreferencesKeys(kPeersPrefsNs,
                                              kPeersPrefsVer,
                                              kPeersPrefsCrc);
        }
        return;
    }

    const bool valid_blob =
        (meta.len == blob.size()) &&
        (meta.len % sizeof(PersistedPeerRecord) == 0) &&
        meta.has_version &&
        (meta.version == kPeersPrefsVersion) &&
        meta.has_crc &&
        (meta.crc == fnv1a32(blob.data(), blob.size()));

    if (!valid_blob)
    {
        chat::infra::clearPreferencesKeys(kPeersPrefsNs,
                                          kPeersPrefsKey,
                                          kPeersPrefsVer,
                                          kPeersPrefsCrc);
        return;
    }

    const size_t record_count = std::min(blob.size() / sizeof(PersistedPeerRecord), kMaxPersistedPeers);
    for (size_t i = 0; i < record_count; ++i)
    {
        PersistedPeerRecord record{};
        memcpy(&record, blob.data() + (i * sizeof(PersistedPeerRecord)), sizeof(record));

        if (isZeroBytes(record.destination_hash, sizeof(record.destination_hash)) ||
            isZeroBytes(record.identity_hash, sizeof(record.identity_hash)) ||
            isZeroBytes(record.enc_pub, sizeof(record.enc_pub)) ||
            isZeroBytes(record.sig_pub, sizeof(record.sig_pub)))
        {
            continue;
        }

        PeerInfo& peer = upsertPeer(record.destination_hash);
        copyHash(peer.identity_hash, record.identity_hash, sizeof(peer.identity_hash));
        memcpy(peer.enc_pub, record.enc_pub, sizeof(peer.enc_pub));
        memcpy(peer.sig_pub, record.sig_pub, sizeof(peer.sig_pub));
        peer.last_seen_s = record.last_seen_s;
        peer.last_path_request_ms = 0;
        copyCString(peer.display_name, sizeof(peer.display_name), record.display_name);

        if (peer.display_name[0] == '\0')
        {
            snprintf(peer.display_name, sizeof(peer.display_name),
                     "%08lX", static_cast<unsigned long>(peer.node_id));
        }

        publishPeerUpdate(peer);
    }
}

bool LxmfAdapter::persistPeers() const
{
    std::vector<const PeerInfo*> ordered;
    ordered.reserve(peers_.size());
    for (const auto& peer : peers_)
    {
        if (isZeroBytes(peer.destination_hash, sizeof(peer.destination_hash)) ||
            isZeroBytes(peer.identity_hash, sizeof(peer.identity_hash)) ||
            isZeroBytes(peer.enc_pub, sizeof(peer.enc_pub)) ||
            isZeroBytes(peer.sig_pub, sizeof(peer.sig_pub)))
        {
            continue;
        }

        ordered.push_back(&peer);
    }

    std::sort(ordered.begin(), ordered.end(),
              [](const PeerInfo* a, const PeerInfo* b)
              {
                  if (a->last_seen_s != b->last_seen_s)
                  {
                      return a->last_seen_s > b->last_seen_s;
                  }
                  return a->node_id < b->node_id;
              });

    if (ordered.size() > kMaxPersistedPeers)
    {
        ordered.resize(kMaxPersistedPeers);
    }

    if (ordered.empty())
    {
        return chat::infra::saveRawBlobToPreferencesWithMetadata(kPeersPrefsNs,
                                                                 kPeersPrefsKey,
                                                                 kPeersPrefsVer,
                                                                 kPeersPrefsCrc,
                                                                 nullptr,
                                                                 0,
                                                                 nullptr,
                                                                 true);
    }

    std::vector<uint8_t> blob(ordered.size() * sizeof(PersistedPeerRecord));
    for (size_t i = 0; i < ordered.size(); ++i)
    {
        PersistedPeerRecord record{};
        copyHash(record.destination_hash,
                 ordered[i]->destination_hash,
                 sizeof(record.destination_hash));
        copyHash(record.identity_hash,
                 ordered[i]->identity_hash,
                 sizeof(record.identity_hash));
        memcpy(record.enc_pub, ordered[i]->enc_pub, sizeof(record.enc_pub));
        memcpy(record.sig_pub, ordered[i]->sig_pub, sizeof(record.sig_pub));
        record.last_seen_s = ordered[i]->last_seen_s;
        copyCString(record.display_name, sizeof(record.display_name), ordered[i]->display_name);
        memcpy(blob.data() + (i * sizeof(PersistedPeerRecord)), &record, sizeof(record));
    }

    chat::infra::PreferencesBlobMetadata meta;
    meta.len = blob.size();
    meta.has_version = true;
    meta.version = kPeersPrefsVersion;
    meta.has_crc = true;
    meta.crc = fnv1a32(blob.data(), blob.size());

    return chat::infra::saveRawBlobToPreferencesWithMetadata(kPeersPrefsNs,
                                                             kPeersPrefsKey,
                                                             kPeersPrefsVer,
                                                             kPeersPrefsCrc,
                                                             blob.data(),
                                                             blob.size(),
                                                             &meta,
                                                             true);
}

uint32_t LxmfAdapter::currentTimestampSeconds() const
{
    const uint32_t epoch_s = now_epoch_seconds();
    if (is_valid_epoch(epoch_s))
    {
        return epoch_s;
    }
    return millis() / 1000U;
}

const char* LxmfAdapter::effectiveDisplayName() const
{
    if (!user_long_name_.empty())
    {
        return user_long_name_.c_str();
    }
    if (!user_short_name_.empty())
    {
        return user_short_name_.c_str();
    }
    return nullptr;
}

uint32_t LxmfAdapter::messageIdFromHash(const uint8_t hash[reticulum::kFullHashSize])
{
    return (static_cast<uint32_t>(hash[28]) << 24) |
           (static_cast<uint32_t>(hash[29]) << 16) |
           (static_cast<uint32_t>(hash[30]) << 8) |
           static_cast<uint32_t>(hash[31]);
}

void LxmfAdapter::pathRequestDestinationHash(uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    if (!out_hash)
    {
        return;
    }

    uint8_t name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("rnstransport", "path.request", name_hash);
    reticulum::computePlainDestinationHash(name_hash, out_hash);
}

} // namespace chat::lxmf
