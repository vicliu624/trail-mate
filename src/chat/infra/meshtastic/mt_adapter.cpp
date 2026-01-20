/**
 * @file mt_adapter.cpp
 * @brief Meshtastic mesh adapter implementation
 */

#include "mt_adapter.h"
#include "../../../sys/event_bus.h"
#include "../../domain/contact_types.h"
#include "../../../team/protocol/team_portnum.h"
#include <Arduino.h>
#include <Crypto.h>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>
#define TEST_CURVE25519_FIELD_OPS
#include "../../../board/TLoRaPagerTypes.h"
#include "generated/meshtastic/config.pb.h"
#include "mt_region.h"
#include <AES.h>
#include <Curve25519.h>
#include <Preferences.h>
#include <RNG.h>
#include <SHA256.h>
#include <vector>

#ifndef LORA_LOG_ENABLE
#define LORA_LOG_ENABLE 1
#endif

#if LORA_LOG_ENABLE
#define LORA_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LORA_LOG(...) \
    do                \
    {                 \
    } while (0)
#endif

namespace
{
constexpr uint8_t kDefaultPsk[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                     0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};
constexpr uint8_t kDefaultPskIndex = 1;
constexpr const char* kPrimaryChannelName = "LongFast";
constexpr const char* kSecondaryChannelName = "Squad";
constexpr uint8_t kLoraSyncWord = 0x2b;
constexpr uint16_t kLoraPreambleLen = 16;
constexpr uint8_t kBitfieldWantResponseMask = 0x02;

bool allowPkiForPortnum(uint32_t portnum)
{
    return portnum != meshtastic_PortNum_NODEINFO_APP &&
           portnum != meshtastic_PortNum_ROUTING_APP &&
           portnum != meshtastic_PortNum_POSITION_APP &&
           portnum != meshtastic_PortNum_TRACEROUTE_APP;
}

static const char* portName(uint32_t portnum)
{
    switch (portnum)
    {
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
        return "TEXT";
    case meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP:
        return "TEXT_COMP";
    case meshtastic_PortNum_NODEINFO_APP:
        return "NODEINFO";
    case meshtastic_PortNum_POSITION_APP:
        return "POSITION";
    case meshtastic_PortNum_TELEMETRY_APP:
        return "TELEMETRY";
    case meshtastic_PortNum_REMOTE_HARDWARE_APP:
        return "REMOTEHW";
    case meshtastic_PortNum_ROUTING_APP:
        return "ROUTING";
    case meshtastic_PortNum_TRACEROUTE_APP:
        return "TRACEROUTE";
    case meshtastic_PortNum_WAYPOINT_APP:
        return "WAYPOINT";
    case team::proto::TEAM_MGMT_APP:
        return "TEAM_MGMT";
    case team::proto::TEAM_POSITION_APP:
        return "TEAM_POS";
    case team::proto::TEAM_WAYPOINT_APP:
        return "TEAM_WP";
    default:
        return "UNKNOWN";
    }
}

static const char* routingErrorName(meshtastic_Routing_Error err)
{
    switch (err)
    {
    case meshtastic_Routing_Error_NONE:
        return "NONE";
    case meshtastic_Routing_Error_NO_ROUTE:
        return "NO_ROUTE";
    case meshtastic_Routing_Error_GOT_NAK:
        return "GOT_NAK";
    case meshtastic_Routing_Error_TIMEOUT:
        return "TIMEOUT";
    case meshtastic_Routing_Error_NO_INTERFACE:
        return "NO_INTERFACE";
    case meshtastic_Routing_Error_MAX_RETRANSMIT:
        return "MAX_RETRANSMIT";
    case meshtastic_Routing_Error_NO_CHANNEL:
        return "NO_CHANNEL";
    case meshtastic_Routing_Error_TOO_LARGE:
        return "TOO_LARGE";
    case meshtastic_Routing_Error_NO_RESPONSE:
        return "NO_RESPONSE";
    case meshtastic_Routing_Error_DUTY_CYCLE_LIMIT:
        return "DUTY_CYCLE_LIMIT";
    case meshtastic_Routing_Error_BAD_REQUEST:
        return "BAD_REQUEST";
    case meshtastic_Routing_Error_NOT_AUTHORIZED:
        return "NOT_AUTHORIZED";
    case meshtastic_Routing_Error_PKI_FAILED:
        return "PKI_FAILED";
    case meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY:
        return "PKI_UNKNOWN_PUBKEY";
    case meshtastic_Routing_Error_ADMIN_BAD_SESSION_KEY:
        return "ADMIN_BAD_SESSION_KEY";
    case meshtastic_Routing_Error_ADMIN_PUBLIC_KEY_UNAUTHORIZED:
        return "ADMIN_PUBLIC_KEY_UNAUTHORIZED";
    case meshtastic_Routing_Error_RATE_LIMIT_EXCEEDED:
        return "RATE_LIMIT_EXCEEDED";
    default:
        return "UNKNOWN";
    }
}

uint8_t xorHash(const uint8_t* data, size_t len)
{
    uint8_t out = 0;
    for (size_t i = 0; i < len; ++i)
    {
        out ^= data[i];
    }
    return out;
}

void expandShortPsk(uint8_t index, uint8_t* out, size_t* out_len)
{
    if (!out || !out_len || index == 0)
    {
        if (out_len)
        {
            *out_len = 0;
        }
        return;
    }
    memcpy(out, kDefaultPsk, sizeof(kDefaultPsk));
    out[sizeof(kDefaultPsk) - 1] =
        static_cast<uint8_t>(out[sizeof(kDefaultPsk) - 1] + index - 1);
    *out_len = sizeof(kDefaultPsk);
}

bool isZeroKey(const uint8_t* key, size_t len)
{
    if (!key || len == 0) return true;
    for (size_t i = 0; i < len; ++i)
    {
        if (key[i] != 0) return false;
    }
    return true;
}

uint8_t computeChannelHash(const char* name, const uint8_t* key, size_t key_len)
{
    uint8_t h = xorHash(reinterpret_cast<const uint8_t*>(name), strlen(name));
    if (key && key_len > 0)
    {
        h ^= xorHash(key, key_len);
    }
    return h;
}

std::string toHex(const uint8_t* data, size_t len, size_t max_len = 64)
{
    if (!data || len == 0)
    {
        return {};
    }
    size_t capped = (len > max_len) ? max_len : len;
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(capped * 2);
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

constexpr size_t kAesBlockSize = 16;

class AesCcmCipher
{
  public:
    ~AesCcmCipher()
    {
        delete aes_;
    }

    void setKey(const uint8_t* key, size_t key_len)
    {
        delete aes_;
        aes_ = nullptr;
        if (key_len != 0)
        {
            aes_ = new AESSmall256();
            aes_->setKey(key, key_len);
        }
    }

    void encryptBlock(uint8_t* out, const uint8_t* in)
    {
        if (!aes_)
        {
            memset(out, 0, kAesBlockSize);
            return;
        }
        aes_->encryptBlock(out, in);
    }

  private:
    AESSmall256* aes_ = nullptr;
};

static AesCcmCipher g_aes_ccm;

static int constant_time_compare(const void* a_, const void* b_, size_t len)
{
    const volatile uint8_t* volatile a = (const volatile uint8_t* volatile)a_;
    const volatile uint8_t* volatile b = (const volatile uint8_t* volatile)b_;
    if (len == 0) return 0;
    if (!a || !b) return -1;
    volatile uint8_t d = 0U;
    for (size_t i = 0; i < len; ++i)
    {
        d |= (a[i] ^ b[i]);
    }
    return (1 & ((d - 1) >> 8)) - 1;
}

static void put_be16(uint8_t* a, uint16_t val)
{
    a[0] = val >> 8;
    a[1] = val & 0xff;
}

static void xor_aes_block(uint8_t* dst, const uint8_t* src)
{
    for (size_t i = 0; i < kAesBlockSize; ++i)
    {
        dst[i] ^= src[i];
    }
}

static void aes_ccm_auth_start(size_t m, size_t l, const uint8_t* nonce,
                               const uint8_t* aad, size_t aad_len, size_t plain_len,
                               uint8_t* x)
{
    uint8_t aad_buf[2 * kAesBlockSize];
    uint8_t b[kAesBlockSize];
    b[0] = aad_len ? 0x40 : 0;
    b[0] |= (((m - 2) / 2) << 3);
    b[0] |= (l - 1);
    memcpy(&b[1], nonce, 15 - l);
    put_be16(&b[kAesBlockSize - l], plain_len);
    g_aes_ccm.encryptBlock(x, b);
    if (!aad_len) return;
    put_be16(aad_buf, aad_len);
    memcpy(aad_buf + 2, aad, aad_len);
    memset(aad_buf + 2 + aad_len, 0, sizeof(aad_buf) - 2 - aad_len);
    xor_aes_block(aad_buf, x);
    g_aes_ccm.encryptBlock(x, aad_buf);
    if (aad_len > kAesBlockSize - 2)
    {
        xor_aes_block(&aad_buf[kAesBlockSize], x);
        g_aes_ccm.encryptBlock(x, &aad_buf[kAesBlockSize]);
    }
}

static void aes_ccm_auth(const uint8_t* data, size_t len, uint8_t* x)
{
    size_t last = len % kAesBlockSize;
    for (size_t i = 0; i < len / kAesBlockSize; ++i)
    {
        xor_aes_block(x, data);
        data += kAesBlockSize;
        g_aes_ccm.encryptBlock(x, x);
    }
    if (last)
    {
        for (size_t i = 0; i < last; ++i)
        {
            x[i] ^= *data++;
        }
        g_aes_ccm.encryptBlock(x, x);
    }
}

static void aes_ccm_encr_start(size_t l, const uint8_t* nonce, uint8_t* a)
{
    a[0] = l - 1;
    memcpy(&a[1], nonce, 15 - l);
}

static void aes_ccm_encr(size_t l, const uint8_t* in, size_t len, uint8_t* out, uint8_t* a)
{
    size_t last = len % kAesBlockSize;
    size_t i = 0;
    for (i = 1; i <= len / kAesBlockSize; ++i)
    {
        put_be16(&a[kAesBlockSize - 2], i);
        g_aes_ccm.encryptBlock(out, a);
        xor_aes_block(out, in);
        out += kAesBlockSize;
        in += kAesBlockSize;
    }
    if (last)
    {
        put_be16(&a[kAesBlockSize - 2], i);
        g_aes_ccm.encryptBlock(out, a);
        for (size_t j = 0; j < last; ++j)
        {
            *out++ ^= *in++;
        }
    }
}

static void aes_ccm_encr_auth(size_t m, const uint8_t* x, uint8_t* a, uint8_t* auth)
{
    uint8_t tmp[kAesBlockSize];
    put_be16(&a[kAesBlockSize - 2], 0);
    g_aes_ccm.encryptBlock(tmp, a);
    for (size_t i = 0; i < m; ++i)
    {
        auth[i] = x[i] ^ tmp[i];
    }
}

static void aes_ccm_decr_auth(size_t m, uint8_t* a, const uint8_t* auth, uint8_t* t)
{
    uint8_t tmp[kAesBlockSize];
    put_be16(&a[kAesBlockSize - 2], 0);
    g_aes_ccm.encryptBlock(tmp, a);
    for (size_t i = 0; i < m; ++i)
    {
        t[i] = auth[i] ^ tmp[i];
    }
}

static void hashSharedKey(uint8_t* bytes, size_t num_bytes)
{
    SHA256 hash;
    size_t posn;
    uint8_t size = static_cast<uint8_t>(num_bytes);
    uint8_t inc = 16;
    hash.reset();
    for (posn = 0; posn < size; posn += inc)
    {
        size_t len = size - posn;
        if (len > inc)
        {
            len = inc;
        }
        hash.update(bytes + posn, len);
    }
    hash.finalize(bytes, 32);
}

static bool aes_ccm_ad(const uint8_t* key, size_t key_len, const uint8_t* nonce, size_t m,
                       const uint8_t* crypt, size_t crypt_len, const uint8_t* aad,
                       size_t aad_len, const uint8_t* auth, uint8_t* plain)
{
    const size_t l = 2;
    uint8_t x[kAesBlockSize];
    uint8_t a[kAesBlockSize];
    uint8_t t[kAesBlockSize];
    if (aad_len > 30 || m > kAesBlockSize) return false;
    g_aes_ccm.setKey(key, key_len);
    aes_ccm_encr_start(l, nonce, a);
    aes_ccm_decr_auth(m, a, auth, t);
    aes_ccm_encr(l, crypt, crypt_len, plain, a);
    aes_ccm_auth_start(m, l, nonce, aad, aad_len, crypt_len, x);
    aes_ccm_auth(plain, crypt_len, x);
    return constant_time_compare(x, t, m) == 0;
}

static void initPkiNonce(uint32_t from, uint64_t packet_id, uint32_t extra_nonce, uint8_t* nonce_out)
{
    memset(nonce_out, 0, kAesBlockSize);
    memcpy(nonce_out, &packet_id, sizeof(packet_id));
    memcpy(nonce_out + sizeof(packet_id), &from, sizeof(from));
    if (extra_nonce)
    {
        memcpy(nonce_out + sizeof(uint32_t), &extra_nonce, sizeof(extra_nonce));
    }
}
} // namespace

// Use protobuf codec and wire packet functions
using chat::meshtastic::buildWirePacket;
using chat::meshtastic::decodeTextMessage;
using chat::meshtastic::decryptPayload;
using chat::meshtastic::encodeNodeInfoMessage;
using chat::meshtastic::encodeAppData;
using chat::meshtastic::encodeTextMessage;
using chat::meshtastic::PacketHeaderWire;
using chat::meshtastic::parseWirePacket;

namespace chat
{
namespace meshtastic
{

MtAdapter::MtAdapter(TLoRaPagerBoard& board)
    : board_(board),
      next_packet_id_(1),
      ready_(false),
      node_id_(0),
      mac_addr_{0},
      last_nodeinfo_ms_(0),
      primary_channel_hash_(0),
      primary_psk_{0},
      primary_psk_len_(0),
      secondary_channel_hash_(0),
      secondary_psk_{0},
      secondary_psk_len_(0),
      pki_ready_(false),
      pki_public_key_{},
      pki_private_key_{},
      last_raw_packet_len_(0),
      has_pending_raw_packet_(false)
{
    config_ = MeshConfig(); // Default config
    initNodeIdentity();
    initPkiKeys();
    loadPkiNodeKeys();
    updateChannelKeys();
}

MtAdapter::~MtAdapter()
{
}

bool MtAdapter::sendText(ChannelId channel, const std::string& text,
                         MessageId* out_msg_id, NodeId peer)
{
    if (!ready_ || text.empty())
    {
        return false;
    }

    PendingSend pending;
    pending.channel = channel;
    pending.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    pending.text = text;
    pending.msg_id = next_packet_id_++;
    pending.dest = (peer != 0) ? peer : 0xFFFFFFFF;
    pending.retry_count = 0;
    pending.last_attempt = 0;

    send_queue_.push(pending);
    LORA_LOG("[LORA] queue text ch=%u len=%u id=%lu\n",
             static_cast<unsigned>(channel),
             static_cast<unsigned>(text.size()),
             static_cast<unsigned long>(pending.msg_id));

    if (out_msg_id)
    {
        *out_msg_id = pending.msg_id;
    }

    return true;
}

bool MtAdapter::pollIncomingText(MeshIncomingText* out)
{
    if (receive_queue_.empty())
    {
        return false;
    }

    *out = receive_queue_.front();
    receive_queue_.pop();
    return true;
}

bool MtAdapter::sendAppData(ChannelId channel, uint32_t portnum,
                            const uint8_t* payload, size_t len,
                            NodeId dest, bool want_ack)
{
    if (!ready_)
    {
        return false;
    }

    uint8_t data_buffer[256];
    size_t data_size = sizeof(data_buffer);
    if (!encodeAppData(portnum, payload, len, want_ack, data_buffer, &data_size))
    {
        return false;
    }

    uint8_t wire_buffer[512];
    size_t wire_size = sizeof(wire_buffer);

    ChannelId out_channel = channel;
    uint8_t channel_hash =
        (out_channel == ChannelId::SECONDARY) ? secondary_channel_hash_ : primary_channel_hash_;
    const uint8_t* psk =
        (out_channel == ChannelId::SECONDARY) ? secondary_psk_ : primary_psk_;
    size_t psk_len =
        (out_channel == ChannelId::SECONDARY) ? secondary_psk_len_ : primary_psk_len_;
    uint8_t hop_limit = config_.hop_limit;
    uint32_t dest_node = (dest != 0) ? dest : 0xFFFFFFFF;
    bool want_ack_flag = want_ack && (dest_node != 0xFFFFFFFF);
    MessageId msg_id = next_packet_id_++;

    const uint8_t* out_payload = data_buffer;
    size_t out_len = data_size;
    bool pki_disabled = false;
    if (dest_node != 0xFFFFFFFF)
    {
        uint32_t now_ms = millis();
        auto disable_it = pki_disabled_until_ms_.find(dest_node);
        if (disable_it != pki_disabled_until_ms_.end())
        {
            if (now_ms < disable_it->second)
            {
                pki_disabled = true;
            }
            else
            {
                pki_disabled_until_ms_.erase(disable_it);
            }
        }
    }
    bool use_pki = (dest_node != 0xFFFFFFFF) && pki_ready_ &&
                   allowPkiForPortnum(portnum) &&
                   !pki_disabled &&
                   (node_public_keys_.find(dest_node) != node_public_keys_.end());
    uint8_t pki_buf[256];
    size_t pki_len = sizeof(pki_buf);
    if (use_pki && encryptPkiPayload(dest_node, msg_id, data_buffer, data_size, pki_buf, &pki_len))
    {
        out_payload = pki_buf;
        out_len = pki_len;
        channel_hash = 0; // PKI channel
        want_ack_flag = true;
        psk = nullptr;
        psk_len = 0;
    }
    else if (dest_node != 0xFFFFFFFF && out_channel == ChannelId::PRIMARY)
    {
        if (pki_disabled)
        {
            uint32_t until_ms = pki_disabled_until_ms_[dest_node];
            LORA_LOG("[LORA] PKI disabled for %08lX until=%lu\n",
                     (unsigned long)dest_node,
                     (unsigned long)until_ms);
        }
        auto it = node_last_channel_.find(dest_node);
        if (it != node_last_channel_.end())
        {
            out_channel = it->second;
            channel_hash =
                (out_channel == ChannelId::SECONDARY) ? secondary_channel_hash_ : primary_channel_hash_;
            psk = (out_channel == ChannelId::SECONDARY) ? secondary_psk_ : primary_psk_;
            psk_len = (out_channel == ChannelId::SECONDARY) ? secondary_psk_len_ : primary_psk_len_;
        }
    }

    if (!buildWirePacket(out_payload, out_len, node_id_, msg_id,
                         dest_node, channel_hash, hop_limit, want_ack_flag,
                         psk, psk_len, wire_buffer, &wire_size))
    {
        return false;
    }

    if (!board_.isHardwareOnline(HW_RADIO_ONLINE))
    {
        return false;
    }

    int state = RADIOLIB_ERR_UNSUPPORTED;
#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    state = board_.radio.transmit(wire_buffer, wire_size);
#endif

    bool ok = (state == RADIOLIB_ERR_NONE);
    LORA_LOG("[LORA] TX app port=%u len=%u ok=%d\n",
             (unsigned)portnum,
             (unsigned)wire_size,
             ok ? 1 : 0);
    if (ok)
    {
        startRadioReceive();
    }
    return ok;
}

bool MtAdapter::pollIncomingData(MeshIncomingData* out)
{
    if (app_receive_queue_.empty())
    {
        return false;
    }

    *out = app_receive_queue_.front();
    app_receive_queue_.pop();
    return true;
}

void MtAdapter::applyConfig(const MeshConfig& config)
{
    config_ = config;
    updateChannelKeys();
    configureRadio();
}

bool MtAdapter::isReady() const
{
    return ready_ && board_.isHardwareOnline(HW_RADIO_ONLINE);
}

bool MtAdapter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    if (!has_pending_raw_packet_ || !out_data || max_len == 0)
    {
        return false;
    }

