#include "chat/linux_raw_lora_mesh_adapter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshtastic/mt_codec_pb.h"
#include "chat/infra/meshtastic/mt_node_payload.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/meshtastic/mt_pki_crypto.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/time_utils.h"
#include "platform/linux/mesh/linux_sqlite_mesh_identity_store.h"
#include "platform/linux/runtime_packet_log.h"
#include "sys/event_bus.h"

#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace trailmate::linux_app
{
namespace
{

constexpr std::uint8_t kMagic0 = 'T';
constexpr std::uint8_t kMagic1 = 'M';
constexpr std::uint8_t kMagic2 = 'L';
constexpr std::uint8_t kMagic3 = '1';
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kBroadcastFlags = 0x01;
constexpr std::size_t kHeaderSize = 26;
constexpr std::size_t kMaxPayloadSize = 190;
constexpr std::uint32_t kBroadcastNodeId = 0xFFFFFFFFUL;
constexpr std::uint8_t kDefaultPskIndex = 1;
constexpr std::uint32_t kRxMonitorHeartbeatSeconds = 15;
constexpr std::uint8_t kBitfieldWantResponseMask = 0x02;
constexpr std::uint32_t kNodeInfoReplySuppressMs = 60000;
constexpr std::uint32_t kKeyVerificationTimeoutMs = 60000;
constexpr std::size_t kMaxPkiNodes = 16;
constexpr std::size_t kPkiKeySize = 32;

struct MeshtasticAirPlan
{
    ::chat::meshtastic::RadioConfig radio{};
    std::uint8_t primary_psk[::chat::kMeshtasticChannelKeyMaxLen]{};
    std::size_t primary_psk_len = 0;
    std::uint8_t secondary_psk[::chat::kMeshtasticChannelKeyMaxLen]{};
    std::size_t secondary_psk_len = 0;
    std::uint8_t primary_channel_hash = 0;
    std::uint8_t secondary_channel_hash = 0;
};

std::uint32_t now_seconds()
{
    using clock = std::chrono::system_clock;
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            clock::now().time_since_epoch())
            .count());
}

bool should_require_direct_pki(std::uint8_t encrypt_mode,
                               std::uint32_t dest_node,
                               std::uint32_t portnum)
{
    return encrypt_mode != 0 && dest_node != kBroadcastNodeId &&
           ::chat::meshtastic::allowPkiForPortnum(portnum);
}

bool secure_random_bytes(std::uint8_t* out, std::size_t len)
{
    if (out == nullptr || len == 0)
    {
        return false;
    }
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (len <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
        RAND_bytes(out, static_cast<int>(len)) == 1)
    {
        return true;
    }
#endif
    std::random_device rd;
    for (std::size_t pos = 0; pos < len;)
    {
        std::uint32_t word = rd();
        const std::size_t chunk = std::min<std::size_t>(sizeof(word), len - pos);
        std::memcpy(out + pos, &word, chunk);
        pos += chunk;
    }
    return true;
}

std::uint32_t random_u32()
{
    std::uint32_t value = 0;
    (void)secure_random_bytes(reinterpret_cast<std::uint8_t*>(&value),
                              sizeof(value));
    if (value == 0)
    {
        value = static_cast<std::uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }
    return value;
}

std::uint64_t random_u64()
{
    std::uint64_t value = 0;
    (void)secure_random_bytes(reinterpret_cast<std::uint8_t*>(&value),
                              sizeof(value));
    if (value == 0)
    {
        value = random_u32();
    }
    return value;
}

std::uint32_t random_packet_id_seed()
{
    std::uint32_t seed = random_u32() & 0x7FFFFFFFUL;
    if (seed == 0)
    {
        seed = 1;
    }
    return seed;
}

std::uint32_t random_security_number()
{
    return (random_u32() % 999999U) + 1U;
}

bool generate_x25519_keypair(std::uint8_t public_key[kPkiKeySize],
                             std::uint8_t private_key[kPkiKeySize])
{
    if (public_key == nullptr || private_key == nullptr)
    {
        return false;
    }
#if defined(TRAIL_MATE_HAS_OPENSSL)
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (ctx == nullptr)
    {
        return false;
    }
    EVP_PKEY* pkey = nullptr;
    bool ok = EVP_PKEY_keygen_init(ctx) == 1 &&
              EVP_PKEY_keygen(ctx, &pkey) == 1 && pkey != nullptr;
    EVP_PKEY_CTX_free(ctx);
    if (!ok)
    {
        if (pkey != nullptr)
        {
            EVP_PKEY_free(pkey);
        }
        return false;
    }

    std::size_t public_len = kPkiKeySize;
    std::size_t private_len = kPkiKeySize;
    ok = EVP_PKEY_get_raw_public_key(pkey, public_key, &public_len) == 1 &&
         EVP_PKEY_get_raw_private_key(pkey, private_key, &private_len) == 1 &&
         public_len == kPkiKeySize && private_len == kPkiKeySize;
    EVP_PKEY_free(pkey);
    return ok;
#else
    (void)public_key;
    (void)private_key;
    return false;
#endif
}

bool derive_x25519_shared_key(const std::uint8_t peer_public_key[kPkiKeySize],
                              const std::uint8_t local_private_key[kPkiKeySize],
                              std::uint8_t out_shared[kPkiKeySize])
{
    if (peer_public_key == nullptr || local_private_key == nullptr ||
        out_shared == nullptr)
    {
        return false;
    }
#if defined(TRAIL_MATE_HAS_OPENSSL)
    EVP_PKEY* local = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_X25519, nullptr, local_private_key, kPkiKeySize);
    EVP_PKEY* peer = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_X25519, nullptr, peer_public_key, kPkiKeySize);
    EVP_PKEY_CTX* ctx = local != nullptr ? EVP_PKEY_CTX_new(local, nullptr)
                                         : nullptr;
    std::size_t shared_len = kPkiKeySize;
    const bool ok =
        local != nullptr && peer != nullptr && ctx != nullptr &&
        EVP_PKEY_derive_init(ctx) == 1 &&
        EVP_PKEY_derive_set_peer(ctx, peer) == 1 &&
        EVP_PKEY_derive(ctx, out_shared, &shared_len) == 1 &&
        shared_len == kPkiKeySize;
    if (ctx != nullptr)
    {
        EVP_PKEY_CTX_free(ctx);
    }
    if (peer != nullptr)
    {
        EVP_PKEY_free(peer);
    }
    if (local != nullptr)
    {
        EVP_PKEY_free(local);
    }
    return ok;
#else
    (void)peer_public_key;
    (void)local_private_key;
    (void)out_shared;
    return false;
#endif
}

void write_u16(std::uint8_t* out, std::uint16_t value)
{
    out[0] = static_cast<std::uint8_t>(value & 0xFF);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void write_u32(std::uint8_t* out, std::uint32_t value)
{
    out[0] = static_cast<std::uint8_t>(value & 0xFF);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    out[2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    out[3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

std::uint16_t read_u16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>(data[0]) |
           (static_cast<std::uint16_t>(data[1]) << 8);
}

std::uint32_t read_u32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

std::string hex_u8(std::uint8_t value)
{
    char buffer[5] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%02X",
                  static_cast<unsigned>(value));
    return buffer;
}

std::string node_hex(std::uint32_t value)
{
    char buffer[9] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%08lX",
                  static_cast<unsigned long>(value));
    return buffer;
}

std::string compact_text(const std::string& text, std::size_t max_len = 80)
{
    if (text.size() <= max_len)
    {
        return text;
    }
    return text.substr(0, max_len) + "...";
}

const char* meshtastic_port_label(std::uint32_t portnum)
{
    switch (static_cast<meshtastic_PortNum>(portnum))
    {
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
        return "TEXT";
    case meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP:
        return "TEXT_COMPRESSED";
    case meshtastic_PortNum_NODEINFO_APP:
        return "NODEINFO";
    case meshtastic_PortNum_POSITION_APP:
        return "POSITION";
    case meshtastic_PortNum_ROUTING_APP:
        return "ROUTING";
    default:
        return "APPDATA";
    }
}

std::string packet_signal_text(
    const ::platform::linux_runtime::Sx126xPacket& packet)
{
    char buffer[96] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "RSSI %.1f dBm / SNR %.1f dB / %.3f MHz / BW %.1f kHz / SF%u / CR 4/%u",
                  static_cast<double>(packet.rssi_dbm),
                  static_cast<double>(packet.snr_db),
                  static_cast<double>(packet.freq_hz) / 1000000.0,
                  static_cast<double>(packet.bw_hz) / 1000.0,
                  static_cast<unsigned>(packet.sf),
                  static_cast<unsigned>(packet.cr));
    return buffer;
}

std::string pb_error_text(const pb_istream_t& stream)
{
    const char* error = PB_GET_ERROR(&stream);
    return (error != nullptr && error[0] != '\0') ? error : "unknown";
}

std::string describe_nodeinfo_payload_failure(const meshtastic_Data& data)
{
    meshtastic_NodeInfo node = meshtastic_NodeInfo_init_default;
    pb_istream_t node_stream =
        pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (pb_decode(&node_stream, meshtastic_NodeInfo_fields, &node))
    {
        return "NodeInfo protobuf decoded but no meaningful node facts were present.";
    }

    const std::string node_error = pb_error_text(node_stream);
    meshtastic_User user = meshtastic_User_init_default;
    pb_istream_t user_stream =
        pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (pb_decode(&user_stream, meshtastic_User_fields, &user))
    {
        return "Legacy User protobuf decoded but no meaningful user facts were present.";
    }

    return "NodeInfo pb=" + node_error +
           " / User pb=" + pb_error_text(user_stream);
}

std::string describe_position_payload_failure(const meshtastic_Data& data)
{
    meshtastic_Position pos = meshtastic_Position_init_zero;
    pb_istream_t stream =
        pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (pb_decode(&stream, meshtastic_Position_fields, &pos))
    {
        return "Position protobuf decoded but latitude/longitude were missing or invalid.";
    }
    return "Position pb=" + pb_error_text(stream);
}

MeshtasticAirPlan build_meshtastic_air_plan(
    const ::chat::MeshConfig& config)
{
    MeshtasticAirPlan plan{};
    plan.radio = ::chat::meshtastic::deriveRadioConfig(config);

    if (::chat::meshtastic::isZeroKey(config.primary_key,
                                      sizeof(config.primary_key)))
    {
        ::chat::meshtastic::expandShortPsk(kDefaultPskIndex,
                                           plan.primary_psk,
                                           &plan.primary_psk_len);
    }
    else
    {
        plan.primary_psk_len = ::chat::normalizeMeshtasticChannelKeyLen(config.primary_key,
                                                                        sizeof(config.primary_key),
                                                                        config.primary_key_len);
        std::memcpy(plan.primary_psk,
                    config.primary_key,
                    plan.primary_psk_len);
    }

    if (!::chat::meshtastic::isZeroKey(config.secondary_key,
                                       sizeof(config.secondary_key)))
    {
        plan.secondary_psk_len = ::chat::normalizeMeshtasticChannelKeyLen(config.secondary_key,
                                                                          sizeof(config.secondary_key),
                                                                          config.secondary_key_len);
        std::memcpy(plan.secondary_psk,
                    config.secondary_key,
                    plan.secondary_psk_len);
    }

    plan.primary_channel_hash = ::chat::meshtastic::computeChannelHash(
        ::chat::meshtastic::primaryChannelName(config),
        plan.primary_psk,
        plan.primary_psk_len);
    plan.secondary_channel_hash = ::chat::meshtastic::computeChannelHash(
        ::chat::meshtastic::secondaryChannelName(config),
        plan.secondary_psk_len > 0 ? plan.secondary_psk : nullptr,
        plan.secondary_psk_len);
    return plan;
}

const std::uint8_t* psk_for_channel(const MeshtasticAirPlan& plan,
                                    ::chat::ChannelId channel,
                                    std::size_t* out_len)
{
    if (channel == ::chat::ChannelId::SECONDARY)
    {
        if (out_len != nullptr)
        {
            *out_len = plan.secondary_psk_len;
        }
        return plan.secondary_psk_len > 0 ? plan.secondary_psk : nullptr;
    }
    if (out_len != nullptr)
    {
        *out_len = plan.primary_psk_len;
    }
    return plan.primary_psk_len > 0 ? plan.primary_psk : nullptr;
}

std::uint8_t channel_hash_for(const MeshtasticAirPlan& plan,
                              ::chat::ChannelId channel)
{
    return channel == ::chat::ChannelId::SECONDARY
               ? plan.secondary_channel_hash
               : plan.primary_channel_hash;
}

bool channel_from_hash(const MeshtasticAirPlan& plan,
                       std::uint8_t hash,
                       ::chat::ChannelId* out)
{
    if (hash == plan.primary_channel_hash)
    {
        if (out != nullptr)
        {
            *out = ::chat::ChannelId::PRIMARY;
        }
        return true;
    }
    if (hash == plan.secondary_channel_hash && plan.secondary_psk_len > 0)
    {
        if (out != nullptr)
        {
            *out = ::chat::ChannelId::SECONDARY;
        }
        return true;
    }
    return false;
}

