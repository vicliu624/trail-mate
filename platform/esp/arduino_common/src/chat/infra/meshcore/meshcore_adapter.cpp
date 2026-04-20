/**
 * @file meshcore_adapter.cpp
 * @brief MeshCore protocol adapter implementation
 */

#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_adapter.h"
#include "../../internal/blob_store_io.h"
#include "chat/domain/contact_types.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "chat/time_utils.h"
#include "sys/event_bus.h"
#include <AES.h>
#include <Arduino.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <SHA256.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace chat
{
namespace meshcore
{

namespace
{
constexpr uint8_t kRouteTypeTransportFlood = 0x00;
constexpr uint8_t kRouteTypeFlood = 0x01;
constexpr uint8_t kRouteTypeDirect = 0x02;
constexpr uint8_t kRouteTypeTransportDirect = 0x03;
constexpr uint8_t kPayloadTypeReq = 0x00;
constexpr uint8_t kPayloadTypeResponse = 0x01;
constexpr uint8_t kPayloadTypeRawCustom = 0x0F;
constexpr uint8_t kPayloadTypeTxtMsg = 0x02;
constexpr uint8_t kPayloadTypeGrpTxt = 0x05;
constexpr uint8_t kPayloadTypeGrpData = 0x06;
constexpr uint8_t kPayloadTypeAnonReq = 0x07;
// Trail-Mate extension: reuse on-air type 0x07 for direct app-data peer envelope.
constexpr uint8_t kPayloadTypeDirectData = kPayloadTypeAnonReq;
constexpr uint8_t kPayloadTypeAck = 0x03;
constexpr uint8_t kPayloadTypeAdvert = 0x04;
constexpr uint8_t kPayloadTypePath = 0x08;
constexpr uint8_t kPayloadTypeTrace = 0x09;
constexpr uint8_t kPayloadTypeMultipart = 0x0A;
constexpr uint8_t kPayloadTypeControl = 0x0B;
constexpr uint8_t kPayloadVer1 = 0x00;
constexpr size_t kMeshcorePathHashSize = 1;
constexpr size_t kMeshcoreMaxPathSize = 64;
constexpr size_t kMeshcoreMaxFrameSize = 255;
constexpr size_t kMeshcoreMaxPayloadSize = 184;
constexpr size_t kMaxScheduledFrames = 24;
constexpr size_t kMaxSeenPackets = 128;
constexpr uint32_t kSeenTtlMs = 60000;
constexpr size_t kMaxPeerRoutes = 128;
constexpr uint32_t kPeerRouteEntryTtlMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kPeerPathTtlMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kRoutePenaltyBlackoutMs = 30UL * 1000UL;
constexpr uint32_t kAckDelayMs = 120;
constexpr uint32_t kAckSpacingMs = 300;
constexpr size_t kCipherBlockSize = 16;
constexpr size_t kCipherMacSize = 2;
constexpr size_t kCipherKeySize = 16;
constexpr size_t kCipherHmacKeySize = 32;
constexpr size_t kGroupPlainPrefixSize = 5;
constexpr uint8_t kTxtTypePlain = 0x00;
constexpr uint8_t kPathExtraNone = 0xFF;
constexpr uint32_t kPathResponseDelayMs = 300;
constexpr uint32_t kPathReciprocalDelayMs = 500;
constexpr NodeId kSyntheticNodePrefix = 0x4D430000UL;
constexpr uint32_t kAppAckTimeoutMs = 15000;
constexpr size_t kMaxPendingAppAcks = 32;
constexpr uint32_t kKeyVerifySessionTtlMs = 60000;
constexpr uint32_t kSendTimeoutBaseMs = 500;
constexpr float kFloodSendTimeoutFactor = 16.0f;
constexpr float kDirectSendPerhopFactor = 6.0f;
constexpr uint32_t kDirectSendPerhopExtraMs = 250;

constexpr uint8_t kPublicGroupPsk[16] = {
    0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
    0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};

constexpr uint8_t kTxtTypeSigned = 0x02;
constexpr uint8_t kDirectAppMagic0 = 0xDA;
constexpr uint8_t kDirectAppMagic1 = 0x7A;
constexpr uint8_t kDirectAppFlagWantAck = 0x01;
constexpr uint8_t kGroupDataMagic0 = 0x47; // 'G'
constexpr uint8_t kGroupDataMagic1 = 0x44; // 'D'
constexpr uint8_t kLoraSyncWordPrivate = 0x12;
constexpr uint8_t kControlMagic0 = 0x54; // 'T'
constexpr uint8_t kControlMagic1 = 0x4D; // 'M'
constexpr uint8_t kControlKindNodeInfo = 0x01;
constexpr uint8_t kControlKindKeyVerify = 0x02;
constexpr uint8_t kNodeInfoTypeQuery = 0x01;
constexpr uint8_t kNodeInfoTypeInfo = 0x02;
constexpr uint8_t kNodeInfoFlagRequestReply = 0x01;
constexpr uint8_t kControlSubtypeDiscoverReq = 0x80;
constexpr uint8_t kControlSubtypeDiscoverResp = 0x90;
constexpr uint8_t kControlSubtypeMask = 0xF0;
constexpr uint8_t kDiscoverPrefixOnlyMask = 0x01;
constexpr uint8_t kKeyVerifyTypeInit = 0x01;
constexpr uint8_t kKeyVerifyTypeReady = 0x02;
constexpr uint8_t kKeyVerifyTypeFinal = 0x03;
constexpr uint32_t kNodeInfoPortnum = 4;
constexpr uint32_t kKeyVerifyPortnum = 12;
constexpr size_t kNodeInfoShortNameFieldSize = 10;
constexpr size_t kNodeInfoLongNameFieldSize = 32;
constexpr uint8_t kAdvertTypeNone = 0x00;
constexpr uint8_t kAdvertTypeChat = 0x01;
constexpr uint8_t kAdvertTypeRepeater = 0x02;
constexpr uint8_t kAdvertTypeRoom = 0x03;
constexpr uint8_t kAdvertTypeSensor = 0x04;
constexpr uint8_t kDiscoverTypeFilterAll =
    static_cast<uint8_t>((1U << kAdvertTypeChat) |
                         (1U << kAdvertTypeRepeater) |
                         (1U << kAdvertTypeRoom) |
                         (1U << kAdvertTypeSensor));
constexpr uint8_t kAdvertFlagHasLocation = 0x10;
constexpr uint8_t kAdvertFlagHasFeature1 = 0x20;
constexpr uint8_t kAdvertFlagHasFeature2 = 0x40;
constexpr uint8_t kAdvertFlagHasName = 0x80;
constexpr size_t kMeshcorePubKeySize = 32;
constexpr size_t kMeshcorePubKeyPrefixSize = 8;
constexpr size_t kAdvertSignatureSize = 64;
constexpr size_t kAdvertMinPayloadSize =
    kMeshcorePubKeySize + sizeof(uint32_t) + kAdvertSignatureSize;

struct PersistedPeerPubKeyEntryV1
{
    uint8_t peer_hash;
    uint8_t flags;
    uint16_t reserved;
    uint8_t pubkey[kMeshcorePubKeySize];
} __attribute__((packed));

constexpr uint8_t kPersistedPeerFlagVerified = 0x01;

template <typename T>
T clampValue(T value, T min_value, T max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

#ifndef MESHCORE_LOG_ENABLE
#define MESHCORE_LOG_ENABLE 0
#endif

#if MESHCORE_LOG_ENABLE
#define MESHCORE_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define MESHCORE_LOG(...) \
    do                    \
    {                     \
    } while (0)
#endif

using chat::meshcore::buildHeader;
using chat::meshcore::computeChannelHash;
using chat::meshcore::computeRxDelayMs;
using chat::meshcore::estimateLoRaAirtimeMs;
using chat::meshcore::estimateSendTimeoutMs;
using chat::meshcore::hashFrame;
using chat::meshcore::isZeroKey;
using chat::meshcore::packetSignature;
using chat::meshcore::ParsedPacket;
using chat::meshcore::parsePacket;
using chat::meshcore::saturatingAddU32;
using chat::meshcore::scoreFromSnr;
using chat::meshcore::sha256Trunc;
using chat::meshcore::sharedSecretToKeys;
using chat::meshcore::toHex;
using chat::meshcore::toHmacKey32;

using chat::meshcore::aesDecrypt;
using chat::meshcore::aesEncrypt;
using chat::meshcore::encryptThenMac;
using chat::meshcore::macThenDecrypt;
using chat::meshcore::trimTrailingZeros;

using chat::meshcore::buildFrameNoTransport;
using chat::meshcore::buildPeerDatagramPayload;
using chat::meshcore::copyPrintableAscii;
using chat::meshcore::copySanitizedName;
using chat::meshcore::decodeAdvertAppData;
using chat::meshcore::DecodedAdvertAppData;
using chat::meshcore::DecodedDirectAppPayload;
using chat::meshcore::DecodedDiscoverRequest;
using chat::meshcore::DecodedDiscoverResponse;
using chat::meshcore::DecodedGroupAppPayload;
using chat::meshcore::decodeDirectAppPayload;
using chat::meshcore::decodeDiscoverRequest;
using chat::meshcore::decodeDiscoverResponse;
using chat::meshcore::decodeGroupAppPayload;
using chat::meshcore::deriveNodeIdFromPubkey;
using chat::meshcore::discoverFilterMatchesType;
using chat::meshcore::formatVerificationCode;
using chat::meshcore::hasControlPrefix;
using chat::meshcore::isAnonReqCipherShape;
using chat::meshcore::isPeerCipherShape;
using chat::meshcore::isPeerPayloadType;
using chat::meshcore::mapAdvertTypeToRole;
using chat::meshcore::selectChannelKey;
using chat::meshcore::shouldUsePublicChannelFallback;
using chat::meshcore::xorCrypt;

bool buildPathPlain(const uint8_t* out_path, size_t out_path_len,
                    uint8_t extra_type, const uint8_t* extra, size_t extra_len,
                    uint8_t* out_plain, size_t out_cap, size_t* out_len)
{
    if (!out_plain || !out_len || out_path_len > kMeshcoreMaxPathSize ||
        (out_path_len > 0 && !out_path))
    {
        return false;
    }

    size_t index = 0;
    if (index + 1 + out_path_len + 1 > out_cap)
    {
        return false;
    }
    out_plain[index++] = static_cast<uint8_t>(out_path_len);
    if (out_path_len > 0)
    {
        memcpy(&out_plain[index], out_path, out_path_len);
        index += out_path_len;
    }

    if (extra && extra_len > 0)
    {
        if (index + 1 + extra_len > out_cap)
        {
            return false;
        }
        out_plain[index++] = static_cast<uint8_t>(extra_type & 0x0F);
        memcpy(&out_plain[index], extra, extra_len);
        index += extra_len;
    }
    else
    {
        if (index + 5 > out_cap)
        {
            return false;
        }
        out_plain[index++] = kPathExtraNone;
        const uint32_t nonce = static_cast<uint32_t>(esp_random());
        memcpy(&out_plain[index], &nonce, sizeof(nonce));
        index += sizeof(nonce);
    }

    *out_len = index;
    return true;
}

} // namespace

MeshCoreAdapter::MeshCoreAdapter(LoraBoard& board)
    : board_(board),
      initialized_(false),
      last_raw_packet_len_(0),
      has_pending_raw_packet_(false),
      next_msg_id_(1),
      min_tx_interval_ms_(0),
      last_tx_ms_(0),
      encrypt_mode_(0),
      pki_enabled_(false),
      node_id_(0),
      self_hash_(0),
      last_rx_rssi_(NAN),
      last_rx_snr_(NAN),
      last_noise_floor_dbm_(0),
      tx_airtime_ms_(0),
      rx_airtime_ms_(0)
{
    const uint64_t raw = ESP.getEfuseMac();
    const uint8_t* mac = reinterpret_cast<const uint8_t*>(&raw);
    node_id_ = (static_cast<uint32_t>(mac[2]) << 24) |
               (static_cast<uint32_t>(mac[3]) << 16) |
               (static_cast<uint32_t>(mac[4]) << 8) |
               static_cast<uint32_t>(mac[5]);
    self_hash_ = static_cast<uint8_t>(node_id_ & 0xFF);
}

MeshCapabilities MeshCoreAdapter::getCapabilities() const
{
    MeshCapabilities caps;
    caps.supports_unicast_text = true;
    caps.supports_unicast_appdata = true;
    caps.supports_broadcast_appdata = true;
    caps.supports_appdata_ack = true;
    caps.provides_appdata_sender = true;
    caps.supports_node_info = true;
    caps.supports_pki = true;
    caps.supports_discovery_actions = true;
    return caps;
}

bool MeshCoreAdapter::pollEvent(Event* out)
{
    if (!out || events_.empty())
    {
        return false;
    }
    *out = std::move(events_.front());
    events_.pop_front();
    return true;
}

MeshCoreAdapter::RadioStats MeshCoreAdapter::getRadioStats() const
{
    RadioStats stats;
    stats.noise_floor_dbm = last_noise_floor_dbm_;
    stats.tx_airtime_ms = tx_airtime_ms_;
    stats.rx_airtime_ms = rx_airtime_ms_;
    return stats;
}

bool MeshCoreAdapter::ensurePeerPublicKey(const uint8_t* pubkey, size_t len, bool verified)
{
    if (!pubkey || len != MeshCoreIdentity::kPubKeySize)
    {
        return false;
    }
    const uint32_t now_ms = millis();
    rememberPeerPubKey(pubkey, now_ms, verified);
    const NodeId node = deriveNodeIdFromPubkey(pubkey, len);
    if (node != 0)
    {
        rememberPeerNodeId(pubkey[0], node, now_ms);
    }
    return true;
}

bool MeshCoreAdapter::sendPeerRequestType(const uint8_t* pubkey, size_t len, uint8_t req_type,
                                          uint32_t* out_tag, uint32_t* out_est_timeout,
                                          bool* out_sent_flood)
{
    uint8_t payload[9] = {};
    payload[0] = req_type;
    memset(payload + 1, 0, 4);
    uint32_t nonce = static_cast<uint32_t>(esp_random());
    memcpy(payload + 5, &nonce, sizeof(nonce));
    return sendPeerRequestPayload(pubkey, len, payload, sizeof(payload), false,
                                  out_tag, out_est_timeout, out_sent_flood);
}

bool MeshCoreAdapter::sendSelfAdvert(bool broadcast)
{
    return sendIdentityAdvert(broadcast);
}

bool MeshCoreAdapter::sendPeerRequestPayload(const uint8_t* pubkey, size_t len,
                                             const uint8_t* payload, size_t payload_len,
                                             bool force_flood,
                                             uint32_t* out_tag, uint32_t* out_est_timeout,
                                             bool* out_sent_flood)
{
    if (!pubkey || len != MeshCoreIdentity::kPubKeySize || !payload || payload_len == 0)
    {
        return false;
    }
    if (!identity_.isReady())
    {
        return false;
    }

    const uint32_t now_ms = millis();
    prunePeerRoutes(now_ms);
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        return false;
    }

    ensurePeerPublicKey(pubkey, len, false);
    const uint8_t peer_hash = pubkey[0];

    uint8_t shared_secret[kCipherHmacKeySize] = {};
    if (!identity_.deriveSharedSecret(pubkey, shared_secret))
    {
        return false;
    }

    uint8_t key16[kCipherKeySize] = {};
    uint8_t key32[kCipherHmacKeySize] = {};
    sharedSecretToKeys(shared_secret, key16, key32);

    uint8_t plain[kMeshcoreMaxPayloadSize] = {};
    size_t plain_len = 0;
    const uint32_t tag = now_message_timestamp();
    memcpy(plain + plain_len, &tag, sizeof(tag));
    plain_len += sizeof(tag);
    if (plain_len + payload_len > sizeof(plain))
    {
        return false;
    }
    memcpy(plain + plain_len, payload, payload_len);
    plain_len += payload_len;

    uint8_t datagram[kMeshcoreMaxPayloadSize] = {};
    size_t datagram_len = 0;
    if (!buildPeerDatagramPayload(peer_hash, self_hash_,
                                  key16, key32,
                                  plain, plain_len,
                                  datagram, sizeof(datagram), &datagram_len))
    {
        return false;
    }

    const PeerRouteEntry* route = selectPeerRouteByHash(peer_hash, now_ms);
    uint8_t route_type = kRouteTypeFlood;
    const uint8_t* out_path = nullptr;
    size_t out_path_len = 0;
    if (!force_flood && route && route->has_out_path)
    {
        route_type = kRouteTypeDirect;
        out_path = route->out_path;
        out_path_len = route->out_path_len;
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!buildFrameNoTransport(route_type, kPayloadTypeReq,
                               out_path, out_path_len,
                               datagram, datagram_len,
                               frame, sizeof(frame), &frame_len))
    {
        return false;
    }

    bool ok = transmitFrameNow(frame, frame_len, now_ms);
    if (!ok)
    {
        ok = enqueueScheduled(frame, frame_len, 50);
    }

    if (out_tag)
    {
        *out_tag = tag;
    }
    if (out_sent_flood)
    {
        *out_sent_flood = (route_type == kRouteTypeFlood);
    }
    if (out_est_timeout)
    {
        *out_est_timeout = estimateSendTimeoutMs(frame_len,
                                                 (route_type == kRouteTypeDirect) ? out_path_len : 0,
                                                 route_type == kRouteTypeFlood,
                                                 config_.meshcore_bw_khz,
                                                 config_.meshcore_sf,
                                                 config_.meshcore_cr);
    }
    return ok;
}

bool MeshCoreAdapter::sendAnonRequestPayload(const uint8_t* pubkey, size_t len,
                                             const uint8_t* payload, size_t payload_len,
                                             uint32_t* out_est_timeout,
                                             bool* out_sent_flood)
{
    if (!pubkey || len != MeshCoreIdentity::kPubKeySize || !payload || payload_len == 0)
    {
        return false;
    }
    if (!identity_.isReady())
    {
        return false;
    }

    const uint32_t now_ms = millis();
    prunePeerRoutes(now_ms);
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        return false;
    }

    ensurePeerPublicKey(pubkey, len, false);
    const uint8_t peer_hash = pubkey[0];

    uint8_t shared_secret[kCipherHmacKeySize] = {};
    if (!identity_.deriveSharedSecret(pubkey, shared_secret))
    {
        return false;
    }

    uint8_t key16[kCipherKeySize] = {};
    uint8_t key32[kCipherHmacKeySize] = {};
    sharedSecretToKeys(shared_secret, key16, key32);

    uint8_t cipher[kMeshcoreMaxPayloadSize] = {};
    size_t cipher_len = encryptThenMac(key16, key32,
                                       cipher, sizeof(cipher),
                                       payload, payload_len);
    if (cipher_len == 0)
    {
        return false;
    }

    uint8_t datagram[kMeshcoreMaxPayloadSize] = {};
    size_t datagram_len = 0;
    if (1 + kMeshcorePubKeySize + cipher_len > sizeof(datagram))
    {
        return false;
    }
    datagram[datagram_len++] = peer_hash;
    memcpy(datagram + datagram_len, identity_.publicKey(), kMeshcorePubKeySize);
    datagram_len += kMeshcorePubKeySize;
    memcpy(datagram + datagram_len, cipher, cipher_len);
    datagram_len += cipher_len;

    const PeerRouteEntry* route = selectPeerRouteByHash(peer_hash, now_ms);
    uint8_t route_type = kRouteTypeFlood;
    const uint8_t* out_path = nullptr;
    size_t out_path_len = 0;
    if (route && route->has_out_path)
    {
        route_type = kRouteTypeDirect;
        out_path = route->out_path;
        out_path_len = route->out_path_len;
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!buildFrameNoTransport(route_type, kPayloadTypeAnonReq,
                               out_path, out_path_len,
                               datagram, datagram_len,
                               frame, sizeof(frame), &frame_len))
    {
        return false;
    }

    bool ok = transmitFrameNow(frame, frame_len, now_ms);
    if (!ok)
    {
        ok = enqueueScheduled(frame, frame_len, 50);
    }

    if (out_sent_flood)
    {
        *out_sent_flood = (route_type == kRouteTypeFlood);
    }
    if (out_est_timeout)
    {
        *out_est_timeout = estimateSendTimeoutMs(frame_len,
                                                 (route_type == kRouteTypeDirect) ? out_path_len : 0,
                                                 route_type == kRouteTypeFlood,
                                                 config_.meshcore_bw_khz,
                                                 config_.meshcore_sf,
                                                 config_.meshcore_cr);
    }
    return ok;
}

bool MeshCoreAdapter::sendRawData(const uint8_t* path, size_t path_len,
                                  const uint8_t* payload, size_t payload_len,
                                  uint32_t* out_est_timeout)
{
    if (!payload || payload_len == 0 || path_len > kMeshcoreMaxPathSize ||
        (path_len > 0 && !path))
    {
        return false;
    }

    const uint32_t now_ms = millis();
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        return false;
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!buildFrameNoTransport(kRouteTypeDirect, kPayloadTypeRawCustom,
                               path, path_len,
                               payload, payload_len,
                               frame, sizeof(frame), &frame_len))
    {
        return false;
    }

    bool ok = transmitFrameNow(frame, frame_len, now_ms);
    if (!ok)
    {
        ok = enqueueScheduled(frame, frame_len, 50);
    }
    if (out_est_timeout)
    {
        *out_est_timeout = estimateSendTimeoutMs(frame_len, path_len, false,
                                                 config_.meshcore_bw_khz,
                                                 config_.meshcore_sf,
                                                 config_.meshcore_cr);
    }
    return ok;
}

bool MeshCoreAdapter::sendTracePath(const uint8_t* path, size_t path_len,
                                    uint32_t tag, uint32_t auth, uint8_t flags,
                                    uint32_t* out_est_timeout)
{
    if (!path || path_len == 0 || path_len > kMeshcoreMaxPathSize)
    {
        return false;
    }

    const uint32_t now_ms = millis();
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        return false;
    }

    uint8_t payload[9] = {};
    memcpy(payload, &tag, sizeof(tag));
    memcpy(payload + 4, &auth, sizeof(auth));
    payload[8] = flags;

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!buildFrameNoTransport(kRouteTypeDirect, kPayloadTypeTrace,
                               path, path_len,
                               payload, sizeof(payload),
                               frame, sizeof(frame), &frame_len))
    {
        return false;
    }

    bool ok = transmitFrameNow(frame, frame_len, now_ms);
    if (!ok)
    {
        ok = enqueueScheduled(frame, frame_len, 50);
    }
    if (out_est_timeout)
    {
        *out_est_timeout = estimateSendTimeoutMs(frame_len, path_len, false,
                                                 config_.meshcore_bw_khz,
                                                 config_.meshcore_sf,
                                                 config_.meshcore_cr);
    }
    return ok;
}

