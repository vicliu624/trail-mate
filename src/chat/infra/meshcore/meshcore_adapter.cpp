/**
 * @file meshcore_adapter.cpp
 * @brief MeshCore protocol adapter implementation
 */

#include "meshcore_adapter.h"
#include "../../time_utils.h"
#include <AES.h>
#include <Arduino.h>
#include <RadioLib.h>
#include <SHA256.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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
constexpr uint8_t kPayloadTypeReq = 0x00;
constexpr uint8_t kPayloadTypeResponse = 0x01;
constexpr uint8_t kPayloadTypeRawCustom = 0x0F;
constexpr uint8_t kPayloadTypeTxtMsg = 0x02;
constexpr uint8_t kPayloadTypeGrpTxt = 0x05;
constexpr uint8_t kPayloadTypeGrpData = 0x06;
constexpr uint8_t kPayloadTypeAck = 0x03;
constexpr uint8_t kPayloadTypePath = 0x08;
constexpr uint8_t kPayloadTypeTrace = 0x09;
constexpr uint8_t kPayloadTypeMultipart = 0x0A;
constexpr uint8_t kPayloadVer1 = 0x00;
constexpr size_t kMeshcorePathHashSize = 1;
constexpr size_t kMeshcoreMaxPathSize = 64;
constexpr size_t kMeshcoreMaxFrameSize = 255;
constexpr size_t kMeshcoreMaxPayloadSize = 184;
constexpr size_t kMaxScheduledFrames = 24;
constexpr size_t kMaxSeenPackets = 128;
constexpr uint32_t kSeenTtlMs = 60000;
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

constexpr uint8_t kPublicGroupPsk[16] = {
    0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
    0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};

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
#define MESHCORE_LOG_ENABLE 1
#endif

#if MESHCORE_LOG_ENABLE
#define MESHCORE_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define MESHCORE_LOG(...) \
    do                    \
    {                     \
    } while (0)
#endif

std::string toHex(const uint8_t* data, size_t len, size_t max_len = 128)
{
    if (!data || len == 0)
    {
        return {};
    }
    size_t capped = (len > max_len) ? max_len : len;
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(capped * 2 + 2);
    for (size_t i = 0; i < capped; ++i)
    {
        uint8_t b = data[i];
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    if (capped < len)
    {
        out.append("..");
    }
    return out;
}

uint8_t buildHeader(uint8_t route_type, uint8_t payload_type, uint8_t payload_ver)
{
    return (route_type & 0x03) | ((payload_type & 0x0F) << 2) | ((payload_ver & 0x03) << 6);
}

struct ParsedPacket
{
    uint8_t route_type = 0;
    uint8_t payload_type = 0;
    uint8_t payload_ver = 0;
    size_t path_len_index = 0;
    size_t path_len = 0;
    const uint8_t* path = nullptr;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
};

bool parsePacket(const uint8_t* data, size_t len, ParsedPacket* out)
{
    if (!data || len < 2 || !out)
    {
        return false;
    }

    uint8_t header = data[0];
    out->route_type = header & 0x03;
    out->payload_type = (header >> 2) & 0x0F;
    out->payload_ver = (header >> 6) & 0x03;

    size_t index = 1;
    if (out->route_type == 0 || out->route_type == 3)
    {
        if (len < index + 4 + 1)
        {
            return false;
        }
        index += 4; // transport codes
    }

    if (index >= len)
    {
        return false;
    }

    out->path_len_index = index;
    out->path_len = data[index++];
    if (index + out->path_len > len)
    {
        return false;
    }
    out->path = &data[index];
    index += out->path_len;

    if (index > len)
    {
        return false;
    }

    out->payload = &data[index];
    out->payload_len = len - index;
    return true;
}

uint32_t hashFrame(const uint8_t* data, size_t len)
{
    if (!data || len == 0)
    {
        return 0;
    }
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; ++i)
    {
        h ^= static_cast<uint32_t>(data[i]);
        h *= 16777619u;
    }
    return h;
}