std::string describe_meshtastic_air_plan(const MeshtasticAirPlan& plan)
{
    const auto* region = ::chat::meshtastic::findRegion(plan.radio.region_code);
    const char* region_label =
        region != nullptr && region->label != nullptr ? region->label : "unknown";
    char buffer[260] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "Meshtastic region %s / preset %s / slot %lu / primary hash %s / secondary hash %s / %.3f MHz / BW %.1f kHz / SF%u / CR 4/%u / sync 0x%02X / preamble %u / TX %d dBm",
                  region_label,
                  plan.radio.channel_name == nullptr ? "Custom"
                                                     : plan.radio.channel_name,
                  static_cast<unsigned long>(plan.radio.channel_slot),
                  hex_u8(plan.primary_channel_hash).c_str(),
                  hex_u8(plan.secondary_channel_hash).c_str(),
                  static_cast<double>(plan.radio.freq_mhz),
                  static_cast<double>(plan.radio.bw_khz),
                  static_cast<unsigned>(plan.radio.sf),
                  static_cast<unsigned>(plan.radio.cr_denom),
                  static_cast<unsigned>(plan.radio.sync_word),
                  static_cast<unsigned>(plan.radio.preamble_len),
                  static_cast<int>(plan.radio.tx_power_dbm));
    return buffer;
}

::chat::RxMeta make_meshtastic_rx_meta(
    const ::chat::meshtastic::PacketHeaderWire& header,
    const ::platform::linux_runtime::Sx126xPacket& packet)
{
    ::chat::RxMeta meta{};
    meta.rx_timestamp_ms = ::sys::millis_now();
    const std::uint32_t epoch = ::chat::now_epoch_seconds();
    if (::chat::is_valid_epoch(epoch))
    {
        meta.rx_timestamp_s = epoch;
        meta.time_source = ::chat::RxTimeSource::DeviceUtc;
    }
    else
    {
        meta.rx_timestamp_s = ::chat::now_uptime_seconds();
        meta.time_source = ::chat::RxTimeSource::Uptime;
    }
    meta.origin = ::chat::RxOrigin::Mesh;
    meta.channel_hash = header.channel;
    meta.wire_flags = header.flags;
    meta.next_hop = header.next_hop;
    meta.relay_node = header.relay_node;
    meta.hop_count = ::chat::meshtastic::computeHopsAway(header.flags);
    meta.hop_limit =
        header.flags & ::chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
    meta.direct = meta.hop_count == 0;
    meta.from_is = false;
    meta.rssi_dbm_x10 =
        static_cast<std::int16_t>(std::lround(packet.rssi_dbm * 10.0f));
    meta.snr_db_x10 =
        static_cast<std::int16_t>(std::lround(packet.snr_db * 10.0f));
    meta.freq_hz = packet.freq_hz;
    meta.bw_hz = packet.bw_hz;
    meta.sf = packet.sf;
    meta.cr = packet.cr;
    return meta;
}

bool aes_ctr_crypt(const std::uint8_t* key,
                   std::size_t key_len,
                   const std::uint8_t* nonce,
                   std::uint8_t* buffer,
                   std::size_t len)
{
    if (key == nullptr || nonce == nullptr || buffer == nullptr || len == 0)
    {
        return false;
    }
#if defined(TRAIL_MATE_HAS_OPENSSL)
    const EVP_CIPHER* cipher = nullptr;
    if (key_len == 16)
    {
        cipher = EVP_aes_128_ctr();
    }
    else if (key_len == 32)
    {
        cipher = EVP_aes_256_ctr();
    }
    if (cipher == nullptr)
    {
        return false;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr)
    {
        return false;
    }
    const std::vector<std::uint8_t> input(buffer, buffer + len);
    int out_len = 0;
    int final_len = 0;
    const bool ok =
        EVP_EncryptInit_ex(ctx, cipher, nullptr, key, nonce) == 1 &&
        EVP_CIPHER_CTX_set_padding(ctx, 0) == 1 &&
        EVP_EncryptUpdate(ctx,
                          buffer,
                          &out_len,
                          input.data(),
                          static_cast<int>(len)) == 1 &&
        EVP_EncryptFinal_ex(ctx, buffer + out_len, &final_len) == 1 &&
        static_cast<std::size_t>(out_len + final_len) == len;
    EVP_CIPHER_CTX_free(ctx);
    return ok;
#else
    (void)key_len;
    return false;
#endif
}

bool meshtastic_crypto_available()
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    return true;
#else
    return false;
#endif
}

void make_meshtastic_nonce(const ::chat::meshtastic::PacketHeaderWire& header,
                           std::uint8_t* out_nonce,
                           std::size_t nonce_len)
{
    if (out_nonce == nullptr || nonce_len < 16)
    {
        return;
    }
    std::memset(out_nonce, 0, nonce_len);
    const std::uint64_t packet_id = static_cast<std::uint64_t>(header.id);
    std::memcpy(out_nonce, &packet_id, sizeof(packet_id));
    std::memcpy(out_nonce + sizeof(packet_id), &header.from, sizeof(header.from));
}

bool parse_meshtastic_wire_packet(
    const std::uint8_t* buffer,
    std::size_t size,
    ::chat::meshtastic::PacketHeaderWire* out_header,
    std::uint8_t* out_payload,
    std::size_t* out_payload_size)
{
    if (buffer == nullptr || out_header == nullptr || out_payload == nullptr ||
        out_payload_size == nullptr ||
        size < sizeof(::chat::meshtastic::PacketHeaderWire))
    {
        return false;
    }
    std::memcpy(out_header,
                buffer,
                sizeof(::chat::meshtastic::PacketHeaderWire));
    const std::size_t payload_size =
        size - sizeof(::chat::meshtastic::PacketHeaderWire);
    if (*out_payload_size < payload_size)
    {
        *out_payload_size = payload_size;
        return false;
    }
    std::memcpy(out_payload,
                buffer + sizeof(::chat::meshtastic::PacketHeaderWire),
                payload_size);
    *out_payload_size = payload_size;
    return true;
}

bool decrypt_meshtastic_payload(
    const ::chat::meshtastic::PacketHeaderWire& header,
    const std::uint8_t* cipher,
    std::size_t cipher_len,
    const std::uint8_t* psk,
    std::size_t psk_len,
    std::uint8_t* out_plaintext,
    std::size_t* out_plain_len)
{
    if (cipher == nullptr || psk == nullptr || out_plaintext == nullptr ||
        out_plain_len == nullptr || cipher_len == 0)
    {
        return false;
    }
    if (*out_plain_len < cipher_len)
    {
        *out_plain_len = cipher_len;
        return false;
    }
    std::memcpy(out_plaintext, cipher, cipher_len);
    std::uint8_t nonce[16] = {};
    make_meshtastic_nonce(header, nonce, sizeof(nonce));
    if (!aes_ctr_crypt(psk, psk_len, nonce, out_plaintext, cipher_len))
    {
        return false;
    }
    *out_plain_len = cipher_len;
    return true;
}

bool build_meshtastic_wire_packet(const std::uint8_t* data_payload,
                                  std::size_t data_len,
                                  std::uint32_t from_node,
                                  std::uint32_t packet_id,
                                  std::uint32_t dest_node,
                                  std::uint8_t channel_hash,
                                  std::uint8_t hop_limit,
                                  bool want_ack,
                                  const std::uint8_t* psk,
                                  std::size_t psk_len,
                                  std::uint8_t* out_buffer,
                                  std::size_t* out_size)
{
    if (data_payload == nullptr || data_len == 0 || out_buffer == nullptr ||
        out_size == nullptr || data_len > 256)
    {
        return false;
    }

    std::uint8_t payload[256] = {};
    std::memcpy(payload, data_payload, data_len);

    ::chat::meshtastic::PacketHeaderWire header{};
    header.to = dest_node;
    header.from = from_node;
    header.id = packet_id;
    const std::uint8_t hop_start = hop_limit;
    header.flags =
        (hop_limit & ::chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK) |
        ((hop_start << ::chat::meshtastic::PACKET_FLAGS_HOP_START_SHIFT) &
         ::chat::meshtastic::PACKET_FLAGS_HOP_START_MASK);
    if (want_ack)
    {
        header.flags |= ::chat::meshtastic::PACKET_FLAGS_WANT_ACK_MASK;
    }
    header.channel = channel_hash;
    header.next_hop = 0;
    header.relay_node = static_cast<std::uint8_t>(from_node & 0xFF);

    if (psk != nullptr && psk_len > 0)
    {
        std::uint8_t nonce[16] = {};
        make_meshtastic_nonce(header, nonce, sizeof(nonce));
        if (!aes_ctr_crypt(psk, psk_len, nonce, payload, data_len))
        {
            return false;
        }
    }

    const std::size_t required =
        sizeof(::chat::meshtastic::PacketHeaderWire) + data_len;
    if (*out_size < required)
    {
        *out_size = required;
        return false;
    }
    std::memcpy(out_buffer,
                &header,
                sizeof(::chat::meshtastic::PacketHeaderWire));
    std::memcpy(out_buffer + sizeof(::chat::meshtastic::PacketHeaderWire),
                payload,
                data_len);
    *out_size = required;
    return true;
}

::platform::linux_runtime::PacketLogEntry make_lora_log(
    ::platform::linux_runtime::PacketLogDirection direction,
    const std::uint8_t* frame,
    std::size_t frame_size,
    const char* title,
    const char* summary)
{
    ::platform::linux_runtime::PacketLogEntry entry{};
    entry.source = ::platform::linux_runtime::PacketLogSource::Lora;
    entry.direction = direction;
    entry.title = title == nullptr ? "LoRa frame" : title;
    entry.summary = summary == nullptr ? "" : summary;
    entry.raw_hex = ::platform::linux_runtime::hex_bytes(frame, frame_size);
    const std::size_t header_len = std::min<std::size_t>(kHeaderSize, frame_size);
    if (header_len > 0)
    {
        entry.segments.push_back({
            .kind = ::platform::linux_runtime::PacketLogSegmentKind::Header,
            .label = "head",
            .text = ::platform::linux_runtime::hex_bytes(frame, header_len),
        });
    }
    if (frame_size > kHeaderSize)
    {
        entry.segments.push_back({
            .kind = ::platform::linux_runtime::PacketLogSegmentKind::Body,
            .label = "body",
            .text = ::platform::linux_runtime::hex_bytes(
                frame + kHeaderSize, frame_size - kHeaderSize),
        });
    }
    entry.segments.push_back({
        .kind = ::platform::linux_runtime::PacketLogSegmentKind::Checksum,
        .label = "crc",
        .text = "radio",
    });
    return entry;
}

::platform::linux_runtime::PacketLogEntry make_meshtastic_wire_log(
    ::platform::linux_runtime::PacketLogDirection direction,
    const std::uint8_t* frame,
    std::size_t frame_size,
    const char* title,
    const char* summary)
{
    ::platform::linux_runtime::PacketLogEntry entry{};
    entry.source = ::platform::linux_runtime::PacketLogSource::Lora;
    entry.direction = direction;
    entry.title = title == nullptr ? "Meshtastic air frame" : title;
    entry.summary = summary == nullptr ? "" : summary;
    entry.raw_hex = ::platform::linux_runtime::hex_bytes(frame, frame_size);

    const std::size_t header_len =
        std::min<std::size_t>(sizeof(::chat::meshtastic::PacketHeaderWire),
                              frame_size);
    if (header_len > 0)
    {
        entry.segments.push_back({
            .kind = ::platform::linux_runtime::PacketLogSegmentKind::Header,
            .label = "air head",
            .text = ::platform::linux_runtime::hex_bytes(frame, header_len),
        });
    }
    if (frame_size > header_len)
    {
        entry.segments.push_back({
            .kind = ::platform::linux_runtime::PacketLogSegmentKind::Body,
            .label = "cipher/data",
            .text = ::platform::linux_runtime::hex_bytes(frame + header_len,
                                                         frame_size - header_len),
        });
    }
    entry.segments.push_back({
        .kind = ::platform::linux_runtime::PacketLogSegmentKind::Checksum,
        .label = "crc",
        .text = "SX1262",
    });
    return entry;
}

void append_lora_system_log(const char* title,
                            const std::string& summary,
                            const std::vector<
                                ::platform::linux_runtime::PacketLogSegment>&
                                segments = {})
{
    ::platform::linux_runtime::PacketLogEntry entry{};
    entry.source = ::platform::linux_runtime::PacketLogSource::Lora;
    entry.direction = ::platform::linux_runtime::PacketLogDirection::System;
    entry.title = title == nullptr ? "LoRa" : title;
    entry.summary = summary;
    entry.segments = segments;
    ::platform::linux_runtime::append_packet_log(std::move(entry));
}

void append_mqtt_system_log(const char* title,
                            const std::string& summary,
                            const std::vector<
                                ::platform::linux_runtime::PacketLogSegment>&
                                segments = {})
{
    ::platform::linux_runtime::PacketLogEntry entry{};
    entry.source = ::platform::linux_runtime::PacketLogSource::Mqtt;
    entry.direction = ::platform::linux_runtime::PacketLogDirection::System;
    entry.title = title == nullptr ? "MQTT" : title;
    entry.summary = summary;
    entry.segments = segments;
    ::platform::linux_runtime::append_packet_log(std::move(entry));
}