bool MeshCoreAdapter::sendControlData(const uint8_t* payload, size_t payload_len)
{
    if (!payload || payload_len == 0 || payload_len > kMeshcoreMaxPayloadSize)
    {
        return false;
    }

    const uint32_t now_ms = millis();
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        return false;
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!buildFrameNoTransport(kRouteTypeDirect, kPayloadTypeControl,
                               nullptr, 0,
                               payload, payload_len,
                               frame, sizeof(frame), &frame_len))
    {
        return false;
    }

    bool ok = transmitFrameNow(frame, frame_len, now_ms);
    if (!ok)
    {
        ok = enqueueScheduled(frame, frame_len, 50);
    }
    return ok;
}

void MeshCoreAdapter::setFloodScopeKey(const uint8_t* key, size_t len)
{
    flood_scope_key_.fill(0);
    if (!key || len == 0)
    {
        return;
    }
    memcpy(flood_scope_key_.data(), key, std::min(len, flood_scope_key_.size()));
}

bool MeshCoreAdapter::sendStoredAdvert(const uint8_t* pubkey, size_t len)
{
    if (!pubkey || len != MeshCoreIdentity::kPubKeySize)
    {
        return false;
    }
    if (identity_.isReady() &&
        memcmp(pubkey, identity_.publicKey(), MeshCoreIdentity::kPubKeySize) == 0)
    {
        return sendIdentityAdvert(true);
    }

    const uint8_t peer_hash = pubkey[0];
    const PeerRouteEntry* entry = findPeerRouteByHash(peer_hash);
    if (!entry || entry->last_advert_len == 0)
    {
        return false;
    }

    const uint32_t now_ms = millis();
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        return false;
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!buildFrameNoTransport(kRouteTypeDirect, kPayloadTypeAdvert,
                               nullptr, 0,
                               entry->last_advert, entry->last_advert_len,
                               frame, sizeof(frame), &frame_len))
    {
        return false;
    }

    bool ok = transmitFrameNow(frame, frame_len, now_ms);
    if (!ok)
    {
        ok = enqueueScheduled(frame, frame_len, 50);
    }
    return ok;
}

bool MeshCoreAdapter::sendIdentityAdvertWithLocation(bool broadcast, bool include_location,
                                                     int32_t lat_i6, int32_t lon_i6)
{
    return sendIdentityAdvert(broadcast, include_location, lat_i6, lon_i6);
}

bool MeshCoreAdapter::resolveGroupSecret(ChannelId channel, uint8_t out_key16[16],
                                         uint8_t out_key32[32], uint8_t* out_hash) const
{
    if (!out_key16 || !out_key32)
    {
        return false;
    }

    const uint8_t* selected = nullptr;
    if (channel == ChannelId::SECONDARY && !isZeroKey(config_.secondary_key, sizeof(config_.secondary_key)))
    {
        selected = config_.secondary_key;
    }
    else if (channel == ChannelId::PRIMARY && !isZeroKey(config_.primary_key, sizeof(config_.primary_key)))
    {
        selected = config_.primary_key;
    }
    else if (!isZeroKey(config_.secondary_key, sizeof(config_.secondary_key)))
    {
        selected = config_.secondary_key;
    }
    else if (!isZeroKey(config_.primary_key, sizeof(config_.primary_key)))
    {
        selected = config_.primary_key;
    }
    else if (shouldUsePublicChannelFallback(config_))
    {
        selected = kPublicGroupPsk;
    }

    if (!selected)
    {
        return false;
    }

    memcpy(out_key16, selected, 16);
    toHmacKey32(out_key16, out_key32);
    if (out_hash)
    {
        *out_hash = computeChannelHash(out_key16);
    }
    return true;
}

ChannelId MeshCoreAdapter::resolveChannelFromHash(uint8_t channel_hash, bool* out_match) const
{
    if (out_match)
    {
        *out_match = false;
    }

    if (!isZeroKey(config_.primary_key, sizeof(config_.primary_key)) &&
        computeChannelHash(config_.primary_key) == channel_hash)
    {
        if (out_match)
        {
            *out_match = true;
        }
        return ChannelId::PRIMARY;
    }
    if (!isZeroKey(config_.secondary_key, sizeof(config_.secondary_key)) &&
        computeChannelHash(config_.secondary_key) == channel_hash)
    {
        if (out_match)
        {
            *out_match = true;
        }
        return ChannelId::SECONDARY;
    }

    if (shouldUsePublicChannelFallback(config_) &&
        computeChannelHash(kPublicGroupPsk) == channel_hash)
    {
        if (out_match)
        {
            *out_match = true;
        }
        return ChannelId::PRIMARY;
    }

    return ChannelId::PRIMARY;
}

MeshCoreAdapter::PeerRouteEntry* MeshCoreAdapter::findPeerRouteByHash(uint8_t peer_hash)
{
    for (auto& entry : peer_routes_)
    {
        if (entry.peer_hash == peer_hash)
        {
            return &entry;
        }
    }
    return nullptr;
}

const MeshCoreAdapter::PeerRouteEntry* MeshCoreAdapter::findPeerRouteByHash(uint8_t peer_hash) const
{
    for (const auto& entry : peer_routes_)
    {
        if (entry.peer_hash == peer_hash)
        {
            return &entry;
        }
    }
    return nullptr;
}

MeshCoreAdapter::PeerRouteEntry* MeshCoreAdapter::selectPeerRouteByHash(uint8_t peer_hash,
                                                                        uint32_t now_ms)
{
    PeerRouteEntry* entry = findPeerRouteByHash(peer_hash);
    if (!entry)
    {
        return nullptr;
    }
    if (!entry->has_out_path || entry->candidate_count == 0)
    {
        return nullptr;
    }
    if (entry->route_blackout_until_ms != 0 &&
        static_cast<int32_t>(now_ms - entry->route_blackout_until_ms) < 0)
    {
        return nullptr;
    }
    return entry;
}

const MeshCoreAdapter::PeerRouteEntry* MeshCoreAdapter::selectPeerRouteByHash(uint8_t peer_hash,
                                                                              uint32_t now_ms) const
{
    const PeerRouteEntry* entry = findPeerRouteByHash(peer_hash);
    if (!entry)
    {
        return nullptr;
    }
    if (!entry->has_out_path || entry->candidate_count == 0)
    {
        return nullptr;
    }
    if (entry->route_blackout_until_ms != 0 &&
        static_cast<int32_t>(now_ms - entry->route_blackout_until_ms) < 0)
    {
        return nullptr;
    }
    return entry;
}

MeshCoreAdapter::PeerRouteEntry& MeshCoreAdapter::upsertPeerRoute(uint8_t peer_hash, uint32_t now_ms)
{
    if (PeerRouteEntry* found = findPeerRouteByHash(peer_hash))
    {
        found->last_seen_ms = now_ms;
        return *found;
    }
    if (peer_routes_.size() >= kMaxPeerRoutes)
    {
        auto oldest = peer_routes_.begin();
        for (auto it = peer_routes_.begin(); it != peer_routes_.end(); ++it)
        {
            if (it->last_seen_ms < oldest->last_seen_ms)
            {
                oldest = it;
            }
        }
        peer_routes_.erase(oldest);
    }
    PeerRouteEntry entry;
    entry.peer_hash = peer_hash;
    entry.last_seen_ms = now_ms;
    peer_routes_.push_back(entry);
    return peer_routes_.back();
}

int16_t MeshCoreAdapter::computePathQuality(uint8_t path_len, int16_t snr_x10,
                                            uint8_t sample_count, uint32_t age_ms)
{
    int score = 1000;
    score -= static_cast<int>(path_len) * 120;
    if (path_len == 0)
    {
        score += 40;
    }
    if (snr_x10 != std::numeric_limits<int16_t>::min())
    {
        int snr_term = static_cast<int>(snr_x10) / 2;
        snr_term = std::max(-220, std::min(220, snr_term));
        score += snr_term;
    }
    score += std::min<int>(static_cast<int>(sample_count) * 4, 24);
    score -= std::min<int>(static_cast<int>(age_ms / 5000UL), 250);

    if (score < std::numeric_limits<int16_t>::min())
    {
        score = std::numeric_limits<int16_t>::min();
    }
    if (score > std::numeric_limits<int16_t>::max())
    {
        score = std::numeric_limits<int16_t>::max();
    }
    return static_cast<int16_t>(score);
}

void MeshCoreAdapter::refreshBestPeerRoute(PeerRouteEntry& entry, uint32_t now_ms)
{
    if (entry.candidate_count == 0)
    {
        entry.best_candidate = 0;
        entry.has_out_path = false;
        entry.out_path_len = 0;
        return;
    }

    uint8_t best_idx = 0;
    int16_t best_quality = std::numeric_limits<int16_t>::min();
    for (uint8_t i = 0; i < entry.candidate_count; ++i)
    {
        PeerRouteEntry::PathCandidate& candidate = entry.candidates[i];
        const uint32_t age_ms = now_ms - candidate.last_seen_ms;
        candidate.quality = computePathQuality(candidate.path_len,
                                               candidate.snr_x10,
                                               candidate.sample_count,
                                               age_ms);
        if (candidate.quality > best_quality)
        {
            best_quality = candidate.quality;
            best_idx = i;
        }
    }

    entry.best_candidate = best_idx;
    const PeerRouteEntry::PathCandidate& best = entry.candidates[best_idx];
    if (best.path_len > 0)
    {
        memcpy(entry.out_path, best.path, best.path_len);
    }
    entry.out_path_len = best.path_len;
    entry.has_out_path = true;
    entry.preferred_channel = best.channel;
}

void MeshCoreAdapter::prunePeerRoutes(uint32_t now_ms)
{
    for (auto it = peer_routes_.begin(); it != peer_routes_.end();)
    {
        PeerRouteEntry& entry = *it;

        uint8_t write_idx = 0;
        for (uint8_t i = 0; i < entry.candidate_count; ++i)
        {
            const PeerRouteEntry::PathCandidate& candidate = entry.candidates[i];
            const uint32_t age_ms = now_ms - candidate.last_seen_ms;
            if (age_ms > kPeerPathTtlMs)
            {
                continue;
            }
            if (write_idx != i)
            {
                entry.candidates[write_idx] = entry.candidates[i];
            }
            ++write_idx;
        }
        entry.candidate_count = write_idx;
        refreshBestPeerRoute(entry, now_ms);

        const uint32_t idle_ms = now_ms - entry.last_seen_ms;
        if (idle_ms > kPeerRouteEntryTtlMs &&
            entry.candidate_count == 0 &&
            entry.node_id_guess == 0)
        {
            it = peer_routes_.erase(it);
            continue;
        }
        ++it;
    }
}

void MeshCoreAdapter::rememberPeerPathCandidate(PeerRouteEntry& entry,
                                                const uint8_t* path, size_t path_len,
                                                ChannelId channel, int16_t snr_x10,
                                                uint32_t now_ms)
{
    uint8_t found_idx = entry.candidate_count;
    for (uint8_t i = 0; i < entry.candidate_count; ++i)
    {
        const PeerRouteEntry::PathCandidate& candidate = entry.candidates[i];
        if (candidate.channel != channel || candidate.path_len != path_len)
        {
            continue;
        }
        if (path_len == 0 || memcmp(candidate.path, path, path_len) == 0)
        {
            found_idx = i;
            break;
        }
    }

    if (found_idx >= entry.candidate_count)
    {
        if (entry.candidate_count < kMaxPeerRouteCandidates)
        {
            found_idx = entry.candidate_count++;
        }
        else
        {
            uint8_t replace_idx = 0;
            int16_t worst_quality = std::numeric_limits<int16_t>::max();
            for (uint8_t i = 0; i < entry.candidate_count; ++i)
            {
                if (entry.candidates[i].quality < worst_quality)
                {
                    worst_quality = entry.candidates[i].quality;
                    replace_idx = i;
                }
            }
            found_idx = replace_idx;
        }

        PeerRouteEntry::PathCandidate fresh{};
        fresh.path_len = static_cast<uint8_t>(path_len);
        if (path_len > 0)
        {
            memcpy(fresh.path, path, path_len);
        }
        fresh.channel = channel;
        fresh.snr_x10 = snr_x10;
        fresh.sample_count = 1;
        fresh.first_seen_ms = now_ms;
        fresh.last_seen_ms = now_ms;
        fresh.quality = computePathQuality(fresh.path_len, fresh.snr_x10, fresh.sample_count, 0);
        entry.candidates[found_idx] = fresh;
    }
    else
    {
        PeerRouteEntry::PathCandidate& candidate = entry.candidates[found_idx];
        candidate.channel = channel;
        candidate.path_len = static_cast<uint8_t>(path_len);
        if (path_len > 0)
        {
            memcpy(candidate.path, path, path_len);
        }
        if (candidate.sample_count < std::numeric_limits<uint8_t>::max())
        {
            ++candidate.sample_count;
        }
        if (snr_x10 != std::numeric_limits<int16_t>::min())
        {
            if (candidate.snr_x10 == std::numeric_limits<int16_t>::min())
            {
                candidate.snr_x10 = snr_x10;
            }
            else
            {
                const int blended = (static_cast<int>(candidate.snr_x10) * 3 + static_cast<int>(snr_x10)) / 4;
                candidate.snr_x10 = static_cast<int16_t>(blended);
            }
        }
        candidate.last_seen_ms = now_ms;
        candidate.quality = computePathQuality(candidate.path_len, candidate.snr_x10, candidate.sample_count, 0);
    }

    entry.last_seen_ms = now_ms;
    if (entry.route_blackout_until_ms != 0)
    {
        // Any freshly observed candidate means relearn succeeded, so lift blackout immediately.
        entry.route_blackout_until_ms = 0;
    }
    refreshBestPeerRoute(entry, now_ms);
}

void MeshCoreAdapter::penalizePeerRoute(uint8_t peer_hash, uint32_t now_ms)
{
    PeerRouteEntry* entry = findPeerRouteByHash(peer_hash);
    if (!entry)
    {
        return;
    }

    if (entry->candidate_count > 1)
    {
        const uint8_t drop_idx = (entry->best_candidate < entry->candidate_count)
                                     ? entry->best_candidate
                                     : static_cast<uint8_t>(entry->candidate_count - 1);
        for (uint8_t i = drop_idx + 1; i < entry->candidate_count; ++i)
        {
            entry->candidates[i - 1] = entry->candidates[i];
        }
        entry->candidate_count = static_cast<uint8_t>(entry->candidate_count - 1);
        entry->route_blackout_until_ms = 0;
        refreshBestPeerRoute(*entry, now_ms);
        MESHCORE_LOG("[MESHCORE] route penalty peer=%02X drop_idx=%u remain=%u -> fallback\n",
                     static_cast<unsigned>(peer_hash),
                     static_cast<unsigned>(drop_idx),
                     static_cast<unsigned>(entry->candidate_count));
        return;
    }

    entry->candidate_count = 0;
    entry->best_candidate = 0;
    entry->has_out_path = false;
    entry->out_path_len = 0;
    entry->route_blackout_until_ms = now_ms + kRoutePenaltyBlackoutMs;
    MESHCORE_LOG("[MESHCORE] route penalty peer=%02X -> blackout %lums\n",
                 static_cast<unsigned>(peer_hash),
                 static_cast<unsigned long>(kRoutePenaltyBlackoutMs));
}

void MeshCoreAdapter::rememberPeerNodeId(uint8_t peer_hash, NodeId node_id, uint32_t now_ms)
{
    if (node_id == 0)
    {
        return;
    }
    PeerRouteEntry& entry = upsertPeerRoute(peer_hash, now_ms);
    entry.node_id_guess = node_id;
    entry.last_seen_ms = now_ms;
}

NodeId MeshCoreAdapter::resolvePeerNodeId(uint8_t peer_hash) const
{
    if (const PeerRouteEntry* entry = findPeerRouteByHash(peer_hash))
    {
        if (entry->node_id_guess != 0)
        {
            return entry->node_id_guess;
        }
    }
    return (kSyntheticNodePrefix | static_cast<NodeId>(peer_hash));
}

void MeshCoreAdapter::rememberPeerPath(uint8_t peer_hash, const uint8_t* path, size_t path_len,
                                       ChannelId channel, uint32_t now_ms)
{
    if (path_len > kMaxPeerPathLen || (path_len > 0 && !path))
    {
        return;
    }
    PeerRouteEntry& entry = upsertPeerRoute(peer_hash, now_ms);
    uint8_t prev_path[kMaxPeerPathLen] = {};
    const uint8_t prev_len = entry.has_out_path ? entry.out_path_len : 0;
    const bool had_path = entry.has_out_path;
    if (had_path && prev_len > 0)
    {
        memcpy(prev_path, entry.out_path, prev_len);
    }
    const int16_t snr_x10 = std::isfinite(last_rx_snr_)
                                ? static_cast<int16_t>(std::lround(last_rx_snr_ * 10.0f))
                                : std::numeric_limits<int16_t>::min();
    rememberPeerPathCandidate(entry, path, path_len, channel, snr_x10, now_ms);
    const bool has_path = entry.has_out_path;
    const uint8_t new_len = entry.out_path_len;
    const bool changed = (had_path != has_path) ||
                         (new_len != prev_len) ||
                         (new_len > 0 && memcmp(entry.out_path, prev_path, new_len) != 0);
    if (changed)
    {
        Event ev{};
        ev.type = Event::Type::PathUpdated;
        ev.peer_hash = peer_hash;
        ev.peer_node = resolvePeerNodeId(peer_hash);
        pushEvent(std::move(ev));
    }
}

bool MeshCoreAdapter::lookupPeerPubKey(uint8_t peer_hash,
                                       uint8_t out_pubkey[MeshCoreIdentity::kPubKeySize]) const
{
    if (!out_pubkey)
    {
        return false;
    }
    const PeerRouteEntry* entry = findPeerRouteByHash(peer_hash);
    // MeshCore peers can start using ECDH direct payloads immediately after DISCOVER_RESP
    // (before a signed ADVERT is observed). Accept any learned full pubkey here to remain
    // interoperable with upstream behavior.
    if (!entry || !entry->has_pubkey)
    {
        return false;
    }
    memcpy(out_pubkey, entry->pubkey, MeshCoreIdentity::kPubKeySize);
    return true;
}

void MeshCoreAdapter::rememberPeerPubKey(const uint8_t pubkey[MeshCoreIdentity::kPubKeySize],
                                         uint32_t now_ms, bool verified)
{
    if (!pubkey || isZeroKey(pubkey, MeshCoreIdentity::kPubKeySize))
    {
        return;
    }

    const uint8_t peer_hash = pubkey[0];
    if (peer_hash == 0x00 || peer_hash == 0xFF || peer_hash == self_hash_)
    {
        return;
    }

    PeerRouteEntry& entry = upsertPeerRoute(peer_hash, now_ms);
    const bool changed = !entry.has_pubkey ||
                         (memcmp(entry.pubkey, pubkey, MeshCoreIdentity::kPubKeySize) != 0) ||
                         (!entry.pubkey_verified && verified);
    entry.has_pubkey = true;
    entry.pubkey_verified = entry.pubkey_verified || verified;
    memcpy(entry.pubkey, pubkey, MeshCoreIdentity::kPubKeySize);
    entry.pubkey_seen_ms = now_ms;
    const NodeId node = deriveNodeIdFromPubkey(pubkey, MeshCoreIdentity::kPubKeySize);
    if (node != 0)
    {
        entry.node_id_guess = node;
    }
    if (changed)
    {
        savePeerPubKeysToPrefs();
    }
}

void MeshCoreAdapter::loadPeerPubKeysFromPrefs()
{
    std::vector<uint8_t> blob;
    chat::infra::PreferencesBlobMetadata meta;
    if (!chat::infra::loadRawBlobFromPreferencesWithMetadata(kPeerPubKeyPrefsNs,
                                                             kPeerPubKeyPrefsKey,
                                                             kPeerPubKeyPrefsKeyVer,
                                                             nullptr,
                                                             blob,
                                                             &meta))
    {
        MESHCORE_LOG("[MESHCORE] peer key store not initialized ns=%s (first run)\n",
                     kPeerPubKeyPrefsNs);
        return;
    }

    if (!meta.has_version || meta.version != kPeerPubKeyPrefsVersion ||
        meta.len < sizeof(PersistedPeerPubKeyEntryV1) ||
        (meta.len % sizeof(PersistedPeerPubKeyEntryV1)) != 0)
    {
        return;
    }

    size_t count = meta.len / sizeof(PersistedPeerPubKeyEntryV1);
    if (count > kMaxPersistedPeerPubKeys)
    {
        count = kMaxPersistedPeerPubKeys;
    }
    const auto* entries = reinterpret_cast<const PersistedPeerPubKeyEntryV1*>(blob.data());

    const uint32_t now_ms = millis();
    size_t loaded = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const PersistedPeerPubKeyEntryV1& persisted = entries[i];
        if (persisted.peer_hash == 0x00 || persisted.peer_hash == 0xFF || persisted.peer_hash == self_hash_)
        {
            continue;
        }
        if (persisted.pubkey[0] != persisted.peer_hash ||
            isZeroKey(persisted.pubkey, sizeof(persisted.pubkey)))
        {
            continue;
        }

        PeerRouteEntry& entry = upsertPeerRoute(persisted.peer_hash, now_ms);
        entry.has_pubkey = true;
        entry.pubkey_verified = (persisted.flags & kPersistedPeerFlagVerified) != 0;
        memcpy(entry.pubkey, persisted.pubkey, sizeof(entry.pubkey));
        entry.pubkey_seen_ms = now_ms;
        const NodeId node = deriveNodeIdFromPubkey(entry.pubkey, sizeof(entry.pubkey));
        if (node != 0)
        {
            entry.node_id_guess = node;
        }
        ++loaded;
    }

    if (loaded > 0)
    {
        MESHCORE_LOG("[MESHCORE] peer keys loaded=%u ns=%s\n",
                     static_cast<unsigned>(loaded),
                     kPeerPubKeyPrefsNs);
    }
}

