/**
 * @file meshcore_adapter.cpp
 * @brief MeshCore protocol adapter implementation
 */

#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_adapter.h"
#include "chat/domain/contact_types.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "chat/infra/voice/vmp_private_crypto.h"
#include "chat/runtime/meshcore_direct_route_policy.h"
#include "chat/runtime/meshcore_direct_secret_core.h"
#include "chat/time_utils.h"
#include "mesh/protocol/meshcore/meshcore_protocol_strategy.h"
#if defined(ARDUINO)
#include "platform/esp/arduino_common/app_tasks.h"
#else
#include "platform/esp/common/reticulum_runtime_compat.h"
#endif
#include "platform/esp/common/meshcore_runtime_compat.h"
#include "sys/event_bus.h"
#if defined(ARDUINO)
#include <Arduino.h>
#include <RadioLib.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <limits>
#include <new>
#include <type_traits>
#include <variant>
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
constexpr uint8_t kPayloadTypeTrace = chat::meshcore::kMeshCorePayloadTypeTrace;
constexpr uint8_t kPayloadTypeMultipart = 0x0A;
constexpr uint8_t kPayloadTypeControl = chat::meshcore::kMeshCorePayloadTypeControl;
constexpr size_t kMeshcoreMaxPathSize = 64;
constexpr size_t kMeshcoreMaxFrameSize = 255;
constexpr size_t kMeshcoreMaxPayloadSize = 184;
constexpr uint32_t kSeenTtlMs = 60000;
constexpr uint32_t kPeerRouteEntryTtlMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kPeerPathTtlMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kRoutePenaltyBlackoutMs = 30UL * 1000UL;
constexpr uint32_t kAckDelayMs = 120;
constexpr uint32_t kAckSpacingMs = 300;
constexpr uint32_t kDiscoverRxGuardMs = 5000;
constexpr size_t kCipherBlockSize = 16;
constexpr size_t kCipherKeySize = 16;
constexpr size_t kCipherHmacKeySize = 32;
constexpr size_t kGroupPlainPrefixSize = 5;
constexpr uint8_t kTxtTypePlain = 0x00;
constexpr uint8_t kPathExtraNone = 0xFF;
constexpr uint32_t kPathResponseDelayMs = 300;
constexpr uint32_t kPathReciprocalDelayMs = 500;
constexpr NodeId kSyntheticNodePrefix = 0x4D430000UL;
constexpr uint32_t kAppAckTimeoutMs = 15000;
constexpr uint32_t kKeyVerifySessionTtlMs = 60000;
constexpr uint32_t kSendTimeoutBaseMs = 500;
constexpr float kFloodSendTimeoutFactor = 16.0f;
constexpr float kDirectSendPerhopFactor = 6.0f;
constexpr uint32_t kDirectSendPerhopExtraMs = 250;

constexpr uint8_t kTxtTypeSigned = 0x02;
constexpr uint8_t kDirectAppMagic0 = 0xDA;
constexpr uint8_t kDirectAppMagic1 = 0x7A;
constexpr uint8_t kDirectAppFlagWantAck = 0x01;
constexpr uint8_t kGroupDataMagic0 = 0x47; // 'G'
constexpr uint8_t kGroupDataMagic1 = 0x44; // 'D'
constexpr uint8_t kLoraSyncWordPrivate = 0x12;
constexpr uint8_t kControlMagic0 = 0x54; // 'T'
constexpr uint8_t kControlMagic1 = 0x4D; // 'M'
constexpr uint8_t kControlKindKeyVerify = 0x02;
constexpr uint8_t kKeyVerifyTypeInit = 0x01;
constexpr uint8_t kKeyVerifyTypeReady = 0x02;
constexpr uint8_t kKeyVerifyTypeFinal = 0x03;
constexpr uint32_t kKeyVerifyPortnum = 12;
constexpr uint8_t kAdvertTypeNone = 0x00;
constexpr uint8_t kAdvertTypeChat = 0x01;
constexpr uint8_t kAdvertTypeRepeater = 0x02;
constexpr uint8_t kAdvertFlagHasLocation = 0x10;
constexpr uint8_t kAdvertFlagHasFeature1 = 0x20;
constexpr uint8_t kAdvertFlagHasFeature2 = 0x40;
constexpr uint8_t kAdvertFlagHasName = 0x80;
constexpr size_t kMeshcorePubKeySize = 32;
constexpr size_t kAdvertSignatureSize = 64;
constexpr size_t kAdvertMinPayloadSize =
    kMeshcorePubKeySize + sizeof(uint32_t) + kAdvertSignatureSize;

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

uint8_t lowHashByte(PayloadProfile profile, const uint8_t* hash)
{
    (void)profile;
    return hash ? hash[0] : 0;
}