::platform::linux_runtime::Sx126xLoRaConfig to_sx126x_lora_config(
    const ::chat::meshtastic::RadioConfig& config)
{
    auto out =
        ::platform::linux_runtime::Sx126xRadio::defaultLoRaConfigFromEnvironment();
    out.freq_mhz = config.freq_mhz;
    out.bw_khz = config.bw_khz;
    out.sf = config.sf;
    out.cr = config.cr_denom;
    out.tx_power_dbm = config.tx_power_dbm;
    out.preamble_len = config.preamble_len;
    out.sync_word = config.sync_word;
    out.crc_len = config.crc_len;
    return out;
}

::platform::linux_runtime::Sx126xLoRaConfig derive_radio_config(
    ::chat::MeshProtocol protocol,
    const ::chat::MeshConfig& config)
{
    if (protocol == ::chat::MeshProtocol::Meshtastic)
    {
        return to_sx126x_lora_config(
            ::chat::meshtastic::deriveRadioConfig(config));
    }

    auto out = ::platform::linux_runtime::Sx126xRadio::
        defaultLoRaConfigFromEnvironment();
    if (config.override_frequency_mhz > 0.0f)
    {
        out.freq_mhz = config.override_frequency_mhz;
    }
    else if (config.meshcore_freq_mhz > 0.0f &&
             config.meshcore_freq_mhz < 1000.0f)
    {
        out.freq_mhz = config.meshcore_freq_mhz;
    }
    out.freq_mhz += config.frequency_offset_mhz;
    if (config.bandwidth_khz > 0.0f)
    {
        out.bw_khz = config.bandwidth_khz;
    }
    if (config.spread_factor >= 5 && config.spread_factor <= 12)
    {
        out.sf = config.spread_factor;
    }
    if (config.coding_rate >= 5 && config.coding_rate <= 8)
    {
        out.cr = config.coding_rate;
    }
    if (config.tx_power != 0)
    {
        out.tx_power_dbm = std::clamp<std::int8_t>(config.tx_power, -9, 22);
    }
    return out;
}

std::string describe_radio_config(
    ::chat::MeshProtocol protocol,
    const ::platform::linux_runtime::Sx126xLoRaConfig& config)
{
    char buffer[192] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%s %.3f MHz / BW %.1f kHz / SF%u / CR 4/%u / sync 0x%02X / preamble %u / TX %d dBm",
                  ::chat::infra::meshProtocolName(protocol),
                  static_cast<double>(config.freq_mhz),
                  static_cast<double>(config.bw_khz),
                  static_cast<unsigned>(config.sf),
                  static_cast<unsigned>(config.cr),
                  static_cast<unsigned>(config.sync_word),
                  static_cast<unsigned>(config.preamble_len),
                  static_cast<int>(config.tx_power_dbm));
    return buffer;
}

std::string describe_active_radio_config(
    ::chat::MeshProtocol protocol,
    const ::chat::MeshConfig& mesh_config,
    const ::platform::linux_runtime::Sx126xLoRaConfig& config)
{
    if (protocol == ::chat::MeshProtocol::Meshtastic)
    {
        return describe_meshtastic_air_plan(
            build_meshtastic_air_plan(mesh_config));
    }
    return describe_radio_config(protocol, config);
}

std::string describe_radio_stats(
    const ::platform::linux_runtime::Sx126xRadioStats& stats)
{
    char buffer[224] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "rx=%lu tx=%lu crc=%lu header=%lu timeout=%lu invalid=%lu read=%lu irq=0x%04lX",
                  static_cast<unsigned long>(stats.rx_packets),
                  static_cast<unsigned long>(stats.tx_packets),
                  static_cast<unsigned long>(stats.rx_crc_errors),
                  static_cast<unsigned long>(stats.rx_header_errors),
                  static_cast<unsigned long>(stats.rx_timeouts),
                  static_cast<unsigned long>(stats.rx_invalid_lengths),
                  static_cast<unsigned long>(stats.rx_read_errors),
                  static_cast<unsigned long>(stats.last_irq_flags));
    return buffer;
}

std::string format_irq_flags(std::uint32_t flags)
{
    char buffer[9] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%04lX",
                  static_cast<unsigned long>(flags));
    return std::string("0x") + buffer;
}

void append_meshtastic_air_log(
    const ::platform::linux_runtime::Sx126xPacket& packet)
{
    if (packet.size < sizeof(::chat::meshtastic::PacketHeaderWire))
    {
        append_lora_system_log(
            "Meshtastic RX ignored",
            "Packet is shorter than the Meshtastic air header.",
            {{
                .kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                .label = "len",
                .text = std::to_string(packet.size),
            }});
        return;
    }

    ::chat::meshtastic::PacketHeaderWire header{};
    std::memcpy(&header, packet.data.data(), sizeof(header));

    char summary[192] = {};
    std::snprintf(summary,
                  sizeof(summary),
                  "Meshtastic wire packet from %08lX to %08lX id %08lX channel 0x%02X.",
                  static_cast<unsigned long>(header.from),
                  static_cast<unsigned long>(header.to),
                  static_cast<unsigned long>(header.id),
                  static_cast<unsigned>(header.channel));

    char head[160] = {};
    std::snprintf(head,
                  sizeof(head),
                  "to=%08lX from=%08lX id=%08lX flags=0x%02X ch=0x%02X next=%u relay=%u",
                  static_cast<unsigned long>(header.to),
                  static_cast<unsigned long>(header.from),
                  static_cast<unsigned long>(header.id),
                  static_cast<unsigned>(header.flags),
                  static_cast<unsigned>(header.channel),
                  static_cast<unsigned>(header.next_hop),
                  static_cast<unsigned>(header.relay_node));

    const std::size_t header_len = sizeof(header);
    const std::size_t body_len = packet.size > header_len
                                     ? packet.size - header_len
                                     : 0U;
    append_lora_system_log(
        "Meshtastic air RX",
        summary,
        {{
             .kind = ::platform::linux_runtime::PacketLogSegmentKind::Header,
             .label = "head",
             .text = head,
         },
         {
             .kind = ::platform::linux_runtime::PacketLogSegmentKind::Body,
             .label = "payload",
             .text = ::platform::linux_runtime::hex_bytes(
                 packet.data.data() + header_len, body_len),
         },
         {
             .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
             .label = "decode",
             .text = "air header parsed; payload decoder not yet bound",
         }});
}

} // namespace

LinuxRawLoraMeshAdapter::LinuxRawLoraMeshAdapter(::chat::NodeId self_node_id)
    : radio_(::platform::linux_runtime::Sx126xRadio::instance()),
      self_node_id_(self_node_id)
{
    next_msg_id_ = random_packet_id_seed();
    (void)initPkiKeys();
    loadPkiNodeKeys();
}

bool LinuxRawLoraMeshAdapter::hardwareCandidatePresent()
{
    return ::platform::linux_runtime::Sx126xRadio::hardwareCandidatePresent();
}

bool LinuxRawLoraMeshAdapter::begin()
{
    return ensureRadioReady();
}

void LinuxRawLoraMeshAdapter::tick()
{
    if (!ensureRadioReady())
    {
        return;
    }
    for (int i = 0; i < 4; ++i)
    {
        ::platform::linux_runtime::Sx126xPacket packet{};
        if (!radio_.pollReceive(&packet))
        {
            break;
        }
        const bool meshtastic =
            active_protocol_ == ::chat::MeshProtocol::Meshtastic;
        ::platform::linux_runtime::append_packet_log(
            meshtastic
                ? make_meshtastic_wire_log(
                      ::platform::linux_runtime::PacketLogDirection::Rx,
                      packet.data.data(),
                      packet.size,
                      "Meshtastic raw RX",
                      packet_signal_text(packet).c_str())
                : make_lora_log(
                      ::platform::linux_runtime::PacketLogDirection::Rx,
                      packet.data.data(),
                      packet.size,
                      "LoRa raw RX",
                      "SX1262 packet received."));
        raw_packets_.push_back(packet);
        const bool parsed_trailmate = parseFrame(packet);
        if (!parsed_trailmate && meshtastic &&
            !parseMeshtasticPacket(packet))
        {
            append_meshtastic_air_log(packet);
        }
    }
    logRadioStatsChanges();
    logRxMonitorHeartbeat();
}

bool LinuxRawLoraMeshAdapter::takePendingSendResult(::chat::MessageId& out_msg_id,
                                                    bool& out_ok)
{
    if (pending_results_.empty())
    {
        return false;
    }
    const PendingResult result = pending_results_.front();
    pending_results_.pop_front();
    out_msg_id = result.msg_id;
    out_ok = result.ok;
    return true;
}

::chat::MeshCapabilities LinuxRawLoraMeshAdapter::getCapabilities() const
{
    return {
        .supports_unicast_text = true,
        .supports_unicast_appdata = true,
        .supports_broadcast_appdata = true,
        .supports_appdata_ack = false,
        .provides_appdata_sender = true,
        .supports_node_info = true,
        .supports_pki = pki_ready_,
        .supports_discovery_actions = false,
    };
}

bool LinuxRawLoraMeshAdapter::sendText(::chat::ChannelId channel,
                                       const std::string& text,
                                       ::chat::MessageId* out_msg_id,
                                       ::chat::NodeId peer)
{
    return sendTextWithId(channel, text, 0, out_msg_id, peer);
}

bool LinuxRawLoraMeshAdapter::sendTextWithId(::chat::ChannelId channel,
                                             const std::string& text,
                                             ::chat::MessageId forced_msg_id,
                                             ::chat::MessageId* out_msg_id,
                                             ::chat::NodeId peer)
{
    if (text.empty())
    {
        return false;
    }
    const ::chat::MessageId msg_id =
        forced_msg_id != 0 ? forced_msg_id : nextMessageId();
    if (out_msg_id != nullptr)
    {
        *out_msg_id = msg_id;
    }
    if (forced_msg_id != 0 && forced_msg_id >= next_msg_id_)
    {
        next_msg_id_ = forced_msg_id + 1U;
        if (next_msg_id_ == 0)
        {
            next_msg_id_ = 1;
        }
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    const ::chat::NodeId dest = peer == 0 ? kBroadcastNodeId : peer;
    const bool ok =
        active_protocol_ == ::chat::MeshProtocol::Meshtastic
            ? sendMeshtasticPayload(channel,
                                    dest,
                                    msg_id,
                                    meshtastic_PortNum_TEXT_MESSAGE_APP,
                                    bytes,
                                    text.size(),
                                    false,
                                    false)
            : sendFrame(PacketKind::Text,
                        channel,
                        dest,
                        msg_id,
                        0,
                        bytes,
                        text.size());
    if (msg_id != 0)
    {
        pending_results_.push_back({.msg_id = msg_id, .ok = ok});
    }
    return ok;
}

bool LinuxRawLoraMeshAdapter::pollIncomingText(::chat::MeshIncomingText* out)
{
    tick();
    if (incoming_text_.empty())
    {
        return false;
    }
    if (out != nullptr)
    {
        *out = incoming_text_.front();
    }
    incoming_text_.pop_front();
    return true;
}

bool LinuxRawLoraMeshAdapter::sendAppData(::chat::ChannelId channel,
                                          std::uint32_t portnum,
                                          const std::uint8_t* payload,
                                          std::size_t len,
                                          ::chat::NodeId dest,
                                          bool want_ack,
                                          ::chat::MessageId packet_id,
                                          bool want_response)
{
    if (payload == nullptr || len == 0)
    {
        return false;
    }
    const ::chat::NodeId effective_dest = dest == 0 ? kBroadcastNodeId : dest;
    const ::chat::MessageId effective_id =
        packet_id != 0 ? packet_id : nextMessageId();
    if (packet_id != 0 && packet_id >= next_msg_id_)
    {
        next_msg_id_ = packet_id + 1U;
        if (next_msg_id_ == 0)
        {
            next_msg_id_ = 1;
        }
    }
    if (active_protocol_ == ::chat::MeshProtocol::Meshtastic)
    {
        return sendMeshtasticPayload(channel,
                                     effective_dest,
                                     effective_id,
                                     portnum,
                                     payload,
                                     len,
                                     want_ack,
                                     want_response);
    }
    return sendFrame(PacketKind::AppData,
                     channel,
                     effective_dest,
                     effective_id,
                     portnum,
                     payload,
                     len);
}

bool LinuxRawLoraMeshAdapter::pollIncomingData(::chat::MeshIncomingData* out)
{
    tick();
    if (incoming_data_.empty())
    {
        return false;
    }
    if (out != nullptr)
    {
        *out = incoming_data_.front();
    }
    incoming_data_.pop_front();
    return true;
}

bool LinuxRawLoraMeshAdapter::requestNodeInfo(::chat::NodeId dest,
                                              bool want_response)
{
    if (active_protocol_ != ::chat::MeshProtocol::Meshtastic)
    {
        append_lora_system_log(
            "NodeInfo request unsupported",
            "Linux raw LoRa NodeInfo exchange is currently Meshtastic only.");
        return false;
    }

    const ::chat::NodeId target = dest == 0 ? kBroadcastNodeId : dest;
    return sendMeshtasticNodeInfoTo(target,
                                    want_response,
                                    ::chat::ChannelId::PRIMARY);
}

bool LinuxRawLoraMeshAdapter::startKeyVerification(::chat::NodeId node_id)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::Idle ||
        node_id == 0 || node_id == kBroadcastNodeId)
    {
        return false;
    }
    if (!pki_ready_ || node_public_keys_.find(node_id) == node_public_keys_.end())
    {
        append_lora_system_log(
            "Key verification unavailable",
            "PKI is not ready or the remote public key has not been exchanged yet.");
        return false;
    }

    kv_remote_node_ = node_id;
    kv_nonce_ = random_u64();
    kv_nonce_ms_ = ::sys::millis_now();
    kv_security_number_ = 0;
    kv_hash1_.fill(0);
    kv_hash2_.fill(0);

    meshtastic_KeyVerification init = meshtastic_KeyVerification_init_zero;
    init.nonce = kv_nonce_;
    init.hash1.size = 0;
    init.hash2.size = 0;

    if (!sendKeyVerificationPacket(kv_remote_node_, init, true))
    {
        resetKeyVerificationState();
        return false;
    }

    kv_state_ = KeyVerificationState::SenderInitiated;
    return true;
}

