/**
 * @file lxmf_adapter.cpp
 * @brief Device-side LXMF adapter over the existing RNode raw carrier
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_adapter.h"

#include "../../internal/blob_store_io.h"
#include "chat/domain/contact_types.h"
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
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
constexpr size_t kMaxLinkSessions = 12;
constexpr uint32_t kLinkSessionTtlMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kLinkHandshakeTimeoutMs = 30000;
constexpr uint32_t kLinkIdleTimeoutMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kLinkRequestTtlMs = 60000;
constexpr uint32_t kResourceTransferTtlMs = 60000;
constexpr uint32_t kResourceWindowSize = 4;
constexpr size_t kMaxPropagationEntries = 64;
constexpr size_t kMaxPropagationTransients = 192;
constexpr size_t kMaxPropagationPeers = 32;
constexpr uint32_t kPropagationEntryTtlS = 3UL * 24UL * 60UL * 60UL;
constexpr uint32_t kPropagationTransientTtlS = 3UL * 24UL * 60UL * 60UL;
constexpr uint32_t kPropagationTransferLimitKb = 64;
constexpr uint32_t kPropagationSyncLimitKb = 64;
constexpr const char* kPropagationOfferPath = "/offer";
constexpr const char* kPropagationGetPath = "/get";
constexpr uint32_t kPropagationErrorNoIdentity = 0xF0;
constexpr uint32_t kPropagationErrorNoAccess = 0xF1;
constexpr uint32_t kPropagationErrorInvalidKey = 0xF3;
constexpr uint32_t kPropagationErrorInvalidData = 0xF4;
constexpr uint8_t kLinkModeAes256Cbc = 0x01;
constexpr size_t kLinkRequestBaseLen = 64;
constexpr size_t kLinkSignallingLen = 3;
constexpr size_t kResourceMapHashLen = 4;
constexpr size_t kResourceDataPrefixLen = 4;
constexpr size_t kResourceAdvertisementOverhead = 134;
constexpr uint8_t kResourceFlagEncrypted = 1U << 0U;
constexpr uint8_t kResourceFlagCompressed = 1U << 1U;
constexpr uint8_t kResourceFlagSplit = 1U << 2U;
constexpr uint8_t kResourceFlagRequest = 1U << 3U;
constexpr uint8_t kResourceFlagResponse = 1U << 4U;
constexpr uint8_t kResourceFlagHasMetadata = 1U << 5U;
constexpr uint32_t kPacketFilterTtlMs = 30000;
constexpr uint32_t kReverseEntryTtlMs = 60000;
constexpr uint32_t kLinkRelayTtlMs = 300000;
constexpr uint8_t kMaxTransportHops = 128;
constexpr const char* kPeersPrefsNs = "lxmf_peers";
constexpr const char* kPeersPrefsKey = "peers";
constexpr const char* kPeersPrefsVer = "ver";
constexpr const char* kPeersPrefsCrc = "crc";
constexpr uint8_t kPeersPrefsVersion = 1;
constexpr uint8_t kPropagationMetaName = 0x01;

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

bool appendMsgpackByte(uint8_t value, uint8_t* out, size_t out_len, size_t& used)
{
    if (!out || used >= out_len)
    {
        return false;
    }

    out[used++] = value;
    return true;
}

bool appendMsgpackBytes(const uint8_t* data, size_t len, uint8_t* out, size_t out_len, size_t& used)
{
    if ((!data && len != 0) || !out || (used + len) > out_len)
    {
        return false;
    }

    if (len != 0)
    {
        memcpy(out + used, data, len);
    }
    used += len;
    return true;
}

bool appendMsgpackArrayHeader(uint8_t count, uint8_t* out, size_t out_len, size_t& used)
{
    if (count <= 0x0FU)
    {
        return appendMsgpackByte(static_cast<uint8_t>(0x90U | count), out, out_len, used);
    }

    return appendMsgpackByte(0xDC, out, out_len, used) &&
           appendMsgpackByte(0x00, out, out_len, used) &&
           appendMsgpackByte(count, out, out_len, used);
}

bool appendMsgpackMapHeader(uint8_t count, uint8_t* out, size_t out_len, size_t& used)
{
    if (count <= 0x0FU)
    {
        return appendMsgpackByte(static_cast<uint8_t>(0x80U | count), out, out_len, used);
    }

    return appendMsgpackByte(0xDE, out, out_len, used) &&
           appendMsgpackByte(0x00, out, out_len, used) &&
           appendMsgpackByte(count, out, out_len, used);
}

bool appendMsgpackBool(bool value, uint8_t* out, size_t out_len, size_t& used)
{
    return appendMsgpackByte(value ? 0xC3 : 0xC2, out, out_len, used);
}

bool appendMsgpackUint(uint32_t value, uint8_t* out, size_t out_len, size_t& used)
{
    if (value <= 0x7FU)
    {
        return appendMsgpackByte(static_cast<uint8_t>(value), out, out_len, used);
    }
    if (value <= 0xFFU)
    {
        return appendMsgpackByte(0xCC, out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>(value), out, out_len, used);
    }
    if (value <= 0xFFFFU)
    {
        return appendMsgpackByte(0xCD, out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>((value >> 8) & 0xFFU), out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>(value & 0xFFU), out, out_len, used);
    }

    return appendMsgpackByte(0xCE, out, out_len, used) &&
           appendMsgpackByte(static_cast<uint8_t>((value >> 24) & 0xFFU), out, out_len, used) &&
           appendMsgpackByte(static_cast<uint8_t>((value >> 16) & 0xFFU), out, out_len, used) &&
           appendMsgpackByte(static_cast<uint8_t>((value >> 8) & 0xFFU), out, out_len, used) &&
           appendMsgpackByte(static_cast<uint8_t>(value & 0xFFU), out, out_len, used);
}

bool appendMsgpackBin(const uint8_t* data, size_t len, uint8_t* out, size_t out_len, size_t& used)
{
    if (len <= 0xFFU)
    {
        return appendMsgpackByte(0xC4, out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>(len), out, out_len, used) &&
               appendMsgpackBytes(data, len, out, out_len, used);
    }
    if (len <= 0xFFFFU)
    {
        return appendMsgpackByte(0xC5, out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>((len >> 8) & 0xFFU), out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>(len & 0xFFU), out, out_len, used) &&
               appendMsgpackBytes(data, len, out, out_len, used);
    }

    return false;
}

bool packPropagationAnnounceAppData(uint32_t timebase_s,
                                    bool node_state,
                                    uint32_t per_transfer_limit_kb,
                                    uint32_t per_sync_limit_kb,
                                    const char* display_name,
                                    uint8_t* out_data,
                                    size_t* inout_len)
{
    if (!out_data || !inout_len)
    {
        return false;
    }

    const uint8_t* name_bytes = reinterpret_cast<const uint8_t*>(display_name ? display_name : "");
    const size_t name_len = (display_name && display_name[0] != '\0') ? strlen(display_name) : 0;

    size_t used = 0;
    const uint8_t metadata_entries = (name_len != 0) ? 1 : 0;
    if (!appendMsgpackArrayHeader(7, out_data, *inout_len, used) ||
        !appendMsgpackBool(false, out_data, *inout_len, used) ||
        !appendMsgpackUint(timebase_s, out_data, *inout_len, used) ||
        !appendMsgpackBool(node_state, out_data, *inout_len, used) ||
        !appendMsgpackUint(per_transfer_limit_kb, out_data, *inout_len, used) ||
        !appendMsgpackUint(per_sync_limit_kb, out_data, *inout_len, used) ||
        !appendMsgpackArrayHeader(3, out_data, *inout_len, used) ||
        !appendMsgpackUint(0, out_data, *inout_len, used) ||
        !appendMsgpackUint(0, out_data, *inout_len, used) ||
        !appendMsgpackUint(0, out_data, *inout_len, used) ||
        !appendMsgpackMapHeader(metadata_entries, out_data, *inout_len, used))
    {
        return false;
    }

    if (metadata_entries != 0 &&
        (!appendMsgpackUint(kPropagationMetaName, out_data, *inout_len, used) ||
         !appendMsgpackBin(name_bytes, name_len, out_data, *inout_len, used)))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

void destinationHashForAspect(const uint8_t identity_hash[reticulum::kTruncatedHashSize],
                              const char* aspect,
                              uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    if (!identity_hash || !aspect || !out_hash)
    {
        return;
    }

    uint8_t name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", aspect, name_hash);
    reticulum::computeDestinationHash(name_hash, identity_hash, out_hash);
}

void propagationPathHash(const char* path,
                         uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    if (!path || !out_hash)
    {
        return;
    }

    reticulum::truncatedHash(reinterpret_cast<const uint8_t*>(path),
                             strlen(path),
                             out_hash);
}

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

bool isLxmfPropagationAnnounce(const reticulum::ParsedAnnounce& announce)
{
    if (!announce.valid || !announce.name_hash)
    {
        return false;
    }

    uint8_t expected_name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", "propagation", expected_name_hash);
    return hashesEqual(expected_name_hash, announce.name_hash, sizeof(expected_name_hash));
}

bool packetContextUsesRawLinkPayload(uint8_t context)
{
    return context == static_cast<uint8_t>(reticulum::PacketContext::Keepalive) ||
           context == static_cast<uint8_t>(reticulum::PacketContext::Resource);
}

size_t resourceHashmapSegmentCapacity(uint16_t mdu)
{
    if (mdu <= kResourceAdvertisementOverhead)
    {
        return 1;
    }

    const size_t available = static_cast<size_t>(mdu) - kResourceAdvertisementOverhead;
    return std::max<size_t>(1, available / kResourceMapHashLen);
}

void buildLinkSignallingBytes(uint16_t mtu, uint8_t out_bytes[kLinkSignallingLen])
{
    if (!out_bytes)
    {
        return;
    }

    const uint32_t signalling_value =
        (static_cast<uint32_t>(mtu) & 0x1FFFFFU) |
        ((static_cast<uint32_t>((kLinkModeAes256Cbc << 5) & 0xE0U)) << 16);
    out_bytes[0] = static_cast<uint8_t>((signalling_value >> 16) & 0xFFU);
    out_bytes[1] = static_cast<uint8_t>((signalling_value >> 8) & 0xFFU);
    out_bytes[2] = static_cast<uint8_t>(signalling_value & 0xFFU);
}

uint16_t mtuFromLinkSignalling(const uint8_t* signalling_bytes, size_t len)
{
    if (!signalling_bytes || len < kLinkSignallingLen)
    {
        return reticulum::kReticulumMtu;
    }
    const uint32_t mtu =
        ((static_cast<uint32_t>(signalling_bytes[0]) << 16) |
         (static_cast<uint32_t>(signalling_bytes[1]) << 8) |
         static_cast<uint32_t>(signalling_bytes[2])) & 0x1FFFFFU;
    return static_cast<uint16_t>(std::min<uint32_t>(mtu, reticulum::kReticulumMtu));
}

uint8_t linkModeFromSignalling(const uint8_t* signalling_bytes, size_t len)
{
    if (!signalling_bytes || len < kLinkSignallingLen)
    {
        return kLinkModeAes256Cbc;
    }
    return static_cast<uint8_t>((signalling_bytes[0] & 0xE0U) >> 5);
}

bool packFloat64(double value, uint8_t* out_payload, size_t* inout_len)
{
    if (!out_payload || !inout_len || *inout_len < 9)
    {
        if (inout_len)
        {
            *inout_len = 9;
        }
        return false;
    }

    union
    {
        double d;
        uint8_t b[8];
    } bits{};
    bits.d = value;

    out_payload[0] = 0xCB;
    for (int i = 7; i >= 0; --i)
    {
        out_payload[8 - i] = bits.b[i];
    }
    *inout_len = 9;
    return true;
}

bool unpackFloat64(const uint8_t* data, size_t len, double* out_value)
{
    if (!data || len != 9 || data[0] != 0xCB || !out_value)
    {
        return false;
    }

    union
    {
        double d;
        uint8_t b[8];
    } bits{};
    for (int i = 7; i >= 0; --i)
    {
        bits.b[i] = data[8 - i];
    }
    *out_value = bits.d;
    return true;
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
    uint8_t seed[sizeof(next_app_packet_id_)] = {};
    fillRandomBytes(seed, sizeof(seed));
    memcpy(&next_app_packet_id_, seed, sizeof(next_app_packet_id_));
    if (next_app_packet_id_ == 0)
    {
        next_app_packet_id_ = 1;
    }
}

MeshCapabilities LxmfAdapter::getCapabilities() const
{
    MeshCapabilities caps;
    caps.supports_unicast_text = true;
    caps.supports_unicast_appdata = true;
    caps.provides_appdata_sender = true;
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

    uint8_t message_hash[reticulum::kFullHashSize] = {};
    LinkSession* active_link =
        findActiveLinkSessionByDestination(peer_info->destination_hash, LocalDestinationKind::Delivery);
    bool ok = false;
    if (active_link)
    {
        uint8_t signed_part[kSignedPartMaxLen] = {};
        size_t signed_part_len = sizeof(signed_part);
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
        uint8_t lxmf_message[kMaxLxmfMessageLen] = {};
        size_t lxmf_message_len = sizeof(lxmf_message);
        if (!identity_.sign(signed_part, signed_part_len, signature) ||
            !packMessage(peer_info->destination_hash,
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
        if (lxmf_message_len <= active_link->mdu)
        {
            ok = sendLinkPacket(*active_link,
                                reticulum::PacketType::Data,
                                reticulum::PacketContext::None,
                                lxmf_message,
                                lxmf_message_len,
                                true);
        }
        else
        {
            ok = queueOutgoingResource(*active_link,
                                       lxmf_message,
                                       lxmf_message_len,
                                       0,
                                       nullptr,
                                       0);
        }
    }
    else
    {
        uint8_t packet[kMaxPacketLen] = {};
        size_t packet_len = sizeof(packet);
        if (!buildSignedMessagePacket(*peer_info,
                                      packed_payload,
                                      packed_payload_len,
                                      packet,
                                      &packet_len,
                                      message_hash))
        {
            if (out_msg_id)
            {
                *out_msg_id = 0;
            }
            return false;
        }
        ok = routeAndSendPacket(packet, packet_len, true);
    }

    const MessageId message_id = messageIdFromHash(message_hash);
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
    (void)want_ack;

    processRadioPackets();

    if (channel != ChannelId::PRIMARY || portnum == 0 || (!payload && len != 0) || !isReady())
    {
        if (packet_id != 0)
        {
            sys::EventBus::publish(new sys::ChatSendResultEvent(packet_id, false), 0);
        }
        return false;
    }

    MessageId effective_packet_id = packet_id;
    if (effective_packet_id == 0)
    {
        effective_packet_id = next_app_packet_id_++;
        if (next_app_packet_id_ == 0)
        {
            next_app_packet_id_ = 1;
        }
    }
    else if (effective_packet_id >= next_app_packet_id_)
    {
        next_app_packet_id_ = effective_packet_id + 1;
        if (next_app_packet_id_ == 0)
        {
            next_app_packet_id_ = 1;
        }
    }

    uint8_t packed_payload[kMaxLxmfMessageLen] = {};
    size_t packed_payload_len = sizeof(packed_payload);
    if (!encodeAppDataPayload(portnum,
                              effective_packet_id,
                              0,
                              want_response,
                              payload,
                              len,
                              packed_payload,
                              &packed_payload_len))
    {
        sys::EventBus::publish(new sys::ChatSendResultEvent(effective_packet_id, false), 0);
        return false;
    }

    bool ok = false;
    if (dest != 0)
    {
        PeerInfo* peer_info = findPeerByNodeId(dest);
        if (!peer_info)
        {
            sys::EventBus::publish(new sys::ChatSendResultEvent(effective_packet_id, false), 0);
            return false;
        }

        if (shouldRequestPath(*peer_info))
        {
            (void)sendPathRequest(*peer_info);
        }

        uint8_t message_hash[reticulum::kFullHashSize] = {};
        LinkSession* active_link =
            findActiveLinkSessionByDestination(peer_info->destination_hash, LocalDestinationKind::Delivery);
        if (active_link)
        {
            uint8_t signed_part[kSignedPartMaxLen] = {};
            size_t signed_part_len = sizeof(signed_part);
            uint8_t signature[reticulum::kSignatureSize] = {};
            uint8_t lxmf_message[kMaxLxmfMessageLen] = {};
            size_t lxmf_message_len = sizeof(lxmf_message);
            if (buildSignedPart(peer_info->destination_hash,
                                identity_.destinationHash(),
                                packed_payload,
                                packed_payload_len,
                                signed_part,
                                &signed_part_len,
                                message_hash) &&
                identity_.sign(signed_part, signed_part_len, signature) &&
                packMessage(peer_info->destination_hash,
                            identity_.destinationHash(),
                            signature,
                            packed_payload,
                            packed_payload_len,
                            lxmf_message,
                            &lxmf_message_len))
            {
                if (lxmf_message_len <= active_link->mdu)
                {
                    ok = sendLinkPacket(*active_link,
                                        reticulum::PacketType::Data,
                                        reticulum::PacketContext::None,
                                        lxmf_message,
                                        lxmf_message_len,
                                        true);
                }
                else
                {
                    ok = queueOutgoingResource(*active_link,
                                               lxmf_message,
                                               lxmf_message_len,
                                               0,
                                               nullptr,
                                               0);
                }
            }
        }
        else
        {
            uint8_t packet[kMaxPacketLen] = {};
            size_t packet_len = sizeof(packet);
            if (buildSignedMessagePacket(*peer_info,
                                         packed_payload,
                                         packed_payload_len,
                                         packet,
                                         &packet_len,
                                         message_hash))
            {
                ok = routeAndSendPacket(packet, packet_len, true);
            }
        }
    }
    else
    {
        bool have_peer = false;
        ok = true;
        // LXMF delivery is single-destination. For device-side team/business
        // traffic we currently treat dest==0 as fan-out to every known peer.
        for (auto& peer_info : peers_)
        {
            if (isZeroBytes(peer_info.destination_hash, sizeof(peer_info.destination_hash)))
            {
                continue;
            }

            have_peer = true;
            if (shouldRequestPath(peer_info))
            {
                (void)sendPathRequest(peer_info);
            }

            uint8_t packet[kMaxPacketLen] = {};
            size_t packet_len = sizeof(packet);
            uint8_t message_hash[reticulum::kFullHashSize] = {};
            if (!buildSignedMessagePacket(peer_info,
                                          packed_payload,
                                          packed_payload_len,
                                          packet,
                                          &packet_len,
                                          message_hash) ||
                !routeAndSendPacket(packet, packet_len, true))
            {
                ok = false;
            }
        }
        ok = have_peer && ok;
    }

    sys::EventBus::publish(new sys::ChatSendResultEvent(effective_packet_id, ok), 0);
    return ok;
}

bool LxmfAdapter::pollIncomingData(MeshIncomingData* out)
{
    processRadioPackets();
    maybeAnnounce();

    if (!out || data_receive_queue_.empty())
    {
        return false;
    }

    *out = std::move(data_receive_queue_.front());
    data_receive_queue_.pop();
    return true;
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
    const bool delivery_ok = sendAnnounce(LocalDestinationKind::Delivery);
    const bool propagation_ok = sendAnnounce(LocalDestinationKind::Propagation);
    if (delivery_ok || propagation_ok)
    {
        last_announce_ms_ = millis();
    }
    announce_pending_ = !(delivery_ok && propagation_ok);
    return delivery_ok || propagation_ok;
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
    cullLinkSessions();
    cullPropagationState();

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
                    !handleLocalLinkPacket(packet, packet_len, parsed) &&
                    !maybeForwardLinkPacket(packet, packet_len, parsed) &&
                    !maybeForwardTransportPacket(packet, packet_len, parsed))
                {
                    handleDataPacket(packet, packet_len, parsed);
                }
            }
            else
            {
                (void)handleLocalLinkPacket(packet, packet_len, parsed);
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

    const bool delivery_ok = sendAnnounce(LocalDestinationKind::Delivery);
    const bool propagation_ok = sendAnnounce(LocalDestinationKind::Propagation);
    if (delivery_ok || propagation_ok)
    {
        last_announce_ms_ = millis();
    }
    announce_pending_ = !(delivery_ok && propagation_ok);
}

bool LxmfAdapter::sendAnnounce(LocalDestinationKind kind,
                               reticulum::PacketContext context)
{
    if (!isReady())
    {
        return false;
    }

    uint8_t app_data[96] = {};
    size_t app_data_len = sizeof(app_data);
    bool app_data_ok = false;
    if (kind == LocalDestinationKind::Propagation)
    {
        app_data_ok = packPropagationAnnounceAppData(currentTimestampSeconds(),
                                                     true,
                                                     kPropagationTransferLimitKb,
                                                     kPropagationSyncLimitKb,
                                                     effectiveDisplayName(),
                                                     app_data,
                                                     &app_data_len);
    }
    else
    {
        app_data_ok = packPeerAnnounceAppData(effectiveDisplayName(),
                                              false,
                                              0,
                                              app_data,
                                              &app_data_len);
    }
    if (!app_data_ok)
    {
        return false;
    }

    uint8_t combined_pub[reticulum::kCombinedPublicKeySize] = {};
    identity_.combinedPublicKey(combined_pub);

    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    localDestinationHash(kind, destination_hash);

    uint8_t name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf",
                               (kind == LocalDestinationKind::Propagation) ? "propagation" : "delivery",
                               name_hash);

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
    memcpy(signed_data + signed_len, destination_hash, reticulum::kTruncatedHashSize);
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
                                       destination_hash,
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

    const bool delivery_announce = isLxmfDeliveryAnnounce(announce);
    const bool propagation_announce = isLxmfPropagationAnnounce(announce);
    const bool local_destination = isLocalDestinationHash(packet.destination_hash, nullptr);

    if (propagation_announce && !local_destination)
    {
        uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
        destinationHashForAspect(identity_hash, "delivery", delivery_hash);
        PropagationPeerState& propagation_peer = upsertPropagationPeer(packet.destination_hash,
                                                                       delivery_hash,
                                                                       identity_hash);
        propagation_peer.last_seen_s = now_s;
    }

    if (!delivery_announce || local_destination)
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

    LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
    if (!isLocalDestinationHash(packet.destination_hash, &local_kind))
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

    bool handled = false;
    if (local_kind == LocalDestinationKind::Propagation)
    {
        LinkSession propagation_session{};
        propagation_session.destination = LocalDestinationKind::Propagation;
        propagation_session.state = LinkState::Active;
        handled = handlePropagationBatch(propagation_session, plaintext, plaintext_len);
    }
    else
    {
        handled = acceptVerifiedEnvelope(plaintext, plaintext_len, raw_packet, raw_len);
    }

    if (!handled)
    {
        return false;
    }

    (void)sendProofForPacket(raw_packet, raw_len);
    return true;
}

bool LxmfAdapter::handleProofPacket(const uint8_t* raw_packet, size_t raw_len,
                                    const reticulum::ParsedPacket& packet)
{
    if (!raw_packet || !packet.destination_hash || packet.packet_type != reticulum::PacketType::Proof)
    {
        return false;
    }

    if (packet.destination_type == reticulum::DestinationType::Link &&
        handleLocalLinkPacket(raw_packet, raw_len, packet))
    {
        return true;
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
    if (packet.hops != reverse->expected_hops)
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

    LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
    if (isLocalDestinationHash(packet.destination_hash, &local_kind))
    {
        if (!packet.payload ||
            (packet.payload_len != kLinkRequestBaseLen &&
             packet.payload_len != (kLinkRequestBaseLen + kLinkSignallingLen)))
        {
            return false;
        }

        const size_t signalling_len =
            (packet.payload_len > kLinkRequestBaseLen) ? (packet.payload_len - kLinkRequestBaseLen) : 0;
        if (signalling_len != 0 &&
            linkModeFromSignalling(packet.payload + kLinkRequestBaseLen, signalling_len) != kLinkModeAes256Cbc)
        {
            return false;
        }

        uint8_t link_id[reticulum::kTruncatedHashSize] = {};
        if (!computeLinkIdFromLinkRequest(raw_packet, raw_len, packet, link_id))
        {
            return false;
        }

        LinkSession* session = findLinkSession(link_id);
        if (!session)
        {
            if (link_sessions_.size() >= kMaxLinkSessions)
            {
                link_sessions_.erase(link_sessions_.begin());
            }

            link_sessions_.push_back(LinkSession{});
            session = &link_sessions_.back();
            copyHash(session->link_id, link_id, sizeof(session->link_id));
            memcpy(session->peer_enc_pub, packet.payload, LxmfIdentity::kEncPubKeySize);
            memcpy(session->peer_link_sig_pub,
                   packet.payload + LxmfIdentity::kEncPubKeySize,
                   LxmfIdentity::kSigPubKeySize);
            Curve25519::dh1(session->local_enc_pub, session->local_enc_priv);
            memcpy(session->local_sig_pub, identity_.signingPublicKey(), sizeof(session->local_sig_pub));
            session->mtu = (signalling_len != 0)
                               ? mtuFromLinkSignalling(packet.payload + kLinkRequestBaseLen, signalling_len)
                               : reticulum::kReticulumMtu;
            session->mdu = linkMduForMtu(session->mtu);
            session->created_ms = millis();
            session->request_ms = session->created_ms;
            session->last_inbound_ms = session->created_ms;
            session->destination = local_kind;
            session->initiator = false;
            session->state = LinkState::Handshake;

            if (!deriveLinkKey(*session))
            {
                link_sessions_.pop_back();
                return false;
            }
        }
        else
        {
            session->last_inbound_ms = millis();
        }

        return sendLinkHandshakeProof(*session);
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

    LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
    if (isLocalDestinationHash(requested_hash, &local_kind))
    {
        (void)tag;
        return sendAnnounce(local_kind, reticulum::PacketContext::PathResponse);
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

bool LxmfAdapter::handleLocalLinkPacket(const uint8_t* raw_packet, size_t raw_len,
                                        const reticulum::ParsedPacket& packet)
{
    if (!packet.destination_hash ||
        packet.destination_type != reticulum::DestinationType::Link)
    {
        return false;
    }

    LinkSession* session = findLinkSession(packet.destination_hash);
    if (!session)
    {
        return false;
    }

    session->last_inbound_ms = millis();
    if (packet.packet_type == reticulum::PacketType::Proof)
    {
        return handleLinkProofPacket(*session, raw_packet, raw_len, packet);
    }
    if (packet.packet_type == reticulum::PacketType::Data)
    {
        return handleLinkDataPacket(*session, raw_packet, raw_len, packet);
    }
    return false;
}

bool LxmfAdapter::handleLinkDataPacket(LinkSession& session,
                                       const uint8_t* raw_packet, size_t raw_len,
                                       const reticulum::ParsedPacket& packet)
{
    if (!packet.payload)
    {
        return false;
    }

    std::vector<uint8_t> plaintext;
    const uint8_t context = packet.context;
    const bool raw_payload = packetContextUsesRawLinkPayload(context);
    if (!raw_payload)
    {
        if (!decryptLinkPayload(session, packet.payload, packet.payload_len, &plaintext))
        {
            return false;
        }
    }

    const uint8_t* payload_ptr = raw_payload ? packet.payload : plaintext.data();
    const size_t payload_len = raw_payload ? packet.payload_len : plaintext.size();
    bool handled = false;
    bool should_prove = false;

    if (context == static_cast<uint8_t>(reticulum::PacketContext::None))
    {
        if (session.state == LinkState::Active)
        {
            if (session.destination == LocalDestinationKind::Propagation)
            {
                handled = handlePropagationBatch(session, payload_ptr, payload_len);
            }
            else
            {
                handled = acceptVerifiedEnvelope(payload_ptr, payload_len, raw_packet, raw_len);
            }
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::LinkIdentify))
    {
        if (payload_len == reticulum::kCombinedPublicKeySize + reticulum::kSignatureSize)
        {
            const uint8_t* combined_pub = payload_ptr;
            const uint8_t* signature = payload_ptr + reticulum::kCombinedPublicKeySize;
            std::array<uint8_t, reticulum::kTruncatedHashSize + reticulum::kCombinedPublicKeySize> signed_data{};
            memcpy(signed_data.data(), session.link_id, reticulum::kTruncatedHashSize);
            memcpy(signed_data.data() + reticulum::kTruncatedHashSize,
                   combined_pub,
                   reticulum::kCombinedPublicKeySize);
            const uint8_t* sign_pub = combined_pub + LxmfIdentity::kEncPubKeySize;
            if (LxmfIdentity::verify(sign_pub, signature, signed_data.data(), signed_data.size()))
            {
                PeerInfo* peer = rememberPeerIdentity(combined_pub);
                if (peer)
                {
                    copyHash(session.remote_destination_hash,
                             peer->destination_hash,
                             sizeof(session.remote_destination_hash));
                    copyHash(session.remote_identity_hash,
                             peer->identity_hash,
                             sizeof(session.remote_identity_hash));
                    memcpy(session.peer_identity_sig_pub,
                           peer->sig_pub,
                           sizeof(session.peer_identity_sig_pub));
                    session.remote_identity_known = true;
                }
                handled = true;
            }
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::Request))
    {
        DecodedLinkRequest request{};
        if (decodeLinkRequestPayload(payload_ptr, payload_len, &request))
        {
            uint8_t request_id[reticulum::kTruncatedHashSize] = {};
            reticulum::computeTruncatedPacketHash(raw_packet, raw_len, request_id);

            if (session.destination == LocalDestinationKind::Propagation)
            {
                handled = handlePropagationRequest(session,
                                                  request,
                                                  request_id,
                                                  sizeof(request_id));
            }
            else
            {
                handled = sendLinkResponse(session,
                                           request_id,
                                           sizeof(request_id),
                                           nullptr,
                                           0,
                                           true);
            }
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::Response))
    {
        DecodedLinkResponse response{};
        if (decodeLinkResponsePayload(payload_ptr, payload_len, &response))
        {
            for (auto& pending : session.pending_requests)
            {
                if (pending.request_id == response.request_id)
                {
                    pending.response_ready = true;
                    if (!response.data_is_nil)
                    {
                        pending.response = std::move(response.packed_data);
                    }
                    handled = true;
                    break;
                }
            }
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::LrRtt))
    {
        double rtt_value = 0.0;
        if (!session.initiator &&
            unpackFloat64(payload_ptr, payload_len, &rtt_value))
        {
            session.rtt_s = static_cast<float>(rtt_value);
            session.state = LinkState::Active;
            handled = true;
        }
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::LinkClose))
    {
        if (payload_len == reticulum::kTruncatedHashSize &&
            hashesEqual(payload_ptr, session.link_id, sizeof(session.link_id)))
        {
            closeLinkSession(session);
            handled = true;
        }
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::Keepalive))
    {
        if (!session.initiator && payload_len == 1 && payload_ptr[0] == 0xFF)
        {
            handled = sendLinkKeepaliveAck(session);
        }
        else
        {
            handled = (payload_len == 1);
        }
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceAdv))
    {
        handled = handleLinkResourceAdvertisement(session, payload_ptr, payload_len);
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceReq))
    {
        handled = handleLinkResourceRequest(session, payload_ptr, payload_len);
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceHmu))
    {
        handled = handleLinkResourceHashmapUpdate(session, payload_ptr, payload_len);
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceIcl))
    {
        if (payload_len == reticulum::kFullHashSize)
        {
            session.incoming_resources.erase(
                std::remove_if(session.incoming_resources.begin(),
                               session.incoming_resources.end(),
                               [payload_ptr](const LinkResourceTransfer& resource)
                               {
                                   return hashesEqual(resource.resource_hash,
                                                      payload_ptr,
                                                      reticulum::kFullHashSize);
                               }),
                session.incoming_resources.end());
            handled = true;
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceRcl))
    {
        if (payload_len == reticulum::kFullHashSize)
        {
            session.outgoing_resources.erase(
                std::remove_if(session.outgoing_resources.begin(),
                               session.outgoing_resources.end(),
                               [payload_ptr](const LinkResourceTransfer& resource)
                               {
                                   return hashesEqual(resource.resource_hash,
                                                      payload_ptr,
                                                      reticulum::kFullHashSize);
                               }),
                session.outgoing_resources.end());
            handled = true;
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::Resource))
    {
        handled = handleLinkResourcePart(session, packet);
    }

    if (handled && should_prove)
    {
        (void)sendLinkPacketProof(session, raw_packet, raw_len);
    }
    return handled;
}

bool LxmfAdapter::handleLinkProofPacket(LinkSession& session,
                                        const uint8_t* raw_packet, size_t raw_len,
                                        const reticulum::ParsedPacket& packet)
{
    (void)raw_packet;
    (void)raw_len;

    if (!packet.payload)
    {
        return false;
    }

    if (packet.context == static_cast<uint8_t>(reticulum::PacketContext::LrProof))
    {
        if (!session.initiator || packet.payload_len < (reticulum::kSignatureSize + LxmfIdentity::kEncPubKeySize))
        {
            return false;
        }

        const uint8_t* signature = packet.payload;
        const uint8_t* peer_enc_pub = packet.payload + reticulum::kSignatureSize;
        const uint8_t* signalling = (packet.payload_len >= (reticulum::kSignatureSize + LxmfIdentity::kEncPubKeySize + kLinkSignallingLen))
                                        ? (packet.payload + reticulum::kSignatureSize + LxmfIdentity::kEncPubKeySize)
                                        : nullptr;
        const size_t signalling_len = signalling ? kLinkSignallingLen : 0;

        const PeerInfo* peer = findPeerByDestinationHash(session.remote_destination_hash);
        if (!peer)
        {
            return false;
        }

        std::array<uint8_t, reticulum::kTruncatedHashSize + LxmfIdentity::kEncPubKeySize +
                               LxmfIdentity::kSigPubKeySize + kLinkSignallingLen>
            signed_data{};
        size_t used = 0;
        memcpy(signed_data.data() + used, session.link_id, reticulum::kTruncatedHashSize);
        used += reticulum::kTruncatedHashSize;
        memcpy(signed_data.data() + used, peer_enc_pub, LxmfIdentity::kEncPubKeySize);
        used += LxmfIdentity::kEncPubKeySize;
        memcpy(signed_data.data() + used, peer->sig_pub, LxmfIdentity::kSigPubKeySize);
        used += LxmfIdentity::kSigPubKeySize;
        if (signalling_len != 0)
        {
            memcpy(signed_data.data() + used, signalling, signalling_len);
            used += signalling_len;
        }

        if (!LxmfIdentity::verify(peer->sig_pub, signature, signed_data.data(), used))
        {
            return false;
        }

        memcpy(session.peer_enc_pub, peer_enc_pub, sizeof(session.peer_enc_pub));
        memcpy(session.peer_identity_sig_pub, peer->sig_pub, sizeof(session.peer_identity_sig_pub));
        copyHash(session.remote_identity_hash, peer->identity_hash, sizeof(session.remote_identity_hash));
        session.remote_identity_known = true;
        session.mtu = signalling ? mtuFromLinkSignalling(signalling, signalling_len) : reticulum::kReticulumMtu;
        session.mdu = linkMduForMtu(session.mtu);
        if (!deriveLinkKey(session))
        {
            return false;
        }

        session.rtt_s = static_cast<float>((millis() - session.request_ms) / 1000.0f);
        session.state = LinkState::Active;
        return sendLinkRtt(session);
    }

    if (packet.context == static_cast<uint8_t>(reticulum::PacketContext::ResourcePrf))
    {
        return handleLinkResourceProof(session, packet);
    }

    return false;
}

LxmfAdapter::LinkResourceTransfer* LxmfAdapter::findLinkResource(
    std::vector<LinkResourceTransfer>& resources,
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    if (!resource_hash)
    {
        return nullptr;
    }

    for (auto& resource : resources)
    {
        if (hashesEqual(resource.resource_hash, resource_hash, sizeof(resource.resource_hash)))
        {
            return &resource;
        }
    }
    return nullptr;
}

bool LxmfAdapter::handleLinkResourceAdvertisement(LinkSession& session,
                                                  const uint8_t* plaintext, size_t plaintext_len)
{
    DecodedResourceAdvertisement advertisement{};
    if (!decodeResourceAdvertisement(plaintext, plaintext_len, &advertisement))
    {
        return false;
    }

    const bool encrypted = (advertisement.flags & kResourceFlagEncrypted) != 0;
    const bool compressed = (advertisement.flags & kResourceFlagCompressed) != 0;
    const bool split = (advertisement.flags & kResourceFlagSplit) != 0;
    const bool has_metadata = (advertisement.flags & kResourceFlagHasMetadata) != 0;
    if (compressed || split || has_metadata ||
        advertisement.total_segments > 1 || advertisement.segment_index != 1)
    {
        (void)sendLinkPacket(session,
                             reticulum::PacketType::Data,
                             reticulum::PacketContext::ResourceRcl,
                             advertisement.resource_hash,
                             reticulum::kFullHashSize,
                             true);
        return false;
    }

    if (advertisement.part_count == 0 ||
        advertisement.hashmap.empty() ||
        (advertisement.hashmap.size() % kResourceMapHashLen) != 0)
    {
        return false;
    }

    if (findLinkResource(session.incoming_resources, advertisement.resource_hash))
    {
        return true;
    }

    LinkResourceTransfer resource{};
    memcpy(resource.resource_hash, advertisement.resource_hash, sizeof(resource.resource_hash));
    memcpy(resource.random_hash, advertisement.random_hash, sizeof(resource.random_hash));
    memcpy(resource.original_hash, advertisement.original_hash, sizeof(resource.original_hash));
    resource.request_id = std::move(advertisement.request_id);
    resource.hashmap = std::move(advertisement.hashmap);
    resource.data_size = advertisement.data_size;
    resource.transfer_size = advertisement.transfer_size;
    resource.part_count = advertisement.part_count;
    resource.hashmap_height = static_cast<uint32_t>(advertisement.hashmap.size() / kResourceMapHashLen);
    resource.window_size = kResourceWindowSize;
    resource.created_ms = millis();
    resource.last_activity_ms = resource.created_ms;
    resource.flags = advertisement.flags;
    resource.incoming = true;
    resource.encrypted = encrypted;
    resource.compressed = compressed;
    resource.has_metadata = has_metadata;
    resource.parts.resize(resource.part_count);
    resource.received_bitmap.assign(resource.part_count, 0);
    resource.map_hashes.resize(resource.part_count);
    resource.map_hash_known.assign(resource.part_count, 0);

    size_t map_index = 0;
    for (size_t i = 0; i < resource.hashmap.size() && map_index < resource.part_count; i += kResourceMapHashLen)
    {
        std::array<uint8_t, kResourceMapHashLen> map_hash{};
        memcpy(map_hash.data(), resource.hashmap.data() + i, map_hash.size());
        resource.map_hashes[map_index] = map_hash;
        resource.map_hash_known[map_index] = 1;
        ++map_index;
    }
    resource.waiting_for_hashmap = resource.hashmap_height < resource.part_count;

    session.incoming_resources.push_back(std::move(resource));
    return requestNextResourceWindow(session, session.incoming_resources.back());
}

bool LxmfAdapter::requestNextResourceWindow(LinkSession& session,
                                            LinkResourceTransfer& resource)
{
    if (resource.complete || resource.part_count == 0)
    {
        return false;
    }

    std::vector<uint8_t> request_data;
    request_data.reserve(1 + kResourceMapHashLen + reticulum::kFullHashSize +
                         (resource.window_size * kResourceMapHashLen));

    const size_t start_index =
        (resource.consecutive_complete_index >= 0)
            ? static_cast<size_t>(resource.consecutive_complete_index + 1)
            : 0;

    uint32_t requested = 0;
    bool needs_more_hashmap = false;
    for (size_t index = start_index;
         index < resource.part_count && requested < resource.window_size;
         ++index)
    {
        if (resource.received_bitmap[index] != 0)
        {
            continue;
        }

        if (index >= resource.map_hash_known.size() || resource.map_hash_known[index] == 0)
        {
            needs_more_hashmap = true;
            break;
        }

        request_data.insert(request_data.end(),
                            resource.map_hashes[index].begin(),
                            resource.map_hashes[index].end());
        ++requested;
    }

    if (requested == 0 && !needs_more_hashmap)
    {
        return false;
    }

    request_data.insert(request_data.begin(),
                        resource.resource_hash,
                        resource.resource_hash + reticulum::kFullHashSize);

    if (needs_more_hashmap)
    {
        if (resource.hashmap_height == 0 || resource.hashmap_height > resource.map_hashes.size())
        {
            return false;
        }

        request_data.insert(request_data.begin(),
                            resource.map_hashes[resource.hashmap_height - 1U].begin(),
                            resource.map_hashes[resource.hashmap_height - 1U].end());
        request_data.insert(request_data.begin(), 0xFF);
    }
    else
    {
        request_data.insert(request_data.begin(), 0x00);
    }

    resource.last_activity_ms = millis();
    resource.waiting_for_hashmap = needs_more_hashmap;
    return sendLinkPacket(session,
                          reticulum::PacketType::Data,
                          reticulum::PacketContext::ResourceReq,
                          request_data.data(),
                          request_data.size(),
                          true);
}

bool LxmfAdapter::handleLinkResourceRequest(LinkSession& session,
                                            const uint8_t* plaintext, size_t plaintext_len)
{
    if (!plaintext || plaintext_len < (1 + reticulum::kFullHashSize))
    {
        return false;
    }

    const bool wants_more_hashmap = plaintext[0] == 0xFF;
    size_t offset = 1;
    const uint8_t* last_map_hash = nullptr;
    if (wants_more_hashmap)
    {
        if (plaintext_len < (1 + kResourceMapHashLen + reticulum::kFullHashSize))
        {
            return false;
        }
        last_map_hash = plaintext + offset;
        offset += kResourceMapHashLen;
    }

    const uint8_t* resource_hash = plaintext + offset;
    offset += reticulum::kFullHashSize;
    LinkResourceTransfer* resource = findLinkResource(session.outgoing_resources, resource_hash);
    if (!resource)
    {
        return false;
    }

    bool sent_any = false;
    while (offset + kResourceMapHashLen <= plaintext_len)
    {
        const uint8_t* requested_hash = plaintext + offset;
        offset += kResourceMapHashLen;

        for (size_t index = 0; index < resource->map_hashes.size() && index < resource->parts.size(); ++index)
        {
            if (memcmp(resource->map_hashes[index].data(), requested_hash, kResourceMapHashLen) == 0)
            {
                if (!resource->parts[index].empty())
                {
                    sent_any = sendLinkPacket(session,
                                              reticulum::PacketType::Data,
                                              reticulum::PacketContext::Resource,
                                              resource->parts[index].data(),
                                              resource->parts[index].size(),
                                              false) || sent_any;
                }
                break;
            }
        }
    }

    if (wants_more_hashmap && last_map_hash)
    {
        const size_t segment_capacity = resourceHashmapSegmentCapacity(session.mdu);
        size_t last_index = resource->part_count;
        for (size_t index = 0; index < resource->map_hashes.size(); ++index)
        {
            if (memcmp(resource->map_hashes[index].data(), last_map_hash, kResourceMapHashLen) == 0)
            {
                last_index = index;
                break;
            }
        }

        if (last_index < resource->part_count)
        {
            const size_t next_index = last_index + 1U;
            if (next_index < resource->part_count && (next_index % segment_capacity) == 0)
            {
                const uint32_t segment = static_cast<uint32_t>(next_index / segment_capacity);
                const size_t slice_offset = next_index * kResourceMapHashLen;
                const size_t remaining_hashes = static_cast<size_t>(resource->part_count) - next_index;
                const size_t slice_hashes = std::min(segment_capacity, remaining_hashes);

                uint8_t update_payload[kMaxPacketLen] = {};
                size_t update_len = sizeof(update_payload);
                if (encodeResourceHashmapUpdate(segment,
                                               resource->hashmap.data() + slice_offset,
                                               slice_hashes * kResourceMapHashLen,
                                               update_payload + reticulum::kFullHashSize,
                                               &update_len))
                {
                    memcpy(update_payload, resource->resource_hash, reticulum::kFullHashSize);
                    const size_t wire_len = reticulum::kFullHashSize + update_len;
                    sent_any = sendLinkPacket(session,
                                              reticulum::PacketType::Data,
                                              reticulum::PacketContext::ResourceHmu,
                                              update_payload,
                                              wire_len,
                                              true) || sent_any;
                }
            }
        }
    }

    resource->last_activity_ms = millis();
    return sent_any;
}

bool LxmfAdapter::handleLinkResourceHashmapUpdate(LinkSession& session,
                                                  const uint8_t* plaintext, size_t plaintext_len)
{
    if (!plaintext || plaintext_len <= reticulum::kFullHashSize)
    {
        return false;
    }

    LinkResourceTransfer* resource = findLinkResource(session.incoming_resources, plaintext);
    if (!resource)
    {
        return false;
    }

    DecodedResourceHashmapUpdate update{};
    if (!decodeResourceHashmapUpdate(plaintext + reticulum::kFullHashSize,
                                     plaintext_len - reticulum::kFullHashSize,
                                     &update) ||
        (update.hashmap.size() % kResourceMapHashLen) != 0)
    {
        return false;
    }

    const size_t segment_capacity = resourceHashmapSegmentCapacity(session.mdu);
    const size_t start_index = static_cast<size_t>(update.segment) * segment_capacity;
    if (start_index >= resource->part_count)
    {
        return false;
    }

    const size_t hash_count = update.hashmap.size() / kResourceMapHashLen;
    const size_t applied = std::min(hash_count, static_cast<size_t>(resource->part_count) - start_index);
    for (size_t index = 0; index < applied; ++index)
    {
        std::array<uint8_t, kResourceMapHashLen> map_hash{};
        memcpy(map_hash.data(),
               update.hashmap.data() + (index * kResourceMapHashLen),
               map_hash.size());
        resource->map_hashes[start_index + index] = map_hash;
        resource->map_hash_known[start_index + index] = 1;
    }

    resource->hashmap_height = std::max<uint32_t>(
        resource->hashmap_height,
        static_cast<uint32_t>(start_index + applied));
    resource->last_activity_ms = millis();
    resource->waiting_for_hashmap = false;
    (void)requestNextResourceWindow(session, *resource);
    return true;
}

bool LxmfAdapter::handleLinkResourcePart(LinkSession& session,
                                         const reticulum::ParsedPacket& packet)
{
    if (!packet.payload || packet.payload_len == 0)
    {
        return false;
    }

    for (auto& resource : session.incoming_resources)
    {
        if (resource.complete)
        {
            continue;
        }

        uint8_t full_hash[reticulum::kFullHashSize] = {};
        std::vector<uint8_t> hash_material(packet.payload_len + sizeof(resource.random_hash), 0);
        memcpy(hash_material.data(), packet.payload, packet.payload_len);
        memcpy(hash_material.data() + packet.payload_len,
               resource.random_hash,
               sizeof(resource.random_hash));
        reticulum::fullHash(hash_material.data(),
                            hash_material.size(),
                            full_hash);

        size_t matched_index = resource.part_count;
        for (size_t index = 0;
             index < resource.map_hashes.size() && index < resource.part_count;
             ++index)
        {
            if (index < resource.map_hash_known.size() &&
                resource.map_hash_known[index] != 0 &&
                memcmp(resource.map_hashes[index].data(), full_hash, kResourceMapHashLen) == 0)
            {
                matched_index = index;
                break;
            }
        }
        if (matched_index >= resource.part_count)
        {
            continue;
        }

        if (resource.received_bitmap[matched_index] == 0)
        {
            resource.parts[matched_index].assign(packet.payload, packet.payload + packet.payload_len);
            resource.received_bitmap[matched_index] = 1;
        }

        while ((resource.consecutive_complete_index + 1) < static_cast<int32_t>(resource.part_count) &&
               resource.received_bitmap[static_cast<size_t>(resource.consecutive_complete_index + 1)] != 0)
        {
            ++resource.consecutive_complete_index;
        }
        resource.last_activity_ms = millis();

        bool complete = true;
        for (uint8_t received : resource.received_bitmap)
        {
            if (received == 0)
            {
                complete = false;
                break;
            }
        }

        if (!complete)
        {
            (void)requestNextResourceWindow(session, resource);
            return true;
        }

        std::vector<uint8_t> assembled;
        assembled.reserve(resource.transfer_size);
        for (const auto& part : resource.parts)
        {
            assembled.insert(assembled.end(), part.begin(), part.end());
        }
        if (assembled.size() > resource.transfer_size)
        {
            assembled.resize(resource.transfer_size);
        }

        std::vector<uint8_t> resource_stream;
        if (resource.encrypted)
        {
            if (!decryptLinkPayload(session,
                                    assembled.data(),
                                    assembled.size(),
                                    &resource_stream))
            {
                return false;
            }
        }
        else
        {
            resource_stream = std::move(assembled);
        }

        if (resource.compressed || resource.has_metadata ||
            resource_stream.size() < kResourceDataPrefixLen)
        {
            return false;
        }

        std::vector<uint8_t> payload_data(resource_stream.begin() + kResourceDataPrefixLen,
                                          resource_stream.end());
        if (payload_data.size() != resource.data_size)
        {
            return false;
        }

        std::vector<uint8_t> resource_hash_material(payload_data.size() + sizeof(resource.random_hash), 0);
        if (!payload_data.empty())
        {
            memcpy(resource_hash_material.data(), payload_data.data(), payload_data.size());
        }
        memcpy(resource_hash_material.data() + payload_data.size(),
               resource.random_hash,
               sizeof(resource.random_hash));

        uint8_t expected_resource_hash[reticulum::kFullHashSize] = {};
        reticulum::fullHash(resource_hash_material.data(),
                            resource_hash_material.size(),
                            expected_resource_hash);
        if (!hashesEqual(expected_resource_hash,
                         resource.resource_hash,
                         reticulum::kFullHashSize))
        {
            return false;
        }

        std::vector<uint8_t> proof_material(payload_data.size() + reticulum::kFullHashSize, 0);
        if (!payload_data.empty())
        {
            memcpy(proof_material.data(), payload_data.data(), payload_data.size());
        }
        memcpy(proof_material.data() + payload_data.size(),
               resource.resource_hash,
               reticulum::kFullHashSize);
        reticulum::fullHash(proof_material.data(),
                            proof_material.size(),
                            resource.expected_proof);

        std::array<uint8_t, reticulum::kFullHashSize * 2> proof_payload{};
        memcpy(proof_payload.data(), resource.resource_hash, reticulum::kFullHashSize);
        memcpy(proof_payload.data() + reticulum::kFullHashSize,
               resource.expected_proof,
               reticulum::kFullHashSize);
        (void)sendLinkPacket(session,
                             reticulum::PacketType::Proof,
                             reticulum::PacketContext::ResourcePrf,
                             proof_payload.data(),
                             proof_payload.size(),
                             false);

        resource.complete = true;

        const bool is_request = (resource.flags & kResourceFlagRequest) != 0;
        const bool is_response = (resource.flags & kResourceFlagResponse) != 0;
        if (is_request)
        {
            DecodedLinkRequest request{};
            if (decodeLinkRequestPayload(payload_data.data(), payload_data.size(), &request))
            {
                uint8_t request_id[reticulum::kTruncatedHashSize] = {};
                reticulum::truncatedHash(payload_data.data(), payload_data.size(), request_id);
                if (session.destination == LocalDestinationKind::Propagation)
                {
                    (void)handlePropagationRequest(session,
                                                   request,
                                                   request_id,
                                                   sizeof(request_id));
                }
                else
                {
                    (void)sendLinkResponse(session,
                                           request_id,
                                           sizeof(request_id),
                                           nullptr,
                                           0,
                                           true);
                }
            }
        }
        else if (is_response)
        {
            DecodedLinkResponse response{};
            if (decodeLinkResponsePayload(payload_data.data(), payload_data.size(), &response))
            {
                for (auto& pending : session.pending_requests)
                {
                    if (pending.request_id == response.request_id)
                    {
                        pending.response_ready = true;
                        if (!response.data_is_nil)
                        {
                            pending.response = std::move(response.packed_data);
                        }
                        break;
                    }
                }
            }
        }
        else if (session.destination == LocalDestinationKind::Delivery)
        {
            (void)acceptVerifiedEnvelope(payload_data.data(), payload_data.size(), nullptr, 0);
        }
        else if (session.destination == LocalDestinationKind::Propagation)
        {
            (void)handlePropagationBatch(session, payload_data.data(), payload_data.size());
        }

        return true;
    }

    return false;
}

bool LxmfAdapter::handleLinkResourceProof(LinkSession& session,
                                          const reticulum::ParsedPacket& packet)
{
    if (!packet.payload || packet.payload_len != (reticulum::kFullHashSize * 2))
    {
        return false;
    }

    const uint8_t* resource_hash = packet.payload;
    const uint8_t* expected_proof = packet.payload + reticulum::kFullHashSize;
    LinkResourceTransfer* resource = findLinkResource(session.outgoing_resources, resource_hash);
    if (!resource)
    {
        return false;
    }

    if (!hashesEqual(expected_proof,
                     resource->expected_proof,
                     reticulum::kFullHashSize))
    {
        return false;
    }

    resource->complete = true;
    resource->last_activity_ms = millis();
    return true;
}

bool LxmfAdapter::handlePropagationBatch(LinkSession& session,
                                         const uint8_t* plaintext,
                                         size_t plaintext_len)
{
    DecodedPropagationBatch batch{};
    if (!decodePropagationBatch(plaintext, plaintext_len, &batch))
    {
        return false;
    }

    if (!session.propagation_offer_validated && batch.messages.size() > 1)
    {
        return false;
    }

    uint8_t remote_propagation_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t remote_delivery_hash[reticulum::kTruncatedHashSize] = {};
    PropagationPeerState* peer_state = nullptr;
    if (session.remote_identity_known)
    {
        destinationHashForAspect(session.remote_identity_hash, "propagation", remote_propagation_hash);
        destinationHashForAspect(session.remote_identity_hash, "delivery", remote_delivery_hash);
        peer_state = &upsertPropagationPeer(remote_propagation_hash,
                                            remote_delivery_hash,
                                            session.remote_identity_hash);
        peer_state->last_seen_s = currentTimestampSeconds();
    }

    bool handled_any = false;
    for (const auto& message : batch.messages)
    {
        if (acceptPropagatedMessage(message.data(),
                                    message.size(),
                                    session.remote_identity_known ? remote_propagation_hash : nullptr))
        {
            handled_any = true;
            if (peer_state)
            {
                peer_state->incoming_messages += 1;
            }
        }
    }

    return handled_any;
}

bool LxmfAdapter::handlePropagationRequest(LinkSession& session,
                                           const DecodedLinkRequest& request,
                                           const uint8_t* request_id,
                                           size_t request_id_len)
{
    auto send_uint_response = [&](uint32_t value) -> bool
    {
        uint8_t packed[8] = {};
        size_t packed_len = sizeof(packed);
        if (!encodeMsgpackUint(value, packed, &packed_len))
        {
            return false;
        }

        return sendLinkResponse(session,
                                request_id,
                                request_id_len,
                                packed,
                                packed_len,
                                false);
    };

    auto send_bool_response = [&](bool value) -> bool
    {
        uint8_t packed[4] = {};
        size_t packed_len = sizeof(packed);
        if (!encodeMsgpackBool(value, packed, &packed_len))
        {
            return false;
        }

        return sendLinkResponse(session,
                                request_id,
                                request_id_len,
                                packed,
                                packed_len,
                                false);
    };

    uint8_t offer_path_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t get_path_hash[reticulum::kTruncatedHashSize] = {};
    propagationPathHash(kPropagationOfferPath, offer_path_hash);
    propagationPathHash(kPropagationGetPath, get_path_hash);

    if (!session.remote_identity_known)
    {
        return send_uint_response(kPropagationErrorNoIdentity);
    }

    uint8_t remote_delivery_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t remote_propagation_hash[reticulum::kTruncatedHashSize] = {};
    destinationHashForAspect(session.remote_identity_hash, "delivery", remote_delivery_hash);
    destinationHashForAspect(session.remote_identity_hash, "propagation", remote_propagation_hash);
    PropagationPeerState& peer_state = upsertPropagationPeer(remote_propagation_hash,
                                                             remote_delivery_hash,
                                                             session.remote_identity_hash);
    peer_state.last_seen_s = currentTimestampSeconds();

    if (hashesEqual(request.path_hash, offer_path_hash, sizeof(offer_path_hash)))
    {
        if (request.data_is_nil)
        {
            return send_uint_response(kPropagationErrorInvalidData);
        }

        DecodedPropagationOffer offer{};
        if (!decodePropagationOfferPayload(request.packed_data.data(),
                                           request.packed_data.size(),
                                           &offer))
        {
            return send_uint_response(kPropagationErrorInvalidData);
        }

        if (offer.peering_key_is_nil || offer.peering_key.empty())
        {
            return send_uint_response(kPropagationErrorInvalidKey);
        }

        session.propagation_offer_validated = true;

        std::vector<std::vector<uint8_t>> wanted_ids;
        wanted_ids.reserve(offer.transient_ids.size());
        for (const auto& transient_id : offer.transient_ids)
        {
            if (transient_id.size() != reticulum::kFullHashSize)
            {
                continue;
            }

            if (!findPropagationEntry(transient_id.data()) &&
                !hasSeenPropagationTransient(transient_id.data(), nullptr))
            {
                wanted_ids.push_back(transient_id);
            }
        }

        if (wanted_ids.empty())
        {
            return send_bool_response(false);
        }

        if (wanted_ids.size() == offer.transient_ids.size())
        {
            return send_bool_response(true);
        }

        const size_t response_capacity = 4 + (wanted_ids.size() * (reticulum::kFullHashSize + 3));
        std::vector<uint8_t> packed(response_capacity, 0);
        size_t packed_len = packed.size();
        if (!encodePropagationIdListPayload(wanted_ids, packed.data(), &packed_len))
        {
            return false;
        }

        return sendLinkResponse(session,
                                request_id,
                                request_id_len,
                                packed.data(),
                                packed_len,
                                false);
    }

    if (!hashesEqual(request.path_hash, get_path_hash, sizeof(get_path_hash)))
    {
        return send_uint_response(kPropagationErrorInvalidData);
    }

    if (request.data_is_nil)
    {
        return send_uint_response(kPropagationErrorInvalidData);
    }

    DecodedPropagationGetRequest get_request{};
    if (!decodePropagationGetRequestPayload(request.packed_data.data(),
                                            request.packed_data.size(),
                                            &get_request))
    {
        return send_uint_response(kPropagationErrorInvalidData);
    }

    if (!get_request.haves_is_nil)
    {
        for (const auto& transient_id : get_request.haves)
        {
            if (transient_id.size() != reticulum::kFullHashSize)
            {
                continue;
            }

            propagation_entries_.erase(
                std::remove_if(propagation_entries_.begin(),
                               propagation_entries_.end(),
                               [&](const PropagationEntry& entry)
                               {
                                   return hashesEqual(entry.transient_id,
                                                      transient_id.data(),
                                                      reticulum::kFullHashSize) &&
                                          hashesEqual(entry.destination_hash,
                                                      remote_delivery_hash,
                                                      reticulum::kTruncatedHashSize);
                               }),
                propagation_entries_.end());
            rememberPropagationTransient(transient_id.data(), true);
        }
    }

    std::vector<std::vector<uint8_t>> response_items;
    if (get_request.wants_is_nil && get_request.haves_is_nil)
    {
        response_items.reserve(propagation_entries_.size());
        for (const auto& entry : propagation_entries_)
        {
            if (hashesEqual(entry.destination_hash,
                            remote_delivery_hash,
                            reticulum::kTruncatedHashSize))
            {
                response_items.emplace_back(entry.transient_id,
                                            entry.transient_id + reticulum::kFullHashSize);
            }
        }

        const size_t response_capacity =
            4 + (response_items.size() * (reticulum::kFullHashSize + 3));
        std::vector<uint8_t> packed(response_capacity, 0);
        size_t packed_len = packed.size();
        if (!encodePropagationIdListPayload(response_items, packed.data(), &packed_len))
        {
            return false;
        }

        return sendLinkResponse(session,
                                request_id,
                                request_id_len,
                                packed.data(),
                                packed_len,
                                false);
    }

    size_t cumulative_size = 24;
    const size_t transfer_limit_bytes =
        get_request.has_transfer_limit ? (static_cast<size_t>(get_request.transfer_limit_kb) * 1000U) : 0U;
    for (const auto& transient_id : get_request.wants)
    {
        if (transient_id.size() != reticulum::kFullHashSize)
        {
            continue;
        }

        PropagationEntry* entry = findPropagationEntry(transient_id.data());
        if (!entry ||
            !hashesEqual(entry->destination_hash,
                         remote_delivery_hash,
                         reticulum::kTruncatedHashSize))
        {
            continue;
        }

        const size_t next_size = cumulative_size + entry->lxmf_data.size() + 16U;
        if (transfer_limit_bytes != 0U && next_size > transfer_limit_bytes)
        {
            break;
        }

        response_items.push_back(entry->lxmf_data);
        cumulative_size = next_size;
        entry->served_count += 1;
    }

    peer_state.served_messages += static_cast<uint32_t>(response_items.size());
    size_t response_capacity = 4;
    for (const auto& item : response_items)
    {
        response_capacity += item.size() + 3;
    }
    std::vector<uint8_t> packed(response_capacity, 0);
    size_t packed_len = packed.size();
    if (!encodePropagationMessageListPayload(response_items, packed.data(), &packed_len))
    {
        return false;
    }

    return sendLinkResponse(session,
                            request_id,
                            request_id_len,
                            packed.data(),
                            packed_len,
                            false);
}

bool LxmfAdapter::acceptPropagatedMessage(const uint8_t* lxmf_data,
                                          size_t lxmf_len,
                                          const uint8_t* remote_propagation_hash)
{
    if (!lxmf_data || lxmf_len <= reticulum::kTruncatedHashSize)
    {
        return false;
    }

    uint8_t transient_id[reticulum::kFullHashSize] = {};
    reticulum::fullHash(lxmf_data, lxmf_len, transient_id);

    if (findPropagationEntry(transient_id) ||
        hasSeenPropagationTransient(transient_id, nullptr))
    {
        return true;
    }

    const uint8_t* destination_hash = lxmf_data;
    LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
    if (isLocalDestinationHash(destination_hash, &local_kind) &&
        local_kind == LocalDestinationKind::Delivery)
    {
        const bool delivered = acceptPropagatedDelivery(lxmf_data + reticulum::kTruncatedHashSize,
                                                        lxmf_len - reticulum::kTruncatedHashSize);
        if (delivered)
        {
            rememberPropagationTransient(transient_id, true);
        }
        return delivered;
    }

    if (propagation_entries_.size() >= kMaxPropagationEntries)
    {
        propagation_entries_.erase(propagation_entries_.begin());
    }

    PropagationEntry entry{};
    memcpy(entry.transient_id, transient_id, sizeof(entry.transient_id));
    memcpy(entry.destination_hash, destination_hash, sizeof(entry.destination_hash));
    entry.lxmf_data.assign(lxmf_data, lxmf_data + lxmf_len);
    entry.created_s = currentTimestampSeconds();
    propagation_entries_.push_back(std::move(entry));
    rememberPropagationTransient(transient_id, false);

    if (remote_propagation_hash)
    {
        for (auto& peer : propagation_peers_)
        {
            if (hashesEqual(peer.propagation_hash,
                            remote_propagation_hash,
                            reticulum::kTruncatedHashSize))
            {
                peer.last_seen_s = currentTimestampSeconds();
                break;
            }
        }
    }

    return true;
}

bool LxmfAdapter::acceptPropagatedDelivery(const uint8_t* propagated_payload,
                                           size_t propagated_payload_len)
{
    if (!propagated_payload ||
        propagated_payload_len <= (reticulum::kEncryptionPublicKeySize + reticulum::kTokenOverhead))
    {
        return false;
    }

    const uint8_t* peer_ephemeral_pub = propagated_payload;
    const uint8_t* token = propagated_payload + reticulum::kEncryptionPublicKeySize;
    const size_t token_len = propagated_payload_len - reticulum::kEncryptionPublicKeySize;

    uint8_t shared_secret[LxmfIdentity::kEncPubKeySize] = {};
    if (!identity_.deriveSharedSecret(peer_ephemeral_pub, shared_secret))
    {
        return false;
    }

    uint8_t derived_key[reticulum::kDerivedTokenKeySize] = {};
    if (!reticulum::hkdfSha256(shared_secret,
                               sizeof(shared_secret),
                               identity_.identityHash(),
                               reticulum::kTruncatedHashSize,
                               nullptr,
                               0,
                               derived_key,
                               sizeof(derived_key)))
    {
        return false;
    }

    std::vector<uint8_t> plaintext(token_len, 0);
    size_t plaintext_len = plaintext.size();
    if (!reticulum::tokenDecrypt(derived_key,
                                 token,
                                 token_len,
                                 plaintext.data(),
                                 &plaintext_len))
    {
        return false;
    }
    plaintext.resize(plaintext_len);

    std::vector<uint8_t> lxmf_message(reticulum::kTruncatedHashSize + plaintext.size(), 0);
    memcpy(lxmf_message.data(), identity_.destinationHash(), reticulum::kTruncatedHashSize);
    if (!plaintext.empty())
    {
        memcpy(lxmf_message.data() + reticulum::kTruncatedHashSize,
               plaintext.data(),
               plaintext.size());
    }

    return acceptVerifiedEnvelope(lxmf_message.data(), lxmf_message.size(), nullptr, 0);
}

LxmfAdapter::PropagationEntry* LxmfAdapter::findPropagationEntry(
    const uint8_t transient_id[reticulum::kFullHashSize])
{
    if (!transient_id)
    {
        return nullptr;
    }

    for (auto& entry : propagation_entries_)
    {
        if (hashesEqual(entry.transient_id, transient_id, reticulum::kFullHashSize))
        {
            return &entry;
        }
    }

    return nullptr;
}

const LxmfAdapter::PropagationEntry* LxmfAdapter::findPropagationEntry(
    const uint8_t transient_id[reticulum::kFullHashSize]) const
{
    if (!transient_id)
    {
        return nullptr;
    }

    for (const auto& entry : propagation_entries_)
    {
        if (hashesEqual(entry.transient_id, transient_id, reticulum::kFullHashSize))
        {
            return &entry;
        }
    }

    return nullptr;
}

LxmfAdapter::PropagationPeerState& LxmfAdapter::upsertPropagationPeer(
    const uint8_t propagation_hash[reticulum::kTruncatedHashSize],
    const uint8_t delivery_hash[reticulum::kTruncatedHashSize],
    const uint8_t identity_hash[reticulum::kTruncatedHashSize])
{
    for (auto& peer : propagation_peers_)
    {
        if (hashesEqual(peer.propagation_hash,
                        propagation_hash,
                        reticulum::kTruncatedHashSize))
        {
            if (delivery_hash)
            {
                copyHash(peer.delivery_hash, delivery_hash, sizeof(peer.delivery_hash));
            }
            if (identity_hash)
            {
                copyHash(peer.identity_hash, identity_hash, sizeof(peer.identity_hash));
            }
            return peer;
        }
    }

    if (propagation_peers_.size() >= kMaxPropagationPeers)
    {
        propagation_peers_.erase(propagation_peers_.begin());
    }

    propagation_peers_.push_back(PropagationPeerState{});
    PropagationPeerState& peer = propagation_peers_.back();
    if (propagation_hash)
    {
        copyHash(peer.propagation_hash, propagation_hash, sizeof(peer.propagation_hash));
    }
    if (delivery_hash)
    {
        copyHash(peer.delivery_hash, delivery_hash, sizeof(peer.delivery_hash));
    }
    if (identity_hash)
    {
        copyHash(peer.identity_hash, identity_hash, sizeof(peer.identity_hash));
    }
    return peer;
}

bool LxmfAdapter::hasSeenPropagationTransient(
    const uint8_t transient_id[reticulum::kFullHashSize],
    bool* out_delivered) const
{
    if (out_delivered)
    {
        *out_delivered = false;
    }

    if (!transient_id)
    {
        return false;
    }

    for (const auto& entry : propagation_transients_)
    {
        if (hashesEqual(entry.transient_id, transient_id, reticulum::kFullHashSize))
        {
            if (out_delivered)
            {
                *out_delivered = entry.delivered;
            }
            return true;
        }
    }

    return false;
}

void LxmfAdapter::rememberPropagationTransient(
    const uint8_t transient_id[reticulum::kFullHashSize],
    bool delivered)
{
    if (!transient_id)
    {
        return;
    }

    for (auto& entry : propagation_transients_)
    {
        if (hashesEqual(entry.transient_id, transient_id, reticulum::kFullHashSize))
        {
            entry.seen_s = currentTimestampSeconds();
            entry.delivered = entry.delivered || delivered;
            return;
        }
    }

    if (propagation_transients_.size() >= kMaxPropagationTransients)
    {
        propagation_transients_.erase(propagation_transients_.begin());
    }

    PropagationTransientEntry entry{};
    memcpy(entry.transient_id, transient_id, sizeof(entry.transient_id));
    entry.seen_s = currentTimestampSeconds();
    entry.delivered = delivered;
    propagation_transients_.push_back(entry);
}

void LxmfAdapter::cullPropagationState()
{
    const uint32_t now_s = currentTimestampSeconds();

    propagation_entries_.erase(
        std::remove_if(propagation_entries_.begin(),
                       propagation_entries_.end(),
                       [now_s](const PropagationEntry& entry)
                       {
                           return entry.created_s == 0 ||
                                  now_s < entry.created_s ||
                                  (now_s - entry.created_s) > kPropagationEntryTtlS;
                       }),
        propagation_entries_.end());

    propagation_transients_.erase(
        std::remove_if(propagation_transients_.begin(),
                       propagation_transients_.end(),
                       [now_s](const PropagationTransientEntry& entry)
                       {
                           return entry.seen_s == 0 ||
                                  now_s < entry.seen_s ||
                                  (now_s - entry.seen_s) > kPropagationTransientTtlS;
                       }),
        propagation_transients_.end());

    propagation_peers_.erase(
        std::remove_if(propagation_peers_.begin(),
                       propagation_peers_.end(),
                       [now_s](const PropagationPeerState& entry)
                       {
                           return entry.last_seen_s != 0 &&
                                  now_s >= entry.last_seen_s &&
                                  (now_s - entry.last_seen_s) > kPropagationEntryTtlS;
                       }),
        propagation_peers_.end());
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

    if (isLocalDestinationHash(packet.destination_hash, nullptr))
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
        rememberReversePath(proof_hash, path->hops);
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

bool LxmfAdapter::buildSignedMessagePacket(const PeerInfo& peer,
                                          const uint8_t* packed_payload, size_t packed_payload_len,
                                          uint8_t* out_packet, size_t* inout_len,
                                          uint8_t out_message_hash[reticulum::kFullHashSize])
{
    if ((!packed_payload && packed_payload_len != 0) || !out_packet || !inout_len || !out_message_hash)
    {
        return false;
    }

    uint8_t signed_part[kSignedPartMaxLen] = {};
    size_t signed_part_len = sizeof(signed_part);
    if (!buildSignedPart(peer.destination_hash,
                         identity_.destinationHash(),
                         packed_payload,
                         packed_payload_len,
                         signed_part,
                         &signed_part_len,
                         out_message_hash))
    {
        return false;
    }

    uint8_t signature[reticulum::kSignatureSize] = {};
    if (!identity_.sign(signed_part, signed_part_len, signature))
    {
        return false;
    }

    uint8_t lxmf_message[kMaxLxmfMessageLen] = {};
    size_t lxmf_message_len = sizeof(lxmf_message);
    if (!packMessage(peer.destination_hash,
                     identity_.destinationHash(),
                     signature,
                     packed_payload,
                     packed_payload_len,
                     lxmf_message,
                     &lxmf_message_len))
    {
        return false;
    }

    return buildEncryptedPacketForPeer(peer, lxmf_message, lxmf_message_len, out_packet, inout_len);
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

void LxmfAdapter::rememberReversePath(const uint8_t proof_hash[reticulum::kTruncatedHashSize],
                                      uint8_t expected_hops)
{
    if (!proof_hash)
    {
        return;
    }

    for (auto& entry : reverse_table_)
    {
        if (hashesEqual(entry.proof_hash, proof_hash, sizeof(entry.proof_hash)))
        {
            entry.expected_hops = expected_hops;
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
    entry.expected_hops = expected_hops;
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

    cullLinkSessions();
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

void LxmfAdapter::localDestinationHash(LocalDestinationKind kind,
                                       uint8_t out_hash[reticulum::kTruncatedHashSize]) const
{
    if (!out_hash)
    {
        return;
    }

    uint8_t name_hash[reticulum::kNameHashSize] = {};
    if (kind == LocalDestinationKind::Propagation)
    {
        reticulum::computeNameHash("lxmf", "propagation", name_hash);
    }
    else
    {
        reticulum::computeNameHash("lxmf", "delivery", name_hash);
    }
    reticulum::computeDestinationHash(name_hash, identity_.identityHash(), out_hash);
}

bool LxmfAdapter::isLocalDestinationHash(const uint8_t hash[reticulum::kTruncatedHashSize],
                                         LocalDestinationKind* out_kind) const
{
    if (!hash)
    {
        return false;
    }

    uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
    localDestinationHash(LocalDestinationKind::Delivery, delivery_hash);
    if (hashesEqual(hash, delivery_hash, sizeof(delivery_hash)))
    {
        if (out_kind)
        {
            *out_kind = LocalDestinationKind::Delivery;
        }
        return true;
    }

    uint8_t propagation_hash[reticulum::kTruncatedHashSize] = {};
    localDestinationHash(LocalDestinationKind::Propagation, propagation_hash);
    if (hashesEqual(hash, propagation_hash, sizeof(propagation_hash)))
    {
        if (out_kind)
        {
            *out_kind = LocalDestinationKind::Propagation;
        }
        return true;
    }

    return false;
}

uint16_t LxmfAdapter::linkMduForMtu(uint16_t mtu)
{
    const size_t encrypted_payload_budget =
        (mtu > reticulum::kPacketHeader1Size) ? (mtu - reticulum::kPacketHeader1Size) : 0;
    if (encrypted_payload_budget <= reticulum::kTokenOverhead)
    {
        return 0;
    }

    const size_t token_budget = encrypted_payload_budget - reticulum::kTokenOverhead;
    const size_t padded = (token_budget / 16U) * 16U;
    if (padded == 0)
    {
        return 0;
    }
    return static_cast<uint16_t>(padded - 1U);
}

bool LxmfAdapter::generateLinkSigningKey(uint8_t out_pub[LxmfIdentity::kSigPubKeySize],
                                         uint8_t out_priv[LxmfIdentity::kSigPrivKeySize])
{
    if (!out_pub || !out_priv)
    {
        return false;
    }

    uint8_t seed[32] = {};
    fillRandomBytes(seed, sizeof(seed));
    ed25519_create_keypair(out_pub, out_priv, seed);
    memset(seed, 0, sizeof(seed));
    return !isZeroBytes(out_pub, LxmfIdentity::kSigPubKeySize) &&
           !isZeroBytes(out_priv, LxmfIdentity::kSigPrivKeySize);
}

bool LxmfAdapter::signWithKey(const uint8_t sign_pub[LxmfIdentity::kSigPubKeySize],
                              const uint8_t sign_priv[LxmfIdentity::kSigPrivKeySize],
                              const uint8_t* message,
                              size_t message_len,
                              uint8_t out_signature[reticulum::kSignatureSize])
{
    if (!sign_pub || !sign_priv || !message || !out_signature)
    {
        return false;
    }

    ed25519_sign(out_signature, message, message_len, sign_pub, sign_priv);
    return true;
}

bool LxmfAdapter::deriveLinkKey(LinkSession& session)
{
    if (isZeroBytes(session.peer_enc_pub, sizeof(session.peer_enc_pub)) ||
        isZeroBytes(session.local_enc_priv, sizeof(session.local_enc_priv)))
    {
        return false;
    }

    uint8_t shared_secret[LxmfIdentity::kEncPubKeySize] = {};
    memcpy(shared_secret, session.peer_enc_pub, sizeof(shared_secret));
    uint8_t local_priv[LxmfIdentity::kEncPrivKeySize] = {};
    memcpy(local_priv, session.local_enc_priv, sizeof(local_priv));
    if (!Curve25519::dh2(shared_secret, local_priv))
    {
        return false;
    }

    return reticulum::hkdfSha256(shared_secret, sizeof(shared_secret),
                                 session.link_id, sizeof(session.link_id),
                                 nullptr, 0,
                                 session.derived_key, sizeof(session.derived_key));
}

bool LxmfAdapter::encryptLinkPayload(const LinkSession& session,
                                     const uint8_t* plaintext, size_t plaintext_len,
                                     uint8_t* out_payload, size_t* inout_len) const
{
    if (!plaintext || plaintext_len == 0 || !out_payload || !inout_len)
    {
        return false;
    }

    uint8_t iv[reticulum::kTokenIvSize] = {};
    fillRandomBytes(iv, sizeof(iv));
    return reticulum::tokenEncrypt(session.derived_key,
                                   iv,
                                   plaintext,
                                   plaintext_len,
                                   out_payload,
                                   inout_len);
}

bool LxmfAdapter::decryptLinkPayload(const LinkSession& session,
                                     const uint8_t* payload, size_t payload_len,
                                     std::vector<uint8_t>* out_plaintext) const
{
    if (!payload || payload_len == 0 || !out_plaintext)
    {
        return false;
    }

    std::vector<uint8_t> plaintext(reticulum::paddedTokenPlaintextSize(payload_len), 0);
    size_t plaintext_len = plaintext.size();
    if (!reticulum::tokenDecrypt(session.derived_key,
                                 payload,
                                 payload_len,
                                 plaintext.data(),
                                 &plaintext_len))
    {
        return false;
    }

    plaintext.resize(plaintext_len);
    *out_plaintext = std::move(plaintext);
    return true;
}

bool LxmfAdapter::sendLinkPacket(LinkSession& session,
                                 reticulum::PacketType packet_type,
                                 reticulum::PacketContext context,
                                 const uint8_t* payload, size_t payload_len,
                                 bool encrypt_payload)
{
    if (!isReady() || (!payload && payload_len != 0))
    {
        return false;
    }

    uint8_t wire_payload[kMaxPacketLen] = {};
    const uint8_t* effective_payload = payload;
    size_t effective_payload_len = payload_len;
    if (encrypt_payload)
    {
        effective_payload = wire_payload;
        effective_payload_len = sizeof(wire_payload);
        if (!encryptLinkPayload(session, payload, payload_len, wire_payload, &effective_payload_len))
        {
            return false;
        }
    }

    uint8_t packet[kMaxPacketLen] = {};
    size_t packet_len = sizeof(packet);
    if (!reticulum::buildHeader1Packet(packet_type,
                                       reticulum::DestinationType::Link,
                                       context,
                                       false,
                                       session.link_id,
                                       effective_payload,
                                       effective_payload_len,
                                       packet,
                                       &packet_len))
    {
        return false;
    }

    const bool ok = raw_.sendAppData(ChannelId::PRIMARY, 0, packet, packet_len);
    if (ok)
    {
        session.last_outbound_ms = millis();
    }
    return ok;
}

bool LxmfAdapter::sendLinkHandshakeProof(LinkSession& session)
{
    uint8_t signalling[kLinkSignallingLen] = {};
    buildLinkSignallingBytes(session.mtu, signalling);

    uint8_t signed_data[reticulum::kTruncatedHashSize +
                        LxmfIdentity::kEncPubKeySize +
                        LxmfIdentity::kSigPubKeySize +
                        kLinkSignallingLen] = {};
    size_t used = 0;
    memcpy(signed_data + used, session.link_id, reticulum::kTruncatedHashSize);
    used += reticulum::kTruncatedHashSize;
    memcpy(signed_data + used, session.local_enc_pub, LxmfIdentity::kEncPubKeySize);
    used += LxmfIdentity::kEncPubKeySize;
    memcpy(signed_data + used, identity_.signingPublicKey(), LxmfIdentity::kSigPubKeySize);
    used += LxmfIdentity::kSigPubKeySize;
    memcpy(signed_data + used, signalling, sizeof(signalling));
    used += sizeof(signalling);

    uint8_t proof_payload[reticulum::kSignatureSize +
                          LxmfIdentity::kEncPubKeySize +
                          kLinkSignallingLen] = {};
    if (!identity_.sign(signed_data, used, proof_payload))
    {
        return false;
    }
    memcpy(proof_payload + reticulum::kSignatureSize,
           session.local_enc_pub,
           LxmfIdentity::kEncPubKeySize);
    memcpy(proof_payload + reticulum::kSignatureSize + LxmfIdentity::kEncPubKeySize,
           signalling,
           sizeof(signalling));

    return sendLinkPacket(session,
                          reticulum::PacketType::Proof,
                          reticulum::PacketContext::LrProof,
                          proof_payload,
                          sizeof(proof_payload),
                          false);
}

bool LxmfAdapter::sendLinkRtt(LinkSession& session)
{
    uint8_t payload[16] = {};
    size_t payload_len = sizeof(payload);
    if (!packFloat64(static_cast<double>(session.rtt_s), payload, &payload_len))
    {
        return false;
    }
    return sendLinkPacket(session,
                          reticulum::PacketType::Data,
                          reticulum::PacketContext::LrRtt,
                          payload,
                          payload_len,
                          true);
}

bool LxmfAdapter::sendLinkKeepaliveAck(LinkSession& session)
{
    const uint8_t ack = 0xFE;
    return sendLinkPacket(session,
                          reticulum::PacketType::Data,
                          reticulum::PacketContext::Keepalive,
                          &ack,
                          sizeof(ack),
                          false);
}

bool LxmfAdapter::sendLinkPacketProof(LinkSession& session,
                                      const uint8_t* raw_packet, size_t raw_len)
{
    if (!raw_packet || raw_len == 0)
    {
        return false;
    }

    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    reticulum::computePacketHash(raw_packet, raw_len, packet_hash);

    uint8_t proof_payload[reticulum::kFullHashSize + reticulum::kSignatureSize] = {};
    memcpy(proof_payload, packet_hash, sizeof(packet_hash));
    bool signed_ok = false;
    if (session.initiator)
    {
        signed_ok = signWithKey(session.local_sig_pub,
                                session.local_sig_priv,
                                packet_hash,
                                sizeof(packet_hash),
                                proof_payload + sizeof(packet_hash));
    }
    else
    {
        signed_ok = identity_.sign(packet_hash,
                                   sizeof(packet_hash),
                                   proof_payload + sizeof(packet_hash));
    }
    if (!signed_ok)
    {
        return false;
    }

    return sendLinkPacket(session,
                          reticulum::PacketType::Proof,
                          reticulum::PacketContext::None,
                          proof_payload,
                          sizeof(proof_payload),
                          false);
}

bool LxmfAdapter::sendLinkResponse(LinkSession& session,
                                   const uint8_t* request_id,
                                   size_t request_id_len,
                                   const uint8_t* packed_data,
                                   size_t packed_data_len,
                                   bool data_is_nil)
{
    const size_t response_capacity = request_id_len + packed_data_len + 32;
    std::vector<uint8_t> response_payload(response_capacity, 0);
    size_t response_len = response_payload.size();
    if (!encodeLinkResponsePayload(request_id,
                                   request_id_len,
                                   packed_data,
                                   packed_data_len,
                                   data_is_nil,
                                   response_payload.data(),
                                   &response_len))
    {
        return false;
    }

    response_payload.resize(response_len);
    if (response_len <= session.mdu)
    {
        return sendLinkPacket(session,
                              reticulum::PacketType::Data,
                              reticulum::PacketContext::Response,
                              response_payload.data(),
                              response_payload.size(),
                              true);
    }

    return queueOutgoingResource(session,
                                 response_payload.data(),
                                 response_payload.size(),
                                 kResourceFlagResponse,
                                 request_id,
                                 request_id_len);
}

bool LxmfAdapter::advertiseLinkResource(LinkSession& session,
                                        LinkResourceTransfer& resource,
                                        uint32_t hashmap_segment)
{
    const size_t segment_capacity = resourceHashmapSegmentCapacity(session.mdu);
    const size_t start_hash = static_cast<size_t>(hashmap_segment) * segment_capacity;
    if (segment_capacity == 0 || start_hash >= resource.part_count)
    {
        return false;
    }

    const size_t remaining_hashes = static_cast<size_t>(resource.part_count) - start_hash;
    size_t slice_hashes = std::min(segment_capacity, remaining_hashes);
    while (slice_hashes > 0)
    {
        const size_t slice_offset = start_hash * kResourceMapHashLen;
        const size_t slice_len = slice_hashes * kResourceMapHashLen;

        uint8_t advertisement[kMaxPacketLen] = {};
        size_t advertisement_len = sizeof(advertisement);
        if (encodeResourceAdvertisement(resource.transfer_size,
                                        resource.data_size,
                                        resource.part_count,
                                        resource.resource_hash,
                                        resource.random_hash,
                                        resource.original_hash,
                                        1,
                                        1,
                                        resource.request_id.empty() ? nullptr : resource.request_id.data(),
                                        resource.request_id.size(),
                                        resource.flags,
                                        resource.hashmap.data() + slice_offset,
                                        slice_len,
                                        advertisement,
                                        &advertisement_len) &&
            advertisement_len <= session.mdu)
        {
            resource.last_activity_ms = millis();
            return sendLinkPacket(session,
                                  reticulum::PacketType::Data,
                                  reticulum::PacketContext::ResourceAdv,
                                  advertisement,
                                  advertisement_len,
                                  true);
        }

        --slice_hashes;
    }

    return false;
}

bool LxmfAdapter::queueOutgoingResource(LinkSession& session,
                                        const uint8_t* data, size_t len,
                                        uint8_t flags,
                                        const uint8_t* request_id,
                                        size_t request_id_len)
{
    if (!data || len == 0 || session.mdu == 0 || (request_id_len != 0 && !request_id))
    {
        return false;
    }

    std::vector<uint8_t> stream(kResourceDataPrefixLen + len, 0);
    fillRandomBytes(stream.data(), kResourceDataPrefixLen);
    memcpy(stream.data() + kResourceDataPrefixLen, data, len);

    const size_t encrypted_capacity = reticulum::tokenSizeForPlaintext(stream.size());
    std::vector<uint8_t> encrypted_stream(encrypted_capacity, 0);
    size_t encrypted_len = encrypted_stream.size();
    uint8_t iv[reticulum::kTokenIvSize] = {};
    fillRandomBytes(iv, sizeof(iv));
    if (!reticulum::tokenEncrypt(session.derived_key,
                                 iv,
                                 stream.data(),
                                 stream.size(),
                                 encrypted_stream.data(),
                                 &encrypted_len))
    {
        return false;
    }
    encrypted_stream.resize(encrypted_len);

    const size_t part_count =
        (encrypted_stream.size() + static_cast<size_t>(session.mdu) - 1U) / static_cast<size_t>(session.mdu);
    if (part_count == 0)
    {
        return false;
    }

    const size_t segment_capacity = resourceHashmapSegmentCapacity(session.mdu);
    const size_t collision_guard = (kResourceWindowSize * 2U) + segment_capacity;

    LinkResourceTransfer resource{};
    if (request_id_len != 0)
    {
        resource.request_id.assign(request_id, request_id + request_id_len);
    }
    resource.data_size = static_cast<uint32_t>(len);
    resource.transfer_size = static_cast<uint32_t>(encrypted_stream.size());
    resource.part_count = static_cast<uint32_t>(part_count);
    resource.hashmap_height = resource.part_count;
    resource.window_size = kResourceWindowSize;
    resource.created_ms = millis();
    resource.last_activity_ms = resource.created_ms;
    resource.flags = flags | kResourceFlagEncrypted;
    resource.incoming = false;
    resource.encrypted = true;
    resource.parts.resize(part_count);
    resource.map_hashes.resize(part_count);
    resource.map_hash_known.assign(part_count, 1);
    resource.received_bitmap.assign(part_count, 0);

    bool mapped = false;
    for (size_t attempt = 0; attempt < 8 && !mapped; ++attempt)
    {
        fillRandomBytes(resource.random_hash, sizeof(resource.random_hash));

        std::vector<uint8_t> hash_material(len + sizeof(resource.random_hash), 0);
        memcpy(hash_material.data(), data, len);
        memcpy(hash_material.data() + len, resource.random_hash, sizeof(resource.random_hash));
        reticulum::fullHash(hash_material.data(),
                            hash_material.size(),
                            resource.resource_hash);
        memcpy(resource.original_hash, resource.resource_hash, sizeof(resource.original_hash));

        std::vector<uint8_t> proof_material(len + reticulum::kFullHashSize, 0);
        memcpy(proof_material.data(), data, len);
        memcpy(proof_material.data() + len,
               resource.resource_hash,
               reticulum::kFullHashSize);
        reticulum::fullHash(proof_material.data(),
                            proof_material.size(),
                            resource.expected_proof);

        resource.hashmap.clear();
        std::vector<std::array<uint8_t, kResourceMapHashLen>> recent_hashes;
        recent_hashes.reserve(collision_guard);
        bool collision = false;

        for (size_t index = 0; index < part_count; ++index)
        {
            const size_t offset = index * static_cast<size_t>(session.mdu);
            const size_t chunk_len =
                std::min(static_cast<size_t>(session.mdu), encrypted_stream.size() - offset);

            resource.parts[index].assign(encrypted_stream.begin() + offset,
                                         encrypted_stream.begin() + offset + chunk_len);

            std::vector<uint8_t> map_material(chunk_len + sizeof(resource.random_hash), 0);
            memcpy(map_material.data(), resource.parts[index].data(), chunk_len);
            memcpy(map_material.data() + chunk_len,
                   resource.random_hash,
                   sizeof(resource.random_hash));

            uint8_t full_hash[reticulum::kFullHashSize] = {};
            reticulum::fullHash(map_material.data(), map_material.size(), full_hash);

            std::array<uint8_t, kResourceMapHashLen> map_hash{};
            memcpy(map_hash.data(), full_hash, map_hash.size());
            if (std::find(recent_hashes.begin(), recent_hashes.end(), map_hash) != recent_hashes.end())
            {
                collision = true;
                break;
            }

            resource.map_hashes[index] = map_hash;
            resource.hashmap.insert(resource.hashmap.end(), map_hash.begin(), map_hash.end());
            recent_hashes.push_back(map_hash);
            if (recent_hashes.size() > collision_guard)
            {
                recent_hashes.erase(recent_hashes.begin());
            }
        }

        mapped = !collision;
    }

    if (!mapped)
    {
        return false;
    }

    session.outgoing_resources.push_back(std::move(resource));
    if (!advertiseLinkResource(session, session.outgoing_resources.back(), 0))
    {
        session.outgoing_resources.pop_back();
        return false;
    }

    return true;
}

void LxmfAdapter::closeLinkSession(LinkSession& session)
{
    session.state = LinkState::Closed;
    session.last_inbound_ms = millis();
}

LxmfAdapter::LinkSession* LxmfAdapter::findLinkSession(
    const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    if (!link_id)
    {
        return nullptr;
    }

    for (auto& session : link_sessions_)
    {
        if (hashesEqual(session.link_id, link_id, sizeof(session.link_id)))
        {
            return &session;
        }
    }
    return nullptr;
}

LxmfAdapter::LinkSession* LxmfAdapter::findActiveLinkSessionByDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    LocalDestinationKind kind)
{
    if (!destination_hash)
    {
        return nullptr;
    }

    for (auto& session : link_sessions_)
    {
        if (session.state == LinkState::Active &&
            session.destination == kind &&
            hashesEqual(session.remote_destination_hash,
                        destination_hash,
                        sizeof(session.remote_destination_hash)))
        {
            return &session;
        }
    }
    return nullptr;
}

void LxmfAdapter::cullLinkSessions()
{
    const uint32_t now_ms = millis();

    for (auto& session : link_sessions_)
    {
        session.pending_requests.erase(
            std::remove_if(session.pending_requests.begin(),
                           session.pending_requests.end(),
                           [now_ms](const LinkPendingRequest& request)
                           {
                               return request.created_ms == 0 ||
                                      (now_ms - request.created_ms) > kLinkRequestTtlMs;
                           }),
            session.pending_requests.end());

        auto cull_resources = [now_ms](std::vector<LinkResourceTransfer>& resources)
        {
            resources.erase(
                std::remove_if(resources.begin(),
                               resources.end(),
                               [now_ms](const LinkResourceTransfer& resource)
                               {
                                   return resource.last_activity_ms == 0 ||
                                          (now_ms - resource.last_activity_ms) > kResourceTransferTtlMs;
                               }),
                resources.end());
        };
        cull_resources(session.incoming_resources);
        cull_resources(session.outgoing_resources);
    }

    link_sessions_.erase(
        std::remove_if(link_sessions_.begin(), link_sessions_.end(),
                       [now_ms](const LinkSession& session)
                       {
                           const uint32_t last_activity =
                               std::max(session.last_inbound_ms, session.last_outbound_ms);
                           const uint32_t age_ms =
                               (last_activity == 0) ? (now_ms - session.created_ms) : (now_ms - last_activity);

                           if (session.state == LinkState::Closed)
                           {
                               return age_ms > 5000;
                           }
                           if (session.state == LinkState::Pending || session.state == LinkState::Handshake)
                           {
                               return age_ms > kLinkHandshakeTimeoutMs;
                           }
                           return age_ms > kLinkIdleTimeoutMs || (now_ms - session.created_ms) > kLinkSessionTtlMs;
                       }),
        link_sessions_.end());
}

LxmfAdapter::PeerInfo* LxmfAdapter::rememberPeerIdentity(
    const uint8_t combined_pub[reticulum::kCombinedPublicKeySize],
    const char* display_name)
{
    if (!combined_pub)
    {
        return nullptr;
    }

    uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::computeIdentityHash(combined_pub, identity_hash);

    uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", "delivery", name_hash);
    reticulum::computeDestinationHash(name_hash, identity_hash, delivery_hash);

    PeerInfo& peer = upsertPeer(delivery_hash);
    copyHash(peer.identity_hash, identity_hash, sizeof(peer.identity_hash));
    memcpy(peer.enc_pub, combined_pub, LxmfIdentity::kEncPubKeySize);
    memcpy(peer.sig_pub,
           combined_pub + LxmfIdentity::kEncPubKeySize,
           LxmfIdentity::kSigPubKeySize);
    peer.last_seen_s = currentTimestampSeconds();
    if (display_name && display_name[0] != '\0')
    {
        copyCString(peer.display_name, sizeof(peer.display_name), display_name);
    }
    publishPeerUpdate(peer);
    (void)persistPeers();
    return &peer;
}

bool LxmfAdapter::acceptVerifiedEnvelope(const uint8_t* plaintext, size_t plaintext_len,
                                         const uint8_t* raw_packet, size_t raw_len)
{
    (void)raw_packet;
    (void)raw_len;

    if (!plaintext || plaintext_len == 0)
    {
        return false;
    }

    DecodedEnvelope envelope{};
    if (!unpackMessageEnvelope(plaintext, plaintext_len, &envelope))
    {
        return false;
    }
    if (!hashesEqual(envelope.destination_hash,
                     identity_.destinationHash(),
                     reticulum::kTruncatedHashSize))
    {
        return false;
    }

    const PeerInfo* peer = findPeerByDestinationHash(envelope.source_hash);
    if (!peer)
    {
        return false;
    }

    const size_t signed_part_required =
        (reticulum::kTruncatedHashSize * 2) +
        envelope.packed_payload.size() +
        reticulum::kFullHashSize;
    std::vector<uint8_t> signed_part(signed_part_required);
    size_t signed_part_len = signed_part.size();
    uint8_t message_hash[reticulum::kFullHashSize] = {};
    if (!buildSignedPart(envelope.destination_hash,
                         envelope.source_hash,
                         envelope.packed_payload.data(),
                         envelope.packed_payload.size(),
                         signed_part.data(),
                         &signed_part_len,
                         message_hash))
    {
        return false;
    }

    if (!LxmfIdentity::verify(peer->sig_pub,
                              envelope.signature,
                              signed_part.data(),
                              signed_part_len))
    {
        return false;
    }

    DecodedAppData app_payload{};
    if (decodeAppDataPayload(envelope.packed_payload.data(), envelope.packed_payload.size(), &app_payload))
    {
        MeshIncomingData incoming;
        incoming.portnum = app_payload.portnum;
        incoming.from = peer->node_id;
        incoming.to = identity_.nodeId();
        incoming.packet_id = app_payload.packet_id;
        incoming.request_id = app_payload.request_id;
        incoming.channel = ChannelId::PRIMARY;
        incoming.want_response = app_payload.want_response;
        incoming.payload = std::move(app_payload.payload);
        populateRxMeta(&incoming.rx_meta);
        data_receive_queue_.push(std::move(incoming));
        return true;
    }

    DecodedTextPayload text_payload{};
    if (!unpackTextPayload(envelope.packed_payload.data(), envelope.packed_payload.size(), &text_payload))
    {
        return false;
    }

    MeshIncomingText incoming;
    incoming.channel = ChannelId::PRIMARY;
    incoming.from = peer->node_id;
    incoming.to = identity_.nodeId();
    incoming.msg_id = messageIdFromHash(message_hash);
    incoming.timestamp = currentTimestampSeconds();
    incoming.text = std::move(text_payload.content);
    incoming.hop_limit = 0xFF;
    incoming.encrypted = true;
    populateRxMeta(&incoming.rx_meta);
    text_receive_queue_.push(std::move(incoming));
    return true;
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

void LxmfAdapter::populateRxMeta(RxMeta* out) const
{
    if (!out)
    {
        return;
    }

    out->rx_timestamp_ms = millis();
    out->rx_timestamp_s = currentTimestampSeconds();
    out->time_source = is_valid_epoch(out->rx_timestamp_s)
                           ? RxTimeSource::DeviceUtc
                           : RxTimeSource::Uptime;
    out->origin = RxOrigin::Mesh;
    out->direct = true;
    out->from_is = false;
    out->rssi_dbm_x10 = static_cast<int16_t>(lround(raw_.lastRxRssi() * 10.0f));
    out->snr_db_x10 = static_cast<int16_t>(lround(raw_.lastRxSnr() * 10.0f));
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