void MeshCoreAdapter::savePeerPubKeysToPrefs()
{
    struct StagedPeerKey
    {
        uint32_t seen_ms = 0;
        PersistedPeerPubKeyEntryV1 entry;
    };

    std::vector<StagedPeerKey> staged;
    staged.reserve(peer_routes_.size());
    for (const PeerRouteEntry& route : peer_routes_)
    {
        if (!route.has_pubkey || route.peer_hash == 0x00 || route.peer_hash == 0xFF ||
            route.peer_hash == self_hash_ || isZeroKey(route.pubkey, sizeof(route.pubkey)))
        {
            continue;
        }

        StagedPeerKey item{};
        item.seen_ms = route.pubkey_seen_ms;
        item.entry.peer_hash = route.peer_hash;
        item.entry.flags = route.pubkey_verified ? kPersistedPeerFlagVerified : 0;
        memcpy(item.entry.pubkey, route.pubkey, sizeof(item.entry.pubkey));
        staged.push_back(item);
    }

    if (staged.size() > kMaxPersistedPeerPubKeys)
    {
        std::sort(staged.begin(), staged.end(),
                  [](const StagedPeerKey& a, const StagedPeerKey& b)
                  {
                      return a.seen_ms > b.seen_ms;
                  });
        staged.resize(kMaxPersistedPeerPubKeys);
    }

    std::vector<PersistedPeerPubKeyEntryV1> entries;
    entries.reserve(staged.size());
    for (const StagedPeerKey& item : staged)
    {
        entries.push_back(item.entry);
    }

    chat::infra::PreferencesBlobMetadata meta;
    if (!entries.empty())
    {
        meta.len = entries.size() * sizeof(PersistedPeerPubKeyEntryV1);
        meta.has_version = true;
        meta.version = kPeerPubKeyPrefsVersion;
    }

    const bool ok = chat::infra::saveRawBlobToPreferencesWithMetadata(
        kPeerPubKeyPrefsNs,
        kPeerPubKeyPrefsKey,
        kPeerPubKeyPrefsKeyVer,
        nullptr,
        entries.empty() ? nullptr : reinterpret_cast<const uint8_t*>(entries.data()),
        entries.size() * sizeof(PersistedPeerPubKeyEntryV1),
        &meta,
        false);
    if (!ok)
    {
        MESHCORE_LOG("[MESHCORE] peer key save failed open ns=%s\n", kPeerPubKeyPrefsNs);
        return;
    }
    if (!entries.empty())
    {
        MESHCORE_LOG("[MESHCORE] peer key saved total=%u ns=%s\n",
                     static_cast<unsigned>(entries.size()),
                     kPeerPubKeyPrefsNs);
    }
}

void MeshCoreAdapter::maybeAutoDiscoverMissingPeer(uint8_t peer_hash, uint32_t now_ms)
{
    if (peer_hash == 0x00 || peer_hash == 0xFF || peer_hash == self_hash_)
    {
        return;
    }

    const bool same_peer = (peer_hash == last_auto_discover_hash_);
    if (same_peer && last_auto_discover_ms_ != 0 &&
        (now_ms - last_auto_discover_ms_) < kAutoDiscoverCooldownMs)
    {
        return;
    }

    if (sendDiscoverRequestLocal())
    {
        last_auto_discover_hash_ = peer_hash;
        last_auto_discover_ms_ = now_ms;
        MESHCORE_LOG("[MESHCORE] auto discover trigger src=%02X cooldown=%lums\n",
                     static_cast<unsigned>(peer_hash),
                     static_cast<unsigned long>(kAutoDiscoverCooldownMs));
    }
}

bool MeshCoreAdapter::deriveIdentitySecret(uint8_t peer_hash,
                                           uint8_t out_key16[16],
                                           uint8_t out_key32[32]) const
{
    if (!out_key16 || !out_key32 || !identity_.isReady())
    {
        return false;
    }

    uint8_t peer_pubkey[MeshCoreIdentity::kPubKeySize] = {};
    if (!lookupPeerPubKey(peer_hash, peer_pubkey))
    {
        return false;
    }

    uint8_t shared_secret[kCipherHmacKeySize] = {};
    if (!identity_.deriveSharedSecret(peer_pubkey, shared_secret))
    {
        return false;
    }

    sharedSecretToKeys(shared_secret, out_key16, out_key32);
    return true;
}

bool MeshCoreAdapter::deriveLegacyDirectSecret(ChannelId channel, uint8_t peer_hash,
                                               uint8_t out_key16[16], uint8_t out_key32[32]) const
{
    if (!out_key16 || !out_key32)
    {
        return false;
    }

    uint8_t base_key16[16];
    uint8_t base_key32[32];
    if (!resolveGroupSecret(channel, base_key16, base_key32, nullptr))
    {
        return false;
    }

    uint8_t material[18];
    memcpy(material, base_key16, sizeof(base_key16));
    material[16] = (peer_hash < self_hash_) ? peer_hash : self_hash_;
    material[17] = (peer_hash < self_hash_) ? self_hash_ : peer_hash;

    sha256Trunc(out_key32, 32, material, sizeof(material));
    memcpy(out_key16, out_key32, 16);
    return true;
}

bool MeshCoreAdapter::deriveDirectSecret(ChannelId channel, uint8_t peer_hash,
                                         uint8_t out_key16[16], uint8_t out_key32[32]) const
{
    if (deriveIdentitySecret(peer_hash, out_key16, out_key32))
    {
        return true;
    }
    return deriveLegacyDirectSecret(channel, peer_hash, out_key16, out_key32);
}

bool MeshCoreAdapter::tryDecryptPeerPayload(uint8_t src_hash,
                                            const uint8_t* cipher, size_t cipher_len,
                                            uint8_t* out_plain, size_t* out_plain_len,
                                            ChannelId* out_channel) const
{
    if (!cipher || cipher_len == 0 || !out_plain || !out_plain_len)
    {
        return false;
    }

    ChannelId order[3] = {ChannelId::PRIMARY, ChannelId::SECONDARY, ChannelId::PRIMARY};
    size_t order_len = 0;
    const PeerRouteEntry* known = findPeerRouteByHash(src_hash);
    if (known)
    {
        order[order_len++] = known->preferred_channel;
        order[order_len++] = (known->preferred_channel == ChannelId::PRIMARY) ? ChannelId::SECONDARY : ChannelId::PRIMARY;
    }
    else
    {
        order[order_len++] = ChannelId::PRIMARY;
        order[order_len++] = ChannelId::SECONDARY;
    }

    uint8_t tried_key16[6][16];
    uint8_t tried_key32[6][32];
    size_t tried = 0;

    auto tryCandidate = [&](ChannelId candidate_channel,
                            const uint8_t key16[16],
                            const uint8_t key32[32]) -> bool
    {
        bool duplicate = false;
        for (size_t j = 0; j < tried; ++j)
        {
            if (memcmp(tried_key16[j], key16, 16) == 0 &&
                memcmp(tried_key32[j], key32, 32) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate || tried >= (sizeof(tried_key16) / sizeof(tried_key16[0])))
        {
            return false;
        }

        memcpy(tried_key16[tried], key16, 16);
        memcpy(tried_key32[tried], key32, 32);
        ++tried;

        size_t plain_len = 0;
        if (!macThenDecrypt(key16, key32, cipher, cipher_len, out_plain, &plain_len))
        {
            return false;
        }
        *out_plain_len = plain_len;
        if (out_channel)
        {
            *out_channel = candidate_channel;
        }
        return true;
    };

    for (size_t i = 0; i < order_len; ++i)
    {
        {
            uint8_t key16[16] = {};
            uint8_t key32[32] = {};
            if (deriveIdentitySecret(src_hash, key16, key32) &&
                tryCandidate(order[i], key16, key32))
            {
                return true;
            }
        }
        {
            uint8_t key16[16] = {};
            uint8_t key32[32] = {};
            if (deriveLegacyDirectSecret(order[i], src_hash, key16, key32) &&
                tryCandidate(order[i], key16, key32))
            {
                return true;
            }
        }
        {
            uint8_t key16[16] = {};
            uint8_t key32[32] = {};
            if (deriveDirectSecret(order[i], src_hash, key16, key32) &&
                tryCandidate(order[i], key16, key32))
            {
                return true;
            }
        }
        if (tried >= (sizeof(tried_key16) / sizeof(tried_key16[0])))
        {
            break;
        }
    }

    return false;
}

MeshCoreAdapter::TxGateReason MeshCoreAdapter::checkTxGate(uint32_t now_ms) const
{
    if (!initialized_)
    {
        return TxGateReason::NotInitialized;
    }
    if (!config_.tx_enabled)
    {
        return TxGateReason::TxDisabled;
    }
    if (!board_.isRadioOnline())
    {
        return TxGateReason::RadioOffline;
    }
    if (min_tx_interval_ms_ > 0 && last_tx_ms_ > 0 &&
        (now_ms - last_tx_ms_) < min_tx_interval_ms_)
    {
        return TxGateReason::DutyCycleLimited;
    }
    return TxGateReason::Ok;
}

const char* MeshCoreAdapter::txGateReasonName(TxGateReason reason)
{
    switch (reason)
    {
    case TxGateReason::NotInitialized:
        return "not_initialized";
    case TxGateReason::TxDisabled:
        return "tx_disabled";
    case TxGateReason::RadioOffline:
        return "radio_offline";
    case TxGateReason::DutyCycleLimited:
        return "duty_cycle_limited";
    case TxGateReason::Ok:
    default:
        return "ok";
    }
}

bool MeshCoreAdapter::canTransmitNow(uint32_t now_ms) const
{
    return checkTxGate(now_ms) == TxGateReason::Ok;
}

bool MeshCoreAdapter::transmitFrameNow(const uint8_t* data, size_t len, uint32_t now_ms)
{
    if (!data || len == 0 || len > kMeshcoreMaxFrameSize || !canTransmitNow(now_ms))
    {
        return false;
    }

    const float air_ms_f = estimateLoRaAirtimeMs(len,
                                                 config_.meshcore_bw_khz,
                                                 config_.meshcore_sf,
                                                 config_.meshcore_cr);
    const uint32_t air_ms = (air_ms_f > 0.0f && std::isfinite(air_ms_f))
                                ? static_cast<uint32_t>(std::lround(air_ms_f))
                                : 0U;

    int state = RADIOLIB_ERR_UNSUPPORTED;
#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    state = board_.transmitRadio(data, len);
#endif
    if (state == RADIOLIB_ERR_NONE)
    {
        ParsedPacket parsed;
        if (parsePacket(data, len, &parsed) && parsed.payload_ver == kPayloadVer1)
        {
            const uint32_t packet_sig = packetSignature(parsed.payload_type, parsed.path_len,
                                                        parsed.payload, parsed.payload_len);
            hasSeenSignature(packet_sig, now_ms);
        }
        last_tx_ms_ = now_ms;
        tx_airtime_ms_ = saturatingAddU32(tx_airtime_ms_, air_ms);
        int rx_state = board_.startRadioReceive();
        if (rx_state != RADIOLIB_ERR_NONE)
        {
            MESHCORE_LOG("[MESHCORE] RX restart fail state=%d\n", rx_state);
        }
        return true;
    }
    return false;
}

bool MeshCoreAdapter::enqueueScheduled(const uint8_t* data, size_t len, uint32_t delay_ms)
{
    if (!data || len == 0 || len > kMeshcoreMaxFrameSize)
    {
        return false;
    }
    if (scheduled_tx_.size() >= kMaxScheduledFrames)
    {
        scheduled_tx_.pop_front();
    }
    ScheduledFrame frame;
    frame.bytes.assign(data, data + len);
    frame.due_ms = millis() + delay_ms;
    scheduled_tx_.push_back(std::move(frame));
    return true;
}

void MeshCoreAdapter::pruneSeen(uint32_t now_ms)
{
    while (!seen_recent_.empty())
    {
        const SeenEntry& e = seen_recent_.front();
        if ((now_ms - e.seen_ms) <= kSeenTtlMs)
        {
            break;
        }
        seen_recent_.pop_front();
    }
}

bool MeshCoreAdapter::hasSeenSignature(uint32_t signature, uint32_t now_ms)
{
    pruneSeen(now_ms);
    for (const SeenEntry& e : seen_recent_)
    {
        if (e.signature == signature)
        {
            return true;
        }
    }
    if (seen_recent_.size() >= kMaxSeenPackets)
    {
        seen_recent_.pop_front();
    }
    SeenEntry entry;
    entry.signature = signature;
    entry.seen_ms = now_ms;
    seen_recent_.push_back(entry);
    return false;
}

void MeshCoreAdapter::prunePendingAppAcks(uint32_t now_ms)
{
    while (!pending_app_acks_.empty())
    {
        const PendingAppAck& front = pending_app_acks_.front();
        if (static_cast<int32_t>(now_ms - front.expire_ms) < 0)
        {
            break;
        }
        const uint8_t peer_hash = static_cast<uint8_t>(front.dest & 0xFFU);
        MESHCORE_LOG("[MESHCORE] ACK timeout sig=%08lX dest=%08lX port=%u age=%lums\n",
                     static_cast<unsigned long>(front.signature),
                     static_cast<unsigned long>(front.dest),
                     static_cast<unsigned>(front.portnum),
                     static_cast<unsigned long>(now_ms - front.created_ms));
        penalizePeerRoute(peer_hash, now_ms);
        pending_app_acks_.pop_front();
    }
}

void MeshCoreAdapter::trackPendingAppAck(uint32_t signature, NodeId dest, uint32_t portnum, uint32_t now_ms)
{
    if (signature == 0 || dest == 0)
    {
        return;
    }

    prunePendingAppAcks(now_ms);
    for (PendingAppAck& entry : pending_app_acks_)
    {
        if (entry.signature == signature)
        {
            entry.dest = dest;
            entry.portnum = portnum;
            entry.created_ms = now_ms;
            entry.expire_ms = now_ms + kAppAckTimeoutMs;
            return;
        }
    }

    if (pending_app_acks_.size() >= kMaxPendingAppAcks)
    {
        pending_app_acks_.pop_front();
    }

    PendingAppAck entry;
    entry.signature = signature;
    entry.dest = dest;
    entry.portnum = portnum;
    entry.created_ms = now_ms;
    entry.expire_ms = now_ms + kAppAckTimeoutMs;
    pending_app_acks_.push_back(entry);
}

bool MeshCoreAdapter::consumePendingAppAck(uint32_t signature, uint32_t now_ms)
{
    if (signature == 0)
    {
        return false;
    }

    prunePendingAppAcks(now_ms);
    for (auto it = pending_app_acks_.begin(); it != pending_app_acks_.end(); ++it)
    {
        if (it->signature != signature)
        {
            continue;
        }

        MESHCORE_LOG("[MESHCORE] ACK matched sig=%08lX dest=%08lX port=%u age=%lums\n",
                     static_cast<unsigned long>(signature),
                     static_cast<unsigned long>(it->dest),
                     static_cast<unsigned>(it->portnum),
                     static_cast<unsigned long>(now_ms - it->created_ms));
        Event ev{};
        ev.type = Event::Type::SendConfirmed;
        ev.tag = signature;
        ev.trip_ms = now_ms - it->created_ms;
        pushEvent(std::move(ev));
        pending_app_acks_.erase(it);
        return true;
    }
    return false;
}

void MeshCoreAdapter::pushEvent(Event&& ev)
{
    if (events_.size() >= kMaxEventQueue)
    {
        events_.pop_front();
    }
    events_.push_back(std::move(ev));
}

bool MeshCoreAdapter::isPeerVerified(NodeId peer) const
{
    if (peer == 0)
    {
        return false;
    }
    for (NodeId known : verified_peers_)
    {
        if (known == peer)
        {
            return true;
        }
    }
    return false;
}

void MeshCoreAdapter::markPeerVerified(NodeId peer)
{
    if (peer == 0 || isPeerVerified(peer))
    {
        return;
    }
    if (verified_peers_.size() >= 128)
    {
        verified_peers_.erase(verified_peers_.begin());
    }
    verified_peers_.push_back(peer);
}

uint32_t MeshCoreAdapter::computeVerificationNumber(NodeId peer, uint64_t nonce) const
{
    if (peer == 0)
    {
        return 0xFFFFFFFFUL;
    }

    const uint8_t peer_hash = static_cast<uint8_t>(peer & 0xFFU);
    ChannelId channel = ChannelId::PRIMARY;
    if (const PeerRouteEntry* route = findPeerRouteByHash(peer_hash))
    {
        channel = route->preferred_channel;
    }

    uint8_t key16[16];
    uint8_t key32[32];
    if (!deriveDirectSecret(channel, peer_hash, key16, key32))
    {
        const ChannelId alternate =
            (channel == ChannelId::PRIMARY) ? ChannelId::SECONDARY : ChannelId::PRIMARY;
        if (!deriveDirectSecret(alternate, peer_hash, key16, key32))
        {
            return 0xFFFFFFFFUL;
        }
    }

    const uint32_t low = std::min(node_id_, peer);
    const uint32_t high = std::max(node_id_, peer);

    uint8_t digest[sizeof(uint32_t)] = {};
    SHA256 sha;
    sha.update(key32, sizeof(key32));
    sha.update(reinterpret_cast<const uint8_t*>(&low), sizeof(low));
    sha.update(reinterpret_cast<const uint8_t*>(&high), sizeof(high));
    sha.update(reinterpret_cast<const uint8_t*>(&nonce), sizeof(nonce));
    sha.finalize(digest, sizeof(digest));

    uint32_t number = 0;
    memcpy(&number, digest, sizeof(number));
    return number % 1000000UL;
}

bool MeshCoreAdapter::sendNodeInfoFrame(NodeId dest, bool is_query, bool request_reply)
{
    uint8_t payload[4 + 1 + 1 + 4 + 4 + kNodeInfoShortNameFieldSize + kNodeInfoLongNameFieldSize] = {};
    size_t payload_len = 0;
    payload[payload_len++] = kControlMagic0;
    payload[payload_len++] = kControlMagic1;
    payload[payload_len++] = kControlKindNodeInfo;
    payload[payload_len++] = is_query ? kNodeInfoTypeQuery : kNodeInfoTypeInfo;

    if (is_query)
    {
        payload[payload_len++] = request_reply ? kNodeInfoFlagRequestReply : 0x00;
    }
    else
    {
        payload[payload_len++] = static_cast<uint8_t>(chat::contacts::NodeRoleType::Client);
        payload[payload_len++] = 0;

        const uint32_t node = node_id_;
        const uint32_t ts = now_message_timestamp();
        memcpy(&payload[payload_len], &node, sizeof(node));
        payload_len += sizeof(node);
        memcpy(&payload[payload_len], &ts, sizeof(ts));
        payload_len += sizeof(ts);

        char short_name[kNodeInfoShortNameFieldSize] = {};
        if (!user_short_name_.empty())
        {
            strncpy(short_name, user_short_name_.c_str(), sizeof(short_name) - 1);
        }
        else
        {
            snprintf(short_name, sizeof(short_name), "%04lX",
                     static_cast<unsigned long>(node_id_ & 0xFFFFUL));
        }
        memcpy(&payload[payload_len], short_name, sizeof(short_name));
        payload_len += sizeof(short_name);

        char long_name[kNodeInfoLongNameFieldSize] = {};
        if (!user_long_name_.empty())
        {
            strncpy(long_name, user_long_name_.c_str(), sizeof(long_name) - 1);
        }
        else
        {
            strncpy(long_name, short_name, sizeof(long_name) - 1);
        }
        memcpy(&payload[payload_len], long_name, sizeof(long_name));
        payload_len += sizeof(long_name);
    }

    NodeId tx_dest = dest;
    if (tx_dest == 0xFFFFFFFFUL)
    {
        tx_dest = 0;
    }

    ChannelId channel = ChannelId::PRIMARY;
    if (tx_dest != 0)
    {
        const uint32_t now_ms = millis();
        prunePeerRoutes(now_ms);
        const uint8_t peer_hash = static_cast<uint8_t>(tx_dest & 0xFFU);
        if (const PeerRouteEntry* route = selectPeerRouteByHash(peer_hash, now_ms))
        {
            channel = route->preferred_channel;
        }
    }

    return sendAppData(channel, kNodeInfoPortnum, payload, payload_len, tx_dest, false);
}

bool MeshCoreAdapter::requestNodeInfo(NodeId dest, bool want_response)
{
    if (!initialized_ || !config_.tx_enabled)
    {
        return false;
    }

    NodeId target = dest;
    if (target == 0xFFFFFFFFUL)
    {
        target = 0;
    }

    if (target == 0)
    {
        return sendNodeInfoFrame(0, want_response, want_response);
    }
    if (want_response)
    {
        return sendNodeInfoFrame(target, true, true);
    }
    return sendNodeInfoFrame(target, false, false);
}

bool MeshCoreAdapter::triggerDiscoveryAction(MeshDiscoveryAction action)
{
    switch (action)
    {
    case MeshDiscoveryAction::ScanLocal:
        return sendDiscoverRequestLocal();
    case MeshDiscoveryAction::SendIdLocal:
        return sendIdentityAdvert(false);
    case MeshDiscoveryAction::SendIdBroadcast:
        return sendIdentityAdvert(true);
    default:
        return false;
    }
}

bool MeshCoreAdapter::sendDiscoverRequestLocal()
{
    const uint32_t now_ms = millis();
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        MESHCORE_LOG("[MESHCORE] TX DISCOVER_REQ blocked reason=%s\n",
                     txGateReasonName(tx_gate));
        return false;
    }

    const uint32_t tag = static_cast<uint32_t>(esp_random());
    uint8_t payload[10] = {};
    payload[0] = kControlSubtypeDiscoverReq; // prefix_only = 0
    payload[1] = kDiscoverTypeFilterAll;
    memcpy(payload + 2, &tag, sizeof(tag));
    uint32_t since = 0;
    memcpy(payload + 6, &since, sizeof(since));

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!buildFrameNoTransport(kRouteTypeDirect, kPayloadTypeControl,
                               nullptr, 0,
                               payload, sizeof(payload),
                               frame, sizeof(frame), &frame_len))
    {
        return false;
    }

    const bool ok = transmitFrameNow(frame, frame_len, now_ms);
    MESHCORE_LOG("[MESHCORE] TX DISCOVER_REQ mode=local tag=%08lX filter=%02X prefix=0 len=%u ok=%u\n",
                 static_cast<unsigned long>(tag),
                 static_cast<unsigned>(payload[1]),
                 static_cast<unsigned>(frame_len),
                 ok ? 1U : 0U);
    return ok;
}