bool LinuxRawLoraMeshAdapter::submitKeyVerificationNumber(
    ::chat::NodeId node_id,
    std::uint64_t nonce,
    std::uint32_t number)
{
    return processKeyVerificationNumber(node_id, nonce, number);
}

void LinuxRawLoraMeshAdapter::applyConfig(const ::chat::MeshConfig& config)
{
    applyProtocolConfig(active_protocol_, config);
}

void LinuxRawLoraMeshAdapter::applyProtocolConfig(
    ::chat::MeshProtocol protocol,
    const ::chat::MeshConfig& config)
{
    active_protocol_ = protocol;
    config_ = config;
    tx_enabled_ = config.tx_enabled;
    if (!started_)
    {
        (void)ensureRadioReady();
        return;
    }
    if (started_)
    {
        const auto radio_config =
            derive_radio_config(active_protocol_, config_);
        if (radio_.configureLoRa(radio_config))
        {
            last_rx_monitor_log_s_ = 0;
            stats_logged_ = false;
            last_logged_stats_ = {};
            append_lora_system_log(
                "LoRa radio configured",
                describe_active_radio_config(active_protocol_,
                                             config_,
                                             radio_config));
        }
        else
        {
            append_lora_system_log(
                "LoRa radio config failed",
                radio_.lastError(),
                {{
                    .kind =
                        ::platform::linux_runtime::PacketLogSegmentKind::Error,
                    .label = "driver",
                    .text = radio_.lastError(),
                }});
        }
    }
}

void LinuxRawLoraMeshAdapter::setUserInfo(const char* long_name,
                                          const char* short_name)
{
    long_name_ = long_name == nullptr ? "" : long_name;
    short_name_ = short_name == nullptr ? "" : short_name;
}

bool LinuxRawLoraMeshAdapter::isPkiReady() const
{
    return pki_ready_;
}

bool LinuxRawLoraMeshAdapter::hasPkiKey(::chat::NodeId dest) const
{
    return dest != 0 && node_public_keys_.find(dest) != node_public_keys_.end();
}

void LinuxRawLoraMeshAdapter::setNetworkLimits(bool duty_cycle_enabled,
                                               std::uint8_t util_percent)
{
    duty_cycle_enabled_ = duty_cycle_enabled;
    channel_util_percent_ = util_percent;
}

void LinuxRawLoraMeshAdapter::setPrivacyConfig(std::uint8_t encrypt_mode)
{
    encrypt_mode_ = std::min<std::uint8_t>(encrypt_mode, 2);
}

::chat::NodeId LinuxRawLoraMeshAdapter::getNodeId() const
{
    return self_node_id_;
}

bool LinuxRawLoraMeshAdapter::isReady() const
{
    return started_ && radio_.isOnline();
}

bool LinuxRawLoraMeshAdapter::pollIncomingRawPacket(std::uint8_t* out_data,
                                                    std::size_t& out_len,
                                                    std::size_t max_len)
{
    tick();
    if (raw_packets_.empty())
    {
        return false;
    }
    const auto packet = raw_packets_.front();
    raw_packets_.pop_front();
    out_len = std::min(max_len, packet.size);
    if (out_data != nullptr && out_len > 0)
    {
        std::memcpy(out_data, packet.data.data(), out_len);
    }
    return true;
}

void LinuxRawLoraMeshAdapter::processSendQueue()
{
    tick();
}

void LinuxRawLoraMeshAdapter::setSelfNodeId(::chat::NodeId id)
{
    self_node_id_ = id;
}

std::string LinuxRawLoraMeshAdapter::statusText() const
{
    if (started_ && radio_.isOnline())
    {
        return "SX1262 raw LoRa transport ready.";
    }
    return last_status_;
}

std::string LinuxRawLoraMeshAdapter::radioConfigText() const
{
    const auto config =
        started_ && radio_.isOnline()
            ? radio_.appliedLoRaConfig()
            : derive_radio_config(active_protocol_, config_);
    return describe_active_radio_config(active_protocol_, config_, config);
}

std::string LinuxRawLoraMeshAdapter::radioStatsText() const
{
    return describe_radio_stats(radio_.stats());
}

std::vector<std::string> LinuxRawLoraMeshAdapter::diagnosticLines() const
{
    std::vector<std::string> out;
    out.push_back(statusText());
    out.push_back("RF: " + radioConfigText());
    out.push_back("Counters: " + radioStatsText());
    const char* error = radio_.lastError();
    if (error != nullptr && error[0] != '\0')
    {
        out.push_back(std::string("Driver: ") + error);
    }
    return out;
}

void LinuxRawLoraMeshAdapter::logStatusIfChanged(const char* title,
                                                 const std::string& status)
{
    if (status.empty() || status == last_logged_status_)
    {
        return;
    }
    last_logged_status_ = status;
    append_lora_system_log(title == nullptr ? "LoRa status" : title, status);
}

void LinuxRawLoraMeshAdapter::logRadioStatsChanges()
{
    const auto stats = radio_.stats();
    if (!stats_logged_)
    {
        last_logged_stats_ = stats;
        stats_logged_ = true;
        return;
    }

    const bool error_changed =
        stats.rx_crc_errors != last_logged_stats_.rx_crc_errors ||
        stats.rx_header_errors != last_logged_stats_.rx_header_errors ||
        stats.rx_timeouts != last_logged_stats_.rx_timeouts ||
        stats.rx_invalid_lengths != last_logged_stats_.rx_invalid_lengths ||
        stats.rx_read_errors != last_logged_stats_.rx_read_errors;
    const bool traffic_changed =
        stats.rx_packets != last_logged_stats_.rx_packets ||
        stats.tx_packets != last_logged_stats_.tx_packets;
    if (!error_changed && !traffic_changed)
    {
        return;
    }

    if (error_changed)
    {
        append_lora_system_log(
            "LoRa RX diagnostic",
            describe_radio_stats(stats),
            {{
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                 .label = "irq",
                 .text = format_irq_flags(stats.last_irq_flags),
             },
             {
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                 .label = "crc/header/timeout",
                 .text = std::to_string(stats.rx_crc_errors) + "/" +
                         std::to_string(stats.rx_header_errors) + "/" +
                         std::to_string(stats.rx_timeouts),
             },
             {
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                 .label = "invalid/read",
                 .text = std::to_string(stats.rx_invalid_lengths) + "/" +
                         std::to_string(stats.rx_read_errors),
             }});
    }
    else if (traffic_changed)
    {
        append_lora_system_log(
            "LoRa traffic counters",
            describe_radio_stats(stats),
            {{
                .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                .label = "irq",
                .text = format_irq_flags(stats.last_irq_flags),
            }});
    }
    last_logged_stats_ = stats;
}

void LinuxRawLoraMeshAdapter::logRxMonitorHeartbeat()
{
    if (!started_ || !radio_.isOnline())
    {
        return;
    }
    const std::uint32_t now = now_seconds();
    if (last_rx_monitor_log_s_ != 0 &&
        now - last_rx_monitor_log_s_ < kRxMonitorHeartbeatSeconds)
    {
        return;
    }
    last_rx_monitor_log_s_ = now;
    const auto stats = radio_.stats();
    append_lora_system_log(
        stats.rx_packets == 0 ? "LoRa RX monitor"
                              : "LoRa RX monitor heartbeat",
        describe_active_radio_config(active_protocol_,
                                     config_,
                                     stats.lora_config) +
            " / " + describe_radio_stats(stats),
        {{
            .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
            .label = "irq",
            .text = format_irq_flags(stats.last_irq_flags),
        }});
}

bool LinuxRawLoraMeshAdapter::ensureRadioReady()
{
    if (started_ && radio_.isOnline())
    {
        return true;
    }
    if (!hardwareCandidatePresent())
    {
        last_status_ = "No Linux LoRa SPI endpoint is present.";
        logStatusIfChanged("LoRa endpoint missing", last_status_);
        return false;
    }
    if (!radio_.acquire())
    {
        last_status_ = std::string("SX1262 acquire failed: ") +
                       radio_.lastError();
        logStatusIfChanged("LoRa acquire failed", last_status_);
        return false;
    }
    const auto radio_config = derive_radio_config(active_protocol_, config_);
    if (!radio_.configureLoRa(radio_config))
    {
        last_status_ = std::string("SX1262 LoRa config failed: ") +
                       radio_.lastError();
        logStatusIfChanged("LoRa radio config failed", last_status_);
        return false;
    }
    started_ = true;
    last_rx_monitor_log_s_ = 0;
    stats_logged_ = false;
    last_logged_stats_ = {};
    last_status_ = "SX1262 raw LoRa transport ready.";
    append_lora_system_log("LoRa radio configured",
                           describe_active_radio_config(active_protocol_,
                                                        config_,
                                                        radio_config));
    return true;
}

::chat::MessageId LinuxRawLoraMeshAdapter::nextMessageId()
{
    if (next_msg_id_ == 0)
    {
        next_msg_id_ = random_packet_id_seed();
    }
    const std::uint32_t msg_id = next_msg_id_++;
    if (next_msg_id_ == 0)
    {
        next_msg_id_ = 1;
    }
    return msg_id == 0 ? nextMessageId() : msg_id;
}

bool LinuxRawLoraMeshAdapter::sendMeshtasticPayload(
    ::chat::ChannelId channel,
    ::chat::NodeId dest,
    ::chat::MessageId msg_id,
    std::uint32_t portnum,
    const std::uint8_t* payload,
    std::size_t len,
    bool want_ack,
    bool want_response)
{
    if (!tx_enabled_ || payload == nullptr || len == 0 || !ensureRadioReady())
    {
        return false;
    }

    std::uint8_t data_payload[256] = {};
    std::size_t data_size = sizeof(data_payload);
    bool encoded = false;
    const bool data_want_response = want_response || want_ack;
    if (portnum == meshtastic_PortNum_TEXT_MESSAGE_APP)
    {
        const std::string text(reinterpret_cast<const char*>(payload), len);
        encoded = ::chat::meshtastic::encodeTextMessage(channel,
                                                        text,
                                                        self_node_id_,
                                                        msg_id,
                                                        dest,
                                                        data_payload,
                                                        &data_size);
    }
    else
    {
        encoded = ::chat::meshtastic::encodeAppData(portnum,
                                                    payload,
                                                    len,
                                                    data_want_response,
                                                    data_payload,
                                                    &data_size);
    }
    if (!encoded)
    {
        append_lora_system_log(
            "Meshtastic TX encode failed",
            std::string("port ") + meshtastic_port_label(portnum) +
                " len " + std::to_string(len),
            {{
                .kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                .label = "protobuf",
                .text = "Data payload could not be encoded",
            }});
        return false;
    }

    const MeshtasticAirPlan plan = build_meshtastic_air_plan(config_);
    std::size_t psk_len = 0;
    const std::uint8_t* psk = psk_for_channel(plan, channel, &psk_len);
    std::uint8_t channel_hash = channel_hash_for(plan, channel);
    const std::uint8_t* wire_payload = data_payload;
    std::size_t wire_payload_size = data_size;
    std::uint8_t pki_payload[256] = {};
    bool use_pki = false;
    bool air_want_ack =
        ::chat::meshtastic::shouldSetAirWantAck(dest, want_ack);

    if (should_require_direct_pki(encrypt_mode_, dest, portnum))
    {
        const bool have_dest_key =
            node_public_keys_.find(dest) != node_public_keys_.end();
        if (!pki_ready_ || !have_dest_key)
        {
            append_lora_system_log(
                "Meshtastic direct TX blocked",
                std::string("PKI is required for direct ") +
                    meshtastic_port_label(portnum) + " to " +
                    node_hex(dest) +
                    (!pki_ready_ ? " but local PKI is not ready."
                                 : " but the remote public key is unknown."),
                {{
                    .kind =
                        ::platform::linux_runtime::PacketLogSegmentKind::Error,
                    .label = "pki",
                    .text = !pki_ready_ ? "pki_not_ready" : "key_missing",
                }});
            if (pki_ready_ && !have_dest_key)
            {
                (void)sendMeshtasticNodeInfoTo(dest, true, channel);
            }
            return false;
        }

        std::size_t pki_len = sizeof(pki_payload);
        if (!encryptPkiPayload(dest,
                               msg_id,
                               data_payload,
                               data_size,
                               pki_payload,
                               &pki_len))
        {
            append_lora_system_log(
                "Meshtastic direct TX blocked",
                "PKI payload encryption failed.",
                {{
                    .kind =
                        ::platform::linux_runtime::PacketLogSegmentKind::Error,
                    .label = "pki",
                    .text = "encrypt_failed",
                }});
            return false;
        }
        wire_payload = pki_payload;
        wire_payload_size = pki_len;
        channel_hash = 0;
        psk = nullptr;
        psk_len = 0;
        air_want_ack = true;
        use_pki = true;
    }

    if (!use_pki && psk != nullptr && psk_len > 0 &&
        !meshtastic_crypto_available())
    {
        append_lora_system_log(
            "Meshtastic TX crypto unavailable",
            "OpenSSL libcrypto was not linked, so encrypted Meshtastic channels cannot be transmitted.",
            {{
                .kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                .label = "dependency",
                .text = "install libssl-dev before building trailmate-uconsole",
            }});
        return false;
    }
    std::uint8_t wire[255] = {};
    std::size_t wire_size = sizeof(wire);
    if (!build_meshtastic_wire_packet(wire_payload,
                                      wire_payload_size,
                                      self_node_id_,
                                      msg_id,
                                      dest,
                                      channel_hash,
                                      config_.hop_limit,
                                      air_want_ack,
                                      psk,
                                      psk_len,
                                      wire,
                                      &wire_size))
    {
        append_lora_system_log(
            "Meshtastic TX packet build failed",
            "Encoded Data payload does not fit in one LoRa packet.",
            {{
                .kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                .label = "size",
                .text = std::to_string(wire_size),
            }});
        return false;
    }

    const std::string summary =
        std::string("Meshtastic ") + meshtastic_port_label(portnum) +
        " TX to " + node_hex(dest) + " id " + node_hex(msg_id) +
        " channel " + hex_u8(channel_hash) +
        (use_pki ? " via PKI." : ".");
    auto entry = make_meshtastic_wire_log(
        ::platform::linux_runtime::PacketLogDirection::Tx,
        wire,
        wire_size,
        portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ? "Meshtastic text TX"
                                                       : "Meshtastic appdata TX",
        summary.c_str());
    entry.segments.push_back({
        .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
        .label = "air",
        .text = describe_meshtastic_air_plan(plan),
    });
    entry.segments.push_back({
        .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
        .label = "path",
        .text = use_pki ? "PKI direct" : "channel",
    });
    ::platform::linux_runtime::append_packet_log(std::move(entry));

    const bool ok = radio_.transmit(wire, wire_size);
    if (!ok)
    {
        append_lora_system_log(
            "Meshtastic TX failed",
            radio_.lastError(),
            {{
                .kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                .label = "driver",
                .text = radio_.lastError(),
            }});
    }
    return ok;
}