uint32_t packetSignature(uint8_t payload_type, size_t path_len,
                         const uint8_t* payload, size_t payload_len)
{
    uint8_t sig_bytes[sizeof(uint32_t)] = {};
    SHA256 sha;
    sha.update(&payload_type, sizeof(payload_type));
    if (payload_type == kPayloadTypeTrace)
    {
        uint8_t p = static_cast<uint8_t>(path_len & 0xFFU);
        sha.update(&p, sizeof(p));
    }
    if (payload && payload_len > 0)
    {
        sha.update(payload, payload_len);
    }
    sha.finalize(sig_bytes, sizeof(sig_bytes));

    uint32_t sig = 0;
    memcpy(&sig, sig_bytes, sizeof(sig));
    return sig;
}

float estimateLoRaAirtimeMs(size_t frame_len, float bw_khz, uint8_t sf, uint8_t cr_denom)
{
    if (bw_khz <= 0.0f || sf < 5 || sf > 12)
    {
        return 0.0f;
    }

    const float bw_hz = bw_khz * 1000.0f;
    const float tsym = std::pow(2.0f, static_cast<float>(sf)) / bw_hz;
    const float de = (sf >= 11 && bw_khz <= 125.0f) ? 1.0f : 0.0f;
    const float ih = 0.0f;  // explicit header
    const float crc = 1.0f; // CRC enabled
    const float cr = static_cast<float>(cr_denom);
    const float payload_bits = (8.0f * static_cast<float>(frame_len)) - (4.0f * sf) + 28.0f + (16.0f * crc) - (20.0f * ih);
    const float denom = 4.0f * (static_cast<float>(sf) - (2.0f * de));
    float payload_sym = 8.0f;
    if (denom > 0.0f)
    {
        payload_sym += std::max(0.0f, std::ceil(payload_bits / denom) * cr);
    }
    const float preamble_sym = 8.0f + 4.25f;
    const float total_sec = (preamble_sym + payload_sym) * tsym;
    return total_sec * 1000.0f;
}

float scoreFromSnr(float snr, uint8_t sf, size_t packet_len)
{
    static const float kSnrThreshold[] = {-7.5f, -10.0f, -12.5f, -15.0f, -17.5f, -20.0f};
    if (!std::isfinite(snr) || sf < 7 || sf > 12)
    {
        return 0.0f;
    }
    float threshold = kSnrThreshold[sf - 7];
    if (snr < threshold)
    {
        return 0.0f;
    }
    float success = (snr - threshold) / 10.0f;
    float collision = 1.0f - (std::min<size_t>(packet_len, 256U) / 256.0f);
    return clampValue<float>(success * collision, 0.0f, 1.0f);
}

uint32_t computeRxDelayMs(float rx_delay_base, float score, uint32_t air_ms)
{
    if (rx_delay_base <= 0.0f || air_ms == 0)
    {
        return 0;
    }
    float d = (std::pow(rx_delay_base, 0.85f - score) - 1.0f) * static_cast<float>(air_ms);
    if (!std::isfinite(d) || d <= 0.0f)
    {
        return 0;
    }
    if (d > 32000.0f)
    {
        d = 32000.0f;
    }
    return static_cast<uint32_t>(d);
}

bool isZeroKey(const uint8_t* key, size_t len)
{
    if (!key || len == 0)
    {
        return true;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (key[i] != 0)
        {
            return false;
        }
    }
    return true;
}

void toHmacKey32(const uint8_t key16[kCipherKeySize], uint8_t out_key32[kCipherHmacKeySize])
{
    memset(out_key32, 0, kCipherHmacKeySize);
    memcpy(out_key32, key16, kCipherKeySize);
}

void sha256Trunc(uint8_t* out_hash, size_t out_len, const uint8_t* msg, size_t msg_len)
{
    if (!out_hash || out_len == 0)
    {
        return;
    }
    SHA256 sha;
    sha.update(msg, static_cast<size_t>(msg_len));
    sha.finalize(out_hash, out_len);
}