bool MeshCoreAdapter::sendIdentityAdvert(bool broadcast)
{
    return sendIdentityAdvert(broadcast, false, 0, 0);
}

bool MeshCoreAdapter::sendIdentityAdvert(bool broadcast, bool include_location,
                                         int32_t lat_i6, int32_t lon_i6)
{
    if (!identity_.isReady())
    {
        MESHCORE_LOG("[MESHCORE] TX ADVERT dropped (identity unavailable)\n");
        return false;
    }

    const uint32_t now_ms = millis();
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        MESHCORE_LOG("[MESHCORE] TX ADVERT blocked reason=%s mode=%s\n",
                     txGateReasonName(tx_gate),
                     broadcast ? "broadcast" : "local");
        return false;
    }

    char name[32] = {};
    size_t name_len = copyPrintableAscii(user_short_name_, name, sizeof(name));
    if (name_len == 0)
    {
        name_len = copyPrintableAscii(user_long_name_, name, sizeof(name));
    }

    const uint8_t node_type = config_.meshcore_client_repeat ? kAdvertTypeRepeater : kAdvertTypeChat;
    uint8_t app_data[1 + 8 + sizeof(name)] = {};
    size_t app_data_len = 0;
    uint8_t flags = static_cast<uint8_t>(node_type & 0x0F);
    if (include_location)
    {
        flags = static_cast<uint8_t>(flags | kAdvertFlagHasLocation);
    }
    if (name_len > 0)
    {
        flags = static_cast<uint8_t>(flags | kAdvertFlagHasName);
    }
    app_data[app_data_len++] = flags;
    if (include_location)
    {
        memcpy(app_data + app_data_len, &lat_i6, sizeof(lat_i6));
        app_data_len += sizeof(lat_i6);
        memcpy(app_data + app_data_len, &lon_i6, sizeof(lon_i6));
        app_data_len += sizeof(lon_i6);
    }
    if (name_len > 0)
    {
        memcpy(app_data + app_data_len, name, name_len);
        app_data_len += name_len;
    }

    const uint8_t* pubkey = identity_.publicKey();
    const uint32_t ts = now_message_timestamp();
    std::array<uint8_t, kMeshcorePubKeySize + sizeof(ts) + sizeof(app_data)> signed_message = {};
    size_t signed_len = 0;
    memcpy(signed_message.data() + signed_len, pubkey, kMeshcorePubKeySize);
    signed_len += kMeshcorePubKeySize;
    memcpy(signed_message.data() + signed_len, &ts, sizeof(ts));
    signed_len += sizeof(ts);
    if (app_data_len > 0)
    {
        memcpy(signed_message.data() + signed_len, app_data, app_data_len);
        signed_len += app_data_len;
    }

    uint8_t signature[MeshCoreIdentity::kSignatureSize] = {};
    if (!identity_.sign(signed_message.data(), signed_len, signature))
    {
        return false;
    }

    uint8_t payload[kMeshcoreMaxPayloadSize] = {};
    size_t payload_len = 0;
    memcpy(payload + payload_len, pubkey, kMeshcorePubKeySize);
    payload_len += kMeshcorePubKeySize;
    memcpy(payload + payload_len, &ts, sizeof(ts));
    payload_len += sizeof(ts);
    memcpy(payload + payload_len, signature, sizeof(signature));
    payload_len += sizeof(signature);
    if (app_data_len > 0)
    {
        memcpy(payload + payload_len, app_data, app_data_len);
        payload_len += app_data_len;
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    const uint8_t route_type = broadcast ? kRouteTypeFlood : kRouteTypeDirect;
    if (!buildFrameNoTransport(route_type, kPayloadTypeAdvert,
                               nullptr, 0,
                               payload, payload_len,
                               frame, sizeof(frame), &frame_len))
    {
        return false;
    }

    bool ok = transmitFrameNow(frame, frame_len, now_ms);
    if (!ok)
    {
        ok = enqueueScheduled(frame, frame_len, 50);
    }
    MESHCORE_LOG("[MESHCORE] TX ADVERT mode=%s node_type=%u name_len=%u len=%u ok=%u\n",
                 broadcast ? "broadcast" : "local",
                 static_cast<unsigned>(node_type),
                 static_cast<unsigned>(name_len),
                 static_cast<unsigned>(frame_len),
                 ok ? 1U : 0U);
    return ok;
}

bool MeshCoreAdapter::exportAdvertFrame(const uint8_t* pubkey, size_t len,
                                        std::vector<uint8_t>& out_frame,
                                        bool include_location,
                                        int32_t lat_i6, int32_t lon_i6) const
{
    out_frame.clear();
    const bool wants_self = (pubkey == nullptr || len == 0 ||
                             (len == MeshCoreIdentity::kPubKeySize &&
                              identity_.isReady() &&
                              memcmp(pubkey, identity_.publicKey(), MeshCoreIdentity::kPubKeySize) == 0));
    if (wants_self)
    {
        if (!identity_.isReady())
        {
            return false;
        }

        char name[32] = {};
        size_t name_len = copyPrintableAscii(user_short_name_, name, sizeof(name));
        if (name_len == 0)
        {
            name_len = copyPrintableAscii(user_long_name_, name, sizeof(name));
        }

        const uint8_t node_type = config_.meshcore_client_repeat ? kAdvertTypeRepeater : kAdvertTypeChat;
        uint8_t app_data[1 + 8 + sizeof(name)] = {};
        size_t app_data_len = 0;
        uint8_t flags = static_cast<uint8_t>(node_type & 0x0F);
        if (include_location)
        {
            flags = static_cast<uint8_t>(flags | kAdvertFlagHasLocation);
        }
        if (name_len > 0)
        {
            flags = static_cast<uint8_t>(flags | kAdvertFlagHasName);
        }
        app_data[app_data_len++] = flags;
        if (include_location)
        {
            memcpy(app_data + app_data_len, &lat_i6, sizeof(lat_i6));
            app_data_len += sizeof(lat_i6);
            memcpy(app_data + app_data_len, &lon_i6, sizeof(lon_i6));
            app_data_len += sizeof(lon_i6);
        }
        if (name_len > 0)
        {
            memcpy(app_data + app_data_len, name, name_len);
            app_data_len += name_len;
        }

        const uint8_t* self_pubkey = identity_.publicKey();
        const uint32_t ts = now_message_timestamp();
        std::array<uint8_t, kMeshcorePubKeySize + sizeof(ts) + sizeof(app_data)> signed_message = {};
        size_t signed_len = 0;
        memcpy(signed_message.data() + signed_len, self_pubkey, kMeshcorePubKeySize);
        signed_len += kMeshcorePubKeySize;
        memcpy(signed_message.data() + signed_len, &ts, sizeof(ts));
        signed_len += sizeof(ts);
        if (app_data_len > 0)
        {
            memcpy(signed_message.data() + signed_len, app_data, app_data_len);
            signed_len += app_data_len;
        }

        uint8_t signature[MeshCoreIdentity::kSignatureSize] = {};
        if (!identity_.sign(signed_message.data(), signed_len, signature))
        {
            return false;
        }

        uint8_t payload[kMeshcoreMaxPayloadSize] = {};
        size_t payload_len = 0;
        memcpy(payload + payload_len, self_pubkey, kMeshcorePubKeySize);
        payload_len += kMeshcorePubKeySize;
        memcpy(payload + payload_len, &ts, sizeof(ts));
        payload_len += sizeof(ts);
        memcpy(payload + payload_len, signature, sizeof(signature));
        payload_len += sizeof(signature);
        if (app_data_len > 0)
        {
            memcpy(payload + payload_len, app_data, app_data_len);
            payload_len += app_data_len;
        }

        uint8_t frame[kMeshcoreMaxFrameSize] = {};
        size_t frame_len = 0;
        if (!buildFrameNoTransport(kRouteTypeFlood, kPayloadTypeAdvert,
                                   nullptr, 0,
                                   payload, payload_len,
                                   frame, sizeof(frame), &frame_len))
        {
            return false;
        }
        out_frame.assign(frame, frame + frame_len);
        return true;
    }

    if (!pubkey || len != MeshCoreIdentity::kPubKeySize)
    {
        return false;
    }
    const PeerRouteEntry* entry = findPeerRouteByHash(pubkey[0]);
    if (!entry || entry->last_advert_len == 0)
    {
        return false;
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!buildFrameNoTransport(kRouteTypeFlood, kPayloadTypeAdvert,
                               nullptr, 0,
                               entry->last_advert, entry->last_advert_len,
                               frame, sizeof(frame), &frame_len))
    {
        return false;
    }
    out_frame.assign(frame, frame + frame_len);
    return true;
}

bool MeshCoreAdapter::importAdvertFrame(const uint8_t* frame, size_t len)
{
    if (!frame || len < 2 || len > kMeshcoreMaxFrameSize)
    {
        return false;
    }
    ParsedPacket parsed{};
    if (!parsePacket(frame, len, &parsed))
    {
        return false;
    }
    if (parsed.payload_ver != kPayloadVer1 ||
        parsed.payload_type != kPayloadTypeAdvert ||
        parsed.payload_len < kAdvertMinPayloadSize)
    {
        return false;
    }
    handleRawPacketInternal(frame, len, true);
    return true;
}

bool MeshCoreAdapter::startKeyVerification(NodeId dest)
{
    if (!isPkiReady() || dest == 0 || dest == 0xFFFFFFFFUL)
    {
        return false;
    }

    const uint32_t now_ms = millis();
    prunePeerRoutes(now_ms);
    if (key_verify_session_.active &&
        static_cast<int32_t>(now_ms - key_verify_session_.started_ms) > static_cast<int32_t>(kKeyVerifySessionTtlMs))
    {
        key_verify_session_ = KeyVerifySession{};
    }
    if (key_verify_session_.active)
    {
        return false;
    }

    const uint64_t nonce = (static_cast<uint64_t>(esp_random()) << 32) |
                           static_cast<uint64_t>(esp_random());
    const uint32_t expected = computeVerificationNumber(dest, nonce);
    if (expected == 0xFFFFFFFFUL)
    {
        return false;
    }

    uint8_t payload[12] = {};
    payload[0] = kControlMagic0;
    payload[1] = kControlMagic1;
    payload[2] = kControlKindKeyVerify;
    payload[3] = kKeyVerifyTypeInit;
    memcpy(payload + 4, &nonce, sizeof(nonce));

    ChannelId channel = ChannelId::PRIMARY;
    const uint8_t peer_hash = static_cast<uint8_t>(dest & 0xFFU);
    if (const PeerRouteEntry* route = selectPeerRouteByHash(peer_hash, now_ms))
    {
        channel = route->preferred_channel;
    }

    if (!sendAppData(channel, kKeyVerifyPortnum, payload, sizeof(payload), dest, true))
    {
        return false;
    }

    key_verify_session_.active = true;
    key_verify_session_.is_initiator = true;
    key_verify_session_.awaiting_user_number = false;
    key_verify_session_.peer = dest;
    key_verify_session_.nonce = nonce;
    key_verify_session_.expected_number = expected;
    key_verify_session_.started_ms = now_ms;
    return true;
}

bool MeshCoreAdapter::submitKeyVerificationNumber(NodeId dest, uint64_t nonce, uint32_t number)
{
    if (!isPkiReady() || dest == 0)
    {
        return false;
    }

    const uint32_t now_ms = millis();
    prunePeerRoutes(now_ms);
    if (key_verify_session_.active &&
        static_cast<int32_t>(now_ms - key_verify_session_.started_ms) > static_cast<int32_t>(kKeyVerifySessionTtlMs))
    {
        key_verify_session_ = KeyVerifySession{};
    }

    if (!key_verify_session_.active ||
        !key_verify_session_.is_initiator ||
        !key_verify_session_.awaiting_user_number)
    {
        return false;
    }

    if (key_verify_session_.peer != dest || key_verify_session_.nonce != nonce)
    {
        return false;
    }

    if ((number % 1000000UL) != key_verify_session_.expected_number)
    {
        return false;
    }

    uint8_t payload[12] = {};
    payload[0] = kControlMagic0;
    payload[1] = kControlMagic1;
    payload[2] = kControlKindKeyVerify;
    payload[3] = kKeyVerifyTypeFinal;
    memcpy(payload + 4, &nonce, sizeof(nonce));

    ChannelId channel = ChannelId::PRIMARY;
    const uint8_t peer_hash = static_cast<uint8_t>(dest & 0xFFU);
    if (const PeerRouteEntry* route = selectPeerRouteByHash(peer_hash, now_ms))
    {
        channel = route->preferred_channel;
    }

    if (!sendAppData(channel, kKeyVerifyPortnum, payload, sizeof(payload), dest, true))
    {
        return false;
    }

    char code[12] = {};
    formatVerificationCode(key_verify_session_.expected_number, code, sizeof(code));
    sys::EventBus::publish(new sys::KeyVerificationFinalEvent(dest, nonce, true, code), 0);
    markPeerVerified(dest);
    key_verify_session_ = KeyVerifySession{};
    return true;
}

bool MeshCoreAdapter::isPkiReady() const
{
    return initialized_ && pki_enabled_ && identity_.isReady();
}

bool MeshCoreAdapter::hasPkiKey(NodeId dest) const
{
    if (!isPkiReady() || dest == 0 || dest == 0xFFFFFFFFUL)
    {
        return false;
    }
    if (dest == node_id_)
    {
        return true;
    }
    const uint8_t peer_hash = static_cast<uint8_t>(dest & 0xFFU);
    const PeerRouteEntry* route = findPeerRouteByHash(peer_hash);
    return route && route->has_pubkey;
}

bool MeshCoreAdapter::handleControlAppData(const MeshIncomingData& incoming)
{
    if (incoming.portnum == kNodeInfoPortnum)
    {
        return handleNodeInfoControl(incoming);
    }
    if (incoming.portnum == kKeyVerifyPortnum)
    {
        return handleKeyVerifyControl(incoming);
    }
    return false;
}

bool MeshCoreAdapter::handleNodeInfoControl(const MeshIncomingData& incoming)
{
    if (!hasControlPrefix(incoming.payload.data(), incoming.payload.size(), kControlKindNodeInfo))
    {
        return false;
    }

    const uint8_t type = incoming.payload[3];
    if (type == kNodeInfoTypeQuery)
    {
        const bool request_reply = (incoming.payload.size() > 4) &&
                                   ((incoming.payload[4] & kNodeInfoFlagRequestReply) != 0);
        if (incoming.from != 0 && request_reply)
        {
            sendNodeInfoFrame(incoming.from, false, false);
        }
        return true;
    }

    if (type != kNodeInfoTypeInfo)
    {
        return true;
    }

    constexpr size_t kInfoSize = 4 + 1 + 1 + 4 + 4 + kNodeInfoShortNameFieldSize + kNodeInfoLongNameFieldSize;
    if (incoming.payload.size() < kInfoSize)
    {
        return true;
    }

    size_t index = 4;
    const uint8_t role = incoming.payload[index++];
    uint8_t hops = incoming.payload[index++];

    NodeId node = 0;
    memcpy(&node, incoming.payload.data() + index, sizeof(node));
    index += sizeof(node);

    uint32_t ts = 0;
    memcpy(&ts, incoming.payload.data() + index, sizeof(ts));
    index += sizeof(ts);
    if (!is_valid_epoch(ts))
    {
        ts = now_message_timestamp();
    }

    char short_name[kNodeInfoShortNameFieldSize + 1] = {};
    memcpy(short_name, incoming.payload.data() + index, kNodeInfoShortNameFieldSize);
    short_name[kNodeInfoShortNameFieldSize] = '\0';
    index += kNodeInfoShortNameFieldSize;

    char long_name[kNodeInfoLongNameFieldSize + 1] = {};
    memcpy(long_name, incoming.payload.data() + index, kNodeInfoLongNameFieldSize);
    long_name[kNodeInfoLongNameFieldSize] = '\0';

    if (node == 0)
    {
        node = incoming.from;
    }
    if (node == 0)
    {
        return true;
    }

    const uint32_t now_ms = millis();
    rememberPeerNodeId(static_cast<uint8_t>(node & 0xFFU), node, now_ms);
    if (incoming.rx_meta.hop_count != 0xFF)
    {
        hops = incoming.rx_meta.hop_count;
    }

    float snr = 0.0f;
    if (incoming.rx_meta.snr_db_x10 != std::numeric_limits<int16_t>::min())
    {
        snr = static_cast<float>(incoming.rx_meta.snr_db_x10) / 10.0f;
    }
    else if (std::isfinite(last_rx_snr_))
    {
        snr = last_rx_snr_;
    }

    float rssi = 0.0f;
    if (incoming.rx_meta.rssi_dbm_x10 != std::numeric_limits<int16_t>::min())
    {
        rssi = static_cast<float>(incoming.rx_meta.rssi_dbm_x10) / 10.0f;
    }
    else if (std::isfinite(last_rx_rssi_))
    {
        rssi = last_rx_rssi_;
    }

    sys::EventBus::publish(
        new sys::NodeInfoUpdateEvent(node, short_name, long_name, snr, rssi, ts,
                                     static_cast<uint8_t>(chat::contacts::NodeProtocolType::MeshCore),
                                     role, hops),
        0);
    return true;
}

bool MeshCoreAdapter::handleKeyVerifyControl(const MeshIncomingData& incoming)
{
    if (!hasControlPrefix(incoming.payload.data(), incoming.payload.size(), kControlKindKeyVerify))
    {
        return false;
    }

    const uint32_t now_ms = millis();
    if (key_verify_session_.active &&
        static_cast<int32_t>(now_ms - key_verify_session_.started_ms) > static_cast<int32_t>(kKeyVerifySessionTtlMs))
    {
        key_verify_session_ = KeyVerifySession{};
    }

    if (incoming.payload.size() < 12 || incoming.from == 0)
    {
        return true;
    }

    const uint8_t type = incoming.payload[3];
    uint64_t nonce = 0;
    memcpy(&nonce, incoming.payload.data() + 4, sizeof(nonce));

    if (type == kKeyVerifyTypeInit)
    {
        if (!isPkiReady())
        {
            return true;
        }

        const uint32_t number = computeVerificationNumber(incoming.from, nonce);
        if (number == 0xFFFFFFFFUL)
        {
            return true;
        }

        key_verify_session_.active = true;
        key_verify_session_.is_initiator = false;
        key_verify_session_.awaiting_user_number = false;
        key_verify_session_.peer = incoming.from;
        key_verify_session_.nonce = nonce;
        key_verify_session_.expected_number = number;
        key_verify_session_.started_ms = now_ms;

        sys::EventBus::publish(new sys::KeyVerificationNumberInformEvent(incoming.from, nonce, number), 0);

        uint8_t reply[12] = {};
        reply[0] = kControlMagic0;
        reply[1] = kControlMagic1;
        reply[2] = kControlKindKeyVerify;
        reply[3] = kKeyVerifyTypeReady;
        memcpy(reply + 4, &nonce, sizeof(nonce));
        sendAppData(incoming.channel, kKeyVerifyPortnum, reply, sizeof(reply), incoming.from, true);
        return true;
    }

    if (type == kKeyVerifyTypeReady)
    {
        if (!key_verify_session_.active ||
            !key_verify_session_.is_initiator ||
            key_verify_session_.peer != incoming.from ||
            key_verify_session_.nonce != nonce)
        {
            return true;
        }

        key_verify_session_.awaiting_user_number = true;
        key_verify_session_.started_ms = now_ms;
        sys::EventBus::publish(new sys::KeyVerificationNumberRequestEvent(incoming.from, nonce), 0);
        return true;
    }

    if (type == kKeyVerifyTypeFinal)
    {
        if (!key_verify_session_.active ||
            key_verify_session_.is_initiator ||
            key_verify_session_.peer != incoming.from ||
            key_verify_session_.nonce != nonce)
        {
            return true;
        }

        char code[12] = {};
        formatVerificationCode(key_verify_session_.expected_number, code, sizeof(code));
        sys::EventBus::publish(new sys::KeyVerificationFinalEvent(incoming.from, nonce, false, code), 0);
        markPeerVerified(incoming.from);
        key_verify_session_ = KeyVerifySession{};
        return true;
    }

    return true;
}