bool LinuxRawLoraMeshAdapter::sendMeshtasticNodeInfoTo(
    ::chat::NodeId dest,
    bool want_response,
    ::chat::ChannelId channel)
{
    if (!tx_enabled_ || !ensureRadioReady())
    {
        return false;
    }

    char user_id[16] = {};
    std::snprintf(user_id,
                  sizeof(user_id),
                  "!%08lX",
                  static_cast<unsigned long>(self_node_id_));

    char long_name[32] = {};
    char short_name[5] = {};
    const std::uint16_t suffix =
        static_cast<std::uint16_t>(self_node_id_ & 0xFFFFU);
    if (!long_name_.empty())
    {
        std::strncpy(long_name, long_name_.c_str(), sizeof(long_name) - 1);
    }
    else
    {
        std::snprintf(long_name, sizeof(long_name), "uconsole-%04X", suffix);
    }
    if (!short_name_.empty())
    {
        std::strncpy(short_name, short_name_.c_str(), sizeof(short_name) - 1);
    }
    else
    {
        std::snprintf(short_name, sizeof(short_name), "%04X", suffix);
    }

    const std::uint8_t mac_addr[6] = {};
    std::uint8_t data_payload[256] = {};
    std::size_t data_size = sizeof(data_payload);
    if (!::chat::meshtastic::encodeNodeInfoMessage(
            user_id,
            long_name,
            short_name,
            meshtastic_HardwareModel_PRIVATE_HW,
            mac_addr,
            pki_ready_ ? pki_public_key_.data() : nullptr,
            pki_ready_ ? pki_public_key_.size() : 0,
            want_response,
            data_payload,
            &data_size))
    {
        append_lora_system_log(
            "Meshtastic NodeInfo encode failed",
            "Local user info could not be encoded.");
        return false;
    }

    const MeshtasticAirPlan plan = build_meshtastic_air_plan(config_);
    std::size_t psk_len = 0;
    const std::uint8_t* psk = psk_for_channel(plan, channel, &psk_len);
    const std::uint8_t channel_hash = channel_hash_for(plan, channel);
    if (psk != nullptr && psk_len > 0 && !meshtastic_crypto_available())
    {
        append_lora_system_log(
            "Meshtastic NodeInfo crypto unavailable",
            "OpenSSL libcrypto was not linked, so encrypted NodeInfo cannot be transmitted.",
            {{
                .kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                .label = "dependency",
                .text = "install libssl-dev before building trailmate-uconsole",
            }});
        return false;
    }

    const ::chat::MessageId msg_id = nextMessageId();
    std::uint8_t wire[255] = {};
    std::size_t wire_size = sizeof(wire);
    const bool want_ack = want_response && dest != kBroadcastNodeId;
    if (!build_meshtastic_wire_packet(data_payload,
                                      data_size,
                                      self_node_id_,
                                      msg_id,
                                      dest,
                                      channel_hash,
                                      config_.hop_limit,
                                      want_ack,
                                      psk,
                                      psk_len,
                                      wire,
                                      &wire_size))
    {
        append_lora_system_log(
            "Meshtastic NodeInfo packet build failed",
            "Encoded NodeInfo payload does not fit in one LoRa packet.");
        return false;
    }

    const std::string summary =
        "Meshtastic NODEINFO TX to " + node_hex(dest) + " id " +
        node_hex(msg_id) + " channel " + hex_u8(channel_hash) +
        (want_response ? " want-response." : ".");
    auto entry = make_meshtastic_wire_log(
        ::platform::linux_runtime::PacketLogDirection::Tx,
        wire,
        wire_size,
        "Meshtastic nodeinfo TX",
        summary.c_str());
    entry.segments.push_back({
        .kind = ::platform::linux_runtime::PacketLogSegmentKind::Body,
        .label = "user",
        .text = std::string(short_name) + " / " + long_name,
    });
    ::platform::linux_runtime::append_packet_log(std::move(entry));

    const bool ok = radio_.transmit(wire, wire_size);
    if (!ok)
    {
        append_lora_system_log(
            "Meshtastic NodeInfo TX failed",
            radio_.lastError(),
            {{
                .kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                .label = "driver",
                .text = radio_.lastError(),
            }});
    }
    return ok;
}

bool LinuxRawLoraMeshAdapter::initPkiKeys()
{
    ::platform::linux_runtime::mesh::LinuxSqliteLocalIdentityStore store;
    ::mesh::LocalIdentity stored_identity{};
    auto loaded = store.load(stored_identity);
    if (loaded.ok &&
        !::chat::meshtastic::isZeroKey(stored_identity.private_key,
                                       sizeof(stored_identity.private_key)))
    {
        std::memcpy(pki_public_key_.data(),
                    stored_identity.public_key,
                    pki_public_key_.size());
        std::memcpy(pki_private_key_.data(),
                    stored_identity.private_key,
                    pki_private_key_.size());
        pki_ready_ = true;
        append_lora_system_log("Meshtastic PKI ready",
                               "Loaded local X25519 identity key.");
        return true;
    }

    pki_public_key_.fill(0);
    pki_private_key_.fill(0);
    if (!generate_x25519_keypair(pki_public_key_.data(),
                                 pki_private_key_.data()) ||
        ::chat::meshtastic::isZeroKey(pki_private_key_.data(),
                                      pki_private_key_.size()))
    {
        pki_ready_ = false;
        append_lora_system_log(
            "Meshtastic PKI unavailable",
            "OpenSSL X25519 key generation failed or libcrypto is not linked.");
        return false;
    }

    ::mesh::LocalIdentity generated{};
    std::memcpy(generated.public_key,
                pki_public_key_.data(),
                sizeof(generated.public_key));
    std::memcpy(generated.private_key,
                pki_private_key_.data(),
                sizeof(generated.private_key));
    generated.valid = true;
    auto saved = store.save(generated);
    pki_ready_ = saved.ok;
    append_lora_system_log(
        pki_ready_ ? "Meshtastic PKI ready" : "Meshtastic PKI save failed",
        pki_ready_ ? "Generated and saved local X25519 identity key."
                   : "Generated a local PKI key but could not persist it.");
    return pki_ready_;
}

void LinuxRawLoraMeshAdapter::loadPkiNodeKeys()
{
    ::platform::linux_runtime::mesh::LinuxSqlitePeerKeyStore store;
    std::vector<::mesh::PeerPublicKey> keys;
    auto loaded = store.loadAll(keys);
    if (!loaded.ok)
    {
        return;
    }

    node_public_keys_.clear();
    node_key_last_seen_.clear();
    for (const auto& peer_key : keys)
    {
        std::array<std::uint8_t, 32> key_copy{};
        std::memcpy(key_copy.data(), peer_key.public_key, key_copy.size());
        node_public_keys_[peer_key.node_id.value] = key_copy;
        node_key_last_seen_[peer_key.node_id.value] = peer_key.updated_at_ms;
    }
}

void LinuxRawLoraMeshAdapter::savePkiNodeKey(::chat::NodeId node_id,
                                             const std::uint8_t* key,
                                             std::size_t key_len)
{
    if (node_id == 0 || node_id == kBroadcastNodeId || key == nullptr ||
        key_len != kPkiKeySize ||
        ::chat::meshtastic::isZeroKey(key, key_len))
    {
        return;
    }

    std::array<std::uint8_t, 32> key_copy{};
    std::memcpy(key_copy.data(), key, key_copy.size());
    node_public_keys_[node_id] = key_copy;
    touchPkiNodeKey(node_id);
    savePkiKeysToStore();
}

void LinuxRawLoraMeshAdapter::savePkiKeysToStore()
{
    std::vector<::mesh::PeerPublicKey> entries;
    entries.reserve(node_public_keys_.size());
    for (const auto& item : node_public_keys_)
    {
        ::mesh::PeerPublicKey entry{};
        entry.node_id = ::mesh::NodeId{item.first};
        const auto seen_it = node_key_last_seen_.find(item.first);
        entry.updated_at_ms =
            seen_it == node_key_last_seen_.end() ? 0 : seen_it->second;
        entry.verified = false;
        std::memcpy(entry.public_key, item.second.data(), sizeof(entry.public_key));
        entries.push_back(entry);
    }

    if (entries.size() > kMaxPkiNodes)
    {
        std::sort(entries.begin(),
                  entries.end(),
                  [](const ::mesh::PeerPublicKey& left,
                     const ::mesh::PeerPublicKey& right)
                  {
                      return left.updated_at_ms < right.updated_at_ms;
                  });
        const std::size_t drop_count = entries.size() - kMaxPkiNodes;
        for (std::size_t index = 0; index < drop_count; ++index)
        {
            node_public_keys_.erase(entries[index].node_id.value);
            node_key_last_seen_.erase(entries[index].node_id.value);
        }
        entries.erase(entries.begin(),
                      entries.begin() +
                          static_cast<std::ptrdiff_t>(drop_count));
    }

    ::platform::linux_runtime::mesh::LinuxSqlitePeerKeyStore store;
    (void)store.replaceAll(entries.empty() ? nullptr : entries.data(), entries.size());
}

void LinuxRawLoraMeshAdapter::forgetPkiNodeKey(::chat::NodeId node_id)
{
    if (node_public_keys_.erase(node_id) > 0)
    {
        node_key_last_seen_.erase(node_id);
        savePkiKeysToStore();
    }
}

void LinuxRawLoraMeshAdapter::touchPkiNodeKey(::chat::NodeId node_id)
{
    node_key_last_seen_[node_id] = now_seconds();
}