    // Copy the stored raw packet data
    size_t copy_len = (last_raw_packet_len_ < max_len) ? last_raw_packet_len_ : max_len;
    memcpy(out_data, last_raw_packet_, copy_len);
    out_len = copy_len;

    // Mark as consumed
    has_pending_raw_packet_ = false;

    return true;
}

void MtAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    processReceivedPacket(data, size);
}

void MtAdapter::processReceivedPacket(const uint8_t* data, size_t size)
{
    if (!data || size == 0)
    {
        return;
    }

    // Store raw packet data for protocol detection
    if (size <= sizeof(last_raw_packet_))
    {
        memcpy(last_raw_packet_, data, size);
        last_raw_packet_len_ = size;
        has_pending_raw_packet_ = true;
    }

    // Parse wire packet header
    PacketHeaderWire header;
    uint8_t payload[256];
    size_t payload_size = sizeof(payload);

    if (!parseWirePacket(data, size, &header, payload, &payload_size))
    {
        std::string raw_hex = toHex(data, size);
        LORA_LOG("[LORA] RX parse fail len=%u hex=%s\n",
                 (unsigned)size,
                 raw_hex.c_str());
        return;
    }

    std::string full_hex = toHex(data, size, size);
    LORA_LOG("[LORA] RX wire from=%08lX to=%08lX id=%08lX ch=0x%02X flags=0x%02X len=%u\n",
             (unsigned long)header.from,
             (unsigned long)header.to,
             (unsigned long)header.id,
             header.channel,
             header.flags,
             (unsigned)payload_size);
    const char* channel_kind = "UNKNOWN";
    if (header.channel == 0) {
        channel_kind = "PKI";
    } else if (header.channel == primary_channel_hash_) {
        channel_kind = "PRIMARY";
    } else if (header.channel == secondary_channel_hash_) {
        channel_kind = "SECONDARY";
    }
    LORA_LOG("[LORA] RX channel kind=%s hash=0x%02X\n", channel_kind, header.channel);
    LORA_LOG("[LORA] RX full packet hex: %s\n", full_hex.c_str());
    if (header.from == node_id_)
    {
        LORA_LOG("[LORA] RX self drop id=%08lX\n", (unsigned long)header.id);
        return;
    }

    // Check for duplicates
    if (dedup_.isDuplicate(header.from, header.id))
    {
        LORA_LOG("[LORA] RX dedup from=%08lX id=%08lX\n",
                 (unsigned long)header.from,
                 (unsigned long)header.id);
        return; // Duplicate, ignore
    }

    // Mark as seen
    dedup_.markSeen(header.from, header.id);

    // Decrypt payload if needed
    uint8_t plaintext[256];
    size_t plaintext_len = sizeof(plaintext);

    const uint8_t* psk = nullptr;
    size_t psk_len = 0;

    bool unknown_channel = false;

    if (header.channel == 0)
    {
        if (header.to != node_id_ || header.to == 0xFFFFFFFF || payload_size <= 12 || !pki_ready_)
        {
            return;
        }
        if (!decryptPkiPayload(header.from, header.id, payload, payload_size, plaintext, &plaintext_len))
        {
            std::string cipher_hex = toHex(payload, payload_size);
            LORA_LOG("[LORA] RX PKI decrypt fail from=%08lX id=%08lX len=%u hex=%s\n",
                     (unsigned long)header.from,
                     (unsigned long)header.id,
                     (unsigned)payload_size,
                     cipher_hex.c_str());
            return;
        }
    }
    else
    {
        if (header.channel == primary_channel_hash_)
        {
            psk = primary_psk_;
            psk_len = primary_psk_len_;
        }
        else if (header.channel == secondary_channel_hash_)
        {
            psk = secondary_psk_;
            psk_len = secondary_psk_len_;
        }
        else
        {
            std::string cipher_hex = toHex(payload, payload_size);
            LORA_LOG("[LORA] RX unknown channel hash=0x%02X from=%08lX id=%08lX len=%u hex=%s (skip decode)\n",
                     header.channel,
                     (unsigned long)header.from,
                     (unsigned long)header.id,
                     (unsigned)payload_size,
                     cipher_hex.c_str());
            unknown_channel = true;
        }

        if (unknown_channel)
        {
            return;
        }

        if (psk && psk_len > 0)
        {
            if (!decryptPayload(header, payload, payload_size, psk, psk_len, plaintext, &plaintext_len))
            {
                std::string cipher_hex = toHex(payload, payload_size, payload_size);
                LORA_LOG("[LORA] RX decrypt fail id=%08lX ch=0x%02X psk=%u len=%u hex=%s\n",
                         (unsigned long)header.id,
                         header.channel,
                         (unsigned)psk_len,
                         (unsigned)payload_size,
                         cipher_hex.c_str());
                return;
            }
        }
        else
        {
            memcpy(plaintext, payload, payload_size);
            plaintext_len = payload_size;
        }

        // Log decrypted protobuf payload (meshtastic_Data wire format)
        if (plaintext_len > 0)
        {
            std::string protobuf_hex = toHex(plaintext, plaintext_len, plaintext_len);
            LORA_LOG("[LORA] RX protobuf hex: %s\n", protobuf_hex.c_str());
        }
    }

    meshtastic_Data decoded = meshtastic_Data_init_default;
    pb_istream_t stream = pb_istream_from_buffer(plaintext, plaintext_len);
    if (pb_decode(&stream, meshtastic_Data_fields, &decoded))
    {
        LORA_LOG("[LORA] RX data portnum=%u (%s) payload=%u\n",
                 (unsigned)decoded.portnum,
                 portName(decoded.portnum),
                 (unsigned)decoded.payload.size);
        if (decoded.payload.size > 0)
        {
            std::string payload_hex = toHex(decoded.payload.bytes, decoded.payload.size, decoded.payload.size);
            LORA_LOG("[LORA] RX data payload hex: %s\n", payload_hex.c_str());
        }

        if (decoded.portnum == meshtastic_PortNum_NODEINFO_APP && decoded.payload.size > 0)
        {
            meshtastic_User user = meshtastic_User_init_default;
            pb_istream_t ustream = pb_istream_from_buffer(decoded.payload.bytes, decoded.payload.size);
            if (pb_decode(&ustream, meshtastic_User_fields, &user))
            {
                const uint32_t node_id = header.from;
                const char* short_name = user.short_name[0] ? user.short_name : "";
                const char* long_name = user.long_name[0] ? user.long_name : "";
                LORA_LOG("[LORA] RX User from %08lX id='%s' short='%s' long='%s'\n",
                         (unsigned long)node_id, user.id, short_name, long_name);
                if (user.public_key.size == 32)
                {
                    std::array<uint8_t, 32> key{};
                    memcpy(key.data(), user.public_key.bytes, 32);
                    node_public_keys_[node_id] = key;
                    savePkiNodeKey(node_id, key.data(), key.size());
                    LORA_LOG("[LORA] PKI key stored for %08lX\n", (unsigned long)node_id);
                }

                // Publish NodeInfo update event (SNR not available in User message, use 0.0)
                uint32_t now_secs = time(nullptr);
                sys::NodeInfoUpdateEvent* event = new sys::NodeInfoUpdateEvent(
                    node_id, short_name, long_name, 0.0f, now_secs,
                    static_cast<uint8_t>(chat::contacts::NodeProtocolType::Meshtastic));
                sys::EventBus::publish(event, 0);

                if (decoded.want_response)
                {
                    uint32_t now_ms = millis();
                    auto it = nodeinfo_last_seen_ms_.find(node_id);
                    bool allow_reply = true;
                    if (it != nodeinfo_last_seen_ms_.end())
                    {
                        uint32_t since = now_ms - it->second;
                        if (since < NODEINFO_REPLY_SUPPRESS_MS)
                        {
                            allow_reply = false;
                        }
                    }
                    nodeinfo_last_seen_ms_[node_id] = now_ms;
                    if (allow_reply && node_id != node_id_)
                    {
                        sendNodeInfoTo(node_id, false);
                    }
                }
            }
            else
            {
                LORA_LOG("[LORA] RX User decode fail from=%08lX err=%s\n",
                         (unsigned long)header.from,
                         PB_GET_ERROR(&ustream));
                meshtastic_NodeInfo node = meshtastic_NodeInfo_init_default;
                pb_istream_t nstream = pb_istream_from_buffer(decoded.payload.bytes, decoded.payload.size);
                if (pb_decode(&nstream, meshtastic_NodeInfo_fields, &node))
                {
                    uint32_t node_id = node.num ? node.num : header.from;
                    const char* short_name = node.has_user ? node.user.short_name : "";
                    const char* long_name = node.has_user ? node.user.long_name : "";
                    float snr = node.snr; // Get SNR from NodeInfo
                    LORA_LOG("[LORA] RX NodeInfo from %08lX short='%s' long='%s' snr=%.1f\n",
                             (unsigned long)node_id, short_name, long_name, snr);

                    // Publish NodeInfo update event (including SNR)
                    uint32_t now_secs = time(nullptr);
                    sys::NodeInfoUpdateEvent* event = new sys::NodeInfoUpdateEvent(
                        node_id, short_name, long_name, snr, now_secs,
                        static_cast<uint8_t>(chat::contacts::NodeProtocolType::Meshtastic));
                    sys::EventBus::publish(event, 0);
                }
                else
                {
                    LORA_LOG("[LORA] RX NodeInfo decode fail from=%08lX err=%s\n",
                             (unsigned long)header.from,
                             PB_GET_ERROR(&nstream));
                }
            }
        }

        if (decoded.portnum == meshtastic_PortNum_ROUTING_APP && decoded.payload.size > 0)
        {
            meshtastic_Routing routing = meshtastic_Routing_init_default;
            pb_istream_t rstream =
                pb_istream_from_buffer(decoded.payload.bytes, decoded.payload.size);
            if (pb_decode(&rstream, meshtastic_Routing_fields, &routing))
            {
                LORA_LOG("[LORA] RX routing from=%08lX req=%08lX dest=%08lX src=%08lX\n",
                         (unsigned long)header.from,
                         (unsigned long)decoded.request_id,
                         (unsigned long)decoded.dest,
                         (unsigned long)decoded.source);
                if (decoded.request_id != 0 && header.to == node_id_)
                {
                    bool ok = true;
                    if (routing.which_variant == meshtastic_Routing_error_reason_tag &&
                        routing.error_reason != meshtastic_Routing_Error_NONE)
                    {
                        ok = false;
                    }
                    if (routing.which_variant == meshtastic_Routing_error_reason_tag &&
                        (routing.error_reason == meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY ||
                         routing.error_reason == meshtastic_Routing_Error_NO_CHANNEL))
                    {
                        uint32_t now_ms = millis();
                        pki_disabled_until_ms_[header.from] = now_ms + PKI_BACKOFF_MS;
                        sendNodeInfoTo(header.from, true);
                        LORA_LOG("[LORA] PKI backoff for %08lX reason=%s until=%lu\n",
                                 (unsigned long)header.from,
                                 routingErrorName(routing.error_reason),
                                 (unsigned long)(now_ms + PKI_BACKOFF_MS));
                    }
                    pending_ack_ms_.erase(decoded.request_id);
                    LORA_LOG("[LORA] RX ack reason=%u (%s)\n",
                             static_cast<unsigned>(routing.error_reason),
                             routingErrorName(routing.error_reason));
                    LORA_LOG("[LORA] RX ack from=%08lX req=%08lX ok=%d\n",
                             (unsigned long)header.from,
                             (unsigned long)decoded.request_id,
                             ok ? 1 : 0);
                    sys::EventBus::publish(
                        new sys::ChatSendResultEvent(decoded.request_id, ok), 0);
                }
            }
            else
            {
                LORA_LOG("[LORA] RX Routing decode fail from=%08lX err=%s\n",
                         (unsigned long)header.from,
                         PB_GET_ERROR(&rstream));
            }
        }

        bool want_ack_flag = (header.flags & PACKET_FLAGS_WANT_ACK_MASK) != 0;
        bool want_response = decoded.want_response ||
                             (decoded.has_bitfield && ((decoded.bitfield & kBitfieldWantResponseMask) != 0));
        bool to_us = (header.to == node_id_);
        bool is_text_port = (decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
                             decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP);
        bool is_nodeinfo_port = (decoded.portnum == meshtastic_PortNum_NODEINFO_APP);
        ChannelId channel_id =
            (header.channel == secondary_channel_hash_) ? ChannelId::SECONDARY : ChannelId::PRIMARY;
        if (header.channel != 0 && header.from != node_id_)
        {
            node_last_channel_[header.from] = channel_id;
        }
        if ((want_ack_flag || want_response) && to_us && is_text_port)
        {
            if (sendRoutingAck(header.from, header.id, header.channel, psk, psk_len))
            {
                LORA_LOG("[LORA] TX ack to=%08lX req=%08lX\n",
                         (unsigned long)header.from,
                         (unsigned long)header.id);
            }
            else
            {
                LORA_LOG("[LORA] TX ack fail to=%08lX req=%08lX\n",
                         (unsigned long)header.from,
                         (unsigned long)header.id);
            }
        }

        if (!is_text_port && !is_nodeinfo_port && decoded.payload.size > 0)
        {
            MeshIncomingData incoming;
            incoming.portnum = decoded.portnum;
            incoming.from = header.from;
            incoming.to = header.to;
            incoming.packet_id = header.id;
            incoming.channel = channel_id;
            incoming.channel_hash = header.channel;
            incoming.want_response = want_response;
            incoming.payload.assign(decoded.payload.bytes,
                                    decoded.payload.bytes + decoded.payload.size);
            if (app_receive_queue_.size() < MAX_APP_QUEUE)
            {
                app_receive_queue_.push(incoming);
            }
        }
    }
    else
    {
        std::string plain_hex = toHex(plaintext, plaintext_len);
        LORA_LOG("[LORA] RX data decode fail id=%08lX err=%s len=%u hex=%s\n",
                 (unsigned long)header.id,
                 PB_GET_ERROR(&stream),
                 (unsigned)plaintext_len,
                 plain_hex.c_str());
    }

    // Decode Data message
    MeshIncomingText incoming;
    if (decodeTextMessage(plaintext, plaintext_len, &incoming))
    {
        // Fill in packet info from header
        incoming.from = header.from;
        incoming.to = header.to;
        incoming.msg_id = header.id;
        if (header.channel == secondary_channel_hash_)
        {
            incoming.channel = ChannelId::SECONDARY;
        }
        else
        {
            incoming.channel = ChannelId::PRIMARY;
        }
        incoming.hop_limit = header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
        incoming.encrypted = (psk != nullptr && psk_len > 0);

        receive_queue_.push(incoming);
        LORA_LOG("[LORA] RX text from=%08lX id=%08lX ch=%u len=%u\n",
                 (unsigned long)incoming.from,
                 (unsigned long)incoming.msg_id,
                 static_cast<unsigned>(incoming.channel),
                 (unsigned)incoming.text.size());
        if (!incoming.text.empty())
        {
            LORA_LOG("[LORA] RX text msg='%s'\n", incoming.text.c_str());
        }
    }
}

void MtAdapter::processSendQueue()
{
    uint32_t now = millis();

    maybeBroadcastNodeInfo(now);

    for (auto it = pending_ack_ms_.begin(); it != pending_ack_ms_.end();)
    {
        if (now - it->second >= ACK_TIMEOUT_MS)
        {
            LORA_LOG("[LORA] RX ack timeout req=%08lX\n",
                     (unsigned long)it->first);
            sys::EventBus::publish(
                new sys::ChatSendResultEvent(it->first, false), 0);
            it = pending_ack_ms_.erase(it);
            continue;
        }
        ++it;
    }

    if (!send_queue_.empty())
    {
        LORA_LOG("[LORA] TX queue pending=%u\n", (unsigned)send_queue_.size());
    }

    while (!send_queue_.empty())
    {
        PendingSend& pending = send_queue_.front();

        // Check if ready to send
        if (now - pending.last_attempt < RETRY_DELAY_MS && pending.retry_count > 0)
        {
            break; // Wait before retry
        }

        // Try to send
        if (sendPacket(pending))
        {
            // Success, remove from queue
            send_queue_.pop();
        }
        else
        {
            // Failed, retry or drop
            pending.retry_count++;
            pending.last_attempt = now;

            if (pending.retry_count > MAX_RETRIES)
            {
                // Max retries reached, drop
                send_queue_.pop();
            }
            else
            {
                // Will retry later
                break;
            }
        }
    }
}

bool MtAdapter::sendPacket(const PendingSend& pending)
{
    // Create Data message payload
    uint8_t data_buffer[256];
    size_t data_size = sizeof(data_buffer);

    NodeId from_node = node_id_;
    if (!encodeTextMessage(pending.channel, pending.text, from_node,
                           pending.msg_id, data_buffer, &data_size))
    {
        return false;
    }

    // Build full wire packet (like M5Tab5-GPS)
    uint8_t wire_buffer[512];
    size_t wire_size = sizeof(wire_buffer);

    ChannelId channel = pending.channel;
    uint8_t channel_hash =
        (channel == ChannelId::SECONDARY) ? secondary_channel_hash_ : primary_channel_hash_;
    uint8_t hop_limit = config_.hop_limit;
    bool want_ack = false; // Broadcast doesn't need ACK unless PKI direct
    uint32_t dest = (pending.dest != 0) ? pending.dest : 0xFFFFFFFF;

    // Try PKI encryption for direct messages when key is known
    const uint8_t* payload = data_buffer;
    size_t payload_len = data_size;
    const uint8_t* psk = nullptr;
    size_t psk_len = 0;
    bool pki_disabled = false;
    if (dest != 0xFFFFFFFF)
    {
        uint32_t now_ms = millis();
        auto disable_it = pki_disabled_until_ms_.find(dest);
        if (disable_it != pki_disabled_until_ms_.end())
        {
            if (now_ms < disable_it->second)
            {
                pki_disabled = true;
            }
            else
            {
                pki_disabled_until_ms_.erase(disable_it);
            }
        }
    }
    bool use_pki = (dest != 0xFFFFFFFF) && pki_ready_ &&
                   allowPkiForPortnum(pending.portnum) &&
                   !pki_disabled &&
                   (node_public_keys_.find(dest) != node_public_keys_.end());
    uint8_t pki_buf[256];
    size_t pki_len = sizeof(pki_buf);
    if (use_pki && encryptPkiPayload(dest, pending.msg_id, data_buffer, data_size, pki_buf, &pki_len))
    {
        payload = pki_buf;
        payload_len = pki_len;
        channel_hash = 0; // PKI channel
        want_ack = true;
    }
    else
    {
        if (dest != 0xFFFFFFFF && channel == ChannelId::PRIMARY)
        {
            if (pki_disabled)
            {
                uint32_t until_ms = pki_disabled_until_ms_[dest];
                LORA_LOG("[LORA] PKI disabled for %08lX until=%lu\n",
                         (unsigned long)dest,
                         (unsigned long)until_ms);
            }
            auto it = node_last_channel_.find(dest);
            if (it != node_last_channel_.end())
            {
                channel = it->second;
                channel_hash =
                    (channel == ChannelId::SECONDARY) ? secondary_channel_hash_ : primary_channel_hash_;
            }
        }
        // Fallback to PSK
        if (channel == ChannelId::SECONDARY)
        {
            psk = secondary_psk_;
            psk_len = secondary_psk_len_;
        }
        else
        {
            psk = primary_psk_;
            psk_len = primary_psk_len_;
        }
    }

    const char* channel_name =
        (channel == ChannelId::SECONDARY) ? kSecondaryChannelName : kPrimaryChannelName;
    LORA_LOG("[LORA] TX channel name='%s' hash=0x%02X psk=%u pki=%u dest=%08lX\n",
             channel_name,
             channel_hash,
             (unsigned)psk_len,
             use_pki ? 1U : 0U,
             (unsigned long)dest);

    if (!buildWirePacket(payload, payload_len, from_node, pending.msg_id,
                         dest, channel_hash, hop_limit, want_ack,
                         psk, psk_len, wire_buffer, &wire_size))
    {
        return false;
    }
    LORA_LOG("[LORA] TX wire ch=0x%02X hop=%u ack=%d psk=%u wire=%u dest=%08lX\n",
             channel_hash,
             hop_limit,
             want_ack ? 1 : 0,
             (unsigned)psk_len,
             (unsigned)wire_size,
             (unsigned long)dest);
    std::string tx_full_hex = toHex(wire_buffer, wire_size, wire_size);
    LORA_LOG("[LORA] TX full packet hex: %s\n", tx_full_hex.c_str());

    // Send via LoRa using RadioLib
    int state = RADIOLIB_ERR_NONE;

    if (!board_.isHardwareOnline(HW_RADIO_ONLINE))
    {
        return false;
    }

#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    // For now, send directly (in production, use task queue)
    state = board_.radio.transmit(wire_buffer, wire_size);
#else
    // Other radio types - implement as needed
    state = RADIOLIB_ERR_UNSUPPORTED;
#endif

    bool ok = (state == RADIOLIB_ERR_NONE);
    LORA_LOG("[LORA] TX text id=%08lX ch=%u len=%u ok=%d\n",
             (unsigned long)pending.msg_id,
             static_cast<unsigned>(channel),
             (unsigned)wire_size,
             ok ? 1 : 0);
    if (ok && want_ack)
    {
        pending_ack_ms_[pending.msg_id] = millis();
    }
    if (ok)
    {
        startRadioReceive();
    }
    return ok;
}

bool MtAdapter::sendNodeInfo()
{
    if (!ready_)
    {
        return false;
    }

    return sendNodeInfoTo(0xFFFFFFFF, false);
}

bool MtAdapter::sendNodeInfoTo(uint32_t dest, bool want_response)
{
    uint8_t data_buffer[256];
    size_t data_size = sizeof(data_buffer);

    char user_id[16];
    snprintf(user_id, sizeof(user_id), "!%08lX", (unsigned long)node_id_);

    char long_name[32];
    char short_name[5];
    uint16_t suffix = static_cast<uint16_t>(node_id_ & 0x0ffff);
    snprintf(long_name, sizeof(long_name), "lilygo-%04X", suffix);
    snprintf(short_name, sizeof(short_name), "%04X", suffix);

    if (!encodeNodeInfoMessage(
            user_id,
            long_name,
            short_name,
            meshtastic_HardwareModel_T_LORA_PAGER,
            mac_addr_,
            pki_public_key_.data(),
            pki_ready_ ? pki_public_key_.size() : 0,
            want_response,
            data_buffer,
            &data_size))
    {
        return false;
    }

    LORA_LOG("[LORA] NodeInfo user_id=%s short=%s long=%s\n",
             user_id, short_name, long_name);

    uint8_t wire_buffer[512];
    size_t wire_size = sizeof(wire_buffer);

    uint8_t channel_hash = primary_channel_hash_;
    uint8_t hop_limit = config_.hop_limit;
    bool want_ack = want_response && (dest != 0xFFFFFFFF);

    if (!buildWirePacket(data_buffer, data_size, node_id_, next_packet_id_++,
                         dest, channel_hash, hop_limit, want_ack,
                         primary_psk_, primary_psk_len_, wire_buffer, &wire_size))
    {
        return false;
    }
    LORA_LOG("[LORA] TX nodeinfo wire ch=0x%02X hop=%u wire=%u\n",
             channel_hash,
             hop_limit,
             (unsigned)wire_size);
    std::string nodeinfo_full_hex = toHex(wire_buffer, wire_size, wire_size);
    LORA_LOG("[LORA] TX nodeinfo full packet hex: %s\n", nodeinfo_full_hex.c_str());

    if (!board_.isHardwareOnline(HW_RADIO_ONLINE))
    {
        return false;
    }

#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    int state = board_.radio.transmit(wire_buffer, wire_size);
#else
    int state = RADIOLIB_ERR_UNSUPPORTED;
#endif
    bool ok = (state == RADIOLIB_ERR_NONE);
    LORA_LOG("[LORA] TX nodeinfo id=%08lX len=%u ok=%d\n",
             (unsigned long)(next_packet_id_ - 1),
             (unsigned)wire_size,
             ok ? 1 : 0);
    if (ok)
    {
        startRadioReceive();
    }
    return ok;
}

void MtAdapter::maybeBroadcastNodeInfo(uint32_t now_ms)
{
    if (!ready_)
    {
        return;
    }

    if (last_nodeinfo_ms_ == 0 || (now_ms - last_nodeinfo_ms_) >= NODEINFO_INTERVAL_MS)
    {
        if (sendNodeInfo())
        {
            last_nodeinfo_ms_ = now_ms;
        }
    }
}

void MtAdapter::configureRadio()
{
    // Configure LoRa radio based on config_
    // This is a placeholder - actual configuration depends on RadioLib API
    // and Meshtastic region/preset settings

    if (!board_.isHardwareOnline(HW_RADIO_ONLINE))
    {
        ready_ = false;
        return;
    }

    meshtastic_Config_LoRaConfig_RegionCode region_code =
        static_cast<meshtastic_Config_LoRaConfig_RegionCode>(config_.region);
    if (region_code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
    {
        region_code = meshtastic_Config_LoRaConfig_RegionCode_CN;
    }
    const chat::meshtastic::RegionInfo* region = chat::meshtastic::findRegion(region_code);

    meshtastic_Config_LoRaConfig_ModemPreset preset =
        static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(config_.modem_preset);

    float bw_khz = 250.0f;
    uint8_t sf = 11;
    uint8_t cr_denom = 5;
    switch (preset)
    {
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        bw_khz = region->wide_lora ? 1625.0f : 500.0f;
        cr_denom = 5;
        sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
        bw_khz = region->wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        bw_khz = region->wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 8;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        bw_khz = region->wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 9;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        bw_khz = region->wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 10;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
        bw_khz = region->wide_lora ? 406.25f : 125.0f;
        cr_denom = 8;
        sf = 11;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        bw_khz = region->wide_lora ? 406.25f : 125.0f;
        cr_denom = 8;
        sf = 12;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
    default:
        bw_khz = region->wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 11;
        break;
    }

    const char* channel_name = chat::meshtastic::presetDisplayName(preset);
    float freq_mhz = chat::meshtastic::computeFrequencyMhz(region, bw_khz, channel_name);
    if (freq_mhz <= 0.0f)
    {
        freq_mhz = region->freq_start_mhz + (bw_khz / 2000.0f);
    }

#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    board_.radio.setFrequency(freq_mhz);
    board_.radio.setBandwidth(bw_khz);
    board_.radio.setSpreadingFactor(sf);
    board_.radio.setCodingRate(cr_denom);
    board_.radio.setOutputPower(config_.tx_power);
    board_.radio.setPreambleLength(kLoraPreambleLen);
    board_.radio.setSyncWord(kLoraSyncWord);
    board_.radio.setCRC(2);
#endif

    ready_ = true;
    last_nodeinfo_ms_ = 0;
    LORA_LOG("[LORA] adapter ready, node_id=%08lX\n", (unsigned long)node_id_);
    LORA_LOG("[LORA] radio config region=%u preset=%u freq=%.3fMHz sf=%u bw=%.1f cr=4/%u sync=0x%02X preamble=%u\n",
             static_cast<unsigned>(region_code),
             static_cast<unsigned>(preset),
             freq_mhz,
             sf,
             bw_khz,
             cr_denom,
             kLoraSyncWord,
             kLoraPreambleLen);
    startRadioReceive();
}

void MtAdapter::initNodeIdentity()
{
    uint64_t mac = ESP.getEfuseMac();
    for (int i = 0; i < 6; i++)
    {
        mac_addr_[5 - i] = (mac >> (8 * i)) & 0xFF;
    }
    node_id_ = (static_cast<uint32_t>(mac_addr_[2]) << 24) |
               (static_cast<uint32_t>(mac_addr_[3]) << 16) |
               (static_cast<uint32_t>(mac_addr_[4]) << 8) |
               static_cast<uint32_t>(mac_addr_[5]);
}

void MtAdapter::updateChannelKeys()
{
    if (isZeroKey(config_.primary_key, sizeof(config_.primary_key)))
    {
        size_t len = 0;
        expandShortPsk(kDefaultPskIndex, primary_psk_, &len);
        primary_psk_len_ = len;
    }
    else
    {
        memcpy(primary_psk_, config_.primary_key, sizeof(primary_psk_));
        primary_psk_len_ = sizeof(primary_psk_);
    }

    if (isZeroKey(config_.secondary_key, sizeof(config_.secondary_key)))
    {
        secondary_psk_len_ = 0;
        memset(secondary_psk_, 0, sizeof(secondary_psk_));
    }
    else
    {
        memcpy(secondary_psk_, config_.secondary_key, sizeof(secondary_psk_));
        secondary_psk_len_ = sizeof(secondary_psk_);
    }

    primary_channel_hash_ = computeChannelHash(kPrimaryChannelName, primary_psk_, primary_psk_len_);
    secondary_channel_hash_ =
        computeChannelHash(kSecondaryChannelName,
                           (secondary_psk_len_ > 0) ? secondary_psk_ : nullptr,
                           secondary_psk_len_);
    std::string primary_psk_hex = toHex(primary_psk_, primary_psk_len_, primary_psk_len_);
    std::string secondary_psk_hex = toHex(secondary_psk_, secondary_psk_len_, secondary_psk_len_);
    if (primary_psk_hex.empty()) primary_psk_hex = "-";
    if (secondary_psk_hex.empty()) secondary_psk_hex = "-";
    LORA_LOG("[LORA] channel primary='%s' hash=0x%02X psk=%u hex=%s\n",
             kPrimaryChannelName,
             primary_channel_hash_,
             (unsigned)primary_psk_len_,
             primary_psk_hex.c_str());
    LORA_LOG("[LORA] channel secondary='%s' hash=0x%02X psk=%u hex=%s\n",
             kSecondaryChannelName,
             secondary_channel_hash_,
             (unsigned)secondary_psk_len_,
             secondary_psk_hex.c_str());
}

void MtAdapter::startRadioReceive()
{
    if (!board_.isHardwareOnline(HW_RADIO_ONLINE))
    {
        return;
    }
#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    int state = board_.radio.startReceive();
    if (state != RADIOLIB_ERR_NONE)
    {
        LORA_LOG("[LORA] RX start fail state=%d\n", state);
    }
#endif
}

bool MtAdapter::initPkiKeys()
{
    Preferences prefs;
    prefs.begin("chat", false);
    size_t pub_len = prefs.getBytes("pki_pub", pki_public_key_.data(), pki_public_key_.size());
    size_t priv_len = prefs.getBytes("pki_priv", pki_private_key_.data(), pki_private_key_.size());
    bool have_keys = (pub_len == pki_public_key_.size() && priv_len == pki_private_key_.size() &&
                      !isZeroKey(pki_private_key_.data(), pki_private_key_.size()));

    if (!have_keys)
    {
        RNG.begin("trail-mate");
        RNG.stir(mac_addr_, sizeof(mac_addr_));
        uint32_t noise = random();
        RNG.stir(reinterpret_cast<uint8_t*>(&noise), sizeof(noise));

        Curve25519::dh1(pki_public_key_.data(), pki_private_key_.data());
        have_keys = !isZeroKey(pki_private_key_.data(), pki_private_key_.size());
        if (have_keys)
        {
            prefs.putBytes("pki_pub", pki_public_key_.data(), pki_public_key_.size());
            prefs.putBytes("pki_priv", pki_private_key_.data(), pki_private_key_.size());
        }
    }
    prefs.end();

    pki_ready_ = have_keys;
    if (pki_ready_)
    {
        LORA_LOG("[LORA] PKI ready, public key set\n");
    }
    else
    {
        LORA_LOG("[LORA] PKI init failed\n");
    }
    return pki_ready_;
}

void MtAdapter::loadPkiNodeKeys()
{
    Preferences prefs;
    prefs.begin(kPkiPrefsNs, true);
    size_t total = prefs.getBytesLength(kPkiPrefsKey);
    struct PkiKeyEntry {
        uint32_t node_id;
        uint8_t key[32];
    };
    if (total < sizeof(PkiKeyEntry)) {
        prefs.end();
        return;
    }
    size_t count = total / sizeof(PkiKeyEntry);
    if (count > kMaxPkiNodes) {
        count = kMaxPkiNodes;
    }
    std::vector<PkiKeyEntry> entries(count);
    size_t read = prefs.getBytes(kPkiPrefsKey, entries.data(),
                                 entries.size() * sizeof(PkiKeyEntry));
    prefs.end();

    size_t actual = read / sizeof(PkiKeyEntry);
    for (size_t i = 0; i < actual; ++i) {
        if (entries[i].node_id == 0) {
            continue;
        }
        std::array<uint8_t, 32> key{};
        memcpy(key.data(), entries[i].key, sizeof(entries[i].key));
        node_public_keys_[entries[i].node_id] = key;
    }
    if (actual > 0) {
        LORA_LOG("[LORA] PKI keys loaded=%u\n", (unsigned)actual);
    }
}

void MtAdapter::savePkiNodeKey(uint32_t node_id, const uint8_t* key, size_t key_len)
{
    if (node_id == 0 || !key || key_len != 32) {
        return;
    }
    struct PkiKeyEntry {
        uint32_t node_id;
        uint8_t key[32];
    };
    std::vector<PkiKeyEntry> entries;

    Preferences prefs;
    prefs.begin(kPkiPrefsNs, false);
    size_t total = prefs.getBytesLength(kPkiPrefsKey);
    if (total >= sizeof(PkiKeyEntry)) {
        size_t count = total / sizeof(PkiKeyEntry);
        if (count > kMaxPkiNodes) {
            count = kMaxPkiNodes;
        }
        entries.resize(count);
        prefs.getBytes(kPkiPrefsKey, entries.data(),
                       entries.size() * sizeof(PkiKeyEntry));
    }

    bool updated = false;
    for (auto& entry : entries) {
        if (entry.node_id == node_id) {
            memcpy(entry.key, key, 32);
            updated = true;
            break;
        }
    }
    if (!updated) {
        if (entries.size() < kMaxPkiNodes) {
            PkiKeyEntry entry{};
            entry.node_id = node_id;
            memcpy(entry.key, key, 32);
            entries.push_back(entry);
        } else if (!entries.empty()) {
            entries.back().node_id = node_id;
            memcpy(entries.back().key, key, 32);
        }
    }
    if (!entries.empty()) {
        prefs.putBytes(kPkiPrefsKey, entries.data(),
                       entries.size() * sizeof(PkiKeyEntry));
    }
    prefs.end();
}

bool MtAdapter::decryptPkiPayload(uint32_t from, uint32_t packet_id,
                                  const uint8_t* cipher, size_t cipher_len,
                                  uint8_t* out_plain, size_t* out_plain_len)
{
    if (!cipher || cipher_len <= 12 || !out_plain || !out_plain_len)
    {
        return false;
    }
    if (!pki_ready_)
    {
        return false;
    }
    auto it = node_public_keys_.find(from);
    if (it == node_public_keys_.end())
    {
        LORA_LOG("[LORA] PKI key missing for %08lX\n", (unsigned long)from);
        return false;
    }

    uint8_t shared[32];
    uint8_t local_priv[32];
    memcpy(shared, it->second.data(), sizeof(shared));
    memcpy(local_priv, pki_private_key_.data(), sizeof(local_priv));
    if (!Curve25519::dh2(shared, local_priv))
    {
        return false;
    }

    hashSharedKey(shared, sizeof(shared));

    const uint8_t* auth = cipher + (cipher_len - 12);
    uint32_t extra_nonce = 0;
    memcpy(&extra_nonce, auth + 8, sizeof(extra_nonce));

    uint8_t nonce[16];
    uint64_t packet_id64 = static_cast<uint64_t>(packet_id);
    initPkiNonce(from, packet_id64, extra_nonce, nonce);

    size_t plain_len = cipher_len - 12;
    if (*out_plain_len < plain_len)
    {
        *out_plain_len = plain_len;
        return false;
    }

    if (!aes_ccm_ad(shared, sizeof(shared), nonce, 8,
                    cipher, plain_len, nullptr, 0, auth, out_plain))
    {
        return false;
    }

    *out_plain_len = plain_len;
    return true;
}

bool MtAdapter::encryptPkiPayload(uint32_t dest, uint32_t packet_id,
                                  const uint8_t* plain, size_t plain_len,
                                  uint8_t* out_cipher, size_t* out_cipher_len)
{
    if (!plain || !out_cipher || !out_cipher_len) return false;
    if (!pki_ready_) return false;
    auto it = node_public_keys_.find(dest);
    if (it == node_public_keys_.end())
    {
        LORA_LOG("[LORA] PKI key missing for %08lX\n", (unsigned long)dest);
        return false;
    }

    uint8_t shared[32];
    uint8_t local_priv[32];
    memcpy(shared, it->second.data(), sizeof(shared));
    memcpy(local_priv, pki_private_key_.data(), sizeof(local_priv));
    if (!Curve25519::dh2(shared, local_priv))
    {
        return false;
    }
    hashSharedKey(shared, sizeof(shared));

    uint32_t extra_nonce = (uint32_t)random();
    uint8_t nonce[16];
    uint64_t packet_id64 = static_cast<uint64_t>(packet_id);
    initPkiNonce(node_id_, packet_id64, extra_nonce, nonce);

    const size_t m = 8;
    const size_t l = 2;
    size_t needed = plain_len + m + sizeof(extra_nonce);
    if (*out_cipher_len < needed)
    {
        *out_cipher_len = needed;
        return false;
    }

    uint8_t x[kAesBlockSize];
    uint8_t a[kAesBlockSize];
    aes_ccm_auth_start(m, l, nonce, nullptr, 0, plain_len, x);
    aes_ccm_auth(plain, plain_len, x);
    aes_ccm_encr_start(l, nonce, a);
    aes_ccm_encr(l, plain, plain_len, out_cipher, a);
    uint8_t auth[kAesBlockSize];
    aes_ccm_encr_auth(m, x, a, auth);
    memcpy(out_cipher + plain_len, auth, m);
    memcpy(out_cipher + plain_len + m, &extra_nonce, sizeof(extra_nonce));
    *out_cipher_len = needed;
    return true;
}

bool MtAdapter::sendRoutingAck(uint32_t dest, uint32_t request_id, uint8_t channel_hash,
                               const uint8_t* psk, size_t psk_len)
{
    if (!board_.isHardwareOnline(HW_RADIO_ONLINE))
    {
        return false;
    }

    if (channel_hash == 0)
    {
        channel_hash = primary_channel_hash_;
        psk = primary_psk_;
        psk_len = primary_psk_len_;
    }

    meshtastic_Routing routing = meshtastic_Routing_init_default;
    routing.which_variant = meshtastic_Routing_error_reason_tag;
    routing.error_reason = meshtastic_Routing_Error_NONE;

    uint8_t routing_buf[64];
    pb_ostream_t rstream = pb_ostream_from_buffer(routing_buf, sizeof(routing_buf));
    if (!pb_encode(&rstream, meshtastic_Routing_fields, &routing))
    {
        return false;
    }

    meshtastic_Data data = meshtastic_Data_init_default;
    data.portnum = meshtastic_PortNum_ROUTING_APP;
    data.want_response = false;
    data.dest = dest;
    data.source = node_id_;
    data.request_id = request_id;
    data.has_bitfield = true;
    data.bitfield = 0;
    data.payload.size = rstream.bytes_written;
    if (data.payload.size > sizeof(data.payload.bytes))
    {
        return false;
    }
    memcpy(data.payload.bytes, routing_buf, data.payload.size);

    uint8_t data_buf[128];
    pb_ostream_t dstream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
    if (!pb_encode(&dstream, meshtastic_Data_fields, &data))
    {
        return false;
    }

    uint8_t wire_buffer[256];
    size_t wire_size = sizeof(wire_buffer);
    uint8_t hop_limit = 0;
    bool want_ack = false;
    if (!buildWirePacket(data_buf, dstream.bytes_written, node_id_, next_packet_id_++,
                         dest, channel_hash, hop_limit, want_ack,
                         psk, psk_len, wire_buffer, &wire_size))
    {
        return false;
    }

    std::string ack_full_hex = toHex(wire_buffer, wire_size, wire_size);
    LORA_LOG("[LORA] TX ack full packet hex: %s\n", ack_full_hex.c_str());

#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    int state = board_.radio.transmit(wire_buffer, wire_size);
#else
    int state = RADIOLIB_ERR_UNSUPPORTED;
#endif
    if (state == RADIOLIB_ERR_NONE)
    {
        startRadioReceive();
        return true;
    }
    return false;
}

} // namespace meshtastic
} // namespace chat