bool MeshCoreAdapter::sendText(ChannelId channel, const std::string& text,
                               MessageId* out_msg_id, NodeId peer)
{
    if (text.empty())
    {
        MESHCORE_LOG("[MESHCORE] TX text dropped (empty)\n");
        return false;
    }

    const uint32_t now_ms = millis();
    prunePeerRoutes(now_ms);
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        MESHCORE_LOG("[MESHCORE] TX text blocked reason=%s now=%lu last_tx=%lu min_interval=%lu tx_en=%u init=%u radio=%u\n",
                     txGateReasonName(tx_gate),
                     static_cast<unsigned long>(now_ms),
                     static_cast<unsigned long>(last_tx_ms_),
                     static_cast<unsigned long>(min_tx_interval_ms_),
                     config_.tx_enabled ? 1U : 0U,
                     initialized_ ? 1U : 0U,
                     board_.isRadioOnline() ? 1U : 0U);
        return false;
    }

    if (peer != 0)
    {
        return sendDirectTextDetailed(channel, text, peer, kTxtTypePlain, nullptr, nullptr, nullptr);
    }

    uint8_t channel_key16[16];
    uint8_t channel_key32[32];
    uint8_t channel_hash = 0;
    if (!resolveGroupSecret(channel, channel_key16, channel_key32, &channel_hash))
    {
        MESHCORE_LOG("[MESHCORE] TX text dropped (no channel secret)\n");
        return false;
    }

    std::string decorated = text;
    if (!user_short_name_.empty())
    {
        decorated = user_short_name_ + ": " + text;
    }
    else if (!user_long_name_.empty())
    {
        decorated = user_long_name_ + ": " + text;
    }

    constexpr size_t kGroupCipherBudget =
        ((kMeshcoreMaxPayloadSize - 1 - kCipherMacSize) / kCipherBlockSize) * kCipherBlockSize;
    constexpr size_t kGroupPlainPrefix = kGroupPlainPrefixSize; // ts(4) + txt_type(1)
    constexpr size_t kGroupTextBudget = (kGroupCipherBudget > kGroupPlainPrefix)
                                            ? (kGroupCipherBudget - kGroupPlainPrefix)
                                            : 0;
    if (decorated.size() > kGroupTextBudget)
    {
        decorated.resize(kGroupTextBudget);
    }

    uint8_t plain[kGroupCipherBudget];
    size_t plain_len = 0;
    uint32_t msg_ts = now_message_timestamp();
    memcpy(&plain[plain_len], &msg_ts, sizeof(msg_ts));
    plain_len += sizeof(msg_ts);
    // Upstream MeshCore companion firmware only renders group text when txt_type is plain.
    plain[plain_len++] = static_cast<uint8_t>(kTxtTypePlain << 2);
    memcpy(&plain[plain_len], decorated.data(), decorated.size());
    plain_len += decorated.size();

    uint8_t encrypted[kMeshcoreMaxPayloadSize];
    size_t encrypted_len = encryptThenMac(channel_key16, channel_key32,
                                          encrypted, sizeof(encrypted),
                                          plain, plain_len);
    if (encrypted_len == 0 || encrypted_len > (kMeshcoreMaxPayloadSize - 1))
    {
        MESHCORE_LOG("[MESHCORE] TX text dropped (encrypt fail) ch=%u plain_len=%u\n",
                     static_cast<unsigned>(channel),
                     static_cast<unsigned>(plain_len));
        return false;
    }

    uint8_t buffer[256];
    size_t index = 0;
    buffer[index++] = buildHeader(kRouteTypeFlood, kPayloadTypeGrpTxt, kPayloadVer1);
    buffer[index++] = 0; // path_len = 0
    buffer[index++] = channel_hash;
    memcpy(&buffer[index], encrypted, encrypted_len);
    index += encrypted_len;

    bool ok = transmitFrameNow(buffer, index, now_ms);
    MESHCORE_LOG("[MESHCORE] TX raw len=%u ok=%u hex=%s\n",
                 static_cast<unsigned>(index),
                 ok ? 1U : 0U,
                 toHex(buffer, index).c_str());

    if (ok)
    {
        if (out_msg_id)
        {
            *out_msg_id = next_msg_id_++;
        }
    }

    return ok;
}

bool MeshCoreAdapter::sendDirectTextDetailed(ChannelId channel, const std::string& text,
                                             NodeId peer, uint8_t txt_type,
                                             uint32_t* out_ack, uint32_t* out_timeout,
                                             bool* out_sent_flood)
{
    if (text.empty() || peer == 0)
    {
        return false;
    }

    const uint32_t now_ms = millis();
    prunePeerRoutes(now_ms);
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        return false;
    }

    const uint8_t peer_hash = static_cast<uint8_t>(peer & 0xFFU);
    rememberPeerNodeId(peer_hash, peer, now_ms);

    if (identity_.isReady())
    {
        const PeerRouteEntry* peer_route = findPeerRouteByHash(peer_hash);
        if (!peer_route || !peer_route->has_pubkey)
        {
            sendDiscoverRequestLocal();
            return false;
        }
    }

    uint8_t route_type = kRouteTypeFlood;
    const uint8_t* out_path = nullptr;
    size_t out_path_len = 0;
    ChannelId tx_channel = channel;
    if (const PeerRouteEntry* route = selectPeerRouteByHash(peer_hash, now_ms))
    {
        route_type = kRouteTypeDirect;
        out_path = route->out_path;
        out_path_len = route->out_path_len;
        tx_channel = route->preferred_channel;
    }

    uint8_t peer_key16[16];
    uint8_t peer_key32[32];
    if (!deriveDirectSecret(tx_channel, peer_hash, peer_key16, peer_key32))
    {
        if (tx_channel == channel ||
            !deriveDirectSecret(channel, peer_hash, peer_key16, peer_key32))
        {
            return false;
        }
    }

    constexpr size_t kDirectPlainPrefixSize = 5; // ts(4) + flags(1)
    constexpr size_t kDirectCipherBudget =
        ((kMeshcoreMaxPayloadSize - 2 - kCipherMacSize) / kCipherBlockSize) * kCipherBlockSize;
    constexpr size_t kDirectTextBudget = (kDirectCipherBudget > kDirectPlainPrefixSize)
                                             ? (kDirectCipherBudget - kDirectPlainPrefixSize)
                                             : 0;

    std::string body = text;
    if (body.size() > kDirectTextBudget)
    {
        body.resize(kDirectTextBudget);
    }

    uint8_t plain[kDirectCipherBudget];
    size_t plain_len = 0;
    uint32_t msg_ts = now_message_timestamp();
    memcpy(&plain[plain_len], &msg_ts, sizeof(msg_ts));
    plain_len += sizeof(msg_ts);
    plain[plain_len++] = static_cast<uint8_t>(txt_type << 2);
    memcpy(&plain[plain_len], body.data(), body.size());
    plain_len += body.size();

    uint32_t ack_value = 0;
    if (identity_.isReady() && txt_type == kTxtTypePlain)
    {
        SHA256 sha;
        sha.update(plain, plain_len);
        sha.update(identity_.publicKey(), kMeshcorePubKeySize);
        sha.finalize(reinterpret_cast<uint8_t*>(&ack_value), sizeof(ack_value));
    }

    uint8_t payload[kMeshcoreMaxPayloadSize];
    size_t payload_len = 0;
    if (!buildPeerDatagramPayload(peer_hash, self_hash_,
                                  peer_key16, peer_key32,
                                  plain, plain_len,
                                  payload, sizeof(payload), &payload_len))
    {
        return false;
    }

    uint8_t frame[kMeshcoreMaxFrameSize];
    size_t frame_len = 0;

    if (!buildFrameNoTransport(route_type, kPayloadTypeTxtMsg,
                               out_path, out_path_len,
                               payload, payload_len,
                               frame, sizeof(frame), &frame_len))
    {
        return false;
    }

    bool ok = transmitFrameNow(frame, frame_len, now_ms);
    if (!ok)
    {
        ok = enqueueScheduled(frame, frame_len, 50);
    }

    if (ok && ack_value != 0)
    {
        trackPendingAppAck(ack_value, peer, 0, now_ms);
    }

    if (out_ack)
    {
        *out_ack = ack_value;
    }
    if (out_timeout)
    {
        *out_timeout = estimateSendTimeoutMs(frame_len,
                                             (route_type == kRouteTypeDirect) ? out_path_len : 0,
                                             route_type == kRouteTypeFlood,
                                             config_.meshcore_bw_khz,
                                             config_.meshcore_sf,
                                             config_.meshcore_cr);
    }
    if (out_sent_flood)
    {
        *out_sent_flood = (route_type == kRouteTypeFlood);
    }
    return ok;
}

bool MeshCoreAdapter::pollIncomingText(MeshIncomingText* out)
{
    if (!initialized_ || !out)
    {
        return false;
    }

    if (receive_queue_.empty())
    {
        return false;
    }

    *out = receive_queue_.front();
    receive_queue_.pop();
    return true;
}

bool MeshCoreAdapter::sendAppData(ChannelId channel, uint32_t portnum,
                                  const uint8_t* payload, size_t len,
                                  NodeId dest, bool want_ack,
                                  MessageId packet_id,
                                  bool want_response)
{
    (void)packet_id;
    (void)want_response;
    if (!payload || len == 0)
    {
        MESHCORE_LOG("[MESHCORE] TX app-data dropped (invalid payload) port=%u len=%u payload=%u\n",
                     static_cast<unsigned>(portnum),
                     static_cast<unsigned>(len),
                     payload ? 1U : 0U);
        return false;
    }

    const uint32_t now_ms = millis();
    prunePeerRoutes(now_ms);
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        MESHCORE_LOG("[MESHCORE] TX app-data blocked reason=%s port=%u dest=%08lX now=%lu last_tx=%lu min_interval=%lu tx_en=%u init=%u radio=%u\n",
                     txGateReasonName(tx_gate),
                     static_cast<unsigned>(portnum),
                     static_cast<unsigned long>(dest),
                     static_cast<unsigned long>(now_ms),
                     static_cast<unsigned long>(last_tx_ms_),
                     static_cast<unsigned long>(min_tx_interval_ms_),
                     config_.tx_enabled ? 1U : 0U,
                     initialized_ ? 1U : 0U,
                     board_.isRadioOnline() ? 1U : 0U);
        return false;
    }

    if (dest != 0)
    {
        const uint8_t peer_hash = static_cast<uint8_t>(dest & 0xFFU);
        rememberPeerNodeId(peer_hash, dest, now_ms);

        if (identity_.isReady())
        {
            const PeerRouteEntry* peer_route = findPeerRouteByHash(peer_hash);
            if (!peer_route || !peer_route->has_pubkey)
            {
                MESHCORE_LOG("[MESHCORE] TX direct app-data dropped (missing peer pubkey) peer=%08lX hash=%02X port=%u -> discover\n",
                             static_cast<unsigned long>(dest),
                             static_cast<unsigned>(peer_hash),
                             static_cast<unsigned>(portnum));
                sendDiscoverRequestLocal();
                return false;
            }
        }

        uint8_t route_type = kRouteTypeFlood;
        const uint8_t* out_path = nullptr;
        size_t out_path_len = 0;
        ChannelId tx_channel = channel;
        if (const PeerRouteEntry* route = selectPeerRouteByHash(peer_hash, now_ms))
        {
            route_type = kRouteTypeDirect;
            out_path = route->out_path;
            out_path_len = route->out_path_len;
            tx_channel = route->preferred_channel;
        }

        uint8_t peer_key16[16];
        uint8_t peer_key32[32];
        if (!deriveDirectSecret(tx_channel, peer_hash, peer_key16, peer_key32))
        {
            if (tx_channel == channel ||
                !deriveDirectSecret(channel, peer_hash, peer_key16, peer_key32))
            {
                MESHCORE_LOG("[MESHCORE] TX direct app-data dropped (no peer secret) peer=%08lX port=%u\n",
                             static_cast<unsigned long>(dest),
                             static_cast<unsigned>(portnum));
                return false;
            }
        }

        constexpr size_t kDirectCipherBudget =
            ((kMeshcoreMaxPayloadSize - 2 - kCipherMacSize) / kCipherBlockSize) * kCipherBlockSize;
        constexpr size_t kDirectPlainPrefix = 2 + 1 + sizeof(portnum);
        if (kDirectCipherBudget <= kDirectPlainPrefix)
        {
            MESHCORE_LOG("[MESHCORE] TX direct app-data dropped (cipher budget too small) peer=%08lX port=%u\n",
                         static_cast<unsigned long>(dest),
                         static_cast<unsigned>(portnum));
            return false;
        }

        size_t body_len = len;
        if (body_len + kDirectPlainPrefix > kDirectCipherBudget)
        {
            body_len = kDirectCipherBudget - kDirectPlainPrefix;
        }

        uint8_t plain[kDirectCipherBudget];
        size_t plain_len = 0;
        plain[plain_len++] = kDirectAppMagic0;
        plain[plain_len++] = kDirectAppMagic1;
        plain[plain_len++] = want_ack ? kDirectAppFlagWantAck : 0x00;
        memcpy(&plain[plain_len], &portnum, sizeof(portnum));
        plain_len += sizeof(portnum);
        memcpy(&plain[plain_len], payload, body_len);
        plain_len += body_len;

        uint8_t peer_payload[kMeshcoreMaxPayloadSize];
        size_t peer_payload_len = 0;
        if (!buildPeerDatagramPayload(peer_hash, self_hash_,
                                      peer_key16, peer_key32,
                                      plain, plain_len,
                                      peer_payload, sizeof(peer_payload), &peer_payload_len))
        {
            MESHCORE_LOG("[MESHCORE] TX direct app-data dropped (build payload fail) peer=%08lX port=%u plain_len=%u\n",
                         static_cast<unsigned long>(dest),
                         static_cast<unsigned>(portnum),
                         static_cast<unsigned>(plain_len));
            return false;
        }

        uint8_t frame[kMeshcoreMaxFrameSize];
        size_t frame_len = 0;

        if (!buildFrameNoTransport(route_type, kPayloadTypeDirectData,
                                   out_path, out_path_len,
                                   peer_payload, peer_payload_len,
                                   frame, sizeof(frame), &frame_len))
        {
            MESHCORE_LOG("[MESHCORE] TX direct app-data dropped (build frame fail) peer=%08lX route=%s path_len=%u port=%u payload_len=%u\n",
                         static_cast<unsigned long>(dest),
                         (route_type == kRouteTypeDirect) ? "direct" : "flood",
                         static_cast<unsigned>(out_path_len),
                         static_cast<unsigned>(portnum),
                         static_cast<unsigned>(peer_payload_len));
            return false;
        }

        bool ok = transmitFrameNow(frame, frame_len, now_ms);
        MESHCORE_LOG("[MESHCORE] TX direct app-data peer=%08lX hash=%02X route=%s path_len=%u port=%u len=%u ok=%u\n",
                     static_cast<unsigned long>(dest),
                     static_cast<unsigned>(peer_hash),
                     (route_type == kRouteTypeDirect) ? "direct" : "flood",
                     static_cast<unsigned>(out_path_len),
                     static_cast<unsigned>(portnum),
                     static_cast<unsigned>(frame_len),
                     ok ? 1U : 0U);
        if (ok && want_ack)
        {
            const uint32_t ack_sig = packetSignature(kPayloadTypeDirectData, out_path_len,
                                                     peer_payload, peer_payload_len);
            trackPendingAppAck(ack_sig, dest, portnum, now_ms);
        }
        return ok;
    }

    uint8_t channel_key16[16];
    uint8_t channel_key32[32];
    uint8_t channel_hash = 0;
    if (!resolveGroupSecret(channel, channel_key16, channel_key32, &channel_hash))
    {
        MESHCORE_LOG("[MESHCORE] TX group app-data dropped (no channel secret) ch=%u port=%u\n",
                     static_cast<unsigned>(channel),
                     static_cast<unsigned>(portnum));
        return false;
    }

    constexpr size_t kGroupCipherBudget =
        ((kMeshcoreMaxPayloadSize - 1 - kCipherMacSize) / kCipherBlockSize) * kCipherBlockSize;
    constexpr size_t kGroupPlainPrefix = 2 + sizeof(uint32_t) + sizeof(portnum);
    if (kGroupCipherBudget <= kGroupPlainPrefix)
    {
        MESHCORE_LOG("[MESHCORE] TX group app-data dropped (cipher budget too small) ch=%u port=%u\n",
                     static_cast<unsigned>(channel),
                     static_cast<unsigned>(portnum));
        return false;
    }
    size_t body_len = len;
    if (body_len + kGroupPlainPrefix > kGroupCipherBudget)
    {
        body_len = kGroupCipherBudget - kGroupPlainPrefix;
    }
    uint8_t plain[kGroupCipherBudget];
    size_t plain_len = 0;
    plain[plain_len++] = kGroupDataMagic0;
    plain[plain_len++] = kGroupDataMagic1;
    memcpy(&plain[plain_len], &node_id_, sizeof(node_id_));
    plain_len += sizeof(node_id_);
    memcpy(&plain[plain_len], &portnum, sizeof(portnum));
    plain_len += sizeof(portnum);
    memcpy(&plain[plain_len], payload, body_len);
    plain_len += body_len;

    uint8_t encrypted[kMeshcoreMaxPayloadSize];
    size_t encrypted_len = encryptThenMac(channel_key16, channel_key32,
                                          encrypted, sizeof(encrypted),
                                          plain, plain_len);
    if (encrypted_len == 0 || encrypted_len > (kMeshcoreMaxPayloadSize - 1))
    {
        MESHCORE_LOG("[MESHCORE] TX group app-data dropped (encrypt fail) ch=%u port=%u plain_len=%u\n",
                     static_cast<unsigned>(channel),
                     static_cast<unsigned>(portnum),
                     static_cast<unsigned>(plain_len));
        return false;
    }

    uint8_t buffer[256];
    size_t index = 0;
    buffer[index++] = buildHeader(kRouteTypeFlood, kPayloadTypeGrpData, kPayloadVer1);
    buffer[index++] = 0; // path_len = 0
    buffer[index++] = channel_hash;
    memcpy(&buffer[index], encrypted, encrypted_len);
    index += encrypted_len;

    return transmitFrameNow(buffer, index, now_ms);
}

bool MeshCoreAdapter::pollIncomingData(MeshIncomingData* out)
{
    if (!initialized_ || !out || app_receive_queue_.empty())
    {
        return false;
    }
    *out = app_receive_queue_.front();
    app_receive_queue_.pop();
    return true;
}

void MeshCoreAdapter::applyConfig(const MeshConfig& config)
{
    config_ = config;
    config_.meshcore_freq_mhz = clampValue<float>(config_.meshcore_freq_mhz, 400.0f, 2500.0f);
    config_.meshcore_bw_khz = clampValue<float>(config_.meshcore_bw_khz, 7.8f, 500.0f);
    config_.meshcore_sf = clampValue<uint8_t>(config_.meshcore_sf, 5, 12);
    config_.meshcore_cr = clampValue<uint8_t>(config_.meshcore_cr, 5, 8);
    config_.tx_power = clampValue<int8_t>(config_.tx_power, 1, 30);
    config_.meshcore_rx_delay_base = clampValue<float>(config_.meshcore_rx_delay_base, 0.0f, 20.0f);
    config_.meshcore_airtime_factor = clampValue<float>(config_.meshcore_airtime_factor, 0.0f, 9.0f);
    config_.meshcore_flood_max = clampValue<uint8_t>(config_.meshcore_flood_max, 0, 64);
    config_.meshcore_channel_slot = clampValue<uint8_t>(config_.meshcore_channel_slot, 0, 14);

    if (config_.meshcore_channel_name[0] == '\0')
    {
        strncpy(config_.meshcore_channel_name, "Public", sizeof(config_.meshcore_channel_name) - 1);
        config_.meshcore_channel_name[sizeof(config_.meshcore_channel_name) - 1] = '\0';
    }

    if (!identity_.isReady())
    {
        identity_.init();
    }
    if (identity_.isReady())
    {
        self_hash_ = identity_.selfHash();
        const NodeId identity_node = deriveNodeIdFromPubkey(identity_.publicKey(), MeshCoreIdentity::kPubKeySize);
        if (identity_node != 0)
        {
            node_id_ = identity_node;
        }
    }
    else
    {
        self_hash_ = static_cast<uint8_t>(node_id_ & 0xFFU);
    }

    const bool has_primary_key = !isZeroKey(config_.primary_key, sizeof(config_.primary_key));
    const bool has_secondary_key = !isZeroKey(config_.secondary_key, sizeof(config_.secondary_key));
    const uint8_t primary_hash = has_primary_key ? computeChannelHash(config_.primary_key) : 0xFF;
    const uint8_t secondary_hash = has_secondary_key ? computeChannelHash(config_.secondary_key) : 0xFF;
    const bool has_public = shouldUsePublicChannelFallback(config_);
    const uint8_t public_hash = has_public ? computeChannelHash(kPublicGroupPsk) : 0xFF;
    MESHCORE_LOG("[MESHCORE] apply cfg preset=%u freq=%.3f bw=%.3f sf=%u cr=%u(4/%u) txp=%d tx_en=%u repeat=%u flood_max=%u multi_acks=%u slot=%u ch='%s' hash[p=%02X s=%02X pub=%02X] identity[ready=%u self=%02X]\n",
                 static_cast<unsigned>(config_.meshcore_region_preset),
                 static_cast<double>(config_.meshcore_freq_mhz),
                 static_cast<double>(config_.meshcore_bw_khz),
                 static_cast<unsigned>(config_.meshcore_sf),
                 static_cast<unsigned>(config_.meshcore_cr),
                 static_cast<unsigned>(config_.meshcore_cr),
                 static_cast<int>(config_.tx_power),
                 config_.tx_enabled ? 1U : 0U,
                 config_.meshcore_client_repeat ? 1U : 0U,
                 static_cast<unsigned>(config_.meshcore_flood_max),
                 config_.meshcore_multi_acks ? 1U : 0U,
                 static_cast<unsigned>(config_.meshcore_channel_slot),
                 config_.meshcore_channel_name,
                 static_cast<unsigned>(primary_hash),
                 static_cast<unsigned>(secondary_hash),
                 static_cast<unsigned>(public_hash),
                 identity_.isReady() ? 1U : 0U,
                 static_cast<unsigned>(self_hash_));

    scheduled_tx_.clear();
    peer_routes_.clear();
    pending_app_acks_.clear();
    key_verify_session_ = KeyVerifySession{};
    verified_peers_.clear();
    last_auto_discover_ms_ = 0;
    last_auto_discover_hash_ = 0;
    loadPeerPubKeysFromPrefs();

#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    if (board_.isRadioOnline())
    {
        board_.configureLoraRadio(config_.meshcore_freq_mhz,
                                  config_.meshcore_bw_khz,
                                  config_.meshcore_sf,
                                  config_.meshcore_cr,
                                  config_.tx_power,
                                  16,
                                  kLoraSyncWordPrivate,
                                  2);
    }
#endif
    initialized_ = true;
}

void MeshCoreAdapter::setUserInfo(const char* long_name, const char* short_name)
{
    user_long_name_ = (long_name && long_name[0]) ? long_name : "";
    user_short_name_ = (short_name && short_name[0]) ? short_name : "";
    if (user_short_name_.size() > 4)
    {
        user_short_name_.resize(4);
    }
}

void MeshCoreAdapter::setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent)
{
    if (!duty_cycle_enabled || util_percent == 0)
    {
        min_tx_interval_ms_ = 0;
        return;
    }
    if (util_percent <= 25)
    {
        min_tx_interval_ms_ = 4000;
    }
    else if (util_percent <= 50)
    {
        min_tx_interval_ms_ = 2000;
    }
    else
    {
        min_tx_interval_ms_ = 0;
    }
}

void MeshCoreAdapter::setPrivacyConfig(uint8_t encrypt_mode)
{
    encrypt_mode_ = encrypt_mode;
    pki_enabled_ = (encrypt_mode_ != 0);
    if (!pki_enabled_)
    {
        key_verify_session_ = KeyVerifySession{};
    }
}