uint8_t computeChannelHash(const uint8_t* key16)
{
    uint8_t hash = 0;
    if (!key16)
    {
        return hash;
    }
    sha256Trunc(&hash, 1, key16, kCipherKeySize);
    return hash;
}

size_t aesEncrypt(const uint8_t key16[kCipherKeySize], uint8_t* dest,
                  const uint8_t* src, size_t src_len)
{
    if (!key16 || !dest || !src || src_len == 0)
    {
        return 0;
    }
    AES128 aes;
    aes.setKey(key16, kCipherKeySize);

    uint8_t* dp = dest;
    const uint8_t* sp = src;
    size_t remaining = src_len;
    while (remaining >= kCipherBlockSize)
    {
        aes.encryptBlock(dp, sp);
        dp += kCipherBlockSize;
        sp += kCipherBlockSize;
        remaining -= kCipherBlockSize;
    }
    if (remaining > 0)
    {
        uint8_t tail[kCipherBlockSize];
        memset(tail, 0, sizeof(tail));
        memcpy(tail, sp, remaining);
        aes.encryptBlock(dp, tail);
        dp += kCipherBlockSize;
    }
    return static_cast<size_t>(dp - dest);
}

size_t aesDecrypt(const uint8_t key16[kCipherKeySize], uint8_t* dest,
                  const uint8_t* src, size_t src_len)
{
    if (!key16 || !dest || !src || src_len == 0 || (src_len % kCipherBlockSize) != 0)
    {
        return 0;
    }
    AES128 aes;
    aes.setKey(key16, kCipherKeySize);

    uint8_t* dp = dest;
    const uint8_t* sp = src;
    size_t remaining = src_len;
    while (remaining >= kCipherBlockSize)
    {
        aes.decryptBlock(dp, sp);
        dp += kCipherBlockSize;
        sp += kCipherBlockSize;
        remaining -= kCipherBlockSize;
    }
    return static_cast<size_t>(dp - dest);
}

size_t encryptThenMac(const uint8_t key16[kCipherKeySize], const uint8_t key32[kCipherHmacKeySize],
                      uint8_t* out, size_t out_cap,
                      const uint8_t* plain, size_t plain_len)
{
    if (!key16 || !key32 || !out || !plain || plain_len == 0 || out_cap <= kCipherMacSize)
    {
        return 0;
    }

    size_t max_cipher = out_cap - kCipherMacSize;
    size_t cipher_len = ((plain_len + (kCipherBlockSize - 1)) / kCipherBlockSize) * kCipherBlockSize;
    if (cipher_len == 0 || cipher_len > max_cipher)
    {
        return 0;
    }

    size_t enc_len = aesEncrypt(key16, out + kCipherMacSize, plain, plain_len);
    if (enc_len != cipher_len)
    {
        return 0;
    }

    SHA256 sha;
    sha.resetHMAC(key32, kCipherHmacKeySize);
    sha.update(out + kCipherMacSize, enc_len);
    sha.finalizeHMAC(key32, kCipherHmacKeySize, out, kCipherMacSize);
    return kCipherMacSize + enc_len;
}

bool macThenDecrypt(const uint8_t key16[kCipherKeySize], const uint8_t key32[kCipherHmacKeySize],
                    const uint8_t* src, size_t src_len, uint8_t* out_plain, size_t* out_plain_len)
{
    if (!key16 || !key32 || !src || src_len <= kCipherMacSize || !out_plain || !out_plain_len)
    {
        return false;
    }
    size_t cipher_len = src_len - kCipherMacSize;
    if ((cipher_len % kCipherBlockSize) != 0)
    {
        return false;
    }

    uint8_t expected[kCipherMacSize];
    SHA256 sha;
    sha.resetHMAC(key32, kCipherHmacKeySize);
    sha.update(src + kCipherMacSize, cipher_len);
    sha.finalizeHMAC(key32, kCipherHmacKeySize, expected, kCipherMacSize);
    if (memcmp(expected, src, kCipherMacSize) != 0)
    {
        return false;
    }

    size_t plain_len = aesDecrypt(key16, out_plain, src + kCipherMacSize, cipher_len);
    if (plain_len == 0)
    {
        return false;
    }
    *out_plain_len = plain_len;
    return true;
}