bool LinuxRawLoraMeshAdapter::encryptPkiPayload(
    ::chat::NodeId dest,
    ::chat::MessageId packet_id,
    const std::uint8_t* plain,
    std::size_t plain_len,
    std::uint8_t* out_cipher,
    std::size_t* out_cipher_len)
{
    if (!pki_ready_ || plain == nullptr || plain_len == 0 ||
        out_cipher == nullptr || out_cipher_len == nullptr)
    {
        return false;
    }
    const auto key_it = node_public_keys_.find(dest);
    if (key_it == node_public_keys_.end())
    {
        return false;
    }

    const std::size_t auth_len = 8;
    const std::size_t needed = plain_len + auth_len + sizeof(std::uint32_t);
    if (*out_cipher_len < needed)
    {
        *out_cipher_len = needed;
        return false;
    }

    std::array<std::uint8_t, 32> shared{};
    if (!derive_x25519_shared_key(key_it->second.data(),
                                  pki_private_key_.data(),
                                  shared.data()))
    {
        return false;
    }
    ::chat::meshtastic::hashSharedKey(shared.data(), shared.size());
    const std::uint32_t extra_nonce = random_u32();
    std::uint8_t nonce[::chat::meshtastic::kPkiNonceSize] = {};
    ::chat::meshtastic::initPkiNonce(self_node_id_,
                                     packet_id,
                                     extra_nonce,
                                     nonce);

    std::uint8_t auth[16] = {};
    if (!::chat::meshtastic::encryptPkiAesCcm(shared.data(),
                                              shared.size(),
                                              nonce,
                                              auth_len,
                                              nullptr,
                                              0,
                                              plain,
                                              plain_len,
                                              out_cipher,
                                              auth))
    {
        return false;
    }

    std::memcpy(out_cipher + plain_len, auth, auth_len);
    std::memcpy(out_cipher + plain_len + auth_len,
                &extra_nonce,
                sizeof(extra_nonce));
    *out_cipher_len = needed;
    touchPkiNodeKey(dest);
    return true;
}

bool LinuxRawLoraMeshAdapter::decryptPkiPayload(
    ::chat::NodeId from,
    ::chat::MessageId packet_id,
    const std::uint8_t* cipher,
    std::size_t cipher_len,
    std::uint8_t* out_plain,
    std::size_t* out_plain_len)
{
    if (!pki_ready_ || cipher == nullptr || cipher_len <= 12 ||
        out_plain == nullptr || out_plain_len == nullptr)
    {
        return false;
    }
    const auto key_it = node_public_keys_.find(from);
    if (key_it == node_public_keys_.end())
    {
        (void)sendMeshtasticNodeInfoTo(from, true, ::chat::ChannelId::PRIMARY);
        return false;
    }

    const std::size_t auth_len = 8;
    const std::size_t plain_len = cipher_len - auth_len - sizeof(std::uint32_t);
    if (*out_plain_len < plain_len)
    {
        *out_plain_len = plain_len;
        return false;
    }

    std::array<std::uint8_t, 32> shared{};
    if (!derive_x25519_shared_key(key_it->second.data(),
                                  pki_private_key_.data(),
                                  shared.data()))
    {
        return false;
    }
    ::chat::meshtastic::hashSharedKey(shared.data(), shared.size());

    const std::uint8_t* auth = cipher + plain_len;
    std::uint32_t extra_nonce = 0;
    std::memcpy(&extra_nonce, auth + auth_len, sizeof(extra_nonce));
    std::uint8_t nonce[::chat::meshtastic::kPkiNonceSize] = {};
    ::chat::meshtastic::initPkiNonce(from, packet_id, extra_nonce, nonce);

    if (!::chat::meshtastic::decryptPkiAesCcm(shared.data(),
                                              shared.size(),
                                              nonce,
                                              auth_len,
                                              cipher,
                                              plain_len,
                                              nullptr,
                                              0,
                                              auth,
                                              out_plain))
    {
        return false;
    }

    *out_plain_len = plain_len;
    touchPkiNodeKey(from);
    return true;
}

void LinuxRawLoraMeshAdapter::updateKeyVerificationState()
{
    if (kv_state_ == KeyVerificationState::Idle)
    {
        return;
    }

    const std::uint32_t now_ms = ::sys::millis_now();
    if (kv_nonce_ms_ != 0 &&
        static_cast<std::uint32_t>(now_ms - kv_nonce_ms_) >
            kKeyVerificationTimeoutMs)
    {
        resetKeyVerificationState();
        return;
    }
    kv_nonce_ms_ = now_ms;
}

void LinuxRawLoraMeshAdapter::resetKeyVerificationState()
{
    kv_state_ = KeyVerificationState::Idle;
    kv_nonce_ = 0;
    kv_nonce_ms_ = 0;
    kv_security_number_ = 0;
    kv_remote_node_ = 0;
    kv_hash1_.fill(0);
    kv_hash2_.fill(0);
}

void LinuxRawLoraMeshAdapter::buildVerificationCode(char* out,
                                                    std::size_t out_len) const
{
    if (out == nullptr || out_len == 0)
    {
        return;
    }
    if (out_len < 10)
    {
        out[0] = '\0';
        return;
    }
    for (int index = 0; index < 4; ++index)
    {
        out[index] = static_cast<char>((kv_hash1_[index] >> 2) + 48);
    }
    out[4] = ' ';
    for (int index = 0; index < 4; ++index)
    {
        out[index + 5] =
            static_cast<char>((kv_hash1_[index + 4] >> 2) + 48);
    }
    out[9] = '\0';
}

bool LinuxRawLoraMeshAdapter::handleKeyVerificationInit(
    const ::chat::meshtastic::PacketHeaderWire& header,
    const meshtastic_KeyVerification& kv)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::Idle ||
        header.to != self_node_id_ || header.to == kBroadcastNodeId ||
        !pki_ready_)
    {
        return false;
    }
    const auto key_it = node_public_keys_.find(header.from);
    if (key_it == node_public_keys_.end())
    {
        return false;
    }

    kv_nonce_ = kv.nonce;
    kv_nonce_ms_ = ::sys::millis_now();
    kv_remote_node_ = header.from;
    kv_security_number_ = random_security_number();
    if (!::chat::meshtastic::computeKeyVerificationHashes(
            kv_security_number_,
            kv_nonce_,
            kv_remote_node_,
            self_node_id_,
            key_it->second.data(),
            key_it->second.size(),
            pki_public_key_.data(),
            pki_public_key_.size(),
            kv_hash1_.data(),
            kv_hash2_.data()))
    {
        resetKeyVerificationState();
        return false;
    }

    meshtastic_KeyVerification reply = meshtastic_KeyVerification_init_zero;
    reply.nonce = kv_nonce_;
    reply.hash1.size = 0;
    reply.hash2.size = static_cast<pb_size_t>(kv_hash2_.size());
    std::memcpy(reply.hash2.bytes, kv_hash2_.data(), kv_hash2_.size());

    if (!sendKeyVerificationPacket(kv_remote_node_, reply, false))
    {
        resetKeyVerificationState();
        return false;
    }

    kv_state_ = KeyVerificationState::ReceiverAwaitingHash1;
    ::sys::EventBus::publish(
        new ::sys::KeyVerificationNumberInformEvent(kv_remote_node_,
                                                    kv_nonce_,
                                                    kv_security_number_),
        0);
    return true;
}

bool LinuxRawLoraMeshAdapter::handleKeyVerificationReply(
    const ::chat::meshtastic::PacketHeaderWire& header,
    const meshtastic_KeyVerification& kv)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::SenderInitiated ||
        header.to != self_node_id_ || header.to == kBroadcastNodeId ||
        kv.nonce != kv_nonce_ || header.from != kv_remote_node_ ||
        kv.hash1.size != 0 || kv.hash2.size != kv_hash2_.size())
    {
        return false;
    }

    std::memcpy(kv_hash2_.data(), kv.hash2.bytes, kv_hash2_.size());
    kv_state_ = KeyVerificationState::SenderAwaitingNumber;
    kv_nonce_ms_ = ::sys::millis_now();
    ::sys::EventBus::publish(
        new ::sys::KeyVerificationNumberRequestEvent(kv_remote_node_,
                                                     kv_nonce_),
        0);
    return true;
}

bool LinuxRawLoraMeshAdapter::processKeyVerificationNumber(
    ::chat::NodeId remote_node,
    std::uint64_t nonce,
    std::uint32_t number)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::SenderAwaitingNumber ||
        kv_remote_node_ != remote_node || kv_nonce_ != nonce)
    {
        return false;
    }
    const auto key_it = node_public_keys_.find(remote_node);
    if (key_it == node_public_keys_.end())
    {
        resetKeyVerificationState();
        return false;
    }

    std::array<std::uint8_t, 32> scratch_hash{};
    kv_security_number_ = number;
    if (!::chat::meshtastic::computeKeyVerificationHashes(
            kv_security_number_,
            kv_nonce_,
            self_node_id_,
            kv_remote_node_,
            pki_public_key_.data(),
            pki_public_key_.size(),
            key_it->second.data(),
            key_it->second.size(),
            kv_hash1_.data(),
            scratch_hash.data()))
    {
        return false;
    }
    if (std::memcmp(scratch_hash.data(), kv_hash2_.data(), kv_hash2_.size()) !=
        0)
    {
        return false;
    }

    meshtastic_KeyVerification response = meshtastic_KeyVerification_init_zero;
    response.nonce = kv_nonce_;
    response.hash1.size = static_cast<pb_size_t>(kv_hash1_.size());
    std::memcpy(response.hash1.bytes, kv_hash1_.data(), kv_hash1_.size());
    response.hash2.size = 0;
    if (!sendKeyVerificationPacket(kv_remote_node_, response, true))
    {
        return false;
    }

    kv_state_ = KeyVerificationState::SenderAwaitingUser;
    kv_nonce_ms_ = ::sys::millis_now();

    char code[12] = {};
    buildVerificationCode(code, sizeof(code));
    ::sys::EventBus::publish(
        new ::sys::KeyVerificationFinalEvent(kv_remote_node_,
                                             kv_nonce_,
                                             true,
                                             code),
        0);
    return true;
}

bool LinuxRawLoraMeshAdapter::handleKeyVerificationFinal(
    const ::chat::meshtastic::PacketHeaderWire& header,
    const meshtastic_KeyVerification& kv)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::ReceiverAwaitingHash1 ||
        header.to != self_node_id_ || header.to == kBroadcastNodeId ||
        kv.nonce != kv_nonce_ || header.from != kv_remote_node_ ||
        kv.hash1.size != kv_hash1_.size() || kv.hash2.size != 0)
    {
        return false;
    }
    if (std::memcmp(kv.hash1.bytes, kv_hash1_.data(), kv_hash1_.size()) != 0)
    {
        return false;
    }

    kv_state_ = KeyVerificationState::ReceiverAwaitingUser;
    kv_nonce_ms_ = ::sys::millis_now();

    char code[12] = {};
    buildVerificationCode(code, sizeof(code));
    ::sys::EventBus::publish(
        new ::sys::KeyVerificationFinalEvent(kv_remote_node_,
                                             kv_nonce_,
                                             false,
                                             code),
        0);
    return true;
}

bool LinuxRawLoraMeshAdapter::sendKeyVerificationPacket(
    ::chat::NodeId dest,
    const meshtastic_KeyVerification& kv,
    bool want_response)
{
    if (!tx_enabled_ || !ensureRadioReady() || !pki_ready_ ||
        node_public_keys_.find(dest) == node_public_keys_.end())
    {
        return false;
    }

    std::uint8_t kv_buf[96] = {};
    pb_ostream_t kv_stream = pb_ostream_from_buffer(kv_buf, sizeof(kv_buf));
    if (!pb_encode(&kv_stream, meshtastic_KeyVerification_fields, &kv))
    {
        return false;
    }

    std::uint8_t data_buf[160] = {};
    std::size_t data_size = sizeof(data_buf);
    if (!::chat::meshtastic::encodeAppData(
            meshtastic_PortNum_KEY_VERIFICATION_APP,
            kv_buf,
            kv_stream.bytes_written,
            want_response,
            data_buf,
            &data_size))
    {
        return false;
    }

    const ::chat::MessageId msg_id = nextMessageId();
    std::uint8_t pki_buf[256] = {};
    std::size_t pki_len = sizeof(pki_buf);
    if (!encryptPkiPayload(dest, msg_id, data_buf, data_size, pki_buf, &pki_len))
    {
        return false;
    }

    std::uint8_t wire[255] = {};
    std::size_t wire_size = sizeof(wire);
    if (!build_meshtastic_wire_packet(pki_buf,
                                      pki_len,
                                      self_node_id_,
                                      msg_id,
                                      dest,
                                      0,
                                      config_.hop_limit,
                                      false,
                                      nullptr,
                                      0,
                                      wire,
                                      &wire_size))
    {
        return false;
    }

    const bool ok = radio_.transmit(wire, wire_size);
    if (ok)
    {
        append_lora_system_log(
            "Meshtastic key verification TX",
            "Sent key verification stage to " + node_hex(dest) +
                " id " + node_hex(msg_id) + ".");
    }
    return ok;
}