void MeshCoreAdapter::setLastRxStats(float rssi, float snr)
{
    last_rx_rssi_ = rssi;
    last_rx_snr_ = snr;
    if (std::isfinite(rssi) && std::isfinite(snr))
    {
        last_noise_floor_dbm_ = static_cast<int16_t>(std::lround(rssi - snr));
    }
}

bool MeshCoreAdapter::isReady() const
{
    return initialized_ && board_.isRadioOnline();
}

bool MeshCoreAdapter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    if (!initialized_ || !out_data || max_len == 0)
    {
        return false;
    }

    if (!has_pending_raw_packet_)
    {
        return false;
    }

    size_t copy_len = (last_raw_packet_len_ < max_len) ? last_raw_packet_len_ : max_len;
    memcpy(out_data, last_raw_packet_, copy_len);
    out_len = copy_len;
    has_pending_raw_packet_ = false;
    return true;
}

void MeshCoreAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    handleRawPacketInternal(data, size, false);
}

void MeshCoreAdapter::handleRawPacketInternal(const uint8_t* data, size_t size, bool allow_duplicate)
{
    if (!data || size < 2 || size > kMeshcoreMaxFrameSize)
    {
        MESHCORE_LOG("[MESHCORE] RX drop invalid frame len=%u\n",
                     static_cast<unsigned>(size));
        return;
    }

    const float air_ms_f = estimateLoRaAirtimeMs(size,
                                                 config_.meshcore_bw_khz,
                                                 config_.meshcore_sf,
                                                 config_.meshcore_cr);
    if (air_ms_f > 0.0f && std::isfinite(air_ms_f))
    {
        rx_airtime_ms_ = saturatingAddU32(
            rx_airtime_ms_,
            static_cast<uint32_t>(std::lround(air_ms_f)));
    }

    const uint8_t header = data[0];
    const uint8_t header_route = header & 0x03;
    const uint8_t header_type = (header >> 2) & 0x0F;
    const uint8_t header_ver = (header >> 6) & 0x03;

    if (size <= sizeof(last_raw_packet_))
    {
        memcpy(last_raw_packet_, data, size);
        last_raw_packet_len_ = size;
        has_pending_raw_packet_ = true;
    }

    ParsedPacket parsed;
    if (!parsePacket(data, size, &parsed))
    {
        MESHCORE_LOG("[MESHCORE] RX parse fail len=%u hdr=%02X route=%u type=%u ver=%u hex=%s\n",
                     static_cast<unsigned>(size),
                     static_cast<unsigned>(header),
                     static_cast<unsigned>(header_route),
                     static_cast<unsigned>(header_type),
                     static_cast<unsigned>(header_ver),
                     toHex(data, size).c_str());
        return;
    }
    if (parsed.payload_ver != kPayloadVer1)
    {
        MESHCORE_LOG("[MESHCORE] RX drop payload ver=%u (want=%u) len=%u type=%u route=%u\n",
                     static_cast<unsigned>(parsed.payload_ver),
                     static_cast<unsigned>(kPayloadVer1),
                     static_cast<unsigned>(size),
                     static_cast<unsigned>(parsed.payload_type),
                     static_cast<unsigned>(parsed.route_type));
        return;
    }

    const uint32_t now_ms = millis();
    prunePendingAppAcks(now_ms);
    prunePeerRoutes(now_ms);

    const uint32_t packet_sig = packetSignature(parsed.payload_type, parsed.path_len,
                                                parsed.payload, parsed.payload_len);
    const bool seen = hasSeenSignature(packet_sig, now_ms);
    if (seen && !allow_duplicate)
    {
        MESHCORE_LOG("[MESHCORE] RX dedup pkt_sig=%08lX len=%u type=%u route=%u\n",
                     static_cast<unsigned long>(packet_sig),
                     static_cast<unsigned>(size),
                     static_cast<unsigned>(parsed.payload_type),
                     static_cast<unsigned>(parsed.route_type));
        return;
    }
    const uint32_t frame_sig = hashFrame(data, size);

    MESHCORE_LOG("[MESHCORE] RX raw len=%u pkt_sig=%08lX raw_sig=%08lX hex=%s\n",
                 static_cast<unsigned>(size),
                 static_cast<unsigned long>(packet_sig),
                 static_cast<unsigned long>(frame_sig),
                 toHex(data, size).c_str());

    const bool is_flood_route = (parsed.route_type == kRouteTypeFlood ||
                                 parsed.route_type == kRouteTypeTransportFlood);
    const bool is_direct_route = (parsed.route_type == kRouteTypeDirect ||
                                  parsed.route_type == kRouteTypeTransportDirect);

    auto decodeMultipartAck = [&](const uint8_t* payload, size_t payload_len,
                                  uint32_t* out_ack_sig, uint8_t* out_remaining) -> bool
    {
        if (!payload || payload_len < 5 || !out_ack_sig || !out_remaining)
        {
            return false;
        }
        const uint8_t wrapped_type = payload[0] & 0x0F;
        if (wrapped_type != kPayloadTypeAck)
        {
            return false;
        }
        *out_remaining = static_cast<uint8_t>(payload[0] >> 4);
        memcpy(out_ack_sig, payload + 1, sizeof(uint32_t));
        return true;
    };

    auto routeDirectAckBurst = [&](const uint8_t* path, size_t path_len, uint32_t ack_sig,
                                   uint8_t remaining, uint32_t delay_ms) -> void
    {
        if (!config_.tx_enabled || !path || path_len == 0 || path_len > kMeshcoreMaxPathSize)
        {
            return;
        }

        auto queueDirect = [&](uint8_t payload_type, const uint8_t* payload, size_t payload_len,
                               uint32_t tx_delay) -> bool
        {
            uint8_t frame[kMeshcoreMaxFrameSize];
            size_t frame_len = 0;
            if (!buildFrameNoTransport(kRouteTypeDirect, payload_type,
                                       path, path_len,
                                       payload, payload_len,
                                       frame, sizeof(frame), &frame_len))
            {
                return false;
            }
            return enqueueScheduled(frame, frame_len, tx_delay);
        };

        const uint8_t extra_acks = config_.meshcore_multi_acks ? 1U : 0U;
        uint32_t tx_delay = delay_ms;
        for (uint8_t n = 0; n < extra_acks; ++n)
        {
            uint8_t multi_ack[5] = {};
            multi_ack[0] = static_cast<uint8_t>(((remaining & 0x0F) << 4) | kPayloadTypeAck);
            memcpy(multi_ack + 1, &ack_sig, sizeof(ack_sig));
            queueDirect(kPayloadTypeMultipart, multi_ack, sizeof(multi_ack), tx_delay);
            tx_delay += kAckSpacingMs;
        }

        uint8_t ack_payload[sizeof(uint32_t)] = {};
        memcpy(ack_payload, &ack_sig, sizeof(ack_sig));
        queueDirect(kPayloadTypeAck, ack_payload, sizeof(ack_payload), tx_delay);
    };

    auto quantizeSnrQuarterDb = [&]() -> uint8_t
    {
        int snr_scaled = 0;
        if (std::isfinite(last_rx_snr_))
        {
            snr_scaled = static_cast<int>(std::lround(last_rx_snr_ * 4.0f));
        }
        if (snr_scaled > 127)
        {
            snr_scaled = 127;
        }
        if (snr_scaled < -128)
        {
            snr_scaled = -128;
        }
        return static_cast<uint8_t>(static_cast<int8_t>(snr_scaled));
    };

    auto publishMeshcoreNodeInfo = [&](NodeId node,
                                       const char* short_name,
                                       const char* long_name,
                                       uint8_t role,
                                       uint8_t hops,
                                       float snr,
                                       float rssi,
                                       uint32_t ts) -> void
    {
        if (node == 0)
        {
            return;
        }

        if (!is_valid_epoch(ts))
        {
            ts = now_message_timestamp();
        }

        sys::EventBus::publish(
            new sys::NodeInfoUpdateEvent(node,
                                         short_name ? short_name : "",
                                         long_name ? long_name : "",
                                         snr,
                                         rssi,
                                         ts,
                                         static_cast<uint8_t>(chat::contacts::NodeProtocolType::MeshCore),
                                         role,
                                         hops),
            0);
    };

    auto publishMeshcorePosition = [&](NodeId node, int32_t lat_i6, int32_t lon_i6, uint32_t ts) -> void
    {
        if (node == 0)
        {
            return;
        }

        if (!is_valid_epoch(ts))
        {
            ts = now_message_timestamp();
        }

        const int64_t lat_i7_raw = static_cast<int64_t>(lat_i6) * 10LL;
        const int64_t lon_i7_raw = static_cast<int64_t>(lon_i6) * 10LL;
        const int32_t lat_i7 = static_cast<int32_t>(clampValue<int64_t>(
            lat_i7_raw,
            static_cast<int64_t>(std::numeric_limits<int32_t>::min()),
            static_cast<int64_t>(std::numeric_limits<int32_t>::max())));
        const int32_t lon_i7 = static_cast<int32_t>(clampValue<int64_t>(
            lon_i7_raw,
            static_cast<int64_t>(std::numeric_limits<int32_t>::min()),
            static_cast<int64_t>(std::numeric_limits<int32_t>::max())));

        sys::EventBus::publish(
            new sys::NodePositionUpdateEvent(node,
                                             lat_i7,
                                             lon_i7,
                                             false,
                                             0,
                                             ts,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0),
            0);
    };

    auto handleZeroHopDiscoverControl = [&]() -> bool
    {
        if (parsed.payload_len == 0)
        {
            return false;
        }

        DecodedDiscoverRequest req{};
        if (decodeDiscoverRequest(parsed.payload, parsed.payload_len, &req))
        {
            const uint8_t local_type = config_.meshcore_client_repeat
                                           ? kAdvertTypeRepeater
                                           : kAdvertTypeChat;
            if (!discoverFilterMatchesType(req.type_filter, local_type))
            {
                return true;
            }

            const uint32_t local_mod_ts = now_epoch_seconds();
            if (req.since != 0 && is_valid_epoch(req.since) &&
                is_valid_epoch(local_mod_ts) && local_mod_ts < req.since)
            {
                return true;
            }

            uint8_t resp_payload[6 + kMeshcorePubKeySize] = {};
            size_t resp_len = 0;
            resp_payload[resp_len++] = static_cast<uint8_t>(kControlSubtypeDiscoverResp |
                                                            (local_type & 0x0F));
            resp_payload[resp_len++] = quantizeSnrQuarterDb();
            memcpy(resp_payload + resp_len, &req.tag, sizeof(req.tag));
            resp_len += sizeof(req.tag);
            if (!identity_.isReady())
            {
                MESHCORE_LOG("[MESHCORE] RX DISCOVER_REQ ignored (identity unavailable)\n");
                return true;
            }

            const size_t key_len = req.prefix_only ? kMeshcorePubKeyPrefixSize : kMeshcorePubKeySize;
            memcpy(resp_payload + resp_len, identity_.publicKey(), key_len);
            resp_len += key_len;

            uint8_t frame[kMeshcoreMaxFrameSize] = {};
            size_t frame_len = 0;
            if (!buildFrameNoTransport(kRouteTypeDirect, kPayloadTypeControl,
                                       nullptr, 0,
                                       resp_payload, resp_len,
                                       frame, sizeof(frame), &frame_len))
            {
                return true;
            }

            float air_ms_f = estimateLoRaAirtimeMs(frame_len,
                                                   config_.meshcore_bw_khz,
                                                   config_.meshcore_sf,
                                                   config_.meshcore_cr);
            if (!std::isfinite(air_ms_f) || air_ms_f <= 0.0f)
            {
                air_ms_f = 50.0f;
            }
            // Align with upstream MeshCore getRetransmitDelay()*4:
            //   t = (airtime * 52 / 50) / 2; delay = random(0..4) * t * 4
            uint32_t t_ms = static_cast<uint32_t>(std::lround((air_ms_f * 52.0f / 50.0f) / 2.0f));
            if (t_ms == 0)
            {
                t_ms = 1;
            }
            const uint32_t delay_ms = static_cast<uint32_t>(random(0, 5)) * t_ms * 4U;
            if (config_.tx_enabled)
            {
                enqueueScheduled(frame, frame_len, delay_ms);
            }

            MESHCORE_LOG("[MESHCORE] RX DISCOVER_REQ tag=%08lX filter=%02X since=%lu prefix=%u -> RESP len=%u delay=%lu\n",
                         static_cast<unsigned long>(req.tag),
                         static_cast<unsigned>(req.type_filter),
                         static_cast<unsigned long>(req.since),
                         req.prefix_only ? 1U : 0U,
                         static_cast<unsigned>(resp_len),
                         static_cast<unsigned long>(delay_ms));
            return true;
        }

        DecodedDiscoverResponse resp{};
        if (decodeDiscoverResponse(parsed.payload, parsed.payload_len, &resp))
        {
            if (resp.pubkey_len == 0 || !resp.pubkey)
            {
                return true;
            }
            if (resp.pubkey[0] == self_hash_)
            {
                return true;
            }

            const NodeId node = deriveNodeIdFromPubkey(resp.pubkey, resp.pubkey_len);
            const uint8_t hops = (parsed.path_len <= 255) ? static_cast<uint8_t>(parsed.path_len) : 0xFF;
            const float snr = static_cast<float>(resp.snr_qdb) / 4.0f;
            const float rssi = std::isfinite(last_rx_rssi_) ? last_rx_rssi_ : NAN;
            const uint32_t ts = now_message_timestamp();

            rememberPeerNodeId(resp.pubkey[0], node, now_ms);
            if (resp.pubkey_len == kMeshcorePubKeySize)
            {
                rememberPeerPubKey(resp.pubkey, now_ms, false);
            }
            publishMeshcoreNodeInfo(node,
                                    "",
                                    "",
                                    mapAdvertTypeToRole(resp.node_type),
                                    hops,
                                    snr,
                                    rssi,
                                    ts);

            MESHCORE_LOG("[MESHCORE] RX DISCOVER_RESP tag=%08lX type=%u snr_qdb=%d hash=%02X key_len=%u\n",
                         static_cast<unsigned long>(resp.tag),
                         static_cast<unsigned>(resp.node_type),
                         static_cast<int>(resp.snr_qdb),
                         static_cast<unsigned>(resp.pubkey[0]),
                         static_cast<unsigned>(resp.pubkey_len));
            return true;
        }

        return false;
    };

    // TRACE direct packets use path[] for accumulated SNR and route hashes live in payload.
    if (is_direct_route && parsed.payload_type == kPayloadTypeTrace && parsed.payload_len >= 9)
    {
        const uint8_t flags = parsed.payload[8];
        const uint8_t path_hash_size_bits = flags & 0x03;
        size_t path_hash_size = static_cast<size_t>(1U << path_hash_size_bits);
        if (path_hash_size == 0 || path_hash_size > 4)
        {
            path_hash_size = 1;
        }

        const size_t trace_hashes_len = parsed.payload_len - 9;
        const size_t offset = parsed.path_len * path_hash_size;
        if (offset >= trace_hashes_len)
        {
            uint32_t tag = 0;
            uint32_t auth = 0;
            memcpy(&tag, parsed.payload, sizeof(tag));
            memcpy(&auth, parsed.payload + 4, sizeof(auth));
            MESHCORE_LOG("[MESHCORE] RX TRACE done tag=%08lX auth=%08lX hops=%u route=%s\n",
                         static_cast<unsigned long>(tag),
                         static_cast<unsigned long>(auth),
                         static_cast<unsigned>(parsed.path_len),
                         is_flood_route ? "flood" : "direct");
            return;
        }

        if (parsed.payload[9 + offset] != self_hash_)
        {
            return;
        }
        if (!config_.meshcore_client_repeat)
        {
            return;
        }
        if (parsed.path_len >= kMeshcoreMaxPathSize || (size + 1) > kMeshcoreMaxFrameSize)
        {
            return;
        }

        const size_t payload_start = static_cast<size_t>(parsed.payload - data);
        std::vector<uint8_t> fwd(size + 1);
        memcpy(fwd.data(), data, parsed.path_len_index);
        fwd[parsed.path_len_index] = static_cast<uint8_t>(parsed.path_len + 1);
        if (parsed.path_len > 0)
        {
            memcpy(&fwd[parsed.path_len_index + 1], parsed.path, parsed.path_len);
        }
        fwd[parsed.path_len_index + 1 + parsed.path_len] = quantizeSnrQuarterDb();
        const size_t new_payload_start = parsed.path_len_index + 1 + parsed.path_len + 1;
        memcpy(&fwd[new_payload_start], &data[payload_start], size - payload_start);
        enqueueScheduled(fwd.data(), fwd.size(), 0);
        return;
    }

    // Upstream behavior: this subset of control payloads is only valid as zero-hop direct.
    // Consume discover control on zero-hop and never route this high-bit subset.
    if (is_direct_route &&
        parsed.payload_type == kPayloadTypeControl &&
        parsed.payload_len > 0 &&
        (parsed.payload[0] & 0x80U) != 0)
    {
        if (parsed.path_len == 0)
        {
            handleZeroHopDiscoverControl();
            Event ev{};
            ev.type = Event::Type::ControlData;
            ev.peer_hash = parsed.payload_len > 0 ? parsed.payload[0] : 0;
            ev.peer_node = 0;
            ev.flags = static_cast<uint8_t>(parsed.path_len);
            if (std::isfinite(last_rx_snr_))
            {
                ev.snr_qdb = static_cast<int8_t>(std::lround(last_rx_snr_ * 4.0f));
            }
            if (std::isfinite(last_rx_rssi_))
            {
                ev.rssi_dbm = static_cast<int8_t>(std::lround(last_rx_rssi_));
            }
            ev.payload.assign(parsed.payload, parsed.payload + parsed.payload_len);
            pushEvent(std::move(ev));
        }
        return;
    }

    // Direct routing hop forwarding: only the addressed next-hop should retransmit.
    if (is_direct_route && parsed.path_len > 0)
    {
        if (parsed.payload_type == kPayloadTypeAck && parsed.payload_len >= sizeof(uint32_t))
        {
            uint32_t ack_sig = 0;
            memcpy(&ack_sig, parsed.payload, sizeof(ack_sig));
            consumePendingAppAck(ack_sig, now_ms);
        }
        else if (parsed.payload_type == kPayloadTypeMultipart)
        {
            uint32_t ack_sig = 0;
            uint8_t remaining = 0;
            if (decodeMultipartAck(parsed.payload, parsed.payload_len, &ack_sig, &remaining))
            {
                consumePendingAppAck(ack_sig, now_ms);
            }
        }

        if (parsed.path[0] != self_hash_)
        {
            return;
        }

        if (!config_.meshcore_client_repeat || size <= 2)
        {
            return;
        }

        const size_t payload_start = static_cast<size_t>(parsed.payload - data);
        const size_t new_path_len = parsed.path_len - 1;

        auto copyForwardPath = [&](uint8_t* out_path) -> void
        {
            if (!out_path || new_path_len == 0)
            {
                return;
            }
            memcpy(out_path, parsed.path + 1, new_path_len);
        };

        if (parsed.payload_type == kPayloadTypeAck && parsed.payload_len >= sizeof(uint32_t))
        {
            uint32_t ack_sig = 0;
            memcpy(&ack_sig, parsed.payload, sizeof(ack_sig));
            if (new_path_len > 0)
            {
                uint8_t next_path[kMeshcoreMaxPathSize] = {};
                copyForwardPath(next_path);
                routeDirectAckBurst(next_path, new_path_len, ack_sig, 0, 0);
            }
            return;
        }

        if (parsed.payload_type == kPayloadTypeMultipart)
        {
            uint32_t ack_sig = 0;
            uint8_t remaining = 0;
            if (decodeMultipartAck(parsed.payload, parsed.payload_len, &ack_sig, &remaining))
            {
                if (new_path_len > 0)
                {
                    uint8_t next_path[kMeshcoreMaxPathSize] = {};
                    copyForwardPath(next_path);
                    routeDirectAckBurst(next_path, new_path_len, ack_sig, remaining,
                                        (static_cast<uint32_t>(remaining) + 1U) * kAckSpacingMs);
                }
            }
            return;
        }

        std::vector<uint8_t> fwd(size - 1);
        memcpy(fwd.data(), data, parsed.path_len_index);
        fwd[parsed.path_len_index] = static_cast<uint8_t>(new_path_len);
        if (new_path_len > 0)
        {
            memcpy(&fwd[parsed.path_len_index + 1], parsed.path + 1, new_path_len);
        }
        const size_t new_payload_start = parsed.path_len_index + 1 + new_path_len;
        memcpy(&fwd[new_payload_start], &data[payload_start], size - payload_start);
        enqueueScheduled(fwd.data(), fwd.size(), 0);
        MESHCORE_LOG("[MESHCORE] DIRECT fwd path=%u->%u type=%u\n",
                     static_cast<unsigned>(parsed.path_len),
                     static_cast<unsigned>(new_path_len),
                     static_cast<unsigned>(parsed.payload_type));
        return;
    }

    // MeshCore repeater behavior: only route payload classes that upstream flood-routes.
    auto shouldFloodRepeatPayload = [&](uint8_t payload_type) -> bool
    {
        switch (payload_type)
        {
        case kPayloadTypeAck:
        case kPayloadTypeReq:
        case kPayloadTypeResponse:
        case kPayloadTypeTxtMsg:
        case kPayloadTypeGrpTxt:
        case kPayloadTypeGrpData:
        case kPayloadTypePath:
        case kPayloadTypeDirectData:
        case kPayloadTypeAdvert:
            return true;
        default:
            return false;
        }
    };

    const bool is_anon_req_payload = (parsed.payload_type == kPayloadTypeAnonReq) &&
                                     isAnonReqCipherShape(parsed.payload_len) &&
                                     !isPeerCipherShape(parsed.payload_len);
    const bool is_peer_payload_candidate = isPeerPayloadType(parsed.payload_type) &&
                                           isPeerCipherShape(parsed.payload_len) &&
                                           !is_anon_req_payload;
    const bool flood_peer_for_self = is_flood_route &&
                                     is_peer_payload_candidate &&
                                     parsed.payload_len >= 1 &&
                                     parsed.payload[0] == self_hash_;
    const bool flood_anon_for_self = is_flood_route &&
                                     is_anon_req_payload &&
                                     parsed.payload_len >= 1 &&
                                     parsed.payload[0] == self_hash_;
    const bool is_multipart_ack = (parsed.payload_type == kPayloadTypeMultipart &&
                                   parsed.payload_len > 0 &&
                                   ((parsed.payload[0] & 0x0F) == kPayloadTypeAck));
    if (config_.meshcore_client_repeat && is_flood_route &&
        !flood_peer_for_self &&
        !flood_anon_for_self &&
        !is_multipart_ack &&
        shouldFloodRepeatPayload(parsed.payload_type) &&
        parsed.path_len < config_.meshcore_flood_max &&
        (parsed.path_len + kMeshcorePathHashSize) <= kMeshcoreMaxPathSize &&
        (size + kMeshcorePathHashSize) <= kMeshcoreMaxFrameSize)
    {
        bool self_in_path = false;
        for (size_t i = 0; i < parsed.path_len; ++i)
        {
            if (parsed.path[i] == self_hash_)
            {
                self_in_path = true;
                break;
            }
        }

        if (!self_in_path)
        {
            const size_t path_start = parsed.path_len_index + 1;
            const size_t payload_start = static_cast<size_t>(parsed.payload - data);

            std::vector<uint8_t> fwd(size + kMeshcorePathHashSize);
            memcpy(fwd.data(), data, path_start + parsed.path_len);
            fwd[parsed.path_len_index] = static_cast<uint8_t>(parsed.path_len + 1);
            fwd[path_start + parsed.path_len] = self_hash_;
            memcpy(&fwd[path_start + parsed.path_len + 1],
                   &data[payload_start],
                   size - payload_start);

            const float air_ms_f = estimateLoRaAirtimeMs(fwd.size(),
                                                         config_.meshcore_bw_khz,
                                                         config_.meshcore_sf,
                                                         config_.meshcore_cr);
            const uint32_t air_ms = (air_ms_f > 0.0f)
                                        ? static_cast<uint32_t>(std::lround(air_ms_f))
                                        : 0U;
            const float score = scoreFromSnr(last_rx_snr_, config_.meshcore_sf, size);
            const uint32_t rx_delay = computeRxDelayMs(config_.meshcore_rx_delay_base, score, air_ms);
            const uint32_t tx_step = static_cast<uint32_t>(
                std::lround(static_cast<float>(air_ms) * config_.meshcore_airtime_factor));
            const uint32_t tx_delay = static_cast<uint32_t>(random(0, 6)) * tx_step;
            const uint32_t total_delay = rx_delay + tx_delay;
            enqueueScheduled(fwd.data(), fwd.size(), total_delay);

            MESHCORE_LOG("[MESHCORE] REPEAT queued path=%u->%u flood_max=%u delay=%lu\n",
                         static_cast<unsigned>(parsed.path_len),
                         static_cast<unsigned>(parsed.path_len + 1),
                         static_cast<unsigned>(config_.meshcore_flood_max),
                         static_cast<unsigned long>(total_delay));
        }
    }

    const bool is_peer_payload = isPeerPayloadType(parsed.payload_type) &&
                                 isPeerCipherShape(parsed.payload_len) &&
                                 !is_anon_req_payload;
    const bool is_legacy_text_payload = (parsed.payload_type == kPayloadTypeTxtMsg &&
                                         parsed.payload_len > 1 &&
                                         !is_peer_payload);
    const bool is_group_text_payload = (parsed.payload_type == kPayloadTypeGrpTxt &&
                                        parsed.payload_len > (1 + kCipherMacSize));
    const bool is_group_data_payload = (parsed.payload_type == kPayloadTypeGrpData &&
                                        parsed.payload_len > (1 + kCipherMacSize));
    const bool is_raw_payload = (parsed.payload_type == kPayloadTypeRawCustom &&
                                 parsed.payload_len > sizeof(uint32_t));

    // Legacy ACK behavior for legacy text/raw payloads.
    if (config_.tx_enabled && (is_legacy_text_payload || is_raw_payload))
    {
        uint8_t ack_frame[6] = {};
        ack_frame[0] = buildHeader(kRouteTypeFlood, kPayloadTypeAck, kPayloadVer1);
        ack_frame[1] = 0;
        memcpy(&ack_frame[2], &frame_sig, sizeof(frame_sig));

        if (config_.meshcore_multi_acks)
        {
            uint8_t multi_ack[7] = {};
            multi_ack[0] = buildHeader(kRouteTypeFlood, kPayloadTypeMultipart, kPayloadVer1);
            multi_ack[1] = 0;
            multi_ack[2] = static_cast<uint8_t>((1U << 4) | kPayloadTypeAck);
            memcpy(&multi_ack[3], &frame_sig, sizeof(frame_sig));
            enqueueScheduled(multi_ack, sizeof(multi_ack), kAckDelayMs);
            enqueueScheduled(ack_frame, sizeof(ack_frame), kAckDelayMs + kAckSpacingMs);
        }
        else
        {
            enqueueScheduled(ack_frame, sizeof(ack_frame), kAckDelayMs);
        }
    }

    auto fill_rx_meta = [&](RxMeta& meta, bool direct)
    {
        meta.rx_timestamp_ms = now_ms;
        uint32_t epoch_s = now_epoch_seconds();
        if (is_valid_epoch(epoch_s))
        {
            meta.rx_timestamp_s = epoch_s;
            meta.time_source = RxTimeSource::DeviceUtc;
        }
        else
        {
            meta.time_source = RxTimeSource::Uptime;
            meta.rx_timestamp_s = meta.rx_timestamp_ms / 1000U;
        }
        meta.origin = RxOrigin::Mesh;
        meta.direct = direct;
        meta.hop_count = (parsed.path_len <= 255) ? static_cast<uint8_t>(parsed.path_len) : 0xFF;
        meta.hop_limit = 0xFF;
        meta.wire_flags = data[0];
        if (parsed.path_len > 0)
        {
            meta.next_hop = parsed.path[0];
            meta.relay_node = parsed.path[parsed.path_len - 1];
        }
        if (std::isfinite(last_rx_rssi_))
        {
            meta.rssi_dbm_x10 = static_cast<int16_t>(std::lround(last_rx_rssi_ * 10.0f));
        }
        if (std::isfinite(last_rx_snr_))
        {
            meta.snr_db_x10 = static_cast<int16_t>(std::lround(last_rx_snr_ * 10.0f));
        }
    };

    auto sendPeerDatagram = [&](uint8_t payload_type, uint8_t dest_hash, ChannelId channel,
                                const uint8_t* plain, size_t plain_len,
                                uint8_t route_type, const uint8_t* route_path, size_t route_path_len,
                                uint32_t delay_ms) -> bool
    {
        if (!config_.tx_enabled || !plain || plain_len == 0)
        {
            return false;
        }

        uint8_t key16[16];
        uint8_t key32[32];
        if (!deriveDirectSecret(channel, dest_hash, key16, key32))
        {
            return false;
        }

        uint8_t payload[kMeshcoreMaxPayloadSize];
        size_t payload_len = 0;
        if (!buildPeerDatagramPayload(dest_hash, self_hash_,
                                      key16, key32,
                                      plain, plain_len,
                                      payload, sizeof(payload), &payload_len))
        {
            return false;
        }

        uint8_t frame[kMeshcoreMaxFrameSize];
        size_t frame_len = 0;
        if (!buildFrameNoTransport(route_type, payload_type,
                                   route_path, route_path_len,
                                   payload, payload_len,
                                   frame, sizeof(frame), &frame_len))
        {
            return false;
        }

        if (delay_ms > 0)
        {
            return enqueueScheduled(frame, frame_len, delay_ms);
        }

        uint32_t tx_now = millis();
        if (transmitFrameNow(frame, frame_len, tx_now))
        {
            return true;
        }
        return enqueueScheduled(frame, frame_len, 50);
    };

    auto sendPathReturn = [&](uint8_t dest_hash, ChannelId channel,
                              const uint8_t* return_path, size_t return_path_len,
                              uint8_t route_type, const uint8_t* route_path, size_t route_path_len,
                              uint8_t extra_type, const uint8_t* extra, size_t extra_len,
                              uint32_t delay_ms) -> bool
    {
        uint8_t plain[kMeshcoreMaxPayloadSize];
        size_t plain_len = 0;
        if (!buildPathPlain(return_path, return_path_len,
                            extra_type, extra, extra_len,
                            plain, sizeof(plain), &plain_len))
        {
            return false;
        }
        return sendPeerDatagram(kPayloadTypePath, dest_hash, channel,
                                plain, plain_len,
                                route_type, route_path, route_path_len,
                                delay_ms);
    };

    auto sendPeerAck = [&](uint8_t src_hash, ChannelId channel, uint32_t ack_value) -> void
    {
        if (!config_.tx_enabled)
        {
            return;
        }

        uint8_t ack_payload[sizeof(uint32_t)] = {};
        memcpy(ack_payload, &ack_value, sizeof(ack_payload));

        if (is_flood_route)
        {
            sendPathReturn(src_hash, channel,
                           parsed.path, parsed.path_len,
                           kRouteTypeFlood, nullptr, 0,
                           kPayloadTypeAck, ack_payload, sizeof(ack_payload),
                           kAckDelayMs);
            return;
        }

        const PeerRouteEntry* route = selectPeerRouteByHash(src_hash, now_ms);
        uint8_t route_type = kRouteTypeFlood;
        const uint8_t* route_path = nullptr;
        size_t route_path_len = 0;
        if (route)
        {
            route_type = kRouteTypeDirect;
            route_path = route->out_path;
            route_path_len = route->out_path_len;
        }

        sendPathReturn(src_hash, channel,
                       nullptr, 0,
                       route_type, route_path, route_path_len,
                       kPayloadTypeAck, ack_payload, sizeof(ack_payload),
                       kAckDelayMs);
    };

    if (is_anon_req_payload)
    {
        const uint8_t dest_hash = parsed.payload[0];
        if (dest_hash != self_hash_)
        {
            return;
        }

        if (!identity_.isReady())
        {
            MESHCORE_LOG("[MESHCORE] RX ANON_REQ drop (identity unavailable) len=%u\n",
                         static_cast<unsigned>(parsed.payload_len));
            return;
        }

        const uint8_t* sender_pubkey = parsed.payload + 1;
        const uint8_t src_hash = sender_pubkey[0];
        const uint8_t* cipher = parsed.payload + 1 + kMeshcorePubKeySize;
        const size_t cipher_len = parsed.payload_len - 1 - kMeshcorePubKeySize;

        uint8_t shared_secret[kCipherHmacKeySize] = {};
        if (!identity_.deriveSharedSecret(sender_pubkey, shared_secret))
        {
            MESHCORE_LOG("[MESHCORE] RX ANON_REQ drop (shared secret failed) src=%02X\n",
                         static_cast<unsigned>(src_hash));
            return;
        }

        uint8_t key16[kCipherKeySize] = {};
        uint8_t key32[kCipherHmacKeySize] = {};
        sharedSecretToKeys(shared_secret, key16, key32);

        uint8_t plain[kMeshcoreMaxPayloadSize] = {};
        size_t plain_len = 0;
        if (!macThenDecrypt(key16, key32, cipher, cipher_len, plain, &plain_len))
        {
            MESHCORE_LOG("[MESHCORE] RX ANON_REQ decrypt failed src=%02X len=%u\n",
                         static_cast<unsigned>(src_hash),
                         static_cast<unsigned>(parsed.payload_len));
            return;
        }
        plain_len = trimTrailingZeros(plain, plain_len);

        const NodeId sender_node = deriveNodeIdFromPubkey(sender_pubkey, kMeshcorePubKeySize);
        rememberPeerPubKey(sender_pubkey, now_ms, true);
        rememberPeerNodeId(src_hash, sender_node, now_ms);

        uint32_t tag = 0;
        if (plain_len >= sizeof(tag))
        {
            memcpy(&tag, plain, sizeof(tag));
        }
        MESHCORE_LOG("[MESHCORE] RX ANON_REQ src=%02X node=%08lX len=%u route=%s tag=%08lX\n",
                     static_cast<unsigned>(src_hash),
                     static_cast<unsigned long>(sender_node),
                     static_cast<unsigned>(plain_len),
                     is_flood_route ? "flood" : "direct",
                     static_cast<unsigned long>(tag));
        return;
    }

    if (is_peer_payload)
    {
        const uint8_t dest_hash = parsed.payload[0];
        const uint8_t src_hash = parsed.payload[1];
        if (dest_hash != self_hash_)
        {
            return;
        }

        uint8_t plain[kMeshcoreMaxPayloadSize];
        size_t plain_len = 0;
        ChannelId peer_channel = ChannelId::PRIMARY;
        if (!tryDecryptPeerPayload(src_hash,
                                   parsed.payload + 2, parsed.payload_len - 2,
                                   plain, &plain_len, &peer_channel))
        {
            const PeerRouteEntry* route = findPeerRouteByHash(src_hash);
            const bool has_pubkey = (route && route->has_pubkey);
            MESHCORE_LOG("[MESHCORE] RX peer decrypt failed type=%u src=%02X has_pubkey=%u\n",
                         static_cast<unsigned>(parsed.payload_type),
                         static_cast<unsigned>(src_hash),
                         has_pubkey ? 1U : 0U);
            if (!has_pubkey)
            {
                maybeAutoDiscoverMissingPeer(src_hash, now_ms);
            }
            return;
        }
        plain_len = trimTrailingZeros(plain, plain_len);
        // Do not infer zero-hop route from direct packets that already traversed a path.
        // Route candidates should be learned from PATH payloads (or flood-derived returns).
        PeerRouteEntry& route = upsertPeerRoute(src_hash, now_ms);
        route.preferred_channel = peer_channel;

        const NodeId from_node = resolvePeerNodeId(src_hash);

        if (parsed.payload_type == kPayloadTypeTxtMsg && plain_len > 5)
        {
            const uint8_t flags = plain[4] >> 2;
            if (flags == kTxtTypePlain)
            {
                uint32_t sender_ts = 0;
                memcpy(&sender_ts, plain, sizeof(sender_ts));

                MeshIncomingText incoming;
                incoming.channel = peer_channel;
                incoming.from = from_node;
                incoming.to = node_id_;
                incoming.msg_id = next_msg_id_++;
                incoming.timestamp = is_valid_epoch(sender_ts) ? sender_ts : now_message_timestamp();
                incoming.text.assign(reinterpret_cast<const char*>(plain + 5),
                                     reinterpret_cast<const char*>(plain + plain_len));
                incoming.hop_limit = 0;
                incoming.encrypted = true;
                fill_rx_meta(incoming.rx_meta, is_direct_route);
                receive_queue_.push(incoming);

                uint32_t ack_value = packet_sig;
                uint8_t sender_pubkey[kMeshcorePubKeySize] = {};
                if (lookupPeerPubKey(src_hash, sender_pubkey))
                {
                    size_t text_len = 0;
                    const size_t text_cap = plain_len - 5;
                    while (text_len < text_cap && plain[5 + text_len] != '\0')
                    {
                        ++text_len;
                    }

                    SHA256 sha;
                    sha.update(plain, 5 + text_len);
                    sha.update(sender_pubkey, kMeshcorePubKeySize);
                    sha.finalize(reinterpret_cast<uint8_t*>(&ack_value), sizeof(ack_value));
                }
                else
                {
                    MESHCORE_LOG("[MESHCORE] RX text ACK fallback src=%02X reason=no_pubkey\n",
                                 static_cast<unsigned>(src_hash));
                }
                sendPeerAck(src_hash, peer_channel, ack_value);
                return;
            }

            if (flags == kTxtTypeSigned && plain_len > 9)
            {
                uint32_t sender_ts = 0;
                memcpy(&sender_ts, plain, sizeof(sender_ts));

                MeshIncomingText incoming;
                incoming.channel = peer_channel;
                incoming.from = from_node;
                incoming.to = node_id_;
                incoming.msg_id = next_msg_id_++;
                incoming.timestamp = is_valid_epoch(sender_ts) ? sender_ts : now_message_timestamp();
                incoming.text.assign(reinterpret_cast<const char*>(plain + 9),
                                     reinterpret_cast<const char*>(plain + plain_len));
                incoming.hop_limit = 0;
                incoming.encrypted = true;
                fill_rx_meta(incoming.rx_meta, is_direct_route);
                receive_queue_.push(incoming);

                uint32_t ack_value = packet_sig;
                const uint8_t* self_pubkey = identity_.isReady() ? identity_.publicKey() : nullptr;
                if (self_pubkey)
                {
                    size_t text_len = 0;
                    const size_t text_cap = plain_len - 9;
                    while (text_len < text_cap && plain[9 + text_len] != '\0')
                    {
                        ++text_len;
                    }

                    SHA256 sha;
                    sha.update(plain, 9 + text_len);
                    sha.update(self_pubkey, kMeshcorePubKeySize);
                    sha.finalize(reinterpret_cast<uint8_t*>(&ack_value), sizeof(ack_value));
                }
                else
                {
                    MESHCORE_LOG("[MESHCORE] RX signed text ACK fallback src=%02X reason=no_identity\n",
                                 static_cast<unsigned>(src_hash));
                }
                sendPeerAck(src_hash, peer_channel, ack_value);
                return;
            }
            return;
        }

        if (parsed.payload_type == kPayloadTypeDirectData && plain_len >= sizeof(uint32_t))
        {
            DecodedDirectAppPayload decoded;
            if (!decodeDirectAppPayload(plain, plain_len, &decoded))
            {
                return;
            }

            MeshIncomingData incoming;
            incoming.portnum = decoded.portnum;
            incoming.from = from_node;
            incoming.to = node_id_;
            incoming.packet_id = next_msg_id_++;
            incoming.channel = peer_channel;
            incoming.channel_hash = 0;
            incoming.want_response = decoded.want_ack;
            incoming.payload.assign(decoded.payload, decoded.payload + decoded.payload_len);
            fill_rx_meta(incoming.rx_meta, is_direct_route);
            if (decoded.want_ack)
            {
                sendPeerAck(src_hash, peer_channel, packet_sig);
            }

            if (!handleControlAppData(incoming))
            {
                app_receive_queue_.push(incoming);
            }
            return;
        }

        if (parsed.payload_type == kPayloadTypeReq && plain_len > 0)
        {
            uint8_t response[kMeshcoreMaxPayloadSize];
            const size_t response_len = std::min(plain_len, sizeof(response));
            memcpy(response, plain, response_len);

            if (is_flood_route)
            {
                sendPathReturn(src_hash, peer_channel,
                               parsed.path, parsed.path_len,
                               kRouteTypeFlood, nullptr, 0,
                               kPayloadTypeResponse, response, response_len,
                               kPathResponseDelayMs);
            }
            else
            {
                const PeerRouteEntry* route = selectPeerRouteByHash(src_hash, now_ms);
                uint8_t route_type = kRouteTypeFlood;
                const uint8_t* route_path = nullptr;
                size_t route_path_len = 0;
                if (route)
                {
                    route_type = kRouteTypeDirect;
                    route_path = route->out_path;
                    route_path_len = route->out_path_len;
                }
                sendPeerDatagram(kPayloadTypeResponse, src_hash, peer_channel,
                                 response, response_len,
                                 route_type, route_path, route_path_len,
                                 kPathResponseDelayMs);
            }
            return;
        }

        if (parsed.payload_type == kPayloadTypeResponse)
        {
            MESHCORE_LOG("[MESHCORE] RX RESPONSE src=%02X len=%u route=%s\n",
                         static_cast<unsigned>(src_hash),
                         static_cast<unsigned>(plain_len),
                         is_flood_route ? "flood" : "direct");
            Event ev{};
            ev.type = Event::Type::Response;
            ev.peer_hash = src_hash;
            ev.peer_node = from_node;
            ev.payload.assign(plain, plain + plain_len);
            pushEvent(std::move(ev));
            return;
        }

        if (parsed.payload_type == kPayloadTypePath && plain_len >= 2)
        {
            size_t index = 0;
            const size_t out_path_len = plain[index++];
            if (out_path_len <= kMeshcoreMaxPathSize && (index + out_path_len + 1) <= plain_len)
            {
                const uint8_t* out_path = &plain[index];
                index += out_path_len;
                const uint8_t extra_type = plain[index++] & 0x0F;
                const uint8_t* extra = &plain[index];
                const size_t extra_len = plain_len - index;

                rememberPeerPath(src_hash, out_path, out_path_len, peer_channel, now_ms);
                MESHCORE_LOG("[MESHCORE] RX PATH src=%02X out_path_len=%u extra_type=%u extra_len=%u route=%s\n",
                             static_cast<unsigned>(src_hash),
                             static_cast<unsigned>(out_path_len),
                             static_cast<unsigned>(extra_type),
                             static_cast<unsigned>(extra_len),
                             is_flood_route ? "flood" : "direct");

                if (extra_type == kPayloadTypeAck && extra_len >= sizeof(uint32_t))
                {
                    uint32_t ack_sig = 0;
                    memcpy(&ack_sig, extra, sizeof(ack_sig));
                    MESHCORE_LOG("[MESHCORE] RX PATH/ACK %08lX\n",
                                 static_cast<unsigned long>(ack_sig));
                    consumePendingAppAck(ack_sig, now_ms);
                }
                else if (extra_type == kPayloadTypeResponse && extra_len > 0)
                {
                    MESHCORE_LOG("[MESHCORE] RX PATH/RESPONSE src=%02X len=%u\n",
                                 static_cast<unsigned>(src_hash),
                                 static_cast<unsigned>(extra_len));
                    Event ev{};
                    ev.type = Event::Type::PathResponse;
                    ev.peer_hash = src_hash;
                    ev.peer_node = from_node;
                    if (extra_len >= sizeof(uint32_t))
                    {
                        memcpy(&ev.tag, extra, sizeof(uint32_t));
                    }
                    ev.payload.assign(extra, extra + extra_len);
                    ev.in_path.assign(parsed.path, parsed.path + parsed.path_len);
                    if (out_path_len > 0)
                    {
                        ev.out_path.assign(out_path, out_path + out_path_len);
                    }
                    pushEvent(std::move(ev));
                }

                if (is_flood_route)
                {
                    sendPathReturn(src_hash, peer_channel,
                                   parsed.path, parsed.path_len,
                                   kRouteTypeDirect, out_path, out_path_len,
                                   kPathExtraNone, nullptr, 0,
                                   kPathReciprocalDelayMs);
                }
            }
            return;
        }
    }

    auto logUnknownGroupHash = [&](const char* kind, uint8_t channel_hash) -> void
    {
        const bool has_primary_key = !isZeroKey(config_.primary_key, sizeof(config_.primary_key));
        const bool has_secondary_key = !isZeroKey(config_.secondary_key, sizeof(config_.secondary_key));
        const uint8_t primary_hash = has_primary_key ? computeChannelHash(config_.primary_key) : 0xFF;
        const uint8_t secondary_hash = has_secondary_key ? computeChannelHash(config_.secondary_key) : 0xFF;
        const bool has_public = shouldUsePublicChannelFallback(config_);
        const uint8_t public_hash = has_public ? computeChannelHash(kPublicGroupPsk) : 0xFF;
        MESHCORE_LOG("[MESHCORE] RX group %s drop unknown hash=%02X local[p=%02X s=%02X pub=%02X]\n",
                     kind,
                     static_cast<unsigned>(channel_hash),
                     static_cast<unsigned>(primary_hash),
                     static_cast<unsigned>(secondary_hash),
                     static_cast<unsigned>(public_hash));
    };

    if (is_group_text_payload)
    {
        const uint8_t channel_hash = parsed.payload[0];
        bool channel_match = false;
        const ChannelId rx_channel = resolveChannelFromHash(channel_hash, &channel_match);
        if (!channel_match)
        {
            logUnknownGroupHash("text", channel_hash);
            return;
        }

        uint8_t key16[16];
        uint8_t key32[32];
        if (!resolveGroupSecret(rx_channel, key16, key32, nullptr))
        {
            return;
        }

        uint8_t plain[kMeshcoreMaxPayloadSize];
        size_t plain_len = 0;
        if (!macThenDecrypt(key16, key32,
                            parsed.payload + 1, parsed.payload_len - 1,
                            plain, &plain_len))
        {
            MESHCORE_LOG("[MESHCORE] RX group text decrypt fail hash=%02X len=%u ch=%u\n",
                         static_cast<unsigned>(channel_hash),
                         static_cast<unsigned>(parsed.payload_len),
                         static_cast<unsigned>(rx_channel));
            return;
        }
        plain_len = trimTrailingZeros(plain, plain_len);
        if (plain_len <= kGroupPlainPrefixSize)
        {
            return;
        }

        uint32_t sender_ts = 0;
        memcpy(&sender_ts, plain, sizeof(sender_ts));
        const uint8_t txt_type = plain[4] >> 2;

        NodeId sender = 0;
        size_t text_offset = kGroupPlainPrefixSize;
        if (txt_type == kTxtTypeSigned)
        {
            if (plain_len < (kGroupPlainPrefixSize + sizeof(uint32_t)))
            {
                return;
            }
            memcpy(&sender, plain + kGroupPlainPrefixSize, sizeof(uint32_t));
            text_offset += sizeof(uint32_t);
        }
        else if (txt_type != kTxtTypePlain)
        {
            return;
        }

        MeshIncomingText incoming;
        incoming.channel = rx_channel;
        incoming.from = sender;
        incoming.to = 0xFFFFFFFF;
        incoming.msg_id = next_msg_id_++;
        incoming.timestamp = is_valid_epoch(sender_ts) ? sender_ts : now_message_timestamp();
        incoming.text.assign(reinterpret_cast<const char*>(plain + text_offset),
                             reinterpret_cast<const char*>(plain + plain_len));
        incoming.hop_limit = 0;
        incoming.encrypted = true;
        fill_rx_meta(incoming.rx_meta, false);
        incoming.rx_meta.channel_hash = channel_hash;
        if (incoming.from != 0)
        {
            rememberPeerNodeId(static_cast<uint8_t>(incoming.from & 0xFFU), incoming.from, now_ms);
        }
        receive_queue_.push(incoming);
    }
    else if (is_group_data_payload)
    {
        const uint8_t channel_hash = parsed.payload[0];
        bool channel_match = false;
        const ChannelId rx_channel = resolveChannelFromHash(channel_hash, &channel_match);
        if (!channel_match)
        {
            logUnknownGroupHash("data", channel_hash);
            return;
        }

        uint8_t key16[16];
        uint8_t key32[32];
        if (!resolveGroupSecret(rx_channel, key16, key32, nullptr))
        {
            return;
        }

        uint8_t plain[kMeshcoreMaxPayloadSize];
        size_t plain_len = 0;
        if (!macThenDecrypt(key16, key32,
                            parsed.payload + 1, parsed.payload_len - 1,
                            plain, &plain_len))
        {
            MESHCORE_LOG("[MESHCORE] RX group data decrypt fail hash=%02X len=%u ch=%u\n",
                         static_cast<unsigned>(channel_hash),
                         static_cast<unsigned>(parsed.payload_len),
                         static_cast<unsigned>(rx_channel));
            return;
        }
        plain_len = trimTrailingZeros(plain, plain_len);
        if (plain_len < sizeof(uint32_t))
        {
            return;
        }

        DecodedGroupAppPayload decoded;
        if (!decodeGroupAppPayload(plain, plain_len, &decoded))
        {
            return;
        }

        MeshIncomingData incoming;
        incoming.portnum = decoded.portnum;
        incoming.from = decoded.sender;
        incoming.to = 0xFFFFFFFF;
        incoming.packet_id = next_msg_id_++;
        incoming.channel = rx_channel;
        incoming.channel_hash = channel_hash;
        incoming.want_response = false;
        incoming.payload.assign(decoded.payload, decoded.payload + decoded.payload_len);
        fill_rx_meta(incoming.rx_meta, false);
        incoming.rx_meta.channel_hash = channel_hash;
        if (incoming.from != 0)
        {
            rememberPeerNodeId(static_cast<uint8_t>(incoming.from & 0xFFU), incoming.from, now_ms);
        }
        if (!handleControlAppData(incoming))
        {
            app_receive_queue_.push(incoming);
        }
    }
    else if (parsed.payload_type == kPayloadTypeAdvert)
    {
        if (parsed.payload_len < kAdvertMinPayloadSize)
        {
            MESHCORE_LOG("[MESHCORE] RX ADVERT drop short len=%u (min=%u)\n",
                         static_cast<unsigned>(parsed.payload_len),
                         static_cast<unsigned>(kAdvertMinPayloadSize));
            return;
        }

        const uint8_t* pubkey = parsed.payload;
        const uint8_t peer_hash = pubkey[0];
        if (peer_hash == self_hash_)
        {
            return;
        }

        uint32_t advert_ts = 0;
        memcpy(&advert_ts, parsed.payload + kMeshcorePubKeySize, sizeof(advert_ts));
        const uint8_t* signature = parsed.payload + kMeshcorePubKeySize + sizeof(uint32_t);

        const uint8_t* app_data = parsed.payload + kAdvertMinPayloadSize;
        const size_t app_data_len = parsed.payload_len - kAdvertMinPayloadSize;

        std::array<uint8_t, kMeshcorePubKeySize + sizeof(uint32_t) + kMeshcoreMaxPayloadSize> signed_message = {};
        size_t signed_len = 0;
        memcpy(signed_message.data() + signed_len, pubkey, kMeshcorePubKeySize);
        signed_len += kMeshcorePubKeySize;
        memcpy(signed_message.data() + signed_len, &advert_ts, sizeof(advert_ts));
        signed_len += sizeof(advert_ts);
        memcpy(signed_message.data() + signed_len, app_data, app_data_len);
        signed_len += app_data_len;
        if (!MeshCoreIdentity::verify(pubkey, signature, signed_message.data(), signed_len))
        {
            MESHCORE_LOG("[MESHCORE] RX ADVERT drop forged signature hash=%02X len=%u\n",
                         static_cast<unsigned>(peer_hash),
                         static_cast<unsigned>(parsed.payload_len));
            return;
        }

        DecodedAdvertAppData advert{};
        if (!decodeAdvertAppData(app_data, app_data_len, &advert))
        {
            MESHCORE_LOG("[MESHCORE] RX ADVERT drop invalid app_data len=%u hash=%02X\n",
                         static_cast<unsigned>(app_data_len),
                         static_cast<unsigned>(peer_hash));
            return;
        }

        const NodeId node = deriveNodeIdFromPubkey(pubkey, kMeshcorePubKeySize);
        if (node == 0 || node == node_id_)
        {
            return;
        }

        const uint8_t hops = (parsed.path_len <= 255) ? static_cast<uint8_t>(parsed.path_len) : 0xFF;
        const float snr = std::isfinite(last_rx_snr_) ? last_rx_snr_ : NAN;
        const float rssi = std::isfinite(last_rx_rssi_) ? last_rx_rssi_ : NAN;
        const uint32_t ts = is_valid_epoch(advert_ts) ? advert_ts : now_message_timestamp();
        const char* name = advert.has_name ? advert.name : "";
        const uint8_t role = mapAdvertTypeToRole(advert.node_type);

        bool is_new_peer = true;
        if (const PeerRouteEntry* existing = findPeerRouteByHash(peer_hash))
        {
            is_new_peer = !existing->has_pubkey;
        }
        rememberPeerPubKey(pubkey, now_ms, true);
        rememberPeerNodeId(peer_hash, node, now_ms);
        if (PeerRouteEntry* entry = findPeerRouteByHash(peer_hash))
        {
            entry->last_advert_len = static_cast<uint16_t>(
                std::min(parsed.payload_len, sizeof(entry->last_advert)));
            if (entry->last_advert_len > 0)
            {
                memcpy(entry->last_advert, parsed.payload, entry->last_advert_len);
            }
            entry->last_advert_ts = ts;
        }
        publishMeshcoreNodeInfo(node, name, name, role, hops, snr, rssi, ts);
        if (advert.has_location)
        {
            publishMeshcorePosition(node, advert.latitude_i6, advert.longitude_i6, ts);
        }
        Event ev{};
        ev.type = Event::Type::Advert;
        ev.peer_hash = peer_hash;
        ev.peer_node = node;
        ev.advert_is_new = is_new_peer;
        pushEvent(std::move(ev));

        MESHCORE_LOG("[MESHCORE] RX ADVERT node=%08lX hash=%02X type=%u hops=%u name='%s' loc=%u app=%u sig=verified\n",
                     static_cast<unsigned long>(node),
                     static_cast<unsigned>(peer_hash),
                     static_cast<unsigned>(advert.node_type),
                     static_cast<unsigned>(hops),
                     name,
                     advert.has_location ? 1U : 0U,
                     static_cast<unsigned>(app_data_len));
    }
    else if (is_legacy_text_payload)
    {
        MeshIncomingText incoming;
        incoming.channel = (parsed.payload[0] == 1) ? ChannelId::SECONDARY : ChannelId::PRIMARY;
        incoming.from = 0;
        incoming.to = 0xFFFFFFFF;
        incoming.msg_id = next_msg_id_++;
        incoming.timestamp = now_message_timestamp();
        std::vector<uint8_t> text_bytes(parsed.payload + 1, parsed.payload + parsed.payload_len);
        if (encrypt_mode_ > 0)
        {
            size_t key_len = 0;
            const uint8_t* key = selectChannelKey(config_, &key_len);
            xorCrypt(text_bytes.data(), text_bytes.size(), key, key_len);
        }
        incoming.text.assign(reinterpret_cast<const char*>(text_bytes.data()), text_bytes.size());
        incoming.hop_limit = 0;
        incoming.encrypted = (encrypt_mode_ > 0);
        fill_rx_meta(incoming.rx_meta, false);
        receive_queue_.push(incoming);
    }
    else if (is_raw_payload)
    {
        Event ev{};
        ev.type = Event::Type::RawData;
        if (std::isfinite(last_rx_snr_))
        {
            ev.snr_qdb = static_cast<int8_t>(std::lround(last_rx_snr_ * 4.0f));
        }
        if (std::isfinite(last_rx_rssi_))
        {
            ev.rssi_dbm = static_cast<int8_t>(std::lround(last_rx_rssi_));
        }
        ev.payload.assign(parsed.payload, parsed.payload + parsed.payload_len);
        pushEvent(std::move(ev));

        MeshIncomingData incoming;
        memcpy(&incoming.portnum, parsed.payload, sizeof(uint32_t));
        incoming.from = 0;
        incoming.to = 0xFFFFFFFF;
        incoming.packet_id = next_msg_id_++;
        incoming.channel = ChannelId::PRIMARY;
        incoming.channel_hash = 0;
        incoming.want_response = false;
        std::vector<uint8_t> app_bytes(parsed.payload + sizeof(uint32_t), parsed.payload + parsed.payload_len);
        if (encrypt_mode_ > 0)
        {
            size_t key_len = 0;
            const uint8_t* key = selectChannelKey(config_, &key_len);
            xorCrypt(app_bytes.data(), app_bytes.size(), key, key_len);
        }
        incoming.payload.assign(app_bytes.begin(), app_bytes.end());
        fill_rx_meta(incoming.rx_meta, false);
        app_receive_queue_.push(incoming);
    }
    else if (parsed.payload_type == kPayloadTypeAck && parsed.payload_len >= sizeof(uint32_t))
    {
        uint32_t ack_sig = 0;
        memcpy(&ack_sig, parsed.payload, sizeof(uint32_t));
        MESHCORE_LOG("[MESHCORE] RX ACK %08lX\n", static_cast<unsigned long>(ack_sig));
        consumePendingAppAck(ack_sig, now_ms);
    }
    else if (parsed.payload_type == kPayloadTypeMultipart && parsed.payload_len >= 5)
    {
        const uint8_t wrapped_type = parsed.payload[0] & 0x0F;
        if (wrapped_type == kPayloadTypeAck)
        {
            uint32_t ack_sig = 0;
            memcpy(&ack_sig, parsed.payload + 1, sizeof(uint32_t));
            MESHCORE_LOG("[MESHCORE] RX MULTIPART/ACK %08lX rem=%u route=%s\n",
                         static_cast<unsigned long>(ack_sig),
                         static_cast<unsigned>(parsed.payload[0] >> 4),
                         is_flood_route ? "flood" : "direct");
            consumePendingAppAck(ack_sig, now_ms);
        }
    }
    else if (parsed.payload_type == kPayloadTypeTrace && parsed.payload_len >= 9)
    {
        const uint8_t flags = parsed.payload[8];
        const uint8_t path_hash_size_bits = flags & 0x03;
        size_t path_hash_size = static_cast<size_t>(1U << path_hash_size_bits);
        if (path_hash_size == 0 || path_hash_size > 4)
        {
            path_hash_size = 1;
        }
        const size_t trace_meta_len = parsed.payload_len - 9;
        const size_t offset = parsed.path_len * path_hash_size;
        if (offset >= trace_meta_len)
        {
            uint32_t tag = 0;
            uint32_t auth = 0;
            memcpy(&tag, parsed.payload, sizeof(tag));
            memcpy(&auth, parsed.payload + 4, sizeof(auth));
            MESHCORE_LOG("[MESHCORE] RX TRACE done tag=%08lX auth=%08lX hops=%u route=%s\n",
                         static_cast<unsigned long>(tag),
                         static_cast<unsigned long>(auth),
                         static_cast<unsigned>(parsed.path_len),
                         is_flood_route ? "flood" : "direct");
            Event ev{};
            ev.type = Event::Type::TraceData;
            ev.tag = tag;
            ev.auth = auth;
            ev.flags = flags;
            ev.trace_hashes.assign(parsed.payload + 9, parsed.payload + parsed.payload_len);
            if (parsed.path_len > 0)
            {
                ev.trace_snrs.assign(parsed.path, parsed.path + parsed.path_len);
            }
            if (std::isfinite(last_rx_snr_))
            {
                ev.snr_qdb = static_cast<int8_t>(std::lround(last_rx_snr_ * 4.0f));
            }
            pushEvent(std::move(ev));
        }
    }
}