size_t trimTrailingZeros(const uint8_t* buf, size_t len)
{
    if (!buf || len == 0)
    {
        return 0;
    }
    while (len > 0 && buf[len - 1] == 0)
    {
        --len;
    }
    return len;
}

bool shouldUsePublicChannelFallback(const chat::MeshConfig& cfg)
{
    if (cfg.meshcore_channel_name[0] == '\0')
    {
        return true;
    }
    return strncmp(cfg.meshcore_channel_name, "Public", sizeof(cfg.meshcore_channel_name)) == 0;
}

void xorCrypt(uint8_t* data, size_t len, const uint8_t* key, size_t key_len)
{
    if (!data || len == 0 || !key || key_len == 0)
    {
        return;
    }
    for (size_t i = 0; i < len; ++i)
    {
        data[i] ^= key[i % key_len];
    }
}

const uint8_t* selectChannelKey(const chat::MeshConfig& cfg, size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0;
    }
    if (!isZeroKey(cfg.secondary_key, sizeof(cfg.secondary_key)))
    {
        if (out_len)
        {
            *out_len = sizeof(cfg.secondary_key);
        }
        return cfg.secondary_key;
    }
    if (!isZeroKey(cfg.primary_key, sizeof(cfg.primary_key)))
    {
        if (out_len)
        {
            *out_len = sizeof(cfg.primary_key);
        }
        return cfg.primary_key;
    }
    return nullptr;
}

bool isPeerPayloadType(uint8_t payload_type)
{
    return payload_type == kPayloadTypeTxtMsg ||
           payload_type == kPayloadTypeReq ||
           payload_type == kPayloadTypeResponse ||
           payload_type == kPayloadTypePath;
}

bool buildFrameNoTransport(uint8_t route_type, uint8_t payload_type,
                           const uint8_t* path, size_t path_len,
                           const uint8_t* payload, size_t payload_len,
                           uint8_t* out_frame, size_t out_cap, size_t* out_len)
{
    if (!out_frame || !out_len || !payload || payload_len == 0 ||
        out_cap > kMeshcoreMaxFrameSize || path_len > kMeshcoreMaxPathSize ||
        (path_len > 0 && !path))
    {
        return false;
    }
    if (route_type == kRouteTypeTransportFlood)
    {
        return false;
    }

    size_t index = 0;
    out_frame[index++] = buildHeader(route_type, payload_type, kPayloadVer1);
    out_frame[index++] = static_cast<uint8_t>(path_len);
    if (path_len > 0)
    {
        memcpy(&out_frame[index], path, path_len);
        index += path_len;
    }
    if (index + payload_len > out_cap)
    {
        return false;
    }
    memcpy(&out_frame[index], payload, payload_len);
    index += payload_len;
    *out_len = index;
    return true;
}