bool LinuxRawLoraMeshAdapter::sendRoutingAck(::chat::NodeId dest,
                                             ::chat::MessageId request_id,
                                             std::uint8_t channel_hash,
                                             const std::uint8_t* psk,
                                             std::size_t psk_len)
{
    if (!tx_enabled_ || !ensureRadioReady() || dest == 0 ||
        dest == kBroadcastNodeId || request_id == 0)
    {
        return false;
    }

    meshtastic_Routing routing = meshtastic_Routing_init_default;
    routing.which_variant = meshtastic_Routing_error_reason_tag;
    routing.error_reason = meshtastic_Routing_Error_NONE;

    std::uint8_t routing_buf[64] = {};
    pb_ostream_t routing_stream =
        pb_ostream_from_buffer(routing_buf, sizeof(routing_buf));
    if (!pb_encode(&routing_stream, meshtastic_Routing_fields, &routing))
    {
        return false;
    }

    meshtastic_Data data = meshtastic_Data_init_default;
    data.portnum = meshtastic_PortNum_ROUTING_APP;
    data.dest = dest;
    data.source = self_node_id_;
    data.request_id = request_id;
    data.want_response = false;
    data.has_bitfield = true;
    data.bitfield = 0;
    data.payload.size = routing_stream.bytes_written;
    if (data.payload.size > sizeof(data.payload.bytes))
    {
        return false;
    }
    std::memcpy(data.payload.bytes, routing_buf, data.payload.size);

    std::uint8_t data_buf[128] = {};
    pb_ostream_t data_stream =
        pb_ostream_from_buffer(data_buf, sizeof(data_buf));
    if (!pb_encode(&data_stream, meshtastic_Data_fields, &data))
    {
        return false;
    }

    const ::chat::MessageId msg_id = nextMessageId();
    const std::uint8_t* payload = data_buf;
    std::size_t payload_len = data_stream.bytes_written;
    std::uint8_t pki_buf[256] = {};
    if (channel_hash == 0)
    {
        std::size_t pki_len = sizeof(pki_buf);
        if (!encryptPkiPayload(dest,
                               msg_id,
                               data_buf,
                               data_stream.bytes_written,
                               pki_buf,
                               &pki_len))
        {
            return false;
        }
        payload = pki_buf;
        payload_len = pki_len;
        psk = nullptr;
        psk_len = 0;
    }

    std::uint8_t wire[255] = {};
    std::size_t wire_size = sizeof(wire);
    if (!build_meshtastic_wire_packet(payload,
                                      payload_len,
                                      self_node_id_,
                                      msg_id,
                                      dest,
                                      channel_hash,
                                      config_.hop_limit,
                                      false,
                                      psk,
                                      psk_len,
                                      wire,
                                      &wire_size))
    {
        return false;
    }

    const bool ok = radio_.transmit(wire, wire_size);
    if (ok)
    {
        append_lora_system_log(
            "Meshtastic routing ACK TX",
            "ACK to " + node_hex(dest) + " for " + node_hex(request_id) +
                " via channel " + hex_u8(channel_hash) + ".");
    }
    return ok;
}

bool LinuxRawLoraMeshAdapter::sendFrame(PacketKind kind,
                                        ::chat::ChannelId channel,
                                        ::chat::NodeId dest,
                                        ::chat::MessageId msg_id,
                                        std::uint32_t portnum,
                                        const std::uint8_t* payload,
                                        std::size_t len)
{
    if (!tx_enabled_ || payload == nullptr || len == 0 ||
        len > kMaxPayloadSize || !ensureRadioReady())
    {
        return false;
    }

    std::uint8_t frame[255] = {};
    frame[0] = kMagic0;
    frame[1] = kMagic1;
    frame[2] = kMagic2;
    frame[3] = kMagic3;
    frame[4] = kVersion;
    frame[5] = static_cast<std::uint8_t>(kind);
    frame[6] = static_cast<std::uint8_t>(channel);
    frame[7] = dest == 0xFFFFFFFFUL ? kBroadcastFlags : 0;
    write_u32(frame + 8, self_node_id_);
    write_u32(frame + 12, dest);
    write_u32(frame + 16, msg_id);
    write_u32(frame + 20, portnum);
    write_u16(frame + 24, static_cast<std::uint16_t>(len));
    std::memcpy(frame + kHeaderSize, payload, len);
    const std::size_t frame_size = kHeaderSize + len;
    ::platform::linux_runtime::append_packet_log(
        make_lora_log(::platform::linux_runtime::PacketLogDirection::Tx,
                      frame,
                      frame_size,
                      kind == PacketKind::Text ? "LoRa text TX"
                                               : "LoRa appdata TX",
                      "Trail Mate raw LoRa frame queued."));
    return radio_.transmit(frame, frame_size);
}

bool LinuxRawLoraMeshAdapter::parseFrame(
    const ::platform::linux_runtime::Sx126xPacket& packet)
{
    if (packet.size < kHeaderSize)
    {
        return false;
    }
    const auto* data = packet.data.data();
    if (data[0] != kMagic0 || data[1] != kMagic1 || data[2] != kMagic2 ||
        data[3] != kMagic3 || data[4] != kVersion)
    {
        return false;
    }

    const auto kind = static_cast<PacketKind>(data[5]);
    const auto channel = static_cast<::chat::ChannelId>(data[6]);
    const ::chat::NodeId from = read_u32(data + 8);
    const ::chat::NodeId to = read_u32(data + 12);
    const ::chat::MessageId msg_id = read_u32(data + 16);
    const std::uint32_t portnum = read_u32(data + 20);
    const std::uint16_t payload_len = read_u16(data + 24);
    if (payload_len > packet.size - kHeaderSize)
    {
        return false;
    }
    if (from == self_node_id_ ||
        (to != 0xFFFFFFFFUL && to != 0 && to != self_node_id_))
    {
        return false;
    }

    ::chat::RxMeta meta{};
    meta.origin = ::chat::RxOrigin::Mesh;
    meta.rx_timestamp_s = now_seconds();
    meta.time_source = ::chat::RxTimeSource::DeviceUtc;
    meta.direct = to == self_node_id_;
    meta.rssi_dbm_x10 = static_cast<std::int16_t>(packet.rssi_dbm * 10.0f);
    meta.snr_db_x10 = static_cast<std::int16_t>(packet.snr_db * 10.0f);
    meta.freq_hz = packet.freq_hz;
    meta.bw_hz = packet.bw_hz;
    meta.sf = packet.sf;
    meta.cr = packet.cr;

    const auto* payload = data + kHeaderSize;
    if (kind == PacketKind::Text)
    {
        ::chat::MeshIncomingText text{};
        text.channel = channel;
        text.from = from;
        text.to = to;
        text.msg_id = msg_id;
        text.timestamp = meta.rx_timestamp_s;
        text.text.assign(reinterpret_cast<const char*>(payload), payload_len);
        text.hop_limit = 0;
        text.encrypted = false;
        text.rx_meta = meta;
        incoming_text_.push_back(std::move(text));
        return true;
    }
    if (kind == PacketKind::AppData)
    {
        ::chat::MeshIncomingData app{};
        app.portnum = portnum;
        app.from = from;
        app.to = to;
        app.packet_id = msg_id;
        app.channel = channel;
        app.hop_limit = 0;
        app.payload.assign(payload, payload + payload_len);
        app.rx_meta = meta;
        incoming_data_.push_back(std::move(app));
        return true;
    }
    return false;
}