void MeshCoreAdapter::processSendQueue()
{
    uint32_t now_ms = millis();
    prunePendingAppAcks(now_ms);
    prunePeerRoutes(now_ms);

    if (scheduled_tx_.empty())
    {
        return;
    }

    for (size_t i = 0; i < scheduled_tx_.size();)
    {
        ScheduledFrame& frame = scheduled_tx_[i];
        if (static_cast<int32_t>(now_ms - frame.due_ms) < 0)
        {
            ++i;
            continue;
        }

        if (transmitFrameNow(frame.bytes.data(), frame.bytes.size(), now_ms))
        {
            scheduled_tx_.erase(scheduled_tx_.begin() + i);
            now_ms = millis();
        }
        else
        {
            frame.due_ms = now_ms + 50;
            ++i;
            break;
        }
    }
}

bool MeshCoreAdapter::getPeerInfos(std::vector<PeerInfo>& out) const
{
    out.clear();
    out.reserve(peer_routes_.size());
    for (const auto& entry : peer_routes_)
    {
        PeerInfo info;
        info.peer_hash = entry.peer_hash;
        info.node_id = entry.node_id_guess;
        info.has_pubkey = entry.has_pubkey;
        info.pubkey_verified = entry.pubkey_verified;
        if (entry.has_pubkey)
        {
            memcpy(info.pubkey, entry.pubkey, sizeof(info.pubkey));
        }
        info.out_path_len = entry.has_out_path ? entry.out_path_len : 0;
        if (info.out_path_len > 0)
        {
            memcpy(info.out_path, entry.out_path, info.out_path_len);
        }
        info.last_seen_ms = entry.last_seen_ms;
        info.preferred_channel = entry.preferred_channel;
        out.push_back(info);
    }
    return true;
}