bool buildPeerDatagramPayload(uint8_t dest_hash, uint8_t src_hash,
                              const uint8_t key16[kCipherKeySize], const uint8_t key32[kCipherHmacKeySize],
                              const uint8_t* plain, size_t plain_len,
                              uint8_t* out_payload, size_t out_cap, size_t* out_len)
{
    if (!key16 || !key32 || !plain || plain_len == 0 || !out_payload || !out_len || out_cap < 3)
    {
        return false;
    }

    size_t index = 0;
    out_payload[index++] = dest_hash;
    out_payload[index++] = src_hash;
    const size_t encrypted_len = encryptThenMac(key16, key32,
                                                &out_payload[index], out_cap - index,
                                                plain, plain_len);
    if (encrypted_len == 0)
    {
        return false;
    }
    index += encrypted_len;
    *out_len = index;
    return true;
}

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
      last_rx_snr_(NAN)
{
    const uint64_t raw = ESP.getEfuseMac();
    const uint8_t* mac = reinterpret_cast<const uint8_t*>(&raw);
    node_id_ = (static_cast<uint32_t>(mac[2]) << 24) |
               (static_cast<uint32_t>(mac[3]) << 16) |
               (static_cast<uint32_t>(mac[4]) << 8) |
               static_cast<uint32_t>(mac[5]);
    self_hash_ = static_cast<uint8_t>(node_id_ & 0xFF);
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

MeshCoreAdapter::PeerRouteEntry& MeshCoreAdapter::upsertPeerRoute(uint8_t peer_hash, uint32_t now_ms)
{
    if (PeerRouteEntry* found = findPeerRouteByHash(peer_hash))
    {
        found->last_seen_ms = now_ms;
        return *found;
    }
    if (peer_routes_.size() >= 128)
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
    if (path_len > 0)
    {
        memcpy(entry.out_path, path, path_len);
    }
    entry.out_path_len = static_cast<uint8_t>(path_len);
    entry.has_out_path = true;
    entry.preferred_channel = channel;
    entry.last_seen_ms = now_ms;
}

bool MeshCoreAdapter::deriveDirectSecret(ChannelId channel, uint8_t peer_hash,
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

    uint8_t tried_key16[3][16];
    uint8_t tried_key32[3][32];
    size_t tried = 0;

    for (size_t i = 0; i < order_len; ++i)
    {
        uint8_t key16[16];
        uint8_t key32[32];
        if (!deriveDirectSecret(order[i], src_hash, key16, key32))
        {
            continue;
        }

        bool duplicate = false;
        for (size_t j = 0; j < tried; ++j)
        {
            if (memcmp(tried_key16[j], key16, sizeof(key16)) == 0 &&
                memcmp(tried_key32[j], key32, sizeof(key32)) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
        {
            continue;
        }

        memcpy(tried_key16[tried], key16, sizeof(key16));
        memcpy(tried_key32[tried], key32, sizeof(key32));
        ++tried;

        size_t plain_len = 0;
        if (macThenDecrypt(key16, key32, cipher, cipher_len, out_plain, &plain_len))
        {
            *out_plain_len = plain_len;
            if (out_channel)
            {
                *out_channel = order[i];
            }
            return true;
        }
    }

    return false;
}

bool MeshCoreAdapter::canTransmitNow(uint32_t now_ms) const
{
    if (!initialized_ || !config_.tx_enabled || !board_.isRadioOnline())
    {
        return false;
    }
    if (min_tx_interval_ms_ > 0 && last_tx_ms_ > 0 &&
        (now_ms - last_tx_ms_) < min_tx_interval_ms_)
    {
        return false;
    }
    return true;
}

bool MeshCoreAdapter::transmitFrameNow(const uint8_t* data, size_t len, uint32_t now_ms)
{
    if (!data || len == 0 || len > kMeshcoreMaxFrameSize || !canTransmitNow(now_ms))
    {
        return false;
    }

    int state = RADIOLIB_ERR_UNSUPPORTED;
#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    state = board_.transmitRadio(data, len);
#endif
    if (state == RADIOLIB_ERR_NONE)
    {
        last_tx_ms_ = now_ms;
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

bool MeshCoreAdapter::sendText(ChannelId channel, const std::string& text,
                               MessageId* out_msg_id, NodeId peer)
{
    if (!initialized_ || !config_.tx_enabled)
    {
        return false;
    }

    if (text.empty())
    {
        return false;
    }

    uint32_t now_ms = millis();
    if (!canTransmitNow(now_ms))
    {
        return false;
    }

    if (peer != 0)
    {
        const uint8_t peer_hash = static_cast<uint8_t>(peer & 0xFFU);
        rememberPeerNodeId(peer_hash, peer, now_ms);

        uint8_t peer_key16[16];
        uint8_t peer_key32[32];
        if (!deriveDirectSecret(channel, peer_hash, peer_key16, peer_key32))
        {
            MESHCORE_LOG("[MESHCORE] TX direct text dropped (no peer secret) peer=%08lX\n",
                         static_cast<unsigned long>(peer));
            return false;
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
        plain[plain_len++] = static_cast<uint8_t>(kTxtTypePlain << 2);
        memcpy(&plain[plain_len], body.data(), body.size());
        plain_len += body.size();

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
        uint8_t route_type = kRouteTypeFlood;
        const uint8_t* out_path = nullptr;
        size_t out_path_len = 0;

        if (const PeerRouteEntry* route = findPeerRouteByHash(peer_hash))
        {
            if (route->has_out_path)
            {
                route_type = kRouteTypeDirect;
                out_path = route->out_path;
                out_path_len = route->out_path_len;
            }
        }

        if (!buildFrameNoTransport(route_type, kPayloadTypeTxtMsg,
                                   out_path, out_path_len,
                                   payload, payload_len,
                                   frame, sizeof(frame), &frame_len))
        {
            return false;
        }

        bool ok = transmitFrameNow(frame, frame_len, now_ms);
        MESHCORE_LOG("[MESHCORE] TX direct text peer=%08lX hash=%02X route=%s path_len=%u len=%u ok=%u\n",
                     static_cast<unsigned long>(peer),
                     static_cast<unsigned>(peer_hash),
                     (route_type == kRouteTypeDirect) ? "direct" : "flood",
                     static_cast<unsigned>(out_path_len),
                     static_cast<unsigned>(frame_len),
                     ok ? 1U : 0U);

        if (ok && out_msg_id)
        {
            *out_msg_id = next_msg_id_++;
        }
        return ok;
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
    constexpr size_t kGroupTextBudget = (kGroupCipherBudget > kGroupPlainPrefixSize)
                                            ? (kGroupCipherBudget - kGroupPlainPrefixSize)
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
    plain[plain_len++] = static_cast<uint8_t>(kTxtTypePlain << 2);
    memcpy(&plain[plain_len], decorated.data(), decorated.size());
    plain_len += decorated.size();

    uint8_t encrypted[kMeshcoreMaxPayloadSize];
    size_t encrypted_len = encryptThenMac(channel_key16, channel_key32,
                                          encrypted, sizeof(encrypted),
                                          plain, plain_len);
    if (encrypted_len == 0 || encrypted_len > (kMeshcoreMaxPayloadSize - 1))
    {
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

bool MeshCoreAdapter::pollIncomingText(MeshIncomingText* out)
{
    // TODO: Implement MeshCore message receiving
    // This is a placeholder implementation

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
                                  NodeId dest, bool want_ack)
{
    if (!initialized_ || !config_.tx_enabled || !payload || len == 0)
    {
        return false;
    }

    (void)dest;
    (void)want_ack;

    uint32_t now_ms = millis();
    if (!canTransmitNow(now_ms))
    {
        return false;
    }

    uint8_t channel_key16[16];
    uint8_t channel_key32[32];
    uint8_t channel_hash = 0;
    if (!resolveGroupSecret(channel, channel_key16, channel_key32, &channel_hash))
    {
        return false;
    }

    constexpr size_t kGroupCipherBudget =
        ((kMeshcoreMaxPayloadSize - 1 - kCipherMacSize) / kCipherBlockSize) * kCipherBlockSize;
    size_t body_len = len;
    if (body_len + sizeof(portnum) > kGroupCipherBudget)
    {
        body_len = kGroupCipherBudget - sizeof(portnum);
    }
    uint8_t plain[kGroupCipherBudget];
    size_t plain_len = 0;
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
    config_.meshcore_freq_mhz = clampValue<float>(config_.meshcore_freq_mhz, 300.0f, 2500.0f);
    config_.meshcore_bw_khz = clampValue<float>(config_.meshcore_bw_khz, 7.0f, 500.0f);
    config_.meshcore_sf = clampValue<uint8_t>(config_.meshcore_sf, 5, 12);
    config_.meshcore_cr = clampValue<uint8_t>(config_.meshcore_cr, 5, 8);
    config_.tx_power = clampValue<int8_t>(config_.tx_power, -9, 30);
    config_.meshcore_rx_delay_base = clampValue<float>(config_.meshcore_rx_delay_base, 0.0f, 20.0f);
    config_.meshcore_airtime_factor = clampValue<float>(config_.meshcore_airtime_factor, 0.0f, 9.0f);
    config_.meshcore_flood_max = clampValue<uint8_t>(config_.meshcore_flood_max, 0, 64);
    config_.meshcore_channel_slot = clampValue<uint8_t>(config_.meshcore_channel_slot, 0, 14);

    if (config_.meshcore_channel_name[0] == '\0')
    {
        strncpy(config_.meshcore_channel_name, "Public", sizeof(config_.meshcore_channel_name) - 1);
        config_.meshcore_channel_name[sizeof(config_.meshcore_channel_name) - 1] = '\0';
    }
    scheduled_tx_.clear();
    peer_routes_.clear();

#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    if (board_.isRadioOnline())
    {
        board_.configureLoraRadio(config_.meshcore_freq_mhz,
                                  config_.meshcore_bw_khz,
                                  config_.meshcore_sf,
                                  config_.meshcore_cr,
                                  config_.tx_power,
                                  16,
                                  0x2b,
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

void MeshCoreAdapter::setPrivacyConfig(uint8_t encrypt_mode, bool pki_enabled)
{
    encrypt_mode_ = encrypt_mode;
    pki_enabled_ = pki_enabled;
    if (encrypt_mode_ == 0)
    {
        pki_enabled_ = false;
    }
}

void MeshCoreAdapter::setLastRxStats(float rssi, float snr)
{
    last_rx_rssi_ = rssi;
    last_rx_snr_ = snr;
}

bool MeshCoreAdapter::isReady() const
{
    // TODO: Check MeshCore radio and network status
    return initialized_;
}

bool MeshCoreAdapter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    // TODO: Implement MeshCore raw packet polling
    // This is a placeholder implementation

    if (!initialized_ || !out_data || max_len == 0)
    {
        return false;
    }

    // Placeholder - MeshCore integration needed
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
    if (!data || size < 2 || size > kMeshcoreMaxFrameSize)
    {
        return;
    }

    if (size <= sizeof(last_raw_packet_))
    {
        memcpy(last_raw_packet_, data, size);
        last_raw_packet_len_ = size;
        has_pending_raw_packet_ = true;
    }

    ParsedPacket parsed;
    if (!parsePacket(data, size, &parsed))
    {
        return;
    }
    if (parsed.payload_ver != kPayloadVer1)
    {
        return;
    }

    const uint32_t now_ms = millis();
    const uint32_t packet_sig = packetSignature(parsed.payload_type, parsed.path_len,
                                                parsed.payload, parsed.payload_len);
    if (hasSeenSignature(packet_sig, now_ms))
    {
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
    const bool is_direct_route = (parsed.route_type == kRouteTypeDirect);

    // Direct routing hop forwarding: only the addressed next-hop should retransmit.
    if (is_direct_route && parsed.path_len > 0)
    {
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

    // MeshCore repeater behavior: forward flood packets with delay and hop cap.
    if (config_.meshcore_client_repeat && is_flood_route)
    {
        bool ack_like = (parsed.payload_type == kPayloadTypeAck);
        if (parsed.payload_type == kPayloadTypeMultipart && parsed.payload_len > 0)
        {
            ack_like = ((parsed.payload[0] & 0x0F) == kPayloadTypeAck);
        }
        if (!ack_like && parsed.path_len < config_.meshcore_flood_max &&
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
    }

    const bool is_peer_payload = isPeerPayloadType(parsed.payload_type) &&
                                 parsed.payload_len > (2 + kCipherMacSize);
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
            MESHCORE_LOG("[MESHCORE] RX peer decrypt failed type=%u src=%02X\n",
                         static_cast<unsigned>(parsed.payload_type),
                         static_cast<unsigned>(src_hash));
            return;
        }
        plain_len = trimTrailingZeros(plain, plain_len);
        if (is_direct_route && parsed.path_len == 0)
        {
            rememberPeerPath(src_hash, nullptr, 0, peer_channel, now_ms);
        }
        else
        {
            PeerRouteEntry& route = upsertPeerRoute(src_hash, now_ms);
            route.preferred_channel = peer_channel;
        }

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

                if (is_flood_route)
                {
                    uint8_t ack_payload[sizeof(uint32_t)];
                    memcpy(ack_payload, &packet_sig, sizeof(uint32_t));
                    sendPathReturn(src_hash, peer_channel,
                                   parsed.path, parsed.path_len,
                                   kRouteTypeFlood, nullptr, 0,
                                   kPayloadTypeAck, ack_payload, sizeof(ack_payload),
                                   kAckDelayMs);
                }
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
                const PeerRouteEntry* route = findPeerRouteByHash(src_hash);
                uint8_t route_type = kRouteTypeFlood;
                const uint8_t* route_path = nullptr;
                size_t route_path_len = 0;
                if (route && route->has_out_path)
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
                }
                else if (extra_type == kPayloadTypeResponse && extra_len > 0)
                {
                    MESHCORE_LOG("[MESHCORE] RX PATH/RESPONSE src=%02X len=%u\n",
                                 static_cast<unsigned>(src_hash),
                                 static_cast<unsigned>(extra_len));
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

    if (is_group_text_payload)
    {
        const uint8_t channel_hash = parsed.payload[0];
        bool channel_match = false;
        const ChannelId rx_channel = resolveChannelFromHash(channel_hash, &channel_match);
        if (!channel_match)
        {
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
            return;
        }
        plain_len = trimTrailingZeros(plain, plain_len);
        if (plain_len <= kGroupPlainPrefixSize)
        {
            return;
        }

        uint32_t sender_ts = 0;
        memcpy(&sender_ts, plain, sizeof(sender_ts));

        MeshIncomingText incoming;
        incoming.channel = rx_channel;
        incoming.from = 0;
        incoming.to = 0xFFFFFFFF;
        incoming.msg_id = next_msg_id_++;
        incoming.timestamp = is_valid_epoch(sender_ts) ? sender_ts : now_message_timestamp();
        incoming.text.assign(reinterpret_cast<const char*>(plain + kGroupPlainPrefixSize),
                             reinterpret_cast<const char*>(plain + plain_len));
        incoming.hop_limit = 0;
        incoming.encrypted = true;
        fill_rx_meta(incoming.rx_meta, false);
        incoming.rx_meta.channel_hash = channel_hash;
        receive_queue_.push(incoming);
    }
    else if (is_group_data_payload)
    {
        const uint8_t channel_hash = parsed.payload[0];
        bool channel_match = false;
        const ChannelId rx_channel = resolveChannelFromHash(channel_hash, &channel_match);
        if (!channel_match)
        {
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
            return;
        }
        plain_len = trimTrailingZeros(plain, plain_len);
        if (plain_len < sizeof(uint32_t))
        {
            return;
        }

        MeshIncomingData incoming;
        memcpy(&incoming.portnum, plain, sizeof(uint32_t));
        incoming.from = 0;
        incoming.to = 0xFFFFFFFF;
        incoming.packet_id = next_msg_id_++;
        incoming.channel = rx_channel;
        incoming.channel_hash = channel_hash;
        incoming.want_response = false;
        incoming.payload.assign(plain + sizeof(uint32_t), plain + plain_len);
        fill_rx_meta(incoming.rx_meta, false);
        incoming.rx_meta.channel_hash = channel_hash;
        app_receive_queue_.push(incoming);
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
    }
}

void MeshCoreAdapter::processSendQueue()
{
    if (scheduled_tx_.empty())
    {
        return;
    }

    uint32_t now_ms = millis();
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

} // namespace meshcore
} // namespace chat