bool LinuxRawLoraMeshAdapter::parseMeshtasticPacket(
    const ::platform::linux_runtime::Sx126xPacket& packet)
{
    if (packet.size < sizeof(::chat::meshtastic::PacketHeaderWire))
    {
        return false;
    }

    ::chat::meshtastic::PacketHeaderWire header{};
    std::uint8_t payload[256] = {};
    std::size_t payload_size = sizeof(payload);
    if (!parse_meshtastic_wire_packet(packet.data.data(),
                                      packet.size,
                                      &header,
                                      payload,
                                      &payload_size))
    {
        append_lora_system_log(
            "Meshtastic RX parse failed",
            "LoRa packet has enough bytes for an air header but could not be split.",
            {{
                .kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                .label = "len",
                .text = std::to_string(packet.size),
            }});
        return true;
    }

    const MeshtasticAirPlan plan = build_meshtastic_air_plan(config_);
    ::chat::ChannelId channel = ::chat::ChannelId::PRIMARY;
    const bool matches_channel = channel_from_hash(plan, header.channel, &channel);
    const bool can_try_pki =
        header.to == self_node_id_ && header.to != kBroadcastNodeId &&
        payload_size > 12 && (header.channel == 0 || !matches_channel);
    if (!matches_channel && !can_try_pki)
    {
        append_lora_system_log(
            "Meshtastic RX unknown channel",
            "Header parsed but channel hash does not match configured primary or secondary channel.",
            {{
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Header,
                 .label = "from/to/id",
                 .text = node_hex(header.from) + "/" + node_hex(header.to) +
                         "/" + node_hex(header.id),
             },
             {
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                 .label = "hash",
                 .text = hex_u8(header.channel) + " expected " +
                         hex_u8(plan.primary_channel_hash) + " or " +
                         hex_u8(plan.secondary_channel_hash),
             },
             {
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                 .label = "signal",
                 .text = packet_signal_text(packet),
             }});
        return true;
    }

    if (config_.ignore_mqtt &&
        (header.flags & ::chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK) != 0)
    {
        append_lora_system_log(
            "Meshtastic RX ignored",
            "Packet has via-MQTT bit and Ignore MQTT is enabled.",
            {{
                .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                .label = "from",
                .text = node_hex(header.from),
            }});
        append_mqtt_system_log(
            "Meshtastic MQTT RX ignored",
            "Packet from " + node_hex(header.from) +
                " was marked via MQTT and Ignore MQTT is enabled.",
            {{
                .kind = ::platform::linux_runtime::PacketLogSegmentKind::Header,
                .label = "air",
                .text = "ch=" + hex_u8(header.channel) +
                        " flags=" + hex_u8(header.flags),
            }});
        return true;
    }

    if (header.from == self_node_id_)
    {
        append_lora_system_log(
            "Meshtastic RX self ignored",
            "Radio heard a packet from the local node id.",
            {{
                .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                .label = "id",
                .text = node_hex(header.id),
            }});
        return true;
    }

    std::size_t psk_len = 0;
    const std::uint8_t* psk =
        matches_channel ? psk_for_channel(plan, channel, &psk_len) : nullptr;
    std::uint8_t plaintext[256] = {};
    std::size_t plaintext_len = sizeof(plaintext);
    bool used_pki_transport = false;
    if (can_try_pki)
    {
        if (!decryptPkiPayload(header.from,
                               header.id,
                               payload,
                               payload_size,
                               plaintext,
                               &plaintext_len))
        {
            append_lora_system_log(
                "Meshtastic PKI RX decrypt failed",
                "Direct PKI payload could not be decoded; requesting fresh NodeInfo from " +
                    node_hex(header.from) + ".",
                {{
                    .kind =
                        ::platform::linux_runtime::PacketLogSegmentKind::Error,
                    .label = "pki",
                    .text = "decrypt_failed",
                }});
            forgetPkiNodeKey(header.from);
            (void)sendMeshtasticNodeInfoTo(header.from,
                                           true,
                                           ::chat::ChannelId::PRIMARY);
            return true;
        }
        used_pki_transport = true;
        channel = ::chat::ChannelId::PRIMARY;
        psk = nullptr;
        psk_len = 0;
    }
    else if (psk != nullptr && psk_len > 0)
    {
        if (!meshtastic_crypto_available())
        {
            append_lora_system_log(
                "Meshtastic RX crypto unavailable",
                "OpenSSL libcrypto was not linked, so encrypted Meshtastic payloads cannot be decoded.",
                {{
                    .kind =
                        ::platform::linux_runtime::PacketLogSegmentKind::Error,
                    .label = "dependency",
                    .text = "install libssl-dev before building trailmate-uconsole",
                }});
            return true;
        }
        if (!decrypt_meshtastic_payload(header,
                                        payload,
                                        payload_size,
                                        psk,
                                        psk_len,
                                        plaintext,
                                        &plaintext_len))
        {
            append_lora_system_log(
                "Meshtastic RX decrypt failed",
                "Channel hash matched but payload decryption failed.",
                {{
                    .kind =
                        ::platform::linux_runtime::PacketLogSegmentKind::Error,
                    .label = "cipher",
                    .text = std::to_string(payload_size) + " bytes",
                }});
            return true;
        }
    }
    else
    {
        plaintext_len = payload_size;
        std::memcpy(plaintext, payload, payload_size);
    }

    meshtastic_Data decoded = meshtastic_Data_init_default;
    pb_istream_t stream = pb_istream_from_buffer(plaintext, plaintext_len);
    if (!pb_decode(&stream, meshtastic_Data_fields, &decoded))
    {
        const std::string pb_error = pb_error_text(stream);
        append_lora_system_log(
            "Meshtastic RX protobuf failed",
            "Channel hash matched but Data protobuf did not decode. Check PSK/channel settings if this repeats.",
            {{
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Header,
                 .label = "from/to/id",
                 .text = node_hex(header.from) + "/" + node_hex(header.to) +
                         "/" + node_hex(header.id),
             },
             {
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                 .label = "plain",
                 .text = ::platform::linux_runtime::hex_bytes(plaintext,
                                                              plaintext_len),
             },
             {
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                 .label = "pb",
                 .text = pb_error,
             }});
        return true;
    }

    const ::chat::RxMeta rx_meta = make_meshtastic_rx_meta(header, packet);
    const bool encrypted =
        used_pki_transport || (psk != nullptr && psk_len > 0);
    const auto channel_index = static_cast<std::uint8_t>(channel);
    const bool via_mqtt_packet =
        (header.flags & ::chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK) != 0;
    const bool decoded_want_response =
        decoded.want_response ||
        (decoded.has_bitfield &&
         ((decoded.bitfield & kBitfieldWantResponseMask) != 0));
    const bool want_ack_flag =
        (header.flags &
         ::chat::meshtastic::PACKET_FLAGS_WANT_ACK_MASK) != 0;
    if (want_ack_flag && header.to == self_node_id_)
    {
        (void)sendRoutingAck(header.from,
                             header.id,
                             header.channel,
                             psk,
                             psk_len);
    }

    if (decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
        decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP)
    {
        ::chat::MeshIncomingText text{};
        if (!::chat::meshtastic::decodeTextPayload(decoded, &text))
        {
            append_lora_system_log(
                "Meshtastic text decode failed",
                "Data protobuf decoded but text payload was invalid.",
                {{
                    .kind =
                        ::platform::linux_runtime::PacketLogSegmentKind::Error,
                    .label = "port",
                    .text = meshtastic_port_label(decoded.portnum),
                }});
            return true;
        }
        text.channel = channel;
        text.from = header.from;
        text.to = header.to;
        text.msg_id = header.id;
        text.timestamp = rx_meta.rx_timestamp_s;
        text.hop_limit =
            header.flags & ::chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
        text.encrypted = encrypted;
        text.rx_meta = rx_meta;
        incoming_text_.push_back(text);

        append_lora_system_log(
            "Meshtastic text RX",
            "From " + node_hex(header.from) + " to " + node_hex(header.to) +
                " id " + node_hex(header.id) + ": " +
                compact_text(text.text),
            {{
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Header,
                 .label = "air",
                 .text = "ch=" + hex_u8(header.channel) +
                         " flags=" + hex_u8(header.flags),
             },
             {
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Body,
                 .label = "text",
                 .text = compact_text(text.text),
             },
             {
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                 .label = "signal",
                 .text = packet_signal_text(packet),
             }});
        if (via_mqtt_packet)
        {
            append_mqtt_system_log(
                "Meshtastic MQTT text RX",
                "From " + node_hex(header.from) + " to " +
                    node_hex(header.to) + " id " + node_hex(header.id) +
                    ": " + compact_text(text.text),
                {{
                     .kind =
                         ::platform::linux_runtime::PacketLogSegmentKind::Header,
                     .label = "air",
                     .text = "ch=" + hex_u8(header.channel) +
                             " flags=" + hex_u8(header.flags),
                 },
                 {
                     .kind =
                         ::platform::linux_runtime::PacketLogSegmentKind::Body,
                     .label = "text",
                     .text = compact_text(text.text),
                 }});
        }
        return true;
    }

    if (decoded.portnum == meshtastic_PortNum_NODEINFO_APP &&
        decoded.payload.size > 0)
    {
        ::chat::meshtastic::NodePayloadDecodeContext context{};
        context.fallback_node_id = header.from;
        context.snr = packet.snr_db;
        context.rssi = packet.rssi_dbm;
        context.timestamp = rx_meta.rx_timestamp_s;
        context.hops_away = rx_meta.hop_count;
        context.channel = channel_index;
        context.via_mqtt = via_mqtt_packet;

        ::chat::meshtastic::DecodedNodePayload node{};
        if (::chat::meshtastic::decodeNodeInfoPayload(decoded, context, &node))
        {
            if (node.has_public_key)
            {
                savePkiNodeKey(node.node_id,
                               node.public_key.data(),
                               node.public_key.size());
            }
            ::sys::EventBus::publish(
                new ::sys::NodeInfoUpdateEvent(
                    node.node_id,
                    node.short_name.c_str(),
                    node.long_name.c_str(),
                    node.snr,
                    node.rssi,
                    node.timestamp,
                    node.protocol,
                    node.role,
                    node.hops_away,
                    node.hw_model,
                    node.channel,
                    node.has_macaddr,
                    node.has_macaddr ? node.macaddr.data() : nullptr,
                    node.via_mqtt,
                    node.is_ignored,
                    node.has_public_key,
                    false,
                    node.has_device_metrics,
                    node.has_device_metrics ? &node.device_metrics : nullptr),
                0);
            if (node.has_position)
            {
                ::sys::EventBus::publish(
                    new ::sys::NodePositionUpdateEvent(
                        node.node_id,
                        node.position.latitude_i,
                        node.position.longitude_i,
                        node.position.has_altitude,
                        node.position.altitude,
                        node.position.timestamp,
                        node.position.precision_bits,
                        node.position.pdop,
                        node.position.hdop,
                        node.position.vdop,
                        node.position.gps_accuracy_mm),
                    0);
            }
            append_lora_system_log(
                "Meshtastic nodeinfo RX",
                "From " + node_hex(header.from) + " node " +
                    node_hex(node.node_id) + " / " + node.short_name + " / " +
                    node.long_name,
                {{
                     .kind =
                         ::platform::linux_runtime::PacketLogSegmentKind::Header,
                     .label = "air",
                     .text = "ch=" + hex_u8(header.channel) +
                             " flags=" + hex_u8(header.flags),
                 },
                 {
                     .kind =
                         ::platform::linux_runtime::PacketLogSegmentKind::Body,
                     .label = "node",
                     .text = std::string(node.via_mqtt ? "mqtt " : "mesh ") +
                             (node.has_position ? "position " : "") +
                             (node.has_device_metrics ? "metrics" : ""),
                 },
                 {
                     .kind =
                         ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                     .label = "signal",
                     .text = packet_signal_text(packet),
                 }});
            if (via_mqtt_packet || node.via_mqtt)
            {
                append_mqtt_system_log(
                    "Meshtastic MQTT nodeinfo RX",
                    "From " + node_hex(header.from) + " node " +
                        node_hex(node.node_id) + " / " + node.short_name +
                        " / " + node.long_name,
                    {{
                         .kind = ::platform::linux_runtime::
                             PacketLogSegmentKind::Header,
                         .label = "air",
                         .text = "ch=" + hex_u8(header.channel) +
                                 " flags=" + hex_u8(header.flags),
                     },
                     {
                         .kind = ::platform::linux_runtime::
                             PacketLogSegmentKind::Body,
                         .label = "node",
                         .text = std::string(node.has_position ? "position " : "") +
                                 (node.has_device_metrics ? "metrics" : ""),
                     }});
            }
            if (decoded_want_response &&
                (header.to == self_node_id_ || header.to == kBroadcastNodeId))
            {
                const std::uint32_t now_ms = ::sys::millis_now();
                bool allow_reply = true;
                const auto last_it = nodeinfo_last_reply_ms_.find(header.from);
                if (last_it != nodeinfo_last_reply_ms_.end() &&
                    static_cast<std::uint32_t>(now_ms - last_it->second) <
                        kNodeInfoReplySuppressMs)
                {
                    allow_reply = false;
                }
                nodeinfo_last_reply_ms_[header.from] = now_ms;
                if (allow_reply)
                {
                    (void)sendMeshtasticNodeInfoTo(header.from, false, channel);
                }
            }
        }
        else
        {
            const std::string reason = describe_nodeinfo_payload_failure(decoded);
            append_lora_system_log(
                "Meshtastic nodeinfo decode failed",
                "NODEINFO_APP payload was neither Meshtastic NodeInfo nor legacy User.",
                {{
                     .kind =
                         ::platform::linux_runtime::PacketLogSegmentKind::Error,
                     .label = "reason",
                     .text = reason,
                 },
                 {
                     .kind =
                         ::platform::linux_runtime::PacketLogSegmentKind::Error,
                     .label = "payload",
                     .text = ::platform::linux_runtime::hex_bytes(
                         decoded.payload.bytes,
                         decoded.payload.size),
                 }});
        }
        return true;
    }

    if (decoded.portnum == meshtastic_PortNum_POSITION_APP &&
        decoded.payload.size > 0)
    {
        ::chat::meshtastic::DecodedPositionPayload position{};
        if (::chat::meshtastic::decodePositionPayload(decoded,
                                                      header.from,
                                                      rx_meta.rx_timestamp_s,
                                                      &position))
        {
            ::sys::EventBus::publish(
                new ::sys::NodePositionUpdateEvent(
                    position.node_id,
                    position.position.latitude_i,
                    position.position.longitude_i,
                    position.position.has_altitude,
                    position.position.altitude,
                    position.position.timestamp,
                    position.position.precision_bits,
                    position.position.pdop,
                    position.position.hdop,
                    position.position.vdop,
                    position.position.gps_accuracy_mm),
                0);
            append_lora_system_log(
                "Meshtastic position RX",
                "From " + node_hex(header.from) + " position payload.",
                {{
                    .kind =
                        ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                    .label = "signal",
                    .text = packet_signal_text(packet),
                }});
            if (via_mqtt_packet)
            {
                append_mqtt_system_log(
                    "Meshtastic MQTT position RX",
                    "From " + node_hex(header.from) + " position payload.",
                    {{
                        .kind = ::platform::linux_runtime::
                            PacketLogSegmentKind::Body,
                        .label = "pos",
                        .text = ::platform::linux_runtime::hex_bytes(
                            decoded.payload.bytes,
                            decoded.payload.size),
                    }});
            }
        }
        else
        {
            const std::string reason = describe_position_payload_failure(decoded);
            append_lora_system_log(
                "Meshtastic position decode failed",
                "POSITION_APP payload did not contain a valid latitude and longitude.",
                {{
                     .kind =
                         ::platform::linux_runtime::PacketLogSegmentKind::Error,
                     .label = "reason",
                     .text = reason,
                 },
                 {
                     .kind =
                         ::platform::linux_runtime::PacketLogSegmentKind::Error,
                     .label = "payload",
                     .text = ::platform::linux_runtime::hex_bytes(
                         decoded.payload.bytes,
                         decoded.payload.size),
                 }});
        }
        return true;
    }

    if (decoded.portnum == meshtastic_PortNum_ROUTING_APP)
    {
        append_lora_system_log(
            "Meshtastic routing RX",
            "Routing payload from " + node_hex(header.from) + ".",
            {{
                .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                .label = "signal",
                .text = packet_signal_text(packet),
            }});
        return true;
    }

    if (decoded.portnum == meshtastic_PortNum_KEY_VERIFICATION_APP &&
        decoded.payload.size > 0)
    {
        meshtastic_KeyVerification kv = meshtastic_KeyVerification_init_default;
        bool handled = false;
        if (::chat::meshtastic::decodeKeyVerificationMessage(plaintext,
                                                             plaintext_len,
                                                             &kv) &&
            header.channel == 0)
        {
            if (kv.hash1.size == 0 && kv.hash2.size == 0)
            {
                handled = handleKeyVerificationInit(header, kv);
            }
            else if (kv.hash1.size == 0 && kv.hash2.size == 32)
            {
                handled = handleKeyVerificationReply(header, kv);
            }
            else if (kv.hash1.size == 32 && kv.hash2.size == 0)
            {
                handled = handleKeyVerificationFinal(header, kv);
            }
        }

        append_lora_system_log(
            handled ? "Meshtastic key verification RX"
                    : "Meshtastic key verification ignored",
            "From " + node_hex(header.from) + " id " + node_hex(header.id) +
                " nonce " + std::to_string(kv.nonce) + ".",
            {{
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                 .label = "stage",
                 .text = ::chat::meshtastic::keyVerificationStage(kv),
             },
             {
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                 .label = "signal",
                 .text = packet_signal_text(packet),
             }});
        return true;
    }

    ::chat::MeshIncomingData app{};
    if (::chat::meshtastic::decodeAppPayload(decoded, &app))
    {
        app.from = header.from;
        app.to = header.to;
        app.packet_id = header.id;
        app.request_id = decoded.request_id;
        app.channel = channel;
        app.channel_hash = header.channel;
        app.hop_limit =
            header.flags & ::chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
        app.want_response = decoded_want_response;
        app.rx_meta = rx_meta;
        incoming_data_.push_back(std::move(app));
    }
    append_lora_system_log(
        "Meshtastic appdata RX",
        std::string("Port ") + meshtastic_port_label(decoded.portnum) +
            " from " + node_hex(header.from) + " len " +
            std::to_string(decoded.payload.size) + ".",
        {{
             .kind = ::platform::linux_runtime::PacketLogSegmentKind::Header,
             .label = "air",
             .text = "ch=" + hex_u8(header.channel) +
                     " flags=" + hex_u8(header.flags),
         },
         {
             .kind = ::platform::linux_runtime::PacketLogSegmentKind::Body,
             .label = "payload",
             .text = ::platform::linux_runtime::hex_bytes(decoded.payload.bytes,
                                                          decoded.payload.size),
         },
         {
             .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
             .label = "signal",
             .text = packet_signal_text(packet),
         }});
    if (via_mqtt_packet)
    {
        append_mqtt_system_log(
            "Meshtastic MQTT appdata RX",
            std::string("Port ") + meshtastic_port_label(decoded.portnum) +
                " from " + node_hex(header.from) + " len " +
                std::to_string(decoded.payload.size) + ".",
            {{
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Header,
                 .label = "air",
                 .text = "ch=" + hex_u8(header.channel) +
                         " flags=" + hex_u8(header.flags),
             },
             {
                 .kind = ::platform::linux_runtime::PacketLogSegmentKind::Body,
                 .label = "payload",
                 .text = ::platform::linux_runtime::hex_bytes(
                     decoded.payload.bytes,
                     decoded.payload.size),
             }});
    }
    return true;
}

} // namespace trailmate::linux_app