bool MeshCoreAdapter::lookupPeerByHash(uint8_t hash, PeerInfo* out) const
{
    if (!out)
    {
        return false;
    }
    const PeerRouteEntry* entry = findPeerRouteByHash(hash);
    if (!entry)
    {
        return false;
    }
    PeerInfo info;
    info.peer_hash = entry->peer_hash;
    info.node_id = entry->node_id_guess;
    info.has_pubkey = entry->has_pubkey;
    info.pubkey_verified = entry->pubkey_verified;
    if (entry->has_pubkey)
    {
        memcpy(info.pubkey, entry->pubkey, sizeof(info.pubkey));
    }
    info.out_path_len = entry->has_out_path ? entry->out_path_len : 0;
    if (info.out_path_len > 0)
    {
        memcpy(info.out_path, entry->out_path, info.out_path_len);
    }
    info.last_seen_ms = entry->last_seen_ms;
    info.preferred_channel = entry->preferred_channel;
    *out = info;
    return true;
}

bool MeshCoreAdapter::lookupPeerByNodeId(NodeId node_id, PeerInfo* out) const
{
    if (!out)
    {
        return false;
    }
    for (const auto& entry : peer_routes_)
    {
        if (entry.node_id_guess != 0 && entry.node_id_guess == node_id)
        {
            PeerInfo info;
            info.peer_hash = entry.peer_hash;
            info.node_id = entry.node_id_guess;
            info.has_pubkey = entry.has_pubkey;
            info.pubkey_verified = entry.pubkey_verified;
            if (entry.has_pubkey)
            {
                memcpy(info.pubkey, entry.pubkey, sizeof(info.pubkey));
            }
            info.out_path_len = entry.has_out_path ? entry.out_path_len : 0;
            if (info.out_path_len > 0)
            {
                memcpy(info.out_path, entry.out_path, info.out_path_len);
            }
            info.last_seen_ms = entry.last_seen_ms;
            info.preferred_channel = entry.preferred_channel;
            *out = info;
            return true;
        }
    }
    return false;
}

bool MeshCoreAdapter::exportIdentityPublicKey(uint8_t out_pubkey[MeshCoreIdentity::kPubKeySize])
{
    return static_cast<const MeshCoreAdapter&>(*this).exportIdentityPublicKey(out_pubkey);
}

bool MeshCoreAdapter::exportIdentityPublicKey(uint8_t out_pubkey[MeshCoreIdentity::kPubKeySize]) const
{
    if (!out_pubkey || !identity_.isReady())
    {
        return false;
    }
    memcpy(out_pubkey, identity_.publicKey(), MeshCoreIdentity::kPubKeySize);
    return true;
}

bool MeshCoreAdapter::exportIdentityPrivateKey(uint8_t out_priv[MeshCoreIdentity::kPrivKeySize])
{
    return static_cast<const MeshCoreAdapter&>(*this).exportIdentityPrivateKey(out_priv);
}

bool MeshCoreAdapter::exportIdentityPrivateKey(uint8_t out_priv[MeshCoreIdentity::kPrivKeySize]) const
{
    if (!out_priv)
    {
        return false;
    }
    return identity_.exportPrivateKey(out_priv);
}

bool MeshCoreAdapter::importIdentityPrivateKey(const uint8_t* in_priv, size_t len)
{
    if (!in_priv || len != MeshCoreIdentity::kPrivKeySize)
    {
        return false;
    }
    if (!identity_.importPrivateKey(in_priv))
    {
        return false;
    }
    self_hash_ = identity_.selfHash();
    const NodeId identity_node = deriveNodeIdFromPubkey(identity_.publicKey(), MeshCoreIdentity::kPubKeySize);
    if (identity_node != 0)
    {
        node_id_ = identity_node;
    }
    return true;
}

bool MeshCoreAdapter::signPayload(const uint8_t* data, size_t len,
                                  uint8_t out_sig[MeshCoreIdentity::kSignatureSize])
{
    return static_cast<const MeshCoreAdapter&>(*this).signPayload(data, len, out_sig);
}

bool MeshCoreAdapter::signPayload(const uint8_t* data, size_t len,
                                  uint8_t out_sig[MeshCoreIdentity::kSignatureSize]) const
{
    if (!data || len == 0 || !out_sig || !identity_.isReady())
    {
        return false;
    }
    return identity_.sign(data, len, out_sig);
}

bool MeshCoreAdapter::clearPeerPath(uint8_t peer_hash)
{
    PeerRouteEntry* entry = findPeerRouteByHash(peer_hash);
    if (!entry)
    {
        return false;
    }
    entry->out_path_len = 0;
    entry->has_out_path = false;
    memset(entry->out_path, 0, sizeof(entry->out_path));
    Event ev{};
    ev.type = Event::Type::PathUpdated;
    ev.peer_hash = peer_hash;
    ev.peer_node = resolvePeerNodeId(peer_hash);
    pushEvent(std::move(ev));
    return true;
}

} // namespace meshcore
} // namespace chat