void copyPeerHashBytes(PayloadProfile profile, const uint8_t* hash,
                       uint8_t out_hash[chat::meshcore::kMeshCoreV2HashBytes],
                       uint8_t* out_len)
{
    if (!out_hash)
    {
        return;
    }
    memset(out_hash, 0, chat::meshcore::kMeshCoreV2HashBytes);
    const size_t hash_bytes = payloadHashBytes(profile);
    if (hash && hash_bytes <= chat::meshcore::kMeshCoreV2HashBytes)
    {
        memcpy(out_hash, hash, hash_bytes);
    }
    if (out_len)
    {
        *out_len = static_cast<uint8_t>(hash_bytes);
    }
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
using chat::meshcore::isValidTracePathHashBytes;
using chat::meshcore::isZeroKey;
using chat::meshcore::packetSignature;
using chat::meshcore::ParsedPacket;
using chat::meshcore::parsePacket;
using chat::meshcore::saturatingAddU32;
using chat::meshcore::scoreFromSnr;
using chat::meshcore::toHex;
using chat::meshcore::toHmacKey32;

using chat::meshcore::aesDecrypt;
using chat::meshcore::aesEncrypt;
using chat::meshcore::encryptThenMac;
using chat::meshcore::macThenDecrypt;
using chat::meshcore::trimTrailingZeros;
using MeshCoreSha256 = ::platform::esp::common::meshcore_runtime::Sha256Digest;

using chat::meshcore::buildDiscoverRequestControlPayload;
using chat::meshcore::buildDiscoverResponseControlPayload;
using chat::meshcore::buildFrameNoTransport;
using chat::meshcore::buildNodeInfoInfoControlPayload;
using chat::meshcore::buildNodeInfoQueryControlPayload;
using chat::meshcore::buildPeerDatagramPayload;
using chat::meshcore::buildTracePayload;
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
using chat::meshcore::DecodedNodeInfoControl;
using chat::meshcore::DecodedTracePayload;
using chat::meshcore::decodeGroupAppPayload;
using chat::meshcore::decodeNodeInfoControlPayload;
using chat::meshcore::decodeTracePayload;
using chat::meshcore::deriveNodeIdFromPubkey;
using chat::meshcore::formatVerificationCode;
using chat::meshcore::hasControlPrefix;
using chat::meshcore::isAnonReqCipherShape;
using chat::meshcore::isPeerCipherShape;
using chat::meshcore::isPeerPayloadType;
using chat::meshcore::kMeshCoreDiscoverTypeFilterAll;
using chat::meshcore::kMeshCoreNodeInfoInfoPayloadSize;
using chat::meshcore::kMeshCoreNodeInfoLongNameFieldSize;
using chat::meshcore::kMeshCoreNodeInfoPortnum;
using chat::meshcore::kMeshCoreNodeInfoShortNameFieldSize;
using chat::meshcore::mapAdvertTypeToRole;
using chat::meshcore::MeshCoreDiscoverRequestBuildInfo;
using chat::meshcore::MeshCoreNodeInfoBuildInfo;
using chat::meshcore::MeshCoreNodeInfoControlType;
using chat::meshcore::publicGroupPsk;
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

MeshCoreAdapter::MeshCoreAdapter(LoraBoard& board,
                                 IMeshPeerDirectory* peer_directory)
    : board_(board),
#if defined(ESP_PLATFORM) && !defined(ARDUINO)
      idf_radio_pump_(board),
#endif
      peer_directory_(peer_directory),
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
      rx_airtime_ms_(0),
      initialized_(false),
      last_raw_packet_len_(0),
      has_pending_raw_packet_(false),
      next_msg_id_(1)
{
    node_id_ = ::platform::esp::common::meshcore_runtime::device_node_id();
    self_hash_ = static_cast<uint8_t>(node_id_ & 0xFF);
}

void* MeshCoreAdapter::operator new(std::size_t size)
{
    void* ptr = heap_caps_malloc_prefer(size,
                                        2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return ptr != nullptr ? ptr : ::operator new(size);
}

void MeshCoreAdapter::operator delete(void* ptr) noexcept
{
    heap_caps_free(ptr);
}

void MeshCoreAdapter::operator delete(void* ptr, std::size_t) noexcept
{
    operator delete(ptr);
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
    caps.supports_node_info_query = true;
    caps.supports_node_info_reply = true;
    caps.supports_node_info_reannounce = true;
    caps.supports_trace_route_request = true;
    caps.supports_trace_route_reply = true;
    caps.supports_protocol_app_response = true;
    caps.supports_protocol_ack_tracking = true;
    caps.supports_meshcore_direct_route_table = true;
    caps.supports_meshcore_identity_keys = true;
    caps.supports_meshcore_peer_secret_derivation = true;
    caps.supports_meshcore_rich_trace_projection = true;
    return caps;
}

bool MeshCoreAdapter::pollEvent(Event* out)
{
    return events_.pop(out);
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
    uint8_t peer_hash_v2[chat::meshcore::kMeshCoreV2HashBytes] = {};
    copyPublicHash(PayloadProfile::V2, pubkey, len, peer_hash_v2, sizeof(peer_hash_v2));
    const PeerRouteEntry* v2_route = selectPeerRouteByHash(PayloadProfile::V2, peer_hash_v2, now_ms);
    const PeerRouteEntry* v1_route = selectPeerRouteByHash(peer_hash, now_ms);
    const PayloadProfile profile =
        (config_.meshcore_send_profile == MeshCorePayloadSendProfile::V1Only)
            ? PayloadProfile::V1
            : PayloadProfile::V2;

    uint8_t shared_secret[kCipherHmacKeySize] = {};
    if (!identity_.deriveSharedSecret(pubkey, shared_secret))
    {
        return false;
    }

    uint8_t key16[kCipherKeySize] = {};
    uint8_t key32[kCipherHmacKeySize] = {};
    if (!chat::runtime::MeshCoreDirectSecretCore::expandSharedSecret(shared_secret, sizeof(shared_secret),
                                                                     key16, sizeof(key16),
                                                                     key32, sizeof(key32)))
    {
        return false;
    }

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
    uint8_t peer_hash_bytes[chat::meshcore::kMeshCoreV2HashBytes] = {};
    uint8_t self_hash_bytes[chat::meshcore::kMeshCoreV2HashBytes] = {};
    if (!copyPublicHash(profile, pubkey, len, peer_hash_bytes, sizeof(peer_hash_bytes)) ||
        !selfHash(profile, self_hash_bytes, sizeof(self_hash_bytes)) ||
        !buildPeerDatagramPayload(profile, peer_hash_bytes, self_hash_bytes,
                                  key16, key32,
                                  plain, plain_len,
                                  datagram, sizeof(datagram), &datagram_len))
    {
        return false;
    }

    const PeerRouteEntry* route = (profile == PayloadProfile::V2) ? v2_route : v1_route;
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
    if (!buildFrameNoTransport(profile,
                               route_type, kPayloadTypeReq,
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
                                                 (route_type == kRouteTypeDirect)
                                                     ? pathHopCount(profile, out_path_len)
                                                     : 0,
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
    uint8_t peer_hash_v2[chat::meshcore::kMeshCoreV2HashBytes] = {};
    copyPublicHash(PayloadProfile::V2, pubkey, len, peer_hash_v2, sizeof(peer_hash_v2));
    const PeerRouteEntry* v2_route = selectPeerRouteByHash(PayloadProfile::V2, peer_hash_v2, now_ms);
    const PeerRouteEntry* v1_route = selectPeerRouteByHash(peer_hash, now_ms);
    const PayloadProfile profile =
        (config_.meshcore_send_profile == MeshCorePayloadSendProfile::V1Only)
            ? PayloadProfile::V1
            : PayloadProfile::V2;

    uint8_t shared_secret[kCipherHmacKeySize] = {};
    if (!identity_.deriveSharedSecret(pubkey, shared_secret))
    {
        return false;
    }

    uint8_t key16[kCipherKeySize] = {};
    uint8_t key32[kCipherHmacKeySize] = {};
    if (!chat::runtime::MeshCoreDirectSecretCore::expandSharedSecret(shared_secret, sizeof(shared_secret),
                                                                     key16, sizeof(key16),
                                                                     key32, sizeof(key32)))
    {
        return false;
    }

    uint8_t cipher[kMeshcoreMaxPayloadSize] = {};
    size_t cipher_len = encryptThenMac(key16, key32,
                                       cipher, sizeof(cipher),
                                       payload, payload_len,
                                       payloadMacBytes(profile));
    if (cipher_len == 0)
    {
        return false;
    }

    uint8_t datagram[kMeshcoreMaxPayloadSize] = {};
    size_t datagram_len = 0;
    uint8_t peer_hash_bytes[chat::meshcore::kMeshCoreV2HashBytes] = {};
    if (!copyPublicHash(profile, pubkey, len, peer_hash_bytes, sizeof(peer_hash_bytes)))
    {
        return false;
    }
    const size_t hash_bytes = payloadHashBytes(profile);
    if (hash_bytes + kMeshcorePubKeySize + cipher_len > sizeof(datagram))
    {
        return false;
    }
    memcpy(datagram + datagram_len, peer_hash_bytes, hash_bytes);
    datagram_len += hash_bytes;
    memcpy(datagram + datagram_len, identity_.publicKey(), kMeshcorePubKeySize);
    datagram_len += kMeshcorePubKeySize;
    memcpy(datagram + datagram_len, cipher, cipher_len);
    datagram_len += cipher_len;

    const PeerRouteEntry* route = (profile == PayloadProfile::V2) ? v2_route : v1_route;
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
    if (!buildFrameNoTransport(profile,
                               route_type, kPayloadTypeAnonReq,
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
                                                 (route_type == kRouteTypeDirect)
                                                     ? pathHopCount(profile, out_path_len)
                                                     : 0,
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
    return sendRawDataEx(chat::meshcore::kMeshCorePayloadVer1,
                         path, path_len, payload, payload_len, out_est_timeout);
}

bool MeshCoreAdapter::sendRawDataEx(uint8_t raw_profile, const uint8_t* path, size_t path_len,
                                    const uint8_t* payload, size_t payload_len,
                                    uint32_t* out_est_timeout)
{
    if (!payload || payload_len == 0 || path_len > kMeshcoreMaxPathSize ||
        (path_len > 0 && !path))
    {
        return false;
    }
    const PayloadProfile profile = raw_profile == chat::meshcore::kMeshCorePayloadVer2
                                       ? PayloadProfile::V2
                                       : PayloadProfile::V1;
    if (!pathIsWellFormed(profile, path_len))
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
    if (!buildFrameNoTransport(profile,
                               kRouteTypeDirect, kPayloadTypeRawCustom,
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
        *out_est_timeout = estimateSendTimeoutMs(frame_len, pathHopCount(profile, path_len), false,
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
    if (!path || path_len == 0 ||
        !isValidTracePathHashBytes(flags, path_len, kMeshcoreMaxPathSize))
    {
        return false;
    }

    const uint32_t now_ms = millis();
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        return false;
    }

    uint8_t payload[kMeshcoreMaxPayloadSize] = {};
    size_t payload_len = 0;
    if (!buildTracePayload(tag, auth, flags, path, path_len,
                           payload, sizeof(payload), &payload_len))
    {
        return false;
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    const PayloadProfile profile =
        (config_.meshcore_send_profile == MeshCorePayloadSendProfile::V1Only)
            ? PayloadProfile::V1
            : PayloadProfile::V2;
    if (!buildFrameNoTransport(profile,
                               kRouteTypeDirect, kPayloadTypeTrace,
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
    const PayloadProfile profile =
        (config_.meshcore_send_profile == MeshCorePayloadSendProfile::V1Only)
            ? PayloadProfile::V1
            : PayloadProfile::V2;
    if (!buildFrameNoTransport(profile,
                               kRouteTypeDirect, kPayloadTypeControl,
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
    const PayloadProfile profile =
        (config_.meshcore_send_profile == MeshCorePayloadSendProfile::V1Only)
            ? PayloadProfile::V1
            : PayloadProfile::V2;
    if (!buildFrameNoTransport(profile,
                               kRouteTypeDirect, kPayloadTypeAdvert,
                               nullptr, 0,
                               entry->last_advert, entry->last_advert_len,
                               frame, sizeof(frame), &frame_len))
    {
        return false;
    }

    if (isDiscoverRxGuardActive(now_ms))
    {
        const uint32_t delay_ms = discover_rx_guard_until_ms_ - now_ms;
        return enqueueScheduled(frame, frame_len, delay_ms, true);
    }

    bool ok = transmitFrameNow(frame, frame_len, now_ms);
    if (!ok)
    {
        ok = enqueueScheduled(frame, frame_len, 50, true);
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
    return resolveGroupSecret(channel, out_key16, out_key32,
                              PayloadProfile::V1, out_hash, out_hash ? 1 : 0);
}

bool MeshCoreAdapter::resolveGroupSecret(ChannelId channel, uint8_t out_key16[16],
                                         uint8_t out_key32[32], PayloadProfile profile,
                                         uint8_t* out_hash, size_t out_hash_cap) const
{
    if (!out_key16 || !out_key32)
    {
        return false;
    }

    const uint8_t slot = chat::meshCoreChannelSlotFromId(channel);
    const chat::MeshCoreChannelConfig& channel_config = config_.meshCoreChannel(slot);
    const uint8_t* selected = nullptr;
    if (channel_config.enabled &&
        !isZeroKey(channel_config.key, chat::kMeshCoreChannelKeyLen))
    {
        selected = channel_config.key;
    }
    else if (slot == 0 && shouldUsePublicChannelFallback(config_))
    {
        selected = publicGroupPsk();
    }

    if (!selected)
    {
        return false;
    }

    memcpy(out_key16, selected, 16);
    toHmacKey32(out_key16, out_key32);
    if (out_hash)
    {
        if (out_hash_cap < payloadHashBytes(profile) ||
            !computeChannelHashBytes(out_key16, out_hash, payloadHashBytes(profile)))
        {
            return false;
        }
    }
    return true;
}

ChannelId MeshCoreAdapter::resolveChannelFromHash(uint8_t channel_hash, bool* out_match) const
{
    return resolveChannelFromHash(PayloadProfile::V1, &channel_hash, out_match);
}

ChannelId MeshCoreAdapter::resolveChannelFromHash(PayloadProfile profile,
                                                  const uint8_t* channel_hash,
                                                  bool* out_match) const
{
    if (out_match)
    {
        *out_match = false;
    }

    if (!channel_hash)
    {
        return ChannelId::PRIMARY;
    }

    const size_t hash_bytes = payloadHashBytes(profile);
    uint8_t expected[chat::meshcore::kMeshCoreV2HashBytes] = {};
    for (uint8_t slot = 0; slot < chat::kMeshCoreChannelMaxCount; ++slot)
    {
        const chat::MeshCoreChannelConfig& channel_config = config_.meshCoreChannel(slot);
        if (!channel_config.enabled)
        {
            continue;
        }
        const uint8_t* key = nullptr;
        if (!isZeroKey(channel_config.key, chat::kMeshCoreChannelKeyLen))
        {
            key = channel_config.key;
        }
        else if (slot == 0 && shouldUsePublicChannelFallback(config_))
        {
            key = publicGroupPsk();
        }
        if (!key)
        {
            continue;
        }
        if (computeChannelHashBytes(key, expected, hash_bytes) &&
            memcmp(expected, channel_hash, hash_bytes) == 0)
        {
            if (out_match)
            {
                *out_match = true;
            }
            return chat::meshCoreChannelIdFromSlot(slot);
        }
    }

    return ChannelId::PRIMARY;
}

MeshCoreAdapter::PeerRouteEntry* MeshCoreAdapter::findPeerRouteByHash(uint8_t peer_hash)
{
    return findPeerRouteByHash(PayloadProfile::V1, &peer_hash);
}

const MeshCoreAdapter::PeerRouteEntry* MeshCoreAdapter::findPeerRouteByHash(uint8_t peer_hash) const
{
    return findPeerRouteByHash(PayloadProfile::V1, &peer_hash);
}

MeshCoreAdapter::PeerRouteEntry* MeshCoreAdapter::findPeerRouteByHash(PayloadProfile profile,
                                                                      const uint8_t* peer_hash)
{
    if (!peer_hash)
    {
        return nullptr;
    }
    const size_t hash_bytes = payloadHashBytes(profile);
    PeerRouteEntry* low_match = nullptr;
    for (auto& entry : peer_routes_)
    {
        if (entry.peer_hash != lowHashByte(profile, peer_hash))
        {
            continue;
        }
        if (entry.peer_hash_len >= hash_bytes &&
            memcmp(entry.peer_hash_bytes, peer_hash, hash_bytes) == 0)
        {
            return &entry;
        }
        if (!low_match && profile == PayloadProfile::V1)
        {
            low_match = &entry;
        }
    }
    return low_match;
}

const MeshCoreAdapter::PeerRouteEntry* MeshCoreAdapter::findPeerRouteByHash(PayloadProfile profile,
                                                                            const uint8_t* peer_hash) const
{
    if (!peer_hash)
    {
        return nullptr;
    }
    const size_t hash_bytes = payloadHashBytes(profile);
    const PeerRouteEntry* low_match = nullptr;
    for (const auto& entry : peer_routes_)
    {
        if (entry.peer_hash != lowHashByte(profile, peer_hash))
        {
            continue;
        }
        if (entry.peer_hash_len >= hash_bytes &&
            memcmp(entry.peer_hash_bytes, peer_hash, hash_bytes) == 0)
        {
            return &entry;
        }
        if (!low_match && profile == PayloadProfile::V1)
        {
            low_match = &entry;
        }
    }
    return low_match;
}

MeshCoreAdapter::PeerRouteEntry* MeshCoreAdapter::selectPeerRouteByHash(uint8_t peer_hash,
                                                                        uint32_t now_ms)
{
    return selectPeerRouteByHash(PayloadProfile::V1, &peer_hash, now_ms);
}

MeshCoreAdapter::PeerRouteEntry* MeshCoreAdapter::selectPeerRouteByHash(PayloadProfile profile,
                                                                        const uint8_t* peer_hash,
                                                                        uint32_t now_ms)
{
    PeerRouteEntry* entry = findPeerRouteByHash(profile, peer_hash);
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
    if (entry->out_path_profile != profile ||
        !pathIsWellFormed(profile, entry->out_path_len))
    {
        return nullptr;
    }
    return entry;
}

const MeshCoreAdapter::PeerRouteEntry* MeshCoreAdapter::selectPeerRouteByHash(uint8_t peer_hash,
                                                                              uint32_t now_ms) const
{
    return selectPeerRouteByHash(PayloadProfile::V1, &peer_hash, now_ms);
}

const MeshCoreAdapter::PeerRouteEntry* MeshCoreAdapter::selectPeerRouteByHash(PayloadProfile profile,
                                                                              const uint8_t* peer_hash,
                                                                              uint32_t now_ms) const
{
    const PeerRouteEntry* entry = findPeerRouteByHash(profile, peer_hash);
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
    if (entry->out_path_profile != profile ||
        !pathIsWellFormed(profile, entry->out_path_len))
    {
        return nullptr;
    }
    return entry;
}

MeshCoreAdapter::PeerRouteEntry& MeshCoreAdapter::upsertPeerRoute(uint8_t peer_hash, uint32_t now_ms)
{
    return upsertPeerRoute(PayloadProfile::V1, &peer_hash, now_ms);
}

MeshCoreAdapter::PeerRouteEntry& MeshCoreAdapter::upsertPeerRoute(PayloadProfile profile,
                                                                  const uint8_t* peer_hash,
                                                                  uint32_t now_ms)
{
    if (PeerRouteEntry* found = findPeerRouteByHash(profile, peer_hash))
    {
        copyPeerHashBytes(profile, peer_hash, found->peer_hash_bytes, &found->peer_hash_len);
        found->peer_hash = lowHashByte(profile, peer_hash);
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
    entry.peer_hash = lowHashByte(profile, peer_hash);
    copyPeerHashBytes(profile, peer_hash, entry.peer_hash_bytes, &entry.peer_hash_len);
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
        entry.out_path_profile = PayloadProfile::V1;
        return;
    }

    uint8_t best_idx = 0;
    int16_t best_quality = std::numeric_limits<int16_t>::min();
    for (uint8_t i = 0; i < entry.candidate_count; ++i)
    {
        PeerRouteEntry::PathCandidate& candidate = entry.candidates[i];
        const uint32_t age_ms = now_ms - candidate.last_seen_ms;
        candidate.quality = computePathQuality(
            static_cast<uint8_t>(pathHopCount(candidate.profile,
                                              candidate.path_len)),
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
    entry.out_path_profile = best.profile;
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
                                                PayloadProfile profile,
                                                const uint8_t* path, size_t path_len,
                                                ChannelId channel, int16_t snr_x10,
                                                uint32_t now_ms)
{
    if (!pathIsWellFormed(profile, path_len))
    {
        return;
    }
    uint8_t found_idx = entry.candidate_count;
    for (uint8_t i = 0; i < entry.candidate_count; ++i)
    {
        const PeerRouteEntry::PathCandidate& candidate = entry.candidates[i];
        if (candidate.channel != channel ||
            candidate.profile != profile ||
            candidate.path_len != path_len)
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
        fresh.profile = profile;
        if (path_len > 0)
        {
            memcpy(fresh.path, path, path_len);
        }
        fresh.channel = channel;
        fresh.snr_x10 = snr_x10;
        fresh.sample_count = 1;
        fresh.first_seen_ms = now_ms;
        fresh.last_seen_ms = now_ms;
        fresh.quality = computePathQuality(static_cast<uint8_t>(pathHopCount(profile, fresh.path_len)),
                                           fresh.snr_x10, fresh.sample_count, 0);
        entry.candidates[found_idx] = fresh;
    }
    else
    {
        PeerRouteEntry::PathCandidate& candidate = entry.candidates[found_idx];
        candidate.channel = channel;
        candidate.path_len = static_cast<uint8_t>(path_len);
        candidate.profile = profile;
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
        candidate.quality = computePathQuality(static_cast<uint8_t>(pathHopCount(profile, candidate.path_len)),
                                               candidate.snr_x10, candidate.sample_count, 0);
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
    rememberPeerPath(PayloadProfile::V1, &peer_hash, path, path_len, channel, now_ms);
}

void MeshCoreAdapter::rememberPeerPath(PayloadProfile profile, const uint8_t* peer_hash,
                                       const uint8_t* path, size_t path_len,
                                       ChannelId channel, uint32_t now_ms)
{
    if (path_len > kMaxPeerPathLen || (path_len > 0 && !path))
    {
        return;
    }
    if (!peer_hash || !pathIsWellFormed(profile, path_len))
    {
        return;
    }
    PeerRouteEntry& entry = upsertPeerRoute(profile, peer_hash, now_ms);
    uint8_t prev_path[kMaxPeerPathLen] = {};
    const uint8_t prev_len = entry.has_out_path ? entry.out_path_len : 0;
    const PayloadProfile prev_profile = entry.out_path_profile;
    const bool had_path = entry.has_out_path;
    if (had_path && prev_len > 0)
    {
        memcpy(prev_path, entry.out_path, prev_len);
    }
    const int16_t snr_x10 = std::isfinite(last_rx_snr_)
                                ? static_cast<int16_t>(std::lround(last_rx_snr_ * 10.0f))
                                : std::numeric_limits<int16_t>::min();
    rememberPeerPathCandidate(entry, profile, path, path_len, channel, snr_x10, now_ms);
    const bool has_path = entry.has_out_path;
    const uint8_t new_len = entry.out_path_len;
    const bool changed = (had_path != has_path) ||
                         (entry.out_path_profile != prev_profile) ||
                         (new_len != prev_len) ||
                         (new_len > 0 && memcmp(entry.out_path, prev_path, new_len) != 0);
    if (changed)
    {
        Event ev{};
        ev.type = Event::Type::PathUpdated;
        ev.peer_hash = lowHashByte(profile, peer_hash);
        ev.peer_node = resolvePeerNodeId(ev.peer_hash);
        ev.path_profile = entry.out_path_profile;
        pushEvent(std::move(ev));
    }
}

PayloadProfile MeshCoreAdapter::selectSendProfile(const PeerRouteEntry* route) const
{
    switch (config_.meshcore_send_profile)
    {
    case MeshCorePayloadSendProfile::V1Only:
        return PayloadProfile::V1;
    case MeshCorePayloadSendProfile::V2Only:
        return PayloadProfile::V2;
    case MeshCorePayloadSendProfile::AutoPreferV2:
    default:
        if (route && route->has_out_path && route->out_path_profile == PayloadProfile::V2)
        {
            return PayloadProfile::V2;
        }
        return PayloadProfile::V2;
    }
}

bool MeshCoreAdapter::allowsForwardingProfile(PayloadProfile profile) const
{
    switch (config_.meshcore_forward_profile)
    {
    case MeshCoreForwardProfile::Any:
        return true;
    case MeshCoreForwardProfile::MultibyteOnly:
    default:
        return payloadHashBytes(profile) > chat::meshcore::kMeshCoreV1HashBytes;
    }
}

bool MeshCoreAdapter::selfHash(PayloadProfile profile, uint8_t* out_hash, size_t out_cap) const
{
    if (!out_hash || out_cap < payloadHashBytes(profile) || !identity_.isReady())
    {
        return false;
    }
    return copyPublicHash(profile, identity_.publicKey(), MeshCoreIdentity::kPubKeySize,
                          out_hash, out_cap);
}

bool MeshCoreAdapter::lookupPeerPubKey(uint8_t peer_hash,
                                       uint8_t out_pubkey[MeshCoreIdentity::kPubKeySize]) const
{
    return lookupPeerPubKey(PayloadProfile::V1, &peer_hash, out_pubkey);
}

bool MeshCoreAdapter::lookupPeerPubKey(PayloadProfile profile, const uint8_t* peer_hash,
                                       uint8_t out_pubkey[MeshCoreIdentity::kPubKeySize]) const
{
    if (!out_pubkey)
    {
        return false;
    }
    const PeerRouteEntry* entry = findPeerRouteByHash(profile, peer_hash);
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

    uint8_t peer_hash2[chat::meshcore::kMeshCoreV2HashBytes] = {};
    copyPublicHash(PayloadProfile::V2, pubkey, MeshCoreIdentity::kPubKeySize,
                   peer_hash2, sizeof(peer_hash2));
    PeerRouteEntry* entry_ptr = findPeerRouteByHash(peer_hash);
    if (!entry_ptr || (entry_ptr->has_pubkey &&
                       memcmp(entry_ptr->pubkey, pubkey, MeshCoreIdentity::kPubKeySize) != 0))
    {
        entry_ptr = &upsertPeerRoute(PayloadProfile::V2, peer_hash2, now_ms);
    }
    else
    {
        copyPeerHashBytes(PayloadProfile::V2, peer_hash2,
                          entry_ptr->peer_hash_bytes, &entry_ptr->peer_hash_len);
    }
    PeerRouteEntry& entry = *entry_ptr;
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
        savePeerPubKeyToDirectory(entry);
    }
}

void MeshCoreAdapter::loadPeerPubKeysFromDirectory()
{
    if (!peer_directory_)
    {
        MESHCORE_LOG("[MESHCORE] peer directory unavailable, hot cache empty\n");
        return;
    }

    size_t count = 0;
    const MeshPeerDirectoryStatus status =
        peer_directory_->loadRecent(MeshProtocol::MeshCore,
                                    peer_directory_load_entries_.data(),
                                    peer_directory_load_entries_.size(),
                                    &count);
    if (!status.succeeded())
    {
        MESHCORE_LOG("[MESHCORE] peer directory load failed status=%u\n",
                     static_cast<unsigned>(status.code));
        return;
    }

    const uint32_t now_ms = millis();
    size_t loaded = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const MeshPeerRecord& persisted = peer_directory_load_entries_[i];
        const uint8_t* pubkey = nullptr;
        bool verified = false;
        if (persisted.meshcore.has_public_key)
        {
            pubkey = persisted.meshcore.public_key;
            verified = persisted.meshcore.public_key_verified;
        }
        else if (persisted.identity.kind == MeshPeerIdentityKind::PublicKey &&
                 meshPeerSameProtocol(persisted.identity.protocol, MeshProtocol::MeshCore) &&
                 persisted.identity.public_key_len == MeshCoreIdentity::kPubKeySize)
        {
            pubkey = persisted.identity.public_key;
        }
        else
        {
            continue;
        }

        const uint8_t peer_hash = pubkey[0];
        if (peer_hash == 0x00 || peer_hash == 0xFF || peer_hash == self_hash_)
        {
            continue;
        }
        if (isZeroKey(pubkey, MeshCoreIdentity::kPubKeySize))
        {
            continue;
        }

        uint8_t peer_hash2[chat::meshcore::kMeshCoreV2HashBytes] = {};
        copyPublicHash(PayloadProfile::V2, pubkey, MeshCoreIdentity::kPubKeySize,
                       peer_hash2, sizeof(peer_hash2));
        PeerRouteEntry& entry = upsertPeerRoute(PayloadProfile::V2, peer_hash2, now_ms);
        entry.has_pubkey = true;
        entry.pubkey_verified = verified;
        memcpy(entry.pubkey, pubkey, sizeof(entry.pubkey));
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
        MESHCORE_LOG("[MESHCORE] peer keys loaded=%u directory=mesh_peer_directory\n",
                     static_cast<unsigned>(loaded));
    }
}

void MeshCoreAdapter::savePeerPubKeyToDirectory(const PeerRouteEntry& route)
{
    if (!peer_directory_ || !route.has_pubkey ||
        route.peer_hash == 0x00 || route.peer_hash == 0xFF ||
        route.peer_hash == self_hash_ ||
        isZeroKey(route.pubkey, sizeof(route.pubkey)))
    {
        return;
    }

    MeshPeerRecord record{};
    record.valid = true;
    if (!makeMeshPeerPublicKeyIdentity(MeshProtocol::MeshCore,
                                       route.pubkey,
                                       sizeof(route.pubkey),
                                       record.identity))
    {
        return;
    }

    const uint32_t now_s = now_message_timestamp();
    record.source = MeshPeerSource::RuntimeRx;
    record.first_seen_s = now_s;
    record.last_seen_s = now_s;
    record.meshcore.has_public_key = true;
    memcpy(record.meshcore.public_key,
           route.pubkey,
           sizeof(record.meshcore.public_key));
    record.meshcore.public_key_verified = route.pubkey_verified;
    record.meshcore.has_peer_hash = true;
    record.meshcore.peer_hash = route.peer_hash;
    record.meshcore.node_id_hint =
        deriveNodeIdFromPubkey(route.pubkey, sizeof(route.pubkey));

    const MeshPeerDirectoryStatus status = peer_directory_->record(record);
    if (!status.succeeded())
    {
        MESHCORE_LOG("[MESHCORE] peer key directory save failed hash=%02X status=%u\n",
                     route.peer_hash,
                     static_cast<unsigned>(status.code));
        return;
    }
    MESHCORE_LOG("[MESHCORE] peer key saved hash=%02X directory=mesh_peer_directory\n",
                 route.peer_hash);
}

void MeshCoreAdapter::maybeAutoDiscoverMissingPeer(uint8_t peer_hash, uint32_t now_ms)
{
    runtime::MeshCoreAutoDiscoverMissingPeerInput input{};
    input.peer_hash = peer_hash;
    input.self_hash = self_hash_;
    input.type_filter = kMeshCoreDiscoverTypeFilterAll;
    input.rx_guard_ms = kDiscoverRxGuardMs;

    runtime::RuntimeContext context = buildRuntimeContext();
    context.now_ms = now_ms;
    protocol_effect_workspace_.primary.clear();
    protocol_runtime_.prepareAutoDiscoverMissingPeer(input,
                                                     context,
                                                     protocol_effect_workspace_.primary);
    if (protocol_effect_workspace_.primary.items.empty())
    {
        return;
    }

    const MeshActionResult result =
        executeDiscoveryEffectsDetailed(protocol_effect_workspace_.primary);
    protocol_runtime_.markAutoDiscoverMissingPeerTxResult(peer_hash, context, result.ok);
    if (result.ok)
    {
        MESHCORE_LOG("[MESHCORE] auto discover trigger src=%02X cooldown=%lums\n",
                     static_cast<unsigned>(peer_hash),
                     static_cast<unsigned long>(runtime::kMeshCoreAutoDiscoverCooldownMs));
    }
}

bool MeshCoreAdapter::deriveIdentitySecret(uint8_t peer_hash,
                                           uint8_t out_key16[16],
                                           uint8_t out_key32[32]) const
{
    return deriveIdentitySecret(PayloadProfile::V1, &peer_hash, out_key16, out_key32);
}

bool MeshCoreAdapter::deriveIdentitySecret(PayloadProfile profile, const uint8_t* peer_hash,
                                           uint8_t out_key16[16],
                                           uint8_t out_key32[32]) const
{
    if (!out_key16 || !out_key32 || !identity_.isReady())
    {
        return false;
    }

    uint8_t peer_pubkey[MeshCoreIdentity::kPubKeySize] = {};
    if (!lookupPeerPubKey(profile, peer_hash, peer_pubkey))
    {
        return false;
    }

    uint8_t shared_secret[kCipherHmacKeySize] = {};
    if (!identity_.deriveSharedSecret(peer_pubkey, shared_secret))
    {
        return false;
    }

    return chat::runtime::MeshCoreDirectSecretCore::expandSharedSecret(shared_secret, sizeof(shared_secret),
                                                                       out_key16, 16,
                                                                       out_key32, 32);
}

bool MeshCoreAdapter::deriveDirectSecret(ChannelId channel, uint8_t peer_hash,
                                         uint8_t out_key16[16], uint8_t out_key32[32]) const
{
    return deriveDirectSecret(channel, PayloadProfile::V1, &peer_hash, out_key16, out_key32);
}

bool MeshCoreAdapter::deriveDirectSecret(ChannelId channel, PayloadProfile profile,
                                         const uint8_t* peer_hash,
                                         uint8_t out_key16[16], uint8_t out_key32[32]) const
{
    (void)channel;
    return deriveIdentitySecret(profile, peer_hash, out_key16, out_key32);
}

bool MeshCoreAdapter::tryDecryptPeerPayload(uint8_t src_hash,
                                            const uint8_t* cipher, size_t cipher_len,
                                            uint8_t* out_plain, size_t* out_plain_len,
                                            ChannelId* out_channel) const
{
    return tryDecryptPeerPayload(PayloadProfile::V1, &src_hash, cipher, cipher_len,
                                 out_plain, out_plain_len, out_channel);
}

bool MeshCoreAdapter::tryDecryptPeerPayload(PayloadProfile profile, const uint8_t* src_hash,
                                            const uint8_t* cipher, size_t cipher_len,
                                            uint8_t* out_plain, size_t* out_plain_len,
                                            ChannelId* out_channel) const
{
    if (!cipher || cipher_len == 0 || !out_plain || !out_plain_len)
    {
        return false;
    }

    ChannelId order[chat::kMeshCoreChannelMaxCount] = {};
    size_t order_len = 0;
    auto addCandidateChannel = [&](ChannelId candidate)
    {
        const uint8_t slot = chat::meshCoreChannelSlotFromId(candidate);
        if (!config_.meshCoreChannel(slot).enabled)
        {
            return;
        }
        for (size_t j = 0; j < order_len; ++j)
        {
            if (order[j] == candidate)
            {
                return;
            }
        }
        if (order_len < (sizeof(order) / sizeof(order[0])))
        {
            order[order_len++] = candidate;
        }
    };
    const PeerRouteEntry* known = findPeerRouteByHash(profile, src_hash);
    if (known)
    {
        addCandidateChannel(known->preferred_channel);
    }
    addCandidateChannel(config_.activeMeshCoreChannel().enabled
                            ? chat::meshCoreChannelIdFromSlot(config_.meshcore_channel_slot)
                            : ChannelId::PRIMARY);
    for (uint8_t slot = 0; slot < chat::kMeshCoreChannelMaxCount; ++slot)
    {
        addCandidateChannel(chat::meshCoreChannelIdFromSlot(slot));
    }

    uint8_t tried_key16[16] = {};
    uint8_t tried_key32[32] = {};
    bool has_tried_key = false;

    auto tryCandidate = [&](ChannelId candidate_channel,
                            const uint8_t key16[16],
                            const uint8_t key32[32]) -> bool
    {
        if (has_tried_key &&
            memcmp(tried_key16, key16, sizeof(tried_key16)) == 0 &&
            memcmp(tried_key32, key32, sizeof(tried_key32)) == 0)
        {
            return false;
        }
        memcpy(tried_key16, key16, sizeof(tried_key16));
        memcpy(tried_key32, key32, sizeof(tried_key32));
        has_tried_key = true;

        size_t plain_len = 0;
        if (!macThenDecrypt(key16, key32, cipher, cipher_len, out_plain, &plain_len,
                            payloadMacBytes(profile)))
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
            if (deriveDirectSecret(order[i], profile, src_hash, key16, key32) &&
                tryCandidate(order[i], key16, key32))
            {
                return true;
            }
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

MeshOperationFailure MeshCoreAdapter::txGateFailure(TxGateReason reason)
{
    switch (reason)
    {
    case TxGateReason::NotInitialized:
        return MeshOperationFailure::NotReady;
    case TxGateReason::TxDisabled:
        return MeshOperationFailure::TxDisabled;
    case TxGateReason::RadioOffline:
        return MeshOperationFailure::RadioOffline;
    case TxGateReason::DutyCycleLimited:
        return MeshOperationFailure::DutyCycleLimited;
    case TxGateReason::Ok:
    default:
        return MeshOperationFailure::None;
    }
}

bool MeshCoreAdapter::canTransmitNow(uint32_t now_ms) const
{
    return checkTxGate(now_ms) == TxGateReason::Ok;
}

MeshActionResult MeshCoreAdapter::transmitFrameNowDetailed(const uint8_t* data, size_t len,
                                                           uint32_t now_ms)
{
    if (!data || len == 0 || len > kMeshcoreMaxFrameSize)
    {
        return MeshActionResult::fail(MeshOperationFailure::InvalidInput);
    }

    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        return MeshActionResult::fail(txGateFailure(tx_gate));
    }

    const float air_ms_f = estimateLoRaAirtimeMs(len,
                                                 config_.meshcore_bw_khz,
                                                 config_.meshcore_sf,
                                                 config_.meshcore_cr);
    const uint32_t air_ms = (air_ms_f > 0.0f && std::isfinite(air_ms_f))
                                ? static_cast<uint32_t>(std::lround(air_ms_f))
                                : 0U;

    int state = -1;
#if defined(ARDUINO)
    app::AppTasks::requestRadioReceiveRestart();
    {
        app::AppTasks::ScopedRadioTransmitActivity tx_activity;
        state = board_.transmitRadio(data, len);
    }
#else
    idf_radio_pump_.markReceiveStopped();
    state = board_.transmitRadio(data, len);
#endif
    if (state == 0)
    {
        ParsedPacket parsed;
        const bool parsed_ok = parsePacket(data, len, &parsed);
        const bool bridge_candidate = parsed_ok && isSupportedPayloadVersion(parsed.payload_ver);
        bool bridge_already_seen = false;
        if (bridge_candidate)
        {
            const uint32_t packet_sig = packetSignature(parsed.payload_type, parsed.path_len,
                                                        parsed.payload, parsed.payload_len);
            bridge_already_seen = hasSeenSignature(packet_sig, now_ms);
        }
        last_tx_ms_ = now_ms;
        tx_airtime_ms_ = saturatingAddU32(tx_airtime_ms_, air_ms);
        if (bridge_candidate && !bridge_already_seen)
        {
            queueMqttBridgePacket(data, len);
        }
#if defined(ARDUINO)
        const int rx_state = board_.startRadioReceive();
#else
        const int rx_state = idf_radio_pump_.restartReceive() ? 0 : -1;
#endif
        if (rx_state != 0)
        {
#if defined(ARDUINO)
            app::AppTasks::requestRadioReceiveRestart();
#endif
            MESHCORE_LOG("[MESHCORE] RX restart fail state=%d\n", rx_state);
        }
        else
        {
#if defined(ARDUINO)
            app::AppTasks::setRadioReceiveActive(true);
#endif
            if (parsePacket(data, len, &parsed))
            {
                MESHCORE_LOG("[MESHCORE] RX restart ok after_tx route=%u type=%u len=%u\n",
                             static_cast<unsigned>(parsed.route_type),
                             static_cast<unsigned>(parsed.payload_type),
                             static_cast<unsigned>(len));
            }
            else
            {
                MESHCORE_LOG("[MESHCORE] RX restart ok after_tx len=%u\n",
                             static_cast<unsigned>(len));
            }
        }
        return MeshActionResult::success();
    }
#if defined(ARDUINO)
    app::AppTasks::requestRadioReceiveRestart();
    const bool radio_busy = (state == RADIOLIB_ERR_SPI_WRITE_FAILED);
#else
    (void)idf_radio_pump_.restartReceive();
    const bool radio_busy = false;
#endif
    return MeshActionResult::fail(radio_busy
                                      ? MeshOperationFailure::Busy
                                      : MeshOperationFailure::RadioTxFailed,
                                  state);
}

bool MeshCoreAdapter::transmitFrameNow(const uint8_t* data, size_t len, uint32_t now_ms)
{
    return transmitFrameNowDetailed(data, len, now_ms).ok;
}

bool MeshCoreAdapter::enqueueScheduled(const uint8_t* data, size_t len, uint32_t delay_ms,
                                       bool defer_during_discover)
{
    if (!data || len == 0 || len > kMeshcoreMaxFrameSize)
    {
        return false;
    }
    ScheduledFrame frame;
    if (!frame.assign(data, len))
    {
        return false;
    }
    frame.due_ms = millis() + delay_ms;
    frame.defer_during_discover = defer_during_discover;
    scheduled_tx_.pushDropOldest(frame);
    return true;
}

void MeshCoreAdapter::armDiscoverRxGuard(uint32_t now_ms, uint32_t duration_ms)
{
    discover_rx_guard_until_ms_ = now_ms + (duration_ms == 0 ? kDiscoverRxGuardMs : duration_ms);
}

bool MeshCoreAdapter::isDiscoverRxGuardActive(uint32_t now_ms) const
{
    return discover_rx_guard_until_ms_ != 0 &&
           static_cast<int32_t>(now_ms - discover_rx_guard_until_ms_) < 0;
}

void MeshCoreAdapter::pruneSeen(uint32_t now_ms)
{
    seen_recent_.prune(now_ms, kSeenTtlMs);
}

bool MeshCoreAdapter::hasSeenSignature(uint32_t signature, uint32_t now_ms)
{
    pruneSeen(now_ms);
    if (seen_recent_.contains(signature))
    {
        return true;
    }
    SeenEntry entry;
    entry.signature = signature;
    entry.seen_ms = now_ms;
    seen_recent_.appendDropOldest(entry);
    return false;
}

void MeshCoreAdapter::queueMqttBridgePacket(const uint8_t* data, size_t len)
{
    if (!mqtt_bridge_enabled_ || !data || len == 0 || len > kScheduledFrameMaxLen)
    {
        return;
    }

    if (mqtt_bridge_count_ >= mqtt_bridge_queue_.size())
    {
        mqtt_bridge_queue_[mqtt_bridge_read_index_].bytes_len = 0;
        mqtt_bridge_read_index_ = (mqtt_bridge_read_index_ + 1U) % mqtt_bridge_queue_.size();
        --mqtt_bridge_count_;
    }

    const size_t write_index =
        (mqtt_bridge_read_index_ + mqtt_bridge_count_) % mqtt_bridge_queue_.size();
    if (mqtt_bridge_queue_[write_index].assign(data, len))
    {
        ++mqtt_bridge_count_;
    }
}

void MeshCoreAdapter::prunePendingAppAcks(uint32_t now_ms)
{
    runtime::RuntimeContext context = buildRuntimeContext();
    context.now_ms = now_ms;
    runtime::FixedProtocolRuntimeContextProvider context_provider(context);
    const auto bundle = protocolRuntimeBundle(context_provider);
    if (!bundle.valid())
    {
        return;
    }
    auto facade = bundle.createFacade(protocol_effect_workspace_,
                                      runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
    facade.tick();
}

void MeshCoreAdapter::trackPendingAppAck(uint32_t signature, NodeId dest, uint32_t portnum, uint32_t now_ms)
{
    runtime::RuntimeContext context = buildRuntimeContext();
    context.now_ms = now_ms;
    runtime::MeshCoreAppAckRegistration ack{};
    ack.signature = signature;
    ack.peer = dest;
    ack.portnum = portnum;
    ack.timeout_ms = kAppAckTimeoutMs;
    protocol_effect_workspace_.primary.clear();
    protocol_runtime_.trackAppAck(ack, context, protocol_effect_workspace_.primary);
    executeProtocolEffects(protocol_effect_workspace_.primary);
}

void MeshCoreAdapter::bindPendingAppAckToChatMessage(uint32_t signature, MessageId msg_id)
{
    protocol_effect_workspace_.primary.clear();
    protocol_runtime_.bindAppAckToMessage(signature,
                                          msg_id,
                                          protocol_effect_workspace_.primary);
    executeProtocolEffects(protocol_effect_workspace_.primary);
}

bool MeshCoreAdapter::consumePendingAppAck(uint32_t signature, uint32_t now_ms)
{
    runtime::RuntimeContext context = buildRuntimeContext();
    context.now_ms = now_ms;
    protocol_effect_workspace_.primary.clear();
    protocol_runtime_.handleAppAck(signature, context, protocol_effect_workspace_.primary);
    bool matched = false;
    for (const auto& effect : protocol_effect_workspace_.primary.items)
    {
        if (const auto* result = std::get_if<runtime::EmitActionResultEffect>(&effect))
        {
            matched = matched ||
                      (result->protocol == MeshProtocol::MeshCore &&
                       result->request_id == signature &&
                       result->state == runtime::ProtocolActionState::Completed);
        }
    }
    executeProtocolEffects(protocol_effect_workspace_.primary);
    return matched;
}

void MeshCoreAdapter::pushEvent(Event&& ev)
{
    events_.pushDropOldest(std::move(ev));
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
    if (verified_peers_.size() >= kMaxVerifiedPeers)
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
    MeshCoreSha256 sha;
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
    uint8_t payload[kMeshCoreNodeInfoInfoPayloadSize] = {};
    size_t payload_len = 0;

    if (is_query)
    {
        if (!buildNodeInfoQueryControlPayload(request_reply, payload, sizeof(payload), &payload_len))
        {
            return false;
        }
    }
    else
    {
        char short_name[kMeshCoreNodeInfoShortNameFieldSize] = {};
        if (!user_short_name_.empty())
        {
            strncpy(short_name, user_short_name_.c_str(), sizeof(short_name) - 1);
        }
        else
        {
            snprintf(short_name, sizeof(short_name), "%04lX",
                     static_cast<unsigned long>(node_id_ & 0xFFFFUL));
        }

        char long_name[kMeshCoreNodeInfoLongNameFieldSize] = {};
        if (!user_long_name_.empty())
        {
            strncpy(long_name, user_long_name_.c_str(), sizeof(long_name) - 1);
        }
        else
        {
            strncpy(long_name, short_name, sizeof(long_name) - 1);
        }

        MeshCoreNodeInfoBuildInfo info{};
        info.role = static_cast<uint8_t>(chat::contacts::NodeRoleType::Client);
        info.hops = 0;
        info.node_id = node_id_;
        info.timestamp = now_message_timestamp();
        info.short_name = short_name;
        info.long_name = long_name;
        if (!buildNodeInfoInfoControlPayload(info, payload, sizeof(payload), &payload_len))
        {
            return false;
        }
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

    return sendAppData(channel, kMeshCoreNodeInfoPortnum, payload, payload_len, tx_dest, false);
}

bool MeshCoreAdapter::requestNodeInfo(NodeId dest, bool want_response)
{
    if (!initialized_ || !config_.tx_enabled)
    {
        return false;
    }

    runtime::FixedProtocolRuntimeContextProvider context_provider(buildRuntimeContext());
    const auto bundle = protocolRuntimeBundle(context_provider);
    if (!bundle.valid())
    {
        return false;
    }
    auto facade = bundle.createFacade(protocol_effect_workspace_,
                                      runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
    return facade.requestNodeInfo(dest, want_response).ok();
}

runtime::RuntimeContext MeshCoreAdapter::buildRuntimeContext() const
{
    runtime::RuntimeContext context{};
    context.protocol = MeshProtocol::MeshCore;
    context.self_node = node_id_;
    context.now_ms = millis();
    context.meshcore_discover_node_type = config_.meshcore_client_repeat
                                              ? kAdvertTypeRepeater
                                              : kAdvertTypeChat;
    context.meshcore_local_modified_epoch = now_epoch_seconds();
    return context;
}

runtime::ProtocolRuntimeBundle MeshCoreAdapter::protocolRuntimeBundle(
    const runtime::IProtocolRuntimeContextProvider& context_provider)
{
    runtime::ProtocolRuntimeSelection selection{};
    selection.meshcore = &protocol_runtime_;
    return runtime::protocolRuntimeFor(MeshProtocol::MeshCore,
                                       selection,
                                       *this,
                                       context_provider);
}

bool MeshCoreAdapter::execute(const runtime::ProtocolEffect& effect)
{
    return executeProtocolEffect(effect);
}

bool MeshCoreAdapter::executeProtocolEffects(const runtime::ProtocolEffects& effects)
{
    bool ok = true;
    for (const auto& effect : effects.items)
    {
        ok = execute(effect) && ok;
    }
    return ok;
}

bool MeshCoreAdapter::executeProtocolEffect(const runtime::ProtocolEffect& effect)
{
    bool ok = false;
    runtime::visitProtocolEffect(
        effect,
        [this, &ok](const auto& item)
        {
            using Effect = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Effect, runtime::SendNodeInfoEffect>)
            {
                if (item.protocol == MeshProtocol::MeshCore)
                {
                    ok = sendNodeInfoFrame(item.peer, item.want_response, item.want_response);
                }
            }
            else if constexpr (std::is_same_v<Effect, runtime::SendTraceRouteEffect>)
            {
                if (item.protocol == MeshProtocol::MeshCore)
                {
                    const uint8_t peer_hash = static_cast<uint8_t>(item.peer & 0xFFU);
                    uint8_t path[kMeshcoreMaxPathSize] = {};
                    size_t path_len = 0;
                    const uint32_t now_ms = millis();
                    if (peer_hash != 0x00 && peer_hash != 0xFF && peer_hash != self_hash_)
                    {
                        if (const PeerRouteEntry* route = selectPeerRouteByHash(peer_hash, now_ms);
                            route && route->has_out_path && route->out_path_len > 0)
                        {
                            path_len = route->out_path_len;
                            memcpy(path, route->out_path, path_len);
                        }
                        else
                        {
                            path[path_len++] = peer_hash;
                        }
                    }

                    uint32_t timeout_ms = item.timeout_ms;
                    ok = path_len > 0 &&
                         sendTracePath(path,
                                       path_len,
                                       item.request_id,
                                       item.auth,
                                       item.flags,
                                       &timeout_ms);

                    runtime::TxResult result{};
                    result.protocol = MeshProtocol::MeshCore;
                    result.request_id = item.request_id;
                    result.peer = item.peer;
                    result.ok = ok;
                    result.detail = ok ? static_cast<int32_t>(timeout_ms) : 0;
                    runtime::FixedProtocolRuntimeContextProvider context_provider(
                        buildRuntimeContext());
                    const auto bundle = protocolRuntimeBundle(context_provider);
                    if (bundle.valid())
                    {
                        auto facade = bundle.createFacade(
                            protocol_effect_workspace_,
                            runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
                        facade.handleTxResult(result);
                    }
                }
            }
            else if constexpr (std::is_same_v<Effect, runtime::SendDiscoverRequestEffect>)
            {
                if (item.protocol == MeshProtocol::MeshCore)
                {
                    ok = sendDiscoverRequestLocalDetailed(item).ok;
                }
            }
            else if constexpr (std::is_same_v<Effect, runtime::SendDiscoverResponseEffect>)
            {
                if (item.protocol == MeshProtocol::MeshCore)
                {
                    if (!identity_.isReady())
                    {
                        MESHCORE_LOG("[MESHCORE] RX DISCOVER_REQ ignored (identity unavailable)\n");
                        ok = true;
                    }
                    else
                    {
                        const uint8_t local_type = config_.meshcore_client_repeat
                                                       ? kAdvertTypeRepeater
                                                       : kAdvertTypeChat;
                        const size_t key_len = item.prefix_only ? chat::meshcore::kMeshCorePubKeyPrefixSize : kMeshcorePubKeySize;
                        uint8_t resp_payload[6 + kMeshcorePubKeySize] = {};
                        size_t resp_len = 0;
                        ok = buildDiscoverResponseControlPayload(local_type,
                                                                 std::isfinite(last_rx_snr_)
                                                                     ? static_cast<int8_t>(std::lround(last_rx_snr_ * 4.0f))
                                                                     : 0,
                                                                 item.tag,
                                                                 identity_.publicKey(),
                                                                 key_len,
                                                                 resp_payload,
                                                                 sizeof(resp_payload),
                                                                 &resp_len);
                        if (ok)
                        {
                            uint8_t frame[kMeshcoreMaxFrameSize] = {};
                            size_t frame_len = 0;
                            const PayloadProfile profile =
                                (config_.meshcore_send_profile == MeshCorePayloadSendProfile::V1Only)
                                    ? PayloadProfile::V1
                                    : PayloadProfile::V2;
                            ok = buildFrameNoTransport(profile,
                                                       kRouteTypeDirect, kPayloadTypeControl,
                                                       nullptr, 0,
                                                       resp_payload, resp_len,
                                                       frame, sizeof(frame), &frame_len);
                            if (ok)
                            {
                                float air_ms_f = estimateLoRaAirtimeMs(frame_len,
                                                                       config_.meshcore_bw_khz,
                                                                       config_.meshcore_sf,
                                                                       config_.meshcore_cr);
                                if (!std::isfinite(air_ms_f) || air_ms_f <= 0.0f)
                                {
                                    air_ms_f = 50.0f;
                                }
                                uint32_t t_ms = static_cast<uint32_t>(std::lround((air_ms_f * 52.0f / 50.0f) / 2.0f));
                                if (t_ms == 0)
                                {
                                    t_ms = 1;
                                }
                                const uint32_t delay_ms =
                                    ::platform::esp::common::meshcore_runtime::random_between(1, 5) *
                                    t_ms * 4U;
                                if (config_.tx_enabled)
                                {
                                    enqueueScheduled(frame, frame_len, delay_ms);
                                }
                                MESHCORE_LOG("[MESHCORE] RX DISCOVER_REQ tag=%08lX prefix=%u -> RESP len=%u delay=%lu\n",
                                             static_cast<unsigned long>(item.tag),
                                             item.prefix_only ? 1U : 0U,
                                             static_cast<unsigned>(resp_len),
                                             static_cast<unsigned long>(delay_ms));
                            }
                        }
                    }
                }
            }
            else if constexpr (std::is_same_v<Effect, runtime::SendSelfAnnouncementEffect>)
            {
                if (item.protocol == MeshProtocol::MeshCore)
                {
                    ok = sendIdentityAdvertDetailed(item.broadcast,
                                                    item.include_location,
                                                    item.lat_i6,
                                                    item.lon_i6)
                             .ok;
                }
            }
            else if constexpr (std::is_same_v<Effect, runtime::PublishNodeInfoEffect>)
            {
                if (item.protocol == MeshProtocol::MeshCore && item.node_id != 0)
                {
                    float snr = 0.0f;
                    if (item.rx_meta.snr_db_x10 != std::numeric_limits<int16_t>::min())
                    {
                        snr = static_cast<float>(item.rx_meta.snr_db_x10) / 10.0f;
                    }
                    else if (std::isfinite(last_rx_snr_))
                    {
                        snr = last_rx_snr_;
                    }

                    float rssi = 0.0f;
                    if (item.rx_meta.rssi_dbm_x10 != std::numeric_limits<int16_t>::min())
                    {
                        rssi = static_cast<float>(item.rx_meta.rssi_dbm_x10) / 10.0f;
                    }
                    else if (std::isfinite(last_rx_rssi_))
                    {
                        rssi = last_rx_rssi_;
                    }

                    const uint32_t ts = is_valid_epoch(item.timestamp)
                                            ? item.timestamp
                                            : now_message_timestamp();
                    sys::EventBus::publish(
                        new sys::NodeInfoUpdateEvent(
                            item.node_id,
                            item.short_name.c_str(),
                            item.long_name.c_str(),
                            snr,
                            rssi,
                            ts,
                            static_cast<uint8_t>(chat::contacts::NodeProtocolType::MeshCore),
                            item.role,
                            item.hops,
                            0,
                            0xFF,
                            false,
                            nullptr,
                            false,
                            false,
                            item.has_public_key,
                            item.key_manually_verified,
                            false,
                            nullptr,
                            true,
                            true),
                        0);
                    ok = true;
                }
            }
            else if constexpr (std::is_same_v<Effect, runtime::UpdatePeerRouteEffect>)
            {
                if (item.protocol == MeshProtocol::MeshCore)
                {
                    const uint32_t now_ms = millis();
                    const uint8_t peer_hash = item.peer_hash != 0
                                                  ? item.peer_hash
                                                  : static_cast<uint8_t>(item.peer & 0xFFU);
                    if (peer_hash != 0)
                    {
                        rememberPeerNodeId(peer_hash, item.peer, now_ms);
                        if (item.public_key.size() == kMeshcorePubKeySize)
                        {
                            rememberPeerPubKey(item.public_key.data(), now_ms, item.public_key_verified);
                        }

                        Event ev{};
                        ev.type = Event::Type::ControlData;
                        ev.peer_hash = peer_hash;
                        ev.peer_node = item.peer;
                        ev.flags = item.hops;
                        ev.tag = item.tag;
                        ev.payload = item.payload;
                        pushEvent(std::move(ev));
                    }
                    ok = true;
                }
            }
            else if constexpr (std::is_same_v<Effect, runtime::EmitActionResultEffect>)
            {
                if (item.protocol == MeshProtocol::MeshCore)
                {
                    const bool is_app_ack =
                        item.request_id != 0 &&
                        (item.action == runtime::ProtocolActionKind::SendText ||
                         item.action == runtime::ProtocolActionKind::Unknown);
                    if (is_app_ack &&
                        item.state == runtime::ProtocolActionState::Completed)
                    {
                        Event ev{};
                        ev.type = Event::Type::SendConfirmed;
                        ev.tag = item.request_id;
                        ev.trip_ms = item.detail > 0 ? static_cast<uint32_t>(item.detail) : 0;
                        pushEvent(std::move(ev));
                        if (item.message_id != 0)
                        {
                            sys::EventBus::publish(
                                new sys::ChatSendResultEvent(
                                    item.message_id,
                                    chat::MessageStatus::Delivered,
                                    chat::MeshProtocol::MeshCore),
                                0);
                        }
                    }
                    else if (is_app_ack &&
                             (item.state == runtime::ProtocolActionState::Failed ||
                              item.state == runtime::ProtocolActionState::TimedOut))
                    {
                        const uint8_t peer_hash = static_cast<uint8_t>(item.peer & 0xFFU);
                        MESHCORE_LOG("[MESHCORE] ACK timeout sig=%08lX dest=%08lX age=%ldms\n",
                                     static_cast<unsigned long>(item.request_id),
                                     static_cast<unsigned long>(item.peer),
                                     static_cast<long>(item.detail));
                        penalizePeerRoute(peer_hash, millis());
                        if (item.message_id != 0)
                        {
                            sys::EventBus::publish(
                                new sys::ChatSendResultEvent(
                                    item.message_id,
                                    chat::MessageStatus::Failed,
                                    chat::MeshProtocol::MeshCore,
                                    chat::delivery::SendFailureKind::AckTimeout),
                                0);
                        }
                    }

                    MESHCORE_LOG("[MESHCORE] action result action=%u state=%u peer=%08lX req=%08lX detail=%ld\n",
                                 static_cast<unsigned>(item.action),
                                 static_cast<unsigned>(item.state),
                                 static_cast<unsigned long>(item.peer),
                                 static_cast<unsigned long>(item.request_id),
                                 static_cast<long>(item.detail));
                    ok = item.state != runtime::ProtocolActionState::Failed &&
                         item.state != runtime::ProtocolActionState::TimedOut;
                }
            }
        });
    return ok;
}

bool MeshCoreAdapter::triggerDiscoveryAction(MeshDiscoveryAction action)
{
    return triggerDiscoveryActionDetailed(action).ok;
}

MeshActionResult MeshCoreAdapter::triggerDiscoveryActionDetailed(MeshDiscoveryAction action)
{
    return executeDiscoverIntentDetailed(action);
}

MeshActionResult MeshCoreAdapter::executeDiscoverIntentDetailed(MeshDiscoveryAction action)
{
    runtime::DiscoverIntent intent{};
    intent.action = action;
    intent.type_filter = kMeshCoreDiscoverTypeFilterAll;
    intent.rx_guard_ms = kDiscoverRxGuardMs;
    protocol_effect_workspace_.primary.clear();
    protocol_runtime_.prepareOutgoing(intent,
                                      buildRuntimeContext(),
                                      protocol_effect_workspace_.primary);
    return executeDiscoveryEffectsDetailed(protocol_effect_workspace_.primary);
}

MeshActionResult MeshCoreAdapter::executeDiscoveryEffectsDetailed(const runtime::ProtocolEffects& effects)
{
    MeshActionResult result = MeshActionResult::fail(MeshOperationFailure::Unsupported);
    for (const runtime::ProtocolEffect& effect : effects.items)
    {
        if (const auto* discover = std::get_if<runtime::SendDiscoverRequestEffect>(&effect))
        {
            result = sendDiscoverRequestLocalDetailed(*discover);
            continue;
        }
        if (const auto* announce = std::get_if<runtime::SendSelfAnnouncementEffect>(&effect))
        {
            result = sendIdentityAdvertDetailed(announce->broadcast,
                                                announce->include_location,
                                                announce->lat_i6,
                                                announce->lon_i6);
        }
    }
    return result;
}

MeshActionResult MeshCoreAdapter::sendDiscoverRequestLocalDetailed(const runtime::SendDiscoverRequestEffect& effect)
{
    const uint32_t now_ms = millis();
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        MESHCORE_LOG("[MESHCORE] TX DISCOVER_REQ blocked reason=%s\n",
                     txGateReasonName(tx_gate));
        return MeshActionResult::fail(txGateFailure(tx_gate));
    }

    MeshCoreDiscoverRequestBuildInfo request{};
    request.prefix_only = effect.prefix_only;
    request.type_filter = effect.type_filter;
    request.tag = effect.tag == 0 ? static_cast<uint32_t>(esp_random()) : effect.tag;
    request.since = effect.since;

    uint8_t payload[10] = {};
    size_t payload_len = 0;
    if (!buildDiscoverRequestControlPayload(request, payload, sizeof(payload), &payload_len))
    {
        return MeshActionResult::fail(MeshOperationFailure::EncodeFailed);
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    const PayloadProfile profile =
        (config_.meshcore_send_profile == MeshCorePayloadSendProfile::V1Only)
            ? PayloadProfile::V1
            : PayloadProfile::V2;
    if (!buildFrameNoTransport(profile,
                               kRouteTypeDirect, kPayloadTypeControl,
                               nullptr, 0,
                               payload, payload_len,
                               frame, sizeof(frame), &frame_len))
    {
        return MeshActionResult::fail(MeshOperationFailure::EncodeFailed);
    }

    MeshActionResult result = transmitFrameNowDetailed(frame, frame_len, now_ms);
    if (result.ok)
    {
        armDiscoverRxGuard(millis(), effect.rx_guard_ms);
    }
    MESHCORE_LOG("[MESHCORE] TX DISCOVER_REQ mode=local tag=%08lX filter=%02X prefix=%u since=%lu len=%u ok=%u guard=%lums detail=%d hex=%s\n",
                 static_cast<unsigned long>(request.tag),
                 static_cast<unsigned>(request.type_filter),
                 request.prefix_only ? 1U : 0U,
                 static_cast<unsigned long>(request.since),
                 static_cast<unsigned>(frame_len),
                 result.ok ? 1U : 0U,
                 result.ok ? static_cast<unsigned long>(effect.rx_guard_ms == 0 ? kDiscoverRxGuardMs : effect.rx_guard_ms) : 0UL,
                 result.detail,
                 toHex(frame, frame_len, frame_len).c_str());
    return result;
}

bool MeshCoreAdapter::sendIdentityAdvert(bool broadcast)
{
    return sendIdentityAdvertDetailed(broadcast, false, 0, 0).ok;
}

bool MeshCoreAdapter::sendIdentityAdvert(bool broadcast, bool include_location,
                                         int32_t lat_i6, int32_t lon_i6)
{
    return sendIdentityAdvertDetailed(broadcast, include_location, lat_i6, lon_i6).ok;
}

MeshActionResult MeshCoreAdapter::sendIdentityAdvertDetailed(bool broadcast)
{
    return sendIdentityAdvertDetailed(broadcast, false, 0, 0);
}

MeshActionResult MeshCoreAdapter::sendIdentityAdvertDetailed(bool broadcast, bool include_location,
                                                             int32_t lat_i6, int32_t lon_i6)
{
    if (!identity_.isReady())
    {
        MESHCORE_LOG("[MESHCORE] TX ADVERT dropped (identity unavailable)\n");
        return MeshActionResult::fail(MeshOperationFailure::LocalIdentityMissing);
    }

    const uint32_t now_ms = millis();
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        MESHCORE_LOG("[MESHCORE] TX ADVERT blocked reason=%s mode=%s\n",
                     txGateReasonName(tx_gate),
                     broadcast ? "broadcast" : "local");
        return MeshActionResult::fail(txGateFailure(tx_gate));
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
        return MeshActionResult::fail(MeshOperationFailure::CryptoFailed);
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
    const PayloadProfile profile =
        (config_.meshcore_send_profile == MeshCorePayloadSendProfile::V1Only)
            ? PayloadProfile::V1
            : PayloadProfile::V2;
    if (!buildFrameNoTransport(profile,
                               route_type, kPayloadTypeAdvert,
                               nullptr, 0,
                               payload, payload_len,
                               frame, sizeof(frame), &frame_len))
    {
        return MeshActionResult::fail(MeshOperationFailure::EncodeFailed);
    }

    if (isDiscoverRxGuardActive(now_ms))
    {
        const uint32_t delay_ms = discover_rx_guard_until_ms_ - now_ms;
        if (enqueueScheduled(frame, frame_len, delay_ms, true))
        {
            MESHCORE_LOG("[MESHCORE] TX ADVERT deferred reason=discover_rx_guard mode=%s remain=%lums len=%u\n",
                         broadcast ? "broadcast" : "local",
                         static_cast<unsigned long>(delay_ms),
                         static_cast<unsigned>(frame_len));
            return MeshActionResult::success();
        }
    }

    MeshActionResult result = transmitFrameNowDetailed(frame, frame_len, now_ms);
    if (!result.ok && result.failure == MeshOperationFailure::Busy)
    {
        if (enqueueScheduled(frame, frame_len, 50, true))
        {
            result = MeshActionResult::success();
        }
    }
    MESHCORE_LOG("[MESHCORE] TX ADVERT mode=%s node_type=%u name_len=%u len=%u ok=%u\n",
                 broadcast ? "broadcast" : "local",
                 static_cast<unsigned>(node_type),
                 static_cast<unsigned>(name_len),
                 static_cast<unsigned>(frame_len),
                 result.ok ? 1U : 0U);
    return result;
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
        const PayloadProfile profile =
            (config_.meshcore_send_profile == MeshCorePayloadSendProfile::V1Only)
                ? PayloadProfile::V1
                : PayloadProfile::V2;
        if (!buildFrameNoTransport(profile,
                                   kRouteTypeFlood, kPayloadTypeAdvert,
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
    const PayloadProfile profile =
        (config_.meshcore_send_profile == MeshCorePayloadSendProfile::V1Only)
            ? PayloadProfile::V1
            : PayloadProfile::V2;
    if (!buildFrameNoTransport(profile,
                               kRouteTypeFlood, kPayloadTypeAdvert,
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
    if (!isSupportedPayloadVersion(parsed.payload_ver) ||
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

bool MeshCoreAdapter::deriveVmpContactSecret(NodeId peer_id,
                                             uint8_t out_secret[32])
{
    if (!out_secret || !isPkiReady() || peer_id == 0U || peer_id == 0xFFFFFFFFUL)
    {
        return false;
    }
    for (const PeerRouteEntry& route : peer_routes_)
    {
        if (route.node_id_guess != peer_id || !route.has_pubkey ||
            !route.pubkey_verified ||
            isZeroKey(route.pubkey, sizeof(route.pubkey)))
        {
            continue;
        }

        uint8_t identity_shared_secret[MeshCoreIdentity::kPubKeySize] = {};
        const bool derived = identity_.deriveSharedSecret(route.pubkey,
                                                          identity_shared_secret) &&
                             ::chat::voice::vmp::deriveVmpContactSecret(
                                 identity_shared_secret,
                                 ::chat::voice::vmp::ContactSecretIdentityFamily::MeshCore,
                                 node_id_,
                                 peer_id,
                                 out_secret);
        volatile uint8_t* const cursor = identity_shared_secret;
        for (std::size_t index = 0U; index < sizeof(identity_shared_secret); ++index)
        {
            cursor[index] = 0U;
        }
        return derived;
    }
    return false;
}

bool MeshCoreAdapter::handleControlAppData(const MeshIncomingData& incoming,
                                           const uint8_t* payload,
                                           size_t payload_len)
{
    if (incoming.portnum == kMeshCoreNodeInfoPortnum)
    {
        return handleNodeInfoControl(incoming, payload, payload_len);
    }
    if (incoming.portnum == kKeyVerifyPortnum)
    {
        return handleKeyVerifyControl(incoming, payload, payload_len);
    }
    return false;
}

bool MeshCoreAdapter::handleNodeInfoControl(const MeshIncomingData& incoming,
                                            const uint8_t* payload,
                                            size_t payload_len)
{
    DecodedNodeInfoControl decoded{};
    if (!decodeNodeInfoControlPayload(payload, payload_len, &decoded))
    {
        return false;
    }

    runtime::IncomingPacket packet{};
    packet.protocol = MeshProtocol::MeshCore;
    packet.channel = incoming.channel;
    packet.from = incoming.from;
    packet.to = incoming.to;
    packet.packet_id = incoming.packet_id;
    packet.request_id = incoming.request_id;
    packet.portnum = incoming.portnum;
    packet.want_response = incoming.want_response;
    if (!packet.payload.assign(payload, payload_len))
    {
        MESHCORE_LOG("[MESHCORE] RX runtime drop nodeinfo payload=%u cap=%u\n",
                     static_cast<unsigned>(payload_len),
                     static_cast<unsigned>(packet.payload.capacity()));
        return false;
    }
    packet.rx_meta = incoming.rx_meta;

    runtime::FixedProtocolRuntimeContextProvider context_provider(buildRuntimeContext());
    const auto bundle = protocolRuntimeBundle(context_provider);
    if (bundle.valid())
    {
        auto facade = bundle.createFacade(protocol_effect_workspace_,
                                          runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
        facade.handleIncoming(packet);
    }
    if (decoded.type == MeshCoreNodeInfoControlType::Info && decoded.complete)
    {
        NodeId node = decoded.info.node_id;
        if (node == 0)
        {
            node = incoming.from;
        }
        if (node != 0)
        {
            rememberPeerNodeId(static_cast<uint8_t>(node & 0xFFU), node, millis());
        }
    }
    return true;
}

bool MeshCoreAdapter::handleKeyVerifyControl(const MeshIncomingData& incoming,
                                             const uint8_t* payload,
                                             size_t payload_len)
{
    if (!hasControlPrefix(payload, payload_len, kControlKindKeyVerify))
    {
        return false;
    }

    const uint32_t now_ms = millis();
    if (key_verify_session_.active &&
        static_cast<int32_t>(now_ms - key_verify_session_.started_ms) > static_cast<int32_t>(kKeyVerifySessionTtlMs))
    {
        key_verify_session_ = KeyVerifySession{};
    }

    if (payload_len < 12 || incoming.from == 0)
    {
        return true;
    }

    const uint8_t type = payload[3];
    uint64_t nonce = 0;
    memcpy(&nonce, payload + 4, sizeof(nonce));

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
    const MeshSendResult result = sendTextDetailed(channel, text, 0, peer);
    if (out_msg_id)
    {
        *out_msg_id = result.msg_id;
    }
    return result.ok;
}

MeshSendResult MeshCoreAdapter::sendTextDetailed(ChannelId channel, const std::string& text,
                                                 MessageId forced_msg_id,
                                                 NodeId peer)
{
    if (text.empty())
    {
        MESHCORE_LOG("[MESHCORE] TX text dropped (empty)\n");
        return MeshSendResult::fail(MeshOperationFailure::InvalidInput);
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
        return MeshSendResult::fail(txGateFailure(tx_gate));
    }

    if (peer != 0)
    {
        uint32_t ack_value = 0;
        const MeshActionResult sent =
            sendDirectTextDetailed(channel, text, peer, kTxtTypePlain, &ack_value, nullptr, nullptr);
        if (!sent.ok)
        {
            return MeshSendResult::fail(sent.failure, 0, sent.detail);
        }
        const MessageId msg_id = (forced_msg_id != 0) ? forced_msg_id : next_msg_id_++;
        if (ack_value != 0)
        {
            bindPendingAppAckToChatMessage(ack_value, msg_id);
        }
        else
        {
            sys::EventBus::publish(
                new sys::ChatSendResultEvent(msg_id,
                                             chat::MessageStatus::Sent,
                                             chat::MeshProtocol::MeshCore),
                0);
        }
        return MeshSendResult::success(msg_id);
    }

    uint8_t channel_key16[16];
    uint8_t channel_key32[32];
    const PayloadProfile profile =
        (config_.meshcore_send_profile == MeshCorePayloadSendProfile::V1Only)
            ? PayloadProfile::V1
            : PayloadProfile::V2;
    uint8_t channel_hash[chat::meshcore::kMeshCoreV2HashBytes] = {};
    if (!resolveGroupSecret(channel, channel_key16, channel_key32,
                            profile, channel_hash, sizeof(channel_hash)))
    {
        MESHCORE_LOG("[MESHCORE] TX text dropped (no channel secret)\n");
        return MeshSendResult::fail(MeshOperationFailure::ChannelKeyMissing);
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

    const size_t channel_hash_len = payloadHashBytes(profile);
    const size_t mac_len = payloadMacBytes(profile);
    const size_t kGroupCipherBudget =
        ((kMeshcoreMaxPayloadSize - channel_hash_len - mac_len) / kCipherBlockSize) * kCipherBlockSize;
    constexpr size_t kGroupPlainPrefix = kGroupPlainPrefixSize; // ts(4) + txt_type(1)
    const size_t kGroupTextBudget = (kGroupCipherBudget > kGroupPlainPrefix)
                                        ? (kGroupCipherBudget - kGroupPlainPrefix)
                                        : 0;
    if (decorated.size() > kGroupTextBudget)
    {
        decorated.resize(kGroupTextBudget);
    }

    uint8_t plain[kMeshcoreMaxPayloadSize];
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
                                          plain, plain_len,
                                          mac_len);
    if (encrypted_len == 0 || encrypted_len > (kMeshcoreMaxPayloadSize - channel_hash_len))
    {
        MESHCORE_LOG("[MESHCORE] TX text dropped (encrypt fail) ch=%u plain_len=%u\n",
                     static_cast<unsigned>(channel),
                     static_cast<unsigned>(plain_len));
        return MeshSendResult::fail(MeshOperationFailure::CryptoFailed);
    }

    uint8_t buffer[256];
    size_t index = 0;
    buffer[index++] = buildHeader(kRouteTypeFlood, kPayloadTypeGrpTxt, payloadVersion(profile));
    uint8_t path_descriptor = 0;
    if (!encodePathDescriptor(profile, 0, &path_descriptor))
    {
        return MeshSendResult::fail(MeshOperationFailure::EncodeFailed);
    }
    buffer[index++] = path_descriptor;
    memcpy(buffer + index, channel_hash, channel_hash_len);
    index += channel_hash_len;
    memcpy(&buffer[index], encrypted, encrypted_len);
    index += encrypted_len;

    const MeshActionResult tx = transmitFrameNowDetailed(buffer, index, now_ms);
    MESHCORE_LOG("[MESHCORE] TX raw len=%u ok=%u hex=%s\n",
                 static_cast<unsigned>(index),
                 tx.ok ? 1U : 0U,
                 toHex(buffer, index).c_str());

    if (!tx.ok)
    {
        return MeshSendResult::fail(tx.failure, 0, tx.detail);
    }

    const MessageId msg_id = (forced_msg_id != 0) ? forced_msg_id : next_msg_id_++;
    sys::EventBus::publish(
        new sys::ChatSendResultEvent(msg_id,
                                     chat::MessageStatus::Sent,
                                     chat::MeshProtocol::MeshCore),
        0);
    return MeshSendResult::success(msg_id);
}

MeshActionResult MeshCoreAdapter::sendDirectTextDetailed(ChannelId channel, const std::string& text,
                                                         NodeId peer, uint8_t txt_type,
                                                         uint32_t* out_ack, uint32_t* out_timeout,
                                                         bool* out_sent_flood)
{
    if (text.empty() || peer == 0)
    {
        return MeshActionResult::fail(MeshOperationFailure::InvalidInput);
    }

    const uint32_t now_ms = millis();
    prunePeerRoutes(now_ms);
    const TxGateReason tx_gate = checkTxGate(now_ms);
    if (tx_gate != TxGateReason::Ok)
    {
        return MeshActionResult::fail(txGateFailure(tx_gate));
    }

    const uint8_t peer_hash = static_cast<uint8_t>(peer & 0xFFU);
    rememberPeerNodeId(peer_hash, peer, now_ms);

    const PeerRouteEntry* peer_route = findPeerRouteByHash(peer_hash);
    const PayloadProfile profile = selectSendProfile(peer_route);
    uint8_t peer_hash_bytes[chat::meshcore::kMeshCoreV2HashBytes] = {};
    if (profile == PayloadProfile::V2)
    {
        if (peer_route && peer_route->has_pubkey)
        {
            copyPublicHash(profile, peer_route->pubkey, sizeof(peer_route->pubkey),
                           peer_hash_bytes, sizeof(peer_hash_bytes));
        }
    }
    else
    {
        peer_hash_bytes[0] = peer_hash;
    }
    const PeerRouteEntry* selected_route =
        (profile == PayloadProfile::V2 && peer_route && peer_route->has_pubkey)
            ? selectPeerRouteByHash(profile, peer_hash_bytes, now_ms)
            : selectPeerRouteByHash(peer_hash, now_ms);
    chat::runtime::MeshCoreDirectRouteFacts route_facts{};
    route_facts.identity_ready = identity_.isReady();
    route_facts.has_peer_route = peer_route != nullptr;
    route_facts.peer_has_public_key = peer_route && peer_route->has_pubkey;
    route_facts.has_selected_route = selected_route != nullptr;
    route_facts.requested_channel = channel;
    route_facts.selected_route_channel = selected_route ? selected_route->preferred_channel : channel;

    const auto route_decision = chat::runtime::resolveMeshCoreDirectRoutePolicy(route_facts);
    if (route_decision.status == chat::runtime::MeshCoreDirectRouteStatus::MissingPeerPublicKey)
    {
        if (route_decision.should_discover)
        {
            executeDiscoverIntentDetailed(MeshDiscoveryAction::ScanLocal);
        }
        return MeshActionResult::fail(MeshOperationFailure::PeerKeyMissing);
    }

    const bool use_direct_route =
        route_decision.route_mode == chat::runtime::MeshCoreRouteMode::Direct;
    const uint8_t route_type = use_direct_route ? kRouteTypeDirect : kRouteTypeFlood;
    const uint8_t* out_path = (use_direct_route && selected_route) ? selected_route->out_path : nullptr;
    const size_t out_path_len = (use_direct_route && selected_route) ? selected_route->out_path_len : 0;

    uint8_t peer_key16[16];
    uint8_t peer_key32[32];
    if (!deriveDirectSecret(route_decision.tx_channel, profile, peer_hash_bytes, peer_key16, peer_key32))
    {
        return MeshActionResult::fail(MeshOperationFailure::PeerKeyMissing);
    }

    constexpr size_t kDirectPlainPrefixSize = 5; // ts(4) + flags(1)
    const size_t hash_bytes = payloadHashBytes(profile);
    const size_t mac_len = payloadMacBytes(profile);
    const size_t kDirectCipherBudget =
        ((kMeshcoreMaxPayloadSize - (hash_bytes * 2U) - mac_len) / kCipherBlockSize) * kCipherBlockSize;
    const size_t kDirectTextBudget = (kDirectCipherBudget > kDirectPlainPrefixSize)
                                         ? (kDirectCipherBudget - kDirectPlainPrefixSize)
                                         : 0;

    std::string body = text;
    if (body.size() > kDirectTextBudget)
    {
        body.resize(kDirectTextBudget);
    }

    uint8_t plain[kMeshcoreMaxPayloadSize];
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
        MeshCoreSha256 sha;
        sha.update(plain, plain_len);
        sha.update(identity_.publicKey(), kMeshcorePubKeySize);
        sha.finalize(reinterpret_cast<uint8_t*>(&ack_value), sizeof(ack_value));
    }

    uint8_t payload[kMeshcoreMaxPayloadSize];
    size_t payload_len = 0;
    uint8_t self_hash_bytes[chat::meshcore::kMeshCoreV2HashBytes] = {};
    if (!selfHash(profile, self_hash_bytes, sizeof(self_hash_bytes)) ||
        !buildPeerDatagramPayload(profile, peer_hash_bytes, self_hash_bytes,
                                  peer_key16, peer_key32,
                                  plain, plain_len,
                                  payload, sizeof(payload), &payload_len))
    {
        return MeshActionResult::fail(MeshOperationFailure::EncodeFailed);
    }

    uint8_t frame[kMeshcoreMaxFrameSize];
    size_t frame_len = 0;

    if (!buildFrameNoTransport(profile,
                               route_type, kPayloadTypeTxtMsg,
                               out_path, out_path_len,
                               payload, payload_len,
                               frame, sizeof(frame), &frame_len))
    {
        return MeshActionResult::fail(MeshOperationFailure::EncodeFailed);
    }

    MeshActionResult tx = transmitFrameNowDetailed(frame, frame_len, now_ms);
    bool ok = tx.ok;
    if (!ok && enqueueScheduled(frame, frame_len, 50))
    {
        ok = true;
        tx = MeshActionResult::success();
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
                                             (route_type == kRouteTypeDirect)
                                                 ? pathHopCount(profile, out_path_len)
                                                 : 0,
                                             route_type == kRouteTypeFlood,
                                             config_.meshcore_bw_khz,
                                             config_.meshcore_sf,
                                             config_.meshcore_cr);
    }
    if (out_sent_flood)
    {
        *out_sent_flood = (route_type == kRouteTypeFlood);
    }
    return ok ? MeshActionResult::success() : tx;
}

bool MeshCoreAdapter::pollIncomingText(MeshIncomingText* out)
{
    if (!initialized_)
    {
        return false;
    }
    return receive_queue_.pop(out);
}

bool MeshCoreAdapter::enqueueIncomingText(const MeshIncomingText& metadata,
                                          const char* text,
                                          size_t text_len)
{
    ::chat::infra::IncomingQueuePushReport report{};
    if (receive_queue_.push(metadata,
                            text,
                            text_len,
                            ::chat::infra::IncomingQueuePriority::P1User,
                            &report))
    {
        if (report.dropped_existing)
        {
            MESHCORE_LOG("[MESHCORE] RX text queue pressure evicted_prio=%u depth=%u\n",
                         static_cast<unsigned>(report.dropped_priority),
                         static_cast<unsigned>(receive_queue_.size()));
        }
        return true;
    }
    MESHCORE_LOG("[MESHCORE] RX text queue drop len=%u depth=%u\n",
                 static_cast<unsigned>(text_len),
                 static_cast<unsigned>(receive_queue_.size()));
    return false;
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

        const PeerRouteEntry* peer_route = findPeerRouteByHash(peer_hash);
        const PayloadProfile profile = selectSendProfile(peer_route);
        uint8_t peer_hash_bytes[chat::meshcore::kMeshCoreV2HashBytes] = {};
        if (profile == PayloadProfile::V2)
        {
            if (peer_route && peer_route->has_pubkey)
            {
                copyPublicHash(profile, peer_route->pubkey, sizeof(peer_route->pubkey),
                               peer_hash_bytes, sizeof(peer_hash_bytes));
            }
        }
        else
        {
            peer_hash_bytes[0] = peer_hash;
        }
        const PeerRouteEntry* selected_route =
            (profile == PayloadProfile::V2 && peer_route && peer_route->has_pubkey)
                ? selectPeerRouteByHash(profile, peer_hash_bytes, now_ms)
                : selectPeerRouteByHash(peer_hash, now_ms);
        chat::runtime::MeshCoreDirectRouteFacts route_facts{};
        route_facts.identity_ready = identity_.isReady();
        route_facts.has_peer_route = peer_route != nullptr;
        route_facts.peer_has_public_key = peer_route && peer_route->has_pubkey;
        route_facts.has_selected_route = selected_route != nullptr;
        route_facts.requested_channel = channel;
        route_facts.selected_route_channel = selected_route ? selected_route->preferred_channel : channel;

        const auto route_decision = chat::runtime::resolveMeshCoreDirectRoutePolicy(route_facts);
        if (route_decision.status == chat::runtime::MeshCoreDirectRouteStatus::MissingPeerPublicKey)
        {
            MESHCORE_LOG("[MESHCORE] TX direct app-data dropped (missing peer pubkey) peer=%08lX hash=%02X port=%u -> discover\n",
                         static_cast<unsigned long>(dest),
                         static_cast<unsigned>(peer_hash),
                         static_cast<unsigned>(portnum));
            if (route_decision.should_discover)
            {
                executeDiscoverIntentDetailed(MeshDiscoveryAction::ScanLocal);
            }
            return false;
        }

        const bool use_direct_route =
            route_decision.route_mode == chat::runtime::MeshCoreRouteMode::Direct;
        const uint8_t route_type = use_direct_route ? kRouteTypeDirect : kRouteTypeFlood;
        const uint8_t* out_path = (use_direct_route && selected_route) ? selected_route->out_path : nullptr;
        const size_t out_path_len = (use_direct_route && selected_route) ? selected_route->out_path_len : 0;

        uint8_t peer_key16[16];
        uint8_t peer_key32[32];
        if (!deriveDirectSecret(route_decision.tx_channel, profile, peer_hash_bytes, peer_key16, peer_key32))
        {
            MESHCORE_LOG("[MESHCORE] TX direct app-data dropped (no peer secret) peer=%08lX port=%u\n",
                         static_cast<unsigned long>(dest),
                         static_cast<unsigned>(portnum));
            return false;
        }
        (void)peer_key16;

        ::mesh::meshcore::MeshCoreProtocolStrategy core_strategy;
        ::mesh::ProtocolBuildContext core_context{};
        core_context.local_node = ::mesh::NodeId{node_id_};
        core_context.channel_key = ::mesh::ByteView{peer_key32, sizeof(peer_key32)};
        core_context.route_type = route_type;
        core_context.route_path = ::mesh::ByteView{out_path, out_path_len};
        core_context.meshcore_payload_ver = payloadVersion(profile);
        core_context.meshcore_peer_hash =
            ::mesh::ByteView{peer_hash_bytes, payloadHashBytes(profile)};
        uint8_t self_hash_bytes[chat::meshcore::kMeshCoreV2HashBytes] = {};
        if (!selfHash(profile, self_hash_bytes, sizeof(self_hash_bytes)))
        {
            return false;
        }
        core_context.meshcore_local_hash =
            ::mesh::ByteView{self_hash_bytes, payloadHashBytes(profile)};

        ::mesh::DirectMessageCommand core_command{
            ::mesh::NodeId{dest},
            ::mesh::ByteView{payload, len},
            want_ack};
        core_command.application_port = portnum;

        ::mesh::EncodedPacket frame{};
        auto built = core_strategy.buildDirectMessage(core_context, core_command, frame);
        if (!built.ok)
        {
            MESHCORE_LOG("[MESHCORE] TX direct app-data dropped (core build fail) peer=%08lX route=%s path_len=%u port=%u code=%u\n",
                         static_cast<unsigned long>(dest),
                         (route_type == kRouteTypeDirect) ? "direct" : "flood",
                         static_cast<unsigned>(out_path_len),
                         static_cast<unsigned>(portnum),
                         static_cast<unsigned>(built.failure));
            return false;
        }

        bool ok = transmitFrameNow(frame.bytes, frame.size, now_ms);
        MESHCORE_LOG("[MESHCORE] TX direct app-data peer=%08lX hash=%02X route=%s path_len=%u port=%u len=%u ok=%u\n",
                     static_cast<unsigned long>(dest),
                     static_cast<unsigned>(peer_hash),
                     (route_type == kRouteTypeDirect) ? "direct" : "flood",
                     static_cast<unsigned>(out_path_len),
                     static_cast<unsigned>(portnum),
                     static_cast<unsigned>(frame.size),
                     ok ? 1U : 0U);
        if (ok && want_ack)
        {
            ParsedPacket built_packet{};
            if (parsePacket(frame.bytes, frame.size, &built_packet))
            {
                const uint32_t ack_sig = packetSignature(built_packet.payload_type,
                                                         built_packet.path_len,
                                                         built_packet.payload,
                                                         built_packet.payload_len);
                trackPendingAppAck(ack_sig, dest, portnum, now_ms);
            }
        }
        return ok;
    }

    uint8_t channel_key16[16];
    uint8_t channel_key32[32];
    const PayloadProfile profile =
        (config_.meshcore_send_profile == MeshCorePayloadSendProfile::V1Only)
            ? PayloadProfile::V1
            : PayloadProfile::V2;
    uint8_t channel_hash[chat::meshcore::kMeshCoreV2HashBytes] = {};
    if (!resolveGroupSecret(channel, channel_key16, channel_key32,
                            profile, channel_hash, sizeof(channel_hash)))
    {
        MESHCORE_LOG("[MESHCORE] TX group app-data dropped (no channel secret) ch=%u port=%u\n",
                     static_cast<unsigned>(channel),
                     static_cast<unsigned>(portnum));
        return false;
    }
    (void)channel_key16;

    ::mesh::meshcore::MeshCoreProtocolStrategy core_strategy;
    ::mesh::ProtocolBuildContext core_context{};
    core_context.local_node = ::mesh::NodeId{node_id_};
    core_context.channel_key = ::mesh::ByteView{channel_key32, sizeof(channel_key32)};
    core_context.channel_hash = channel_hash[0];
    core_context.meshcore_payload_ver = payloadVersion(profile);
    core_context.meshcore_channel_hash =
        ::mesh::ByteView{channel_hash, payloadHashBytes(profile)};

    ::mesh::DirectMessageCommand core_command{
        ::mesh::NodeId{},
        ::mesh::ByteView{payload, len},
        false};
    core_command.application_port = portnum;

    ::mesh::EncodedPacket frame{};
    auto built = core_strategy.buildDirectMessage(core_context, core_command, frame);
    if (!built.ok)
    {
        MESHCORE_LOG("[MESHCORE] TX group app-data dropped (core build fail) ch=%u port=%u code=%u\n",
                     static_cast<unsigned>(channel),
                     static_cast<unsigned>(portnum),
                     static_cast<unsigned>(built.failure));
        return false;
    }

    return transmitFrameNow(frame.bytes, frame.size, now_ms);
}

bool MeshCoreAdapter::pollIncomingData(MeshIncomingData* out)
{
    if (!initialized_)
    {
        return false;
    }
    return app_receive_queue_.pop(out);
}

bool MeshCoreAdapter::enqueueIncomingData(const MeshIncomingData& metadata,
                                          const uint8_t* payload,
                                          size_t payload_len)
{
    ::chat::infra::IncomingQueuePushReport report{};
    if (app_receive_queue_.push(metadata,
                                payload,
                                payload_len,
                                ::chat::infra::IncomingQueuePriority::P1User,
                                &report))
    {
        if (report.dropped_existing)
        {
            MESHCORE_LOG("[MESHCORE] RX data queue pressure evicted_prio=%u depth=%u\n",
                         static_cast<unsigned>(report.dropped_priority),
                         static_cast<unsigned>(app_receive_queue_.size()));
        }
        return true;
    }
    MESHCORE_LOG("[MESHCORE] RX data queue drop port=%u len=%u depth=%u\n",
                 static_cast<unsigned>(metadata.portnum),
                 static_cast<unsigned>(payload_len),
                 static_cast<unsigned>(app_receive_queue_.size()));
    return false;
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
    config_.meshcore_channel_slot =
        chat::normalizeMeshCoreChannelSlot(config_.meshcore_channel_slot);
    if (static_cast<uint8_t>(config_.meshcore_send_profile) >
        static_cast<uint8_t>(MeshCorePayloadSendProfile::V2Only))
    {
        config_.meshcore_send_profile = MeshCorePayloadSendProfile::AutoPreferV2;
    }
    if (static_cast<uint8_t>(config_.meshcore_forward_profile) >
        static_cast<uint8_t>(MeshCoreForwardProfile::MultibyteOnly))
    {
        config_.meshcore_forward_profile = MeshCoreForwardProfile::MultibyteOnly;
    }

    config_.meshCoreChannel(0).enabled = true;
    if (config_.meshCoreChannel(0).name[0] == '\0')
    {
        strncpy(config_.meshCoreChannel(0).name,
                "Public",
                sizeof(config_.meshCoreChannel(0).name) - 1);
        config_.meshCoreChannel(0).name[sizeof(config_.meshCoreChannel(0).name) - 1] = '\0';
    }
    for (uint8_t slot = 1; slot < chat::kMeshCoreChannelMaxCount; ++slot)
    {
        chat::MeshCoreChannelConfig& channel = config_.meshCoreChannel(slot);
        if (channel.enabled && channel.name[0] == '\0')
        {
            snprintf(channel.name, sizeof(channel.name), "Channel %u", static_cast<unsigned>(slot));
        }
    }
    config_.syncMeshCoreLegacyChannelMirror();

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

    const chat::MeshCoreChannelConfig& active_channel = config_.activeMeshCoreChannel();
    const bool active_has_key =
        !isZeroKey(active_channel.key, chat::kMeshCoreChannelKeyLen);
    const uint8_t active_hash =
        active_has_key ? computeChannelHash(active_channel.key)
                       : ((config_.meshcore_channel_slot == 0 &&
                           shouldUsePublicChannelFallback(config_))
                              ? computeChannelHash(publicGroupPsk())
                              : 0xFF);
    unsigned enabled_channels = 0;
    for (uint8_t slot = 0; slot < chat::kMeshCoreChannelMaxCount; ++slot)
    {
        if (config_.meshCoreChannel(slot).enabled)
        {
            ++enabled_channels;
        }
    }
    MESHCORE_LOG("[MESHCORE] apply cfg preset=%u freq=%.3f bw=%.3f sf=%u cr=%u(4/%u) txp=%d tx_en=%u repeat=%u flood_max=%u multi_acks=%u send_profile=%u fwd_profile=%u slot=%u ch='%s' ch_en=%u ch_key=%u ch_hash=%02X channels=%u identity[ready=%u self=%02X]\n",
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
                 static_cast<unsigned>(static_cast<uint8_t>(config_.meshcore_send_profile)),
                 static_cast<unsigned>(static_cast<uint8_t>(config_.meshcore_forward_profile)),
                 static_cast<unsigned>(config_.meshcore_channel_slot),
                 active_channel.name,
                 active_channel.enabled ? 1U : 0U,
                 active_has_key ? 1U : 0U,
                 static_cast<unsigned>(active_hash),
                 enabled_channels,
                 identity_.isReady() ? 1U : 0U,
                 static_cast<unsigned>(self_hash_));

    scheduled_tx_.clear();
    peer_routes_.clear();
    key_verify_session_ = KeyVerifySession{};
    verified_peers_.clear();
    protocol_runtime_.resetAutoDiscoverState();
    loadPeerPubKeysFromDirectory();

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
#if defined(ESP_PLATFORM) && !defined(ARDUINO)
        (void)idf_radio_pump_.restartReceive();
#endif
    }
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

bool MeshCoreAdapter::pollMqttBridgePacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    if (!out_data || max_len == 0 || mqtt_bridge_count_ == 0)
    {
        return false;
    }

    MqttBridgeFrame& frame = mqtt_bridge_queue_[mqtt_bridge_read_index_];
    if (frame.bytes_len == 0 || frame.bytes_len > max_len)
    {
        mqtt_bridge_read_index_ = (mqtt_bridge_read_index_ + 1U) % mqtt_bridge_queue_.size();
        --mqtt_bridge_count_;
        return false;
    }

    memcpy(out_data, frame.bytes.data(), frame.bytes_len);
    out_len = frame.bytes_len;
    frame.bytes_len = 0;
    mqtt_bridge_read_index_ = (mqtt_bridge_read_index_ + 1U) % mqtt_bridge_queue_.size();
    --mqtt_bridge_count_;
    return true;
}

void MeshCoreAdapter::handleMqttBridgePacket(const uint8_t* data, size_t size)
{
    MESHCORE_LOG("[MC][MQTT] downlink local rx begin bytes=%u\n",
                 static_cast<unsigned>(size));
    handleRawPacketInternal(data, size, false);
    MESHCORE_LOG("[MC][MQTT] downlink local rx complete bytes=%u\n",
                 static_cast<unsigned>(size));
}

void MeshCoreAdapter::setMqttBridgeEnabled(bool enabled)
{
    mqtt_bridge_enabled_ = enabled;
    if (!enabled)
    {
        mqtt_bridge_read_index_ = 0;
        mqtt_bridge_count_ = 0;
        for (MqttBridgeFrame& frame : mqtt_bridge_queue_)
        {
            frame.bytes_len = 0;
        }
    }
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
    if (!isSupportedPayloadVersion(parsed.payload_ver))
    {
        MESHCORE_LOG("[MESHCORE] RX drop payload ver=%u len=%u type=%u route=%u\n",
                     static_cast<unsigned>(parsed.payload_ver),
                     static_cast<unsigned>(size),
                     static_cast<unsigned>(parsed.payload_type),
                     static_cast<unsigned>(parsed.route_type));
        return;
    }
    const PayloadProfile profile = payloadProfileFromVersion(parsed.payload_ver);
    const size_t hash_bytes = payloadHashBytes(profile);
    const size_t mac_len = payloadMacBytes(profile);
    const bool trace_path = (parsed.payload_type == kPayloadTypeTrace);
    if (!trace_path && !pathIsWellFormed(profile, parsed.path_len))
    {
        MESHCORE_LOG("[MESHCORE] RX drop malformed path ver=%u path_len=%u hash_bytes=%u type=%u route=%u\n",
                     static_cast<unsigned>(parsed.payload_ver),
                     static_cast<unsigned>(parsed.path_len),
                     static_cast<unsigned>(hash_bytes),
                     static_cast<unsigned>(parsed.payload_type),
                     static_cast<unsigned>(parsed.route_type));
        return;
    }
    const size_t path_hops = trace_path ? parsed.path_len : pathHopCount(profile, parsed.path_len);

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
            if (!buildFrameNoTransport(profile,
                                       kRouteTypeDirect, payload_type,
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

    // TRACE direct packets use path[] for accumulated SNR and route hashes live in payload.
    if (is_direct_route && parsed.payload_type == kPayloadTypeTrace && parsed.payload_len >= 9)
    {
        DecodedTracePayload trace{};
        if (!decodeTracePayload(parsed.payload, parsed.payload_len, parsed.path_len, &trace))
        {
            return;
        }

        if (trace.terminal)
        {
            runtime::IncomingPacket packet{};
            packet.protocol = MeshProtocol::MeshCore;
            packet.payload_type = kPayloadTypeTrace;
            if (!packet.payload.assign(parsed.payload, parsed.payload_len))
            {
                MESHCORE_LOG("[MESHCORE] RX runtime drop type=%u payload=%u cap=%u\n",
                             static_cast<unsigned>(parsed.payload_type),
                             static_cast<unsigned>(parsed.payload_len),
                             static_cast<unsigned>(packet.payload.capacity()));
                return;
            }
            if (parsed.path_len > 0)
            {
                if (!packet.path.assign(parsed.path, parsed.path_len))
                {
                    MESHCORE_LOG("[MESHCORE] RX runtime drop type=%u path=%u cap=%u\n",
                                 static_cast<unsigned>(parsed.payload_type),
                                 static_cast<unsigned>(parsed.path_len),
                                 static_cast<unsigned>(packet.path.capacity()));
                    return;
                }
            }
            runtime::FixedProtocolRuntimeContextProvider context_provider(buildRuntimeContext());
            const auto bundle = protocolRuntimeBundle(context_provider);
            if (bundle.valid())
            {
                auto facade = bundle.createFacade(
                    protocol_effect_workspace_,
                    runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
                facade.handleIncoming(packet);
            }

            MESHCORE_LOG("[MESHCORE] RX TRACE done tag=%08lX auth=%08lX hops=%u route=%s\n",
                         static_cast<unsigned long>(trace.tag),
                         static_cast<unsigned long>(trace.auth),
                         static_cast<unsigned>(parsed.path_len),
                         is_flood_route ? "flood" : "direct");
            return;
        }

        if (trace.next_hash != self_hash_)
        {
            return;
        }
        if (!config_.meshcore_client_repeat)
        {
            return;
        }
        if (parsed.path_hash_bytes != chat::meshcore::kMeshCoreV1HashBytes ||
            parsed.path_len >= kMeshcoreMaxPathSize || (size + 1) > kMeshcoreMaxFrameSize)
        {
            return;
        }

        const size_t payload_start = static_cast<size_t>(parsed.payload - data);
        uint8_t next_path_descriptor = 0;
        if (!encodePathDescriptor(PayloadProfile::V1,
                                  parsed.path_len + 1,
                                  &next_path_descriptor))
        {
            return;
        }
        std::vector<uint8_t> fwd(size + 1);
        memcpy(fwd.data(), data, parsed.path_len_index);
        fwd[parsed.path_len_index] = next_path_descriptor;
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

    // Upstream discover control is a zero-hop control exchange. Some compat
    // peers flood the same high-bit control subset, so consume either direct
    // or flood zero-hop packets before generic routing/data paths.
    if ((is_direct_route || is_flood_route) &&
        parsed.payload_type == kPayloadTypeControl &&
        parsed.payload_len > 0 &&
        (parsed.payload[0] & 0x80U) != 0)
    {
        if (parsed.path_len == 0)
        {
            runtime::IncomingPacket packet{};
            packet.protocol = MeshProtocol::MeshCore;
            packet.payload_type = kPayloadTypeControl;
            if (!packet.payload.assign(parsed.payload, parsed.payload_len))
            {
                MESHCORE_LOG("[MESHCORE] RX runtime drop type=%u payload=%u cap=%u\n",
                             static_cast<unsigned>(parsed.payload_type),
                             static_cast<unsigned>(parsed.payload_len),
                             static_cast<unsigned>(packet.payload.capacity()));
                return;
            }
            if (std::isfinite(last_rx_snr_))
            {
                packet.rx_meta.snr_db_x10 = static_cast<int16_t>(std::lround(last_rx_snr_ * 10.0f));
            }
            if (std::isfinite(last_rx_rssi_))
            {
                packet.rx_meta.rssi_dbm_x10 = static_cast<int16_t>(std::lround(last_rx_rssi_ * 10.0f));
            }

            runtime::FixedProtocolRuntimeContextProvider context_provider(buildRuntimeContext());
            const auto bundle = protocolRuntimeBundle(context_provider);
            if (bundle.valid())
            {
                auto facade = bundle.createFacade(
                    protocol_effect_workspace_,
                    runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
                facade.handleIncoming(packet);
            }

            DecodedDiscoverRequest discover_request{};
            DecodedDiscoverResponse discover_response{};
            const bool known_discover_control =
                decodeDiscoverRequest(parsed.payload, parsed.payload_len, &discover_request) ||
                decodeDiscoverResponse(parsed.payload, parsed.payload_len, &discover_response);
            if (!known_discover_control)
            {
                MESHCORE_LOG("[MESHCORE] RX DISCOVER_CTRL unknown route=%u path=%u len=%u first=%02X\n",
                             static_cast<unsigned>(parsed.route_type),
                             static_cast<unsigned>(parsed.path_len),
                             static_cast<unsigned>(parsed.payload_len),
                             parsed.payload_len > 0 ? static_cast<unsigned>(parsed.payload[0]) : 0U);
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
                if (ev.payload.assign(parsed.payload, parsed.payload_len))
                {
                    pushEvent(std::move(ev));
                }
            }
        }
        return;
    }

    uint8_t self_hash_bytes[chat::meshcore::kMeshCoreV2HashBytes] = {};
    const bool have_self_hash = selfHash(profile, self_hash_bytes, sizeof(self_hash_bytes));

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

        if (!have_self_hash ||
            parsed.path_len < hash_bytes ||
            memcmp(parsed.path, self_hash_bytes, hash_bytes) != 0)
        {
            return;
        }

        if (!config_.meshcore_client_repeat || !allowsForwardingProfile(profile) || size <= 2)
        {
            return;
        }

        const size_t payload_start = static_cast<size_t>(parsed.payload - data);
        const size_t new_path_len = parsed.path_len - hash_bytes;

        auto copyForwardPath = [&](uint8_t* out_path) -> void
        {
            if (!out_path || new_path_len == 0)
            {
                return;
            }
            memcpy(out_path, parsed.path + hash_bytes, new_path_len);
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

        uint8_t next_path_descriptor = 0;
        if (!encodePathDescriptor(profile, new_path_len, &next_path_descriptor))
        {
            return;
        }
        std::vector<uint8_t> fwd(size - hash_bytes);
        memcpy(fwd.data(), data, parsed.path_len_index);
        fwd[parsed.path_len_index] = next_path_descriptor;
        if (new_path_len > 0)
        {
            memcpy(&fwd[parsed.path_len_index + 1], parsed.path + hash_bytes, new_path_len);
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
                                     isAnonReqCipherShape(profile, parsed.payload_len) &&
                                     !isPeerCipherShape(profile, parsed.payload_len);
    const bool is_peer_payload_candidate = isPeerPayloadType(parsed.payload_type) &&
                                           isPeerCipherShape(profile, parsed.payload_len) &&
                                           !is_anon_req_payload;
    const bool flood_peer_for_self = is_flood_route &&
                                     is_peer_payload_candidate &&
                                     parsed.payload_len >= hash_bytes &&
                                     have_self_hash &&
                                     memcmp(parsed.payload, self_hash_bytes, hash_bytes) == 0;
    const bool flood_anon_for_self = is_flood_route &&
                                     is_anon_req_payload &&
                                     parsed.payload_len >= hash_bytes &&
                                     have_self_hash &&
                                     memcmp(parsed.payload, self_hash_bytes, hash_bytes) == 0;
    const bool is_multipart_ack = (parsed.payload_type == kPayloadTypeMultipart &&
                                   parsed.payload_len > 0 &&
                                   ((parsed.payload[0] & 0x0F) == kPayloadTypeAck));
    if (config_.meshcore_client_repeat && is_flood_route &&
        allowsForwardingProfile(profile) &&
        !flood_peer_for_self &&
        !flood_anon_for_self &&
        !is_multipart_ack &&
        shouldFloodRepeatPayload(parsed.payload_type) &&
        path_hops < config_.meshcore_flood_max &&
        (parsed.path_len + hash_bytes) <= kMeshcoreMaxPathSize &&
        (size + hash_bytes) <= kMeshcoreMaxFrameSize)
    {
        bool self_in_path = false;
        for (size_t i = 0; i + hash_bytes <= parsed.path_len; i += hash_bytes)
        {
            if (memcmp(parsed.path + i, self_hash_bytes, hash_bytes) == 0)
            {
                self_in_path = true;
                break;
            }
        }

        if (!self_in_path)
        {
            const size_t path_start = parsed.path_len_index + 1;
            const size_t payload_start = static_cast<size_t>(parsed.payload - data);

            uint8_t next_path_descriptor = 0;
            if (!encodePathDescriptor(profile,
                                      parsed.path_len + hash_bytes,
                                      &next_path_descriptor))
            {
                return;
            }
            std::vector<uint8_t> fwd(size + hash_bytes);
            memcpy(fwd.data(), data, path_start + parsed.path_len);
            fwd[parsed.path_len_index] = next_path_descriptor;
            memcpy(&fwd[path_start + parsed.path_len], self_hash_bytes, hash_bytes);
            memcpy(&fwd[path_start + parsed.path_len + hash_bytes],
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
            const uint32_t tx_delay =
                ::platform::esp::common::meshcore_runtime::random_between(0, 6) * tx_step;
            const uint32_t total_delay = rx_delay + tx_delay;
            enqueueScheduled(fwd.data(), fwd.size(), total_delay);

            MESHCORE_LOG("[MESHCORE] REPEAT queued path=%u->%u flood_max=%u delay=%lu\n",
                         static_cast<unsigned>(path_hops),
                         static_cast<unsigned>(path_hops + 1),
                         static_cast<unsigned>(config_.meshcore_flood_max),
                         static_cast<unsigned long>(total_delay));
        }
    }

    const bool is_peer_payload = isPeerPayloadType(parsed.payload_type) &&
                                 isPeerCipherShape(profile, parsed.payload_len) &&
                                 !is_anon_req_payload;
    const bool is_group_text_payload = (parsed.payload_type == kPayloadTypeGrpTxt &&
                                        parsed.payload_len > (hash_bytes + mac_len));
    const bool is_group_data_payload = (parsed.payload_type == kPayloadTypeGrpData &&
                                        parsed.payload_len > (hash_bytes + mac_len));
    const bool is_raw_payload = (parsed.payload_type == kPayloadTypeRawCustom &&
                                 parsed.payload_len > sizeof(uint32_t));

    if (config_.tx_enabled && is_raw_payload)
    {
        uint8_t ack_frame[6] = {};
        ack_frame[0] = buildHeader(kRouteTypeFlood, kPayloadTypeAck, payloadVersion(profile));
        ack_frame[1] = 0;
        memcpy(&ack_frame[2], &frame_sig, sizeof(frame_sig));

        if (config_.meshcore_multi_acks)
        {
            uint8_t multi_ack[7] = {};
            multi_ack[0] = buildHeader(kRouteTypeFlood, kPayloadTypeMultipart, payloadVersion(profile));
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
        meta.hop_count = (path_hops <= 255) ? static_cast<uint8_t>(path_hops) : 0xFF;
        meta.hop_limit = 0xFF;
        meta.wire_flags = data[0];
        if (parsed.path_len > 0)
        {
            meta.next_hop = parsed.path[0];
            const size_t relay_offset = !trace_path && parsed.path_len >= hash_bytes
                                            ? parsed.path_len - hash_bytes
                                            : parsed.path_len - 1;
            meta.relay_node = parsed.path[relay_offset];
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

    auto sendPeerDatagram = [&](uint8_t payload_type, const uint8_t* dest_hash, ChannelId channel,
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
        if (!deriveDirectSecret(channel, profile, dest_hash, key16, key32))
        {
            return false;
        }

        uint8_t payload[kMeshcoreMaxPayloadSize];
        size_t payload_len = 0;
        if (!have_self_hash ||
            !buildPeerDatagramPayload(profile, dest_hash, self_hash_bytes,
                                      key16, key32,
                                      plain, plain_len,
                                      payload, sizeof(payload), &payload_len))
        {
            return false;
        }

        uint8_t frame[kMeshcoreMaxFrameSize];
        size_t frame_len = 0;
        if (!buildFrameNoTransport(profile,
                                   route_type, payload_type,
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

    auto sendPathReturn = [&](const uint8_t* dest_hash, ChannelId channel,
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

    auto sendPeerAck = [&](const uint8_t* src_hash, ChannelId channel, uint32_t ack_value) -> void
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

        const PeerRouteEntry* route = selectPeerRouteByHash(profile, src_hash, now_ms);
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
        const uint8_t* dest_hash = parsed.payload;
        if (!have_self_hash || memcmp(dest_hash, self_hash_bytes, hash_bytes) != 0)
        {
            return;
        }

        if (!identity_.isReady())
        {
            MESHCORE_LOG("[MESHCORE] RX ANON_REQ drop (identity unavailable) len=%u\n",
                         static_cast<unsigned>(parsed.payload_len));
            return;
        }

        const uint8_t* sender_pubkey = parsed.payload + hash_bytes;
        uint8_t src_hash[chat::meshcore::kMeshCoreV2HashBytes] = {};
        copyPublicHash(profile, sender_pubkey, kMeshcorePubKeySize, src_hash, sizeof(src_hash));
        const uint8_t* cipher = parsed.payload + hash_bytes + kMeshcorePubKeySize;
        const size_t cipher_len = parsed.payload_len - hash_bytes - kMeshcorePubKeySize;

        uint8_t shared_secret[kCipherHmacKeySize] = {};
        if (!identity_.deriveSharedSecret(sender_pubkey, shared_secret))
        {
            MESHCORE_LOG("[MESHCORE] RX ANON_REQ drop (shared secret failed) src=%02X\n",
                         static_cast<unsigned>(src_hash[0]));
            return;
        }

        uint8_t key16[kCipherKeySize] = {};
        uint8_t key32[kCipherHmacKeySize] = {};
        if (!chat::runtime::MeshCoreDirectSecretCore::expandSharedSecret(shared_secret, sizeof(shared_secret),
                                                                         key16, sizeof(key16),
                                                                         key32, sizeof(key32)))
        {
            MESHCORE_LOG("[MESHCORE] RX ANON_REQ drop (shared secret expand failed) src=%02X\n",
                         static_cast<unsigned>(src_hash[0]));
            return;
        }

        uint8_t plain[kMeshcoreMaxPayloadSize] = {};
        size_t plain_len = 0;
        if (!macThenDecrypt(key16, key32, cipher, cipher_len, plain, &plain_len, mac_len))
        {
            MESHCORE_LOG("[MESHCORE] RX ANON_REQ decrypt failed src=%02X len=%u\n",
                         static_cast<unsigned>(src_hash[0]),
                         static_cast<unsigned>(parsed.payload_len));
            return;
        }
        plain_len = trimTrailingZeros(plain, plain_len);

        const NodeId sender_node = deriveNodeIdFromPubkey(sender_pubkey, kMeshcorePubKeySize);
        rememberPeerPubKey(sender_pubkey, now_ms, true);
        rememberPeerNodeId(src_hash[0], sender_node, now_ms);

        uint32_t tag = 0;
        if (plain_len >= sizeof(tag))
        {
            memcpy(&tag, plain, sizeof(tag));
        }
        MESHCORE_LOG("[MESHCORE] RX ANON_REQ src=%02X node=%08lX len=%u route=%s tag=%08lX\n",
                     static_cast<unsigned>(src_hash[0]),
                     static_cast<unsigned long>(sender_node),
                     static_cast<unsigned>(plain_len),
                     is_flood_route ? "flood" : "direct",
                     static_cast<unsigned long>(tag));
        return;
    }

    if (is_peer_payload)
    {
        const uint8_t* dest_hash = parsed.payload;
        const uint8_t* src_hash = parsed.payload + hash_bytes;
        if (!have_self_hash || memcmp(dest_hash, self_hash_bytes, hash_bytes) != 0)
        {
            return;
        }

        uint8_t plain[kMeshcoreMaxPayloadSize];
        size_t plain_len = 0;
        ChannelId peer_channel = ChannelId::PRIMARY;
        if (!tryDecryptPeerPayload(profile, src_hash,
                                   parsed.payload + (hash_bytes * 2U),
                                   parsed.payload_len - (hash_bytes * 2U),
                                   plain, &plain_len, &peer_channel))
        {
            const PeerRouteEntry* route = findPeerRouteByHash(profile, src_hash);
            const bool has_pubkey = (route && route->has_pubkey);
            MESHCORE_LOG("[MESHCORE] RX peer decrypt failed type=%u src=%02X has_pubkey=%u\n",
                         static_cast<unsigned>(parsed.payload_type),
                         static_cast<unsigned>(src_hash[0]),
                         has_pubkey ? 1U : 0U);
            if (!has_pubkey)
            {
                maybeAutoDiscoverMissingPeer(src_hash[0], now_ms);
            }
            return;
        }
        plain_len = chat::meshcore::trimPeerPlaintextPadding(parsed.payload_type, plain, plain_len);
        // Do not infer zero-hop route from direct packets that already traversed a path.
        // Route candidates should be learned from PATH payloads (or flood-derived returns).
        PeerRouteEntry& route = upsertPeerRoute(profile, src_hash, now_ms);
        route.preferred_channel = peer_channel;

        const NodeId from_node = resolvePeerNodeId(src_hash[0]);

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
                incoming.hop_limit = 0;
                incoming.encrypted = true;
                fill_rx_meta(incoming.rx_meta, is_direct_route);
                enqueueIncomingText(incoming,
                                    reinterpret_cast<const char*>(plain + 5),
                                    plain_len - 5);

                uint32_t ack_value = packet_sig;
                uint8_t sender_pubkey[kMeshcorePubKeySize] = {};
                if (lookupPeerPubKey(profile, src_hash, sender_pubkey))
                {
                    size_t text_len = 0;
                    const size_t text_cap = plain_len - 5;
                    while (text_len < text_cap && plain[5 + text_len] != '\0')
                    {
                        ++text_len;
                    }

                    MeshCoreSha256 sha;
                    sha.update(plain, 5 + text_len);
                    sha.update(sender_pubkey, kMeshcorePubKeySize);
                    sha.finalize(reinterpret_cast<uint8_t*>(&ack_value), sizeof(ack_value));
                }
                else
                {
                    MESHCORE_LOG("[MESHCORE] RX text ACK fallback src=%02X reason=no_pubkey\n",
                                 static_cast<unsigned>(src_hash[0]));
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
                incoming.hop_limit = 0;
                incoming.encrypted = true;
                fill_rx_meta(incoming.rx_meta, is_direct_route);
                enqueueIncomingText(incoming,
                                    reinterpret_cast<const char*>(plain + 9),
                                    plain_len - 9);

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

                    MeshCoreSha256 sha;
                    sha.update(plain, 9 + text_len);
                    sha.update(self_pubkey, kMeshcorePubKeySize);
                    sha.finalize(reinterpret_cast<uint8_t*>(&ack_value), sizeof(ack_value));
                }
                else
                {
                    MESHCORE_LOG("[MESHCORE] RX signed text ACK fallback src=%02X reason=no_identity\n",
                                 static_cast<unsigned>(src_hash[0]));
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
            fill_rx_meta(incoming.rx_meta, is_direct_route);
            if (decoded.want_ack)
            {
                sendPeerAck(src_hash, peer_channel, packet_sig);
            }

            if (!handleControlAppData(incoming, decoded.payload, decoded.payload_len))
            {
                enqueueIncomingData(incoming, decoded.payload, decoded.payload_len);
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
                const PeerRouteEntry* route = selectPeerRouteByHash(profile, src_hash, now_ms);
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
                         static_cast<unsigned>(src_hash[0]),
                         static_cast<unsigned>(plain_len),
                         is_flood_route ? "flood" : "direct");
            Event ev{};
            ev.type = Event::Type::Response;
            ev.peer_hash = src_hash[0];
            ev.peer_node = from_node;
            if (ev.payload.assign(plain, plain_len))
            {
                pushEvent(std::move(ev));
            }
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

                rememberPeerPath(profile, src_hash, out_path, out_path_len, peer_channel, now_ms);
                MESHCORE_LOG("[MESHCORE] RX PATH src=%02X out_path_len=%u extra_type=%u extra_len=%u route=%s\n",
                             static_cast<unsigned>(src_hash[0]),
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
                                 static_cast<unsigned>(src_hash[0]),
                                 static_cast<unsigned>(extra_len));
                    Event ev{};
                    ev.type = Event::Type::PathResponse;
                    ev.peer_hash = src_hash[0];
                    ev.peer_node = from_node;
                    if (extra_len >= sizeof(uint32_t))
                    {
                        memcpy(&ev.tag, extra, sizeof(uint32_t));
                    }
                    const bool payload_ok = ev.payload.assign(extra, extra_len);
                    const bool in_path_ok = ev.in_path.assign(parsed.path, parsed.path_len);
                    bool out_path_ok = true;
                    if (out_path_len > 0)
                    {
                        out_path_ok = ev.out_path.assign(out_path, out_path_len);
                    }
                    if (payload_ok && in_path_ok && out_path_ok)
                    {
                        pushEvent(std::move(ev));
                    }
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

    auto logUnknownGroupHash = [&](const char* kind, const uint8_t* channel_hash) -> void
    {
        const chat::MeshCoreChannelConfig& active_channel = config_.activeMeshCoreChannel();
        const bool active_has_key =
            !isZeroKey(active_channel.key, chat::kMeshCoreChannelKeyLen);
        const uint8_t active_hash =
            active_has_key ? computeChannelHash(active_channel.key)
                           : ((config_.meshcore_channel_slot == 0 &&
                               shouldUsePublicChannelFallback(config_))
                                  ? computeChannelHash(publicGroupPsk())
                                  : 0xFF);
        MESHCORE_LOG("[MESHCORE] RX group %s drop unknown hash=%s local[slot=%u hash=%02X enabled=%u]\n",
                     kind,
                     toHex(channel_hash, hash_bytes).c_str(),
                     static_cast<unsigned>(config_.meshcore_channel_slot),
                     static_cast<unsigned>(active_hash),
                     active_channel.enabled ? 1U : 0U);
    };

    if (is_group_text_payload)
    {
        const uint8_t* channel_hash = parsed.payload;
        bool channel_match = false;
        const ChannelId rx_channel = resolveChannelFromHash(profile, channel_hash, &channel_match);
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
                            parsed.payload + hash_bytes, parsed.payload_len - hash_bytes,
                            plain, &plain_len, mac_len))
        {
            MESHCORE_LOG("[MESHCORE] RX group text decrypt fail hash=%s len=%u ch=%u\n",
                         toHex(channel_hash, hash_bytes).c_str(),
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
        incoming.hop_limit = 0;
        incoming.encrypted = true;
        fill_rx_meta(incoming.rx_meta, false);
        incoming.rx_meta.channel_hash = channel_hash[0];
        if (incoming.from != 0)
        {
            rememberPeerNodeId(static_cast<uint8_t>(incoming.from & 0xFFU), incoming.from, now_ms);
        }
        enqueueIncomingText(incoming,
                            reinterpret_cast<const char*>(plain + text_offset),
                            plain_len - text_offset);
    }
    else if (is_group_data_payload)
    {
        const uint8_t* channel_hash = parsed.payload;
        bool channel_match = false;
        const ChannelId rx_channel = resolveChannelFromHash(profile, channel_hash, &channel_match);
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
                            parsed.payload + hash_bytes, parsed.payload_len - hash_bytes,
                            plain, &plain_len, mac_len))
        {
            MESHCORE_LOG("[MESHCORE] RX group data decrypt fail hash=%s len=%u ch=%u\n",
                         toHex(channel_hash, hash_bytes).c_str(),
                         static_cast<unsigned>(parsed.payload_len),
                         static_cast<unsigned>(rx_channel));
            return;
        }
        plain_len = chat::meshcore::trimGroupPlaintextPadding(plain, plain_len);
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
        incoming.channel_hash = channel_hash[0];
        incoming.want_response = false;
        fill_rx_meta(incoming.rx_meta, false);
        incoming.rx_meta.channel_hash = channel_hash[0];
        if (incoming.from != 0)
        {
            rememberPeerNodeId(static_cast<uint8_t>(incoming.from & 0xFFU), incoming.from, now_ms);
        }
        if (!handleControlAppData(incoming, decoded.payload, decoded.payload_len))
        {
            enqueueIncomingData(incoming, decoded.payload, decoded.payload_len);
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

        const uint8_t hops = (path_hops <= 255) ? static_cast<uint8_t>(path_hops) : 0xFF;
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
        ev.path_profile = profile;
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
        if (ev.payload.assign(parsed.payload, parsed.payload_len))
        {
            pushEvent(std::move(ev));
        }

        MeshIncomingData incoming;
        memcpy(&incoming.portnum, parsed.payload, sizeof(uint32_t));
        incoming.from = 0;
        incoming.to = 0xFFFFFFFF;
        incoming.packet_id = next_msg_id_++;
        incoming.channel = ChannelId::PRIMARY;
        incoming.channel_hash = 0;
        incoming.want_response = false;
        const size_t app_len = parsed.payload_len - sizeof(uint32_t);
        if (app_len > incoming_data_scratch_.size())
        {
            MESHCORE_LOG("[MESHCORE] RX raw app-data drop len=%u cap=%u\n",
                         static_cast<unsigned>(app_len),
                         static_cast<unsigned>(incoming_data_scratch_.size()));
            return;
        }
        memcpy(incoming_data_scratch_.data(), parsed.payload + sizeof(uint32_t), app_len);
        if (encrypt_mode_ > 0)
        {
            size_t key_len = 0;
            const uint8_t* key = selectChannelKey(config_, &key_len);
            xorCrypt(incoming_data_scratch_.data(), app_len, key, key_len);
        }
        fill_rx_meta(incoming.rx_meta, false);
        enqueueIncomingData(incoming, incoming_data_scratch_.data(), app_len);
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
        DecodedTracePayload trace{};
        if (decodeTracePayload(parsed.payload, parsed.payload_len, parsed.path_len, &trace) &&
            trace.terminal)
        {
            runtime::IncomingPacket packet{};
            packet.protocol = MeshProtocol::MeshCore;
            packet.payload_type = kPayloadTypeTrace;
            if (!packet.payload.assign(parsed.payload, parsed.payload_len))
            {
                MESHCORE_LOG("[MESHCORE] RX runtime drop type=%u payload=%u cap=%u\n",
                             static_cast<unsigned>(parsed.payload_type),
                             static_cast<unsigned>(parsed.payload_len),
                             static_cast<unsigned>(packet.payload.capacity()));
                return;
            }
            if (parsed.path_len > 0)
            {
                if (!packet.path.assign(parsed.path, parsed.path_len))
                {
                    MESHCORE_LOG("[MESHCORE] RX runtime drop type=%u path=%u cap=%u\n",
                                 static_cast<unsigned>(parsed.payload_type),
                                 static_cast<unsigned>(parsed.path_len),
                                 static_cast<unsigned>(packet.path.capacity()));
                    return;
                }
            }
            runtime::FixedProtocolRuntimeContextProvider context_provider(buildRuntimeContext());
            const auto bundle = protocolRuntimeBundle(context_provider);
            if (bundle.valid())
            {
                auto facade = bundle.createFacade(
                    protocol_effect_workspace_,
                    runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
                facade.handleIncoming(packet);
            }

            MESHCORE_LOG("[MESHCORE] RX TRACE done tag=%08lX auth=%08lX hops=%u route=%s\n",
                         static_cast<unsigned long>(trace.tag),
                         static_cast<unsigned long>(trace.auth),
                         static_cast<unsigned>(parsed.path_len),
                         is_flood_route ? "flood" : "direct");
            Event ev{};
            ev.type = Event::Type::TraceData;
            ev.tag = trace.tag;
            ev.auth = trace.auth;
            ev.flags = trace.flags;
            bool trace_ok = ev.trace_hashes.assign(trace.trace_hashes,
                                                   trace.trace_hashes_len);
            if (parsed.path_len > 0)
            {
                trace_ok = ev.trace_snrs.assign(parsed.path, parsed.path_len) && trace_ok;
            }
            if (std::isfinite(last_rx_snr_))
            {
                ev.snr_qdb = static_cast<int8_t>(std::lround(last_rx_snr_ * 4.0f));
            }
            if (trace_ok)
            {
                pushEvent(std::move(ev));
            }
        }
    }
}

#if defined(ESP_PLATFORM) && !defined(ARDUINO)
void MeshCoreAdapter::pollIdfRadio()
{
    if (!initialized_ || !board_.isRadioOnline())
    {
        return;
    }

    ::platform::esp::radio::IdfLoraRadioFrame frame{};
    const auto result = idf_radio_pump_.poll(frame);
    if (result == ::platform::esp::radio::IdfLoraPollResult::Frame)
    {
        setLastRxStats(frame.rssi, frame.snr);
        MESHCORE_LOG("[MESHCORE] IDF RX raw len=%u rssi=%.1f snr=%.1f irq=0x%04lX\n",
                     static_cast<unsigned>(frame.len),
                     static_cast<double>(frame.rssi),
                     static_cast<double>(frame.snr),
                     static_cast<unsigned long>(frame.irq));
        handleRawPacket(frame.data, frame.len);
        return;
    }

    if (result == ::platform::esp::radio::IdfLoraPollResult::InvalidPacketLength)
    {
        MESHCORE_LOG("[MESHCORE] IDF RX invalid length=%d irq=0x%04lX\n",
                     frame.packet_length,
                     static_cast<unsigned long>(frame.irq));
    }
    else if (result == ::platform::esp::radio::IdfLoraPollResult::ReadFailed)
    {
        MESHCORE_LOG("[MESHCORE] IDF RX read failed length=%d irq=0x%04lX\n",
                     frame.packet_length,
                     static_cast<unsigned long>(frame.irq));
    }
}
#endif

void MeshCoreAdapter::processSendQueue()
{
#if defined(ESP_PLATFORM) && !defined(ARDUINO)
    pollIdfRadio();
#endif
    uint32_t now_ms = millis();
    prunePendingAppAcks(now_ms);
    prunePeerRoutes(now_ms);
    runtime::FixedProtocolRuntimeContextProvider context_provider(buildRuntimeContext());
    const auto bundle = protocolRuntimeBundle(context_provider);
    if (bundle.valid())
    {
        auto facade = bundle.createFacade(protocol_effect_workspace_,
                                          runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
        facade.tick();
    }

    if (scheduled_tx_.empty())
    {
        return;
    }

    for (size_t i = 0; i < scheduled_tx_.size();)
    {
        ScheduledFrame* scheduled = scheduled_tx_.at(i);
        if (!scheduled)
        {
            break;
        }
        ScheduledFrame& frame = *scheduled;
        if (static_cast<int32_t>(now_ms - frame.due_ms) < 0)
        {
            ++i;
            continue;
        }
        if (frame.defer_during_discover && isDiscoverRxGuardActive(now_ms))
        {
            frame.due_ms = now_ms + 100;
            MESHCORE_LOG("[MESHCORE] TX scheduled deferred reason=discover_rx_guard remain=%lums len=%u\n",
                         static_cast<unsigned long>(discover_rx_guard_until_ms_ - now_ms),
                         static_cast<unsigned>(frame.bytes_len));
            ++i;
            continue;
        }

        if (transmitFrameNow(frame.bytes.data(), frame.bytes_len, now_ms))
        {
            scheduled_tx_.removeAt(i);
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
        info.peer_hash_len = entry.peer_hash_len;
        memcpy(info.peer_hash_bytes, entry.peer_hash_bytes, sizeof(info.peer_hash_bytes));
        info.node_id = entry.node_id_guess;
        info.has_pubkey = entry.has_pubkey;
        info.pubkey_verified = entry.pubkey_verified;
        if (entry.has_pubkey)
        {
            memcpy(info.pubkey, entry.pubkey, sizeof(info.pubkey));
        }
        info.out_path_profile = entry.out_path_profile;
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
    info.peer_hash_len = entry->peer_hash_len;
    memcpy(info.peer_hash_bytes, entry->peer_hash_bytes, sizeof(info.peer_hash_bytes));
    info.node_id = entry->node_id_guess;
    info.has_pubkey = entry->has_pubkey;
    info.pubkey_verified = entry->pubkey_verified;
    if (entry->has_pubkey)
    {
        memcpy(info.pubkey, entry->pubkey, sizeof(info.pubkey));
    }
    info.out_path_profile = entry->out_path_profile;
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
            info.peer_hash_len = entry.peer_hash_len;
            memcpy(info.peer_hash_bytes, entry.peer_hash_bytes, sizeof(info.peer_hash_bytes));
            info.node_id = entry.node_id_guess;
            info.has_pubkey = entry.has_pubkey;
            info.pubkey_verified = entry.pubkey_verified;
            if (entry.has_pubkey)
            {
                memcpy(info.pubkey, entry.pubkey, sizeof(info.pubkey));
            }
            info.out_path_profile = entry.out_path_profile;
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
    entry->out_path_profile = PayloadProfile::V1;
    entry->has_out_path = false;
    memset(entry->out_path, 0, sizeof(entry->out_path));
    Event ev{};
    ev.type = Event::Type::PathUpdated;
    ev.peer_hash = peer_hash;
    ev.peer_node = resolvePeerNodeId(peer_hash);
    ev.path_profile = PayloadProfile::V1;
    pushEvent(std::move(ev));
    return true;
}

} // namespace meshcore
} // namespace chat
