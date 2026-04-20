#include "platform/nrf52/arduino_common/chat/infra/meshtastic/meshtastic_radio_adapter.h"

#include "chat/domain/contact_types.h"
#include "chat/infra/meshtastic/mt_codec_pb.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/meshtastic/mt_pki_crypto.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "chat/runtime/meshtastic_self_announcement_core.h"
#include "chat/runtime/self_identity_policy.h"
#include "meshtastic/mqtt.pb.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"
#include "platform/nrf52/arduino_common/device_identity.h"
#include "platform/nrf52/arduino_common/sys/event_bus.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/settings_store.h"
#include "sys/clock.h"

#include <Arduino.h>
#include <Curve25519.h>
#include <RNG.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <string>

namespace platform::nrf52::arduino_common::chat::meshtastic
{
namespace
{
constexpr size_t kMaxMqttProxyQueue = 12;
constexpr size_t kPacketHistoryCapacity = 160;
constexpr uint32_t kPacketHistoryTimeoutMs = 10UL * 60UL * 1000UL;
constexpr uint8_t kDefaultNextHopRetries = 2;
constexpr uint8_t kDefaultAckRetries = 3;
constexpr uint32_t kRetransmitIntervalMs = 1500;
constexpr uint32_t kImplicitAckTimeoutMs = 15000;
constexpr uint32_t kNodeInfoIntervalMs = 3UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kNodeInfoReplySuppressMs = 12UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kPositionReplySuppressMs = 3UL * 60UL * 1000UL;
constexpr uint32_t kKeyVerificationTimeoutMs = 60000UL;
constexpr uint32_t kPkiNodeSaveDebounceMs = 2000UL;
constexpr ::chat::NodeId kBroadcastNode = 0xFFFFFFFFUL;
constexpr uint8_t kDefaultPskIndex = 1;
constexpr uint8_t kBitfieldWantResponseMask = 0x02U;
constexpr size_t kMaxPkiNodes = 16;
constexpr const char* kPkiNodeNs = "chat_pki";
constexpr const char* kPkiNodeKey = "pki_nodes";
constexpr const char* kPkiPublicNs = "chat";
constexpr const char* kPkiPublicKey = "pki_pub";
constexpr const char* kPkiPrivateKey = "pki_priv";

using ::chat::meshtastic::allowPkiForPortnum;
using ::chat::meshtastic::computeKeyVerificationHashes;
using ::chat::meshtastic::expandShortPsk;
using ::chat::meshtastic::fillDecodedPacketCommon;
using ::chat::meshtastic::hashSharedKey;
using ::chat::meshtastic::initPkiNonce;
using ::chat::meshtastic::isZeroKey;
using ::chat::meshtastic::keyVerificationStage;
using ::chat::meshtastic::makeEncryptedPacketFromWire;
using ::chat::meshtastic::readPbString;
using ::chat::meshtastic::shouldSetAirWantAck;
using ::chat::meshtastic::toHex;

void logMeshtasticRx(const char* format, ...);
uint32_t nowSeconds();

bool shouldRequireDirectPki(uint8_t encrypt_mode, ::chat::NodeId dest_node, uint32_t portnum)
{
    return encrypt_mode != 0 &&
           dest_node != kBroadcastNode &&
           allowPkiForPortnum(portnum);
}

void logMeshtasticRx(const char* format, ...)
{
    char buffer[192] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.print(buffer);
    Serial2.print(buffer);
}

void toHexString(const uint8_t* data, size_t len, char* out, size_t out_len, size_t max_len = 64)
{
    if (!out || out_len == 0)
    {
        return;
    }

    out[0] = '\0';
    if (!data || len == 0)
    {
        return;
    }

    static constexpr char kHex[] = "0123456789ABCDEF";
    const size_t capped = std::min(len, max_len);
    size_t pos = 0;
    for (size_t i = 0; i < capped && pos + 2 < out_len; ++i)
    {
        const uint8_t b = data[i];
        out[pos++] = kHex[(b >> 4) & 0x0F];
        out[pos++] = kHex[b & 0x0F];
    }
    if (capped < len && pos + 2 < out_len)
    {
        out[pos++] = '.';
        out[pos++] = '.';
    }
    out[pos] = '\0';
}

std::array<uint8_t, 6> readMac()
{
    return device_identity::deriveMacAddressFromDeviceAddress(NRF_FICR->DEVICEADDR[0],
                                                              NRF_FICR->DEVICEADDR[1]);
}

const uint8_t* selectKey(const ::chat::MeshConfig& config,
                         ::chat::ChannelId channel,
                         size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0;
    }

    if (channel == ::chat::ChannelId::SECONDARY)
    {
        if (!isZeroKey(config.secondary_key, sizeof(config.secondary_key)))
        {
            if (out_len)
            {
                *out_len = sizeof(config.secondary_key);
            }
            return config.secondary_key;
        }
        return nullptr;
    }

    if (!isZeroKey(config.primary_key, sizeof(config.primary_key)))
    {
        if (out_len)
        {
            *out_len = sizeof(config.primary_key);
        }
        return config.primary_key;
    }

    static uint8_t default_primary_psk[16] = {};
    size_t expanded_len = 0;
    expandShortPsk(kDefaultPskIndex, default_primary_psk, &expanded_len);
    if (out_len)
    {
        *out_len = expanded_len;
    }
    return expanded_len > 0 ? default_primary_psk : nullptr;
}

const char* channelNameFor(const ::chat::MeshConfig& config, ::chat::ChannelId channel)
{
    if (channel == ::chat::ChannelId::SECONDARY)
    {
        return "Secondary";
    }

    if (config.use_preset)
    {
        const char* preset_name = ::chat::meshtastic::presetDisplayName(
            static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(config.modem_preset));
        if (preset_name && preset_name[0] != '\0' && std::strcmp(preset_name, "Invalid") != 0)
        {
            return preset_name;
        }
    }
    return "Custom";
}

const char* channelDebugLabel(::chat::ChannelId channel)
{
    return (channel == ::chat::ChannelId::SECONDARY) ? "secondary" : "primary";
}

uint8_t channelHashFor(const ::chat::MeshConfig& config, ::chat::ChannelId channel)
{
    size_t key_len = 0;
    const uint8_t* key = selectKey(config, channel, &key_len);
    return ::chat::meshtastic::computeChannelHash(channelNameFor(config, channel), key, key_len);
}

bool decodeMeshtasticData(const uint8_t* plain, size_t plain_len, meshtastic_Data* out_decoded)
{
    if (!plain || plain_len == 0 || !out_decoded)
    {
        return false;
    }

    *out_decoded = meshtastic_Data_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(plain, plain_len);
    return pb_decode(&stream, meshtastic_Data_fields, out_decoded);
}

bool isPlausibleDecodedData(const meshtastic_Data& decoded)
{
    if (decoded.portnum == meshtastic_PortNum_UNKNOWN_APP ||
        decoded.portnum > meshtastic_PortNum_MAX)
    {
        return false;
    }

    return decoded.payload.size > 0 ||
           decoded.request_id != 0 ||
           decoded.reply_id != 0 ||
           decoded.want_response ||
           decoded.has_bitfield ||
           decoded.portnum == meshtastic_PortNum_ROUTING_APP;
}

bool tryDecodeWithChannelKey(const ::chat::MeshConfig& config,
                             ::chat::ChannelId channel,
                             const ::chat::meshtastic::PacketHeaderWire& header,
                             const uint8_t* payload,
                             size_t payload_size,
                             uint8_t* out_plain,
                             size_t* inout_plain_len,
                             meshtastic_Data* out_decoded,
                             const uint8_t** out_key,
                             size_t* out_key_len)
{
    if (!payload || !out_plain || !inout_plain_len || !out_decoded)
    {
        return false;
    }

    size_t key_len = 0;
    const uint8_t* key = selectKey(config, channel, &key_len);
    size_t plain_len = *inout_plain_len;

    if (key && key_len > 0)
    {
        if (!::chat::meshtastic::decryptPayload(header,
                                                payload,
                                                payload_size,
                                                key,
                                                key_len,
                                                out_plain,
                                                &plain_len))
        {
            return false;
        }
    }
    else
    {
        if (plain_len < payload_size)
        {
            *inout_plain_len = payload_size;
            return false;
        }

        std::memcpy(out_plain, payload, payload_size);
        plain_len = payload_size;
    }

    meshtastic_Data decoded = meshtastic_Data_init_zero;
    if (!decodeMeshtasticData(out_plain, plain_len, &decoded) ||
        !isPlausibleDecodedData(decoded))
    {
        return false;
    }

    *inout_plain_len = plain_len;
    *out_decoded = decoded;
    if (out_key)
    {
        *out_key = key;
    }
    if (out_key_len)
    {
        *out_key_len = key_len;
    }
    return true;
}

bool resolvePacketWithKnownKeys(const ::chat::MeshConfig& config,
                                const ::chat::meshtastic::PacketHeaderWire& header,
                                const uint8_t* payload,
                                size_t payload_size,
                                bool skip_channel,
                                ::chat::ChannelId skipped,
                                ::chat::ChannelId* out_channel,
                                const uint8_t** out_key,
                                size_t* out_key_len,
                                uint8_t* out_plain,
                                size_t* inout_plain_len,
                                meshtastic_Data* out_decoded)
{
    static constexpr ::chat::ChannelId kCandidates[] = {
        ::chat::ChannelId::PRIMARY,
        ::chat::ChannelId::SECONDARY,
    };

    for (::chat::ChannelId candidate : kCandidates)
    {
        if (skip_channel && candidate == skipped)
        {
            continue;
        }

        size_t candidate_plain_len = *inout_plain_len;
        meshtastic_Data decoded = meshtastic_Data_init_zero;
        const uint8_t* key = nullptr;
        size_t key_len = 0;
        if (!tryDecodeWithChannelKey(config,
                                     candidate,
                                     header,
                                     payload,
                                     payload_size,
                                     out_plain,
                                     &candidate_plain_len,
                                     &decoded,
                                     &key,
                                     &key_len))
        {
            continue;
        }

        *inout_plain_len = candidate_plain_len;
        *out_decoded = decoded;
        if (out_channel)
        {
            *out_channel = candidate;
        }
        if (out_key)
        {
            *out_key = key;
        }
        if (out_key_len)
        {
            *out_key_len = key_len;
        }
        return true;
    }

    return false;
}

const uint8_t* selectKeyByHash(const ::chat::MeshConfig& config,
                               uint8_t channel_hash,
                               size_t* out_len,
                               ::chat::ChannelId* out_channel)
{
    if (out_len)
    {
        *out_len = 0;
    }
    if (out_channel)
    {
        *out_channel = ::chat::ChannelId::PRIMARY;
    }

    size_t key_len = 0;
    const uint8_t* key = selectKey(config, ::chat::ChannelId::PRIMARY, &key_len);
    if (::chat::meshtastic::computeChannelHash(channelNameFor(config, ::chat::ChannelId::PRIMARY),
                                               key,
                                               key_len) == channel_hash)
    {
        if (out_len)
        {
            *out_len = key_len;
        }
        return key;
    }

    key = selectKey(config, ::chat::ChannelId::SECONDARY, &key_len);
    if (::chat::meshtastic::computeChannelHash(channelNameFor(config, ::chat::ChannelId::SECONDARY),
                                               key,
                                               key_len) == channel_hash)
    {
        if (out_len)
        {
            *out_len = key_len;
        }
        if (out_channel)
        {
            *out_channel = ::chat::ChannelId::SECONDARY;
        }
        return key;
    }

    return nullptr;
}

uint32_t nowSeconds()
{
    return sys::epoch_seconds_now();
}

bool buildSelfPositionPayload(uint8_t* out_buf, size_t* out_len)
{
    if (!out_buf || !out_len || *out_len == 0)
    {
        return false;
    }

    const platform::ui::gps::GpsState gps_state = platform::ui::gps::get_data();
    if (!gps_state.valid)
    {
        return false;
    }

    meshtastic_Position pos = meshtastic_Position_init_zero;
    pos.has_latitude_i = true;
    pos.latitude_i = static_cast<int32_t>(gps_state.lat * 1e7);
    pos.has_longitude_i = true;
    pos.longitude_i = static_cast<int32_t>(gps_state.lng * 1e7);
    pos.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;

    if (gps_state.has_alt)
    {
        pos.has_altitude = true;
        pos.altitude = static_cast<int32_t>(lround(gps_state.alt_m));
        pos.altitude_source = meshtastic_Position_AltSource_ALT_INTERNAL;
    }
    if (gps_state.has_speed)
    {
        pos.has_ground_speed = true;
        pos.ground_speed = static_cast<uint32_t>(lround(gps_state.speed_mps));
    }
    if (gps_state.has_course)
    {
        double course = gps_state.course_deg;
        if (course < 0.0)
        {
            course = 0.0;
        }
        uint32_t cdeg = static_cast<uint32_t>(lround(course * 100.0));
        if (cdeg >= 36000U)
        {
            cdeg = 35999U;
        }
        pos.has_ground_track = true;
        pos.ground_track = cdeg;
    }
    if (gps_state.satellites > 0)
    {
        pos.sats_in_view = gps_state.satellites;
    }

    const uint32_t ts = nowSeconds();
    if (ts >= 1577836800U)
    {
        pos.timestamp = ts;
    }

    pb_ostream_t stream = pb_ostream_from_buffer(out_buf, *out_len);
    if (!pb_encode(&stream, meshtastic_Position_fields, &pos))
    {
        return false;
    }

    *out_len = stream.bytes_written;
    return true;
}

} // namespace

MeshtasticRadioAdapter::MeshtasticRadioAdapter(const ::chat::runtime::SelfIdentityProvider* identity_provider,
                                               NodeStore* node_store,
                                               ::chat::contacts::ContactService* contact_service)
    : node_id_(device_identity::getSelfNodeId()),
      identity_provider_(identity_provider),
      node_store_(node_store),
      contact_service_(contact_service)
{
    randomSeed(static_cast<unsigned long>(NRF_FICR->DEVICEADDR[0] ^ NRF_FICR->DEVICEADDR[1] ^ micros()));
    next_packet_id_ = static_cast<::chat::MessageId>(random(1, 0x7FFFFFFF));
    pki_ready_ = initPkiKeys();
    loadPkiNodeKeys();
}

::chat::MeshCapabilities MeshtasticRadioAdapter::getCapabilities() const
{
    ::chat::MeshCapabilities caps{};
    caps.supports_unicast_text = true;
    caps.supports_unicast_appdata = true;
    caps.supports_broadcast_appdata = true;
    caps.supports_appdata_ack = true;
    caps.provides_appdata_sender = true;
    caps.supports_node_info = true;
    caps.supports_pki = true;
    return caps;
}

bool MeshtasticRadioAdapter::sendText(::chat::ChannelId channel, const std::string& text,
                                      ::chat::MessageId* out_msg_id, ::chat::NodeId peer)
{
    return sendTextWithId(channel, text, 0, out_msg_id, peer);
}

bool MeshtasticRadioAdapter::sendTextWithId(::chat::ChannelId channel, const std::string& text,
                                            ::chat::MessageId forced_msg_id,
                                            ::chat::MessageId* out_msg_id, ::chat::NodeId peer)
{
    if (!isReady() || text.empty() || !config_.tx_enabled)
    {
        return false;
    }

    ::chat::ChannelId out_channel = channel;
    if (encrypt_mode_ == 0 || encrypt_mode_ == 2)
    {
        out_channel = ::chat::ChannelId::PRIMARY;
    }

    if (shouldRequireDirectPki(encrypt_mode_,
                               (peer == 0) ? kBroadcastNode : peer,
                               meshtastic_PortNum_TEXT_MESSAGE_APP))
    {
        const ::chat::MessageId packet_id = (forced_msg_id != 0) ? forced_msg_id : next_packet_id_;
        const bool ok = sendAppData(out_channel,
                                    meshtastic_PortNum_TEXT_MESSAGE_APP,
                                    reinterpret_cast<const uint8_t*>(text.data()),
                                    text.size(),
                                    peer,
                                    true,
                                    packet_id,
                                    false);
        if (ok && out_msg_id)
        {
            *out_msg_id = packet_id;
        }
        return ok;
    }

    auto& scratch = tx_scratch_;
    std::fill(scratch.app_data.begin(), scratch.app_data.end(), 0);
    std::fill(scratch.wire.begin(), scratch.wire.end(), 0);
    uint8_t* payload = scratch.app_data.data();
    size_t payload_size = scratch.app_data.size();
    const ::chat::MessageId packet_id = (forced_msg_id != 0) ? forced_msg_id : next_packet_id_++;
    if (forced_msg_id != 0 && forced_msg_id >= next_packet_id_)
    {
        next_packet_id_ = forced_msg_id + 1;
        if (next_packet_id_ == 0)
        {
            next_packet_id_ = 1;
        }
    }
    const ::chat::NodeId dest = (peer == 0) ? kBroadcastNode : peer;
    if (!::chat::meshtastic::encodeTextMessage(out_channel,
                                               text,
                                               node_id_,
                                               packet_id,
                                               dest,
                                               payload,
                                               &payload_size))
    {
        return false;
    }

    size_t key_len = 0;
    const uint8_t* key = selectKey(config_, out_channel, &key_len);
    const uint8_t channel_hash = ::chat::meshtastic::computeChannelHash(channelNameFor(config_, out_channel),
                                                                        key,
                                                                        key_len);
    const bool track_ack = true;
    const bool air_want_ack = shouldSetAirWantAck(dest, track_ack);

    uint8_t* wire = scratch.wire.data();
    size_t wire_size = scratch.wire.size();
    if (!::chat::meshtastic::buildWirePacket(payload,
                                             payload_size,
                                             node_id_,
                                             packet_id,
                                             dest,
                                             channel_hash,
                                             config_.hop_limit,
                                             air_want_ack,
                                             key,
                                             key_len,
                                             wire,
                                             &wire_size))
    {
        return false;
    }

    meshtastic_Data mqtt_data = meshtastic_Data_init_default;
    mqtt_data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    mqtt_data.want_response = false;
    mqtt_data.dest = dest;
    mqtt_data.source = node_id_;
    mqtt_data.has_bitfield = true;
    mqtt_data.payload.size = std::min(text.size(), sizeof(mqtt_data.payload.bytes));
    if (mqtt_data.payload.size > 0)
    {
        std::memcpy(mqtt_data.payload.bytes, text.data(), mqtt_data.payload.size);
    }

    if (!transmitPreparedWire(wire, wire_size, out_channel, &mqtt_data, track_ack, true, 0, true))
    {
        return false;
    }

    if (out_msg_id)
    {
        *out_msg_id = packet_id;
    }
    return true;
}

bool MeshtasticRadioAdapter::pollIncomingText(::chat::MeshIncomingText* out)
{
    if (!out || text_queue_.empty())
    {
        return false;
    }
    *out = text_queue_.front();
    text_queue_.pop();
    return true;
}

bool MeshtasticRadioAdapter::sendAppData(::chat::ChannelId channel, uint32_t portnum,
                                         const uint8_t* payload, size_t len,
                                         ::chat::NodeId dest, bool want_ack,
                                         ::chat::MessageId packet_id,
                                         bool want_response)
{
    if (!isReady() || !payload || len == 0 || !config_.tx_enabled)
    {
        return false;
    }

    ::chat::ChannelId out_channel = channel;
    if (encrypt_mode_ == 0 || encrypt_mode_ == 2)
    {
        out_channel = ::chat::ChannelId::PRIMARY;
    }

    const bool is_broadcast = (dest == 0 || dest == kBroadcastNode);
    if (is_broadcast)
    {
        want_ack = false;
        want_response = false;
    }

    const bool effective_want_response = want_response || want_ack;

    auto& scratch = tx_scratch_;
    std::fill(scratch.app_data.begin(), scratch.app_data.end(), 0);
    std::fill(scratch.aux_data.begin(), scratch.aux_data.end(), 0);
    std::fill(scratch.wire.begin(), scratch.wire.end(), 0);
    uint8_t* data_pb = scratch.app_data.data();
    size_t data_pb_size = scratch.app_data.size();
    if (!::chat::meshtastic::encodeAppData(portnum, payload, len, effective_want_response, data_pb, &data_pb_size))
    {
        return false;
    }

    if (packet_id == 0)
    {
        packet_id = next_packet_id_++;
    }
    else if (packet_id >= next_packet_id_)
    {
        next_packet_id_ = packet_id + 1;
        if (next_packet_id_ == 0)
        {
            next_packet_id_ = 1;
        }
    }

    const ::chat::NodeId wire_dest = (dest == 0) ? kBroadcastNode : dest;
    size_t key_len = 0;
    const uint8_t* key = selectKey(config_, out_channel, &key_len);
    uint8_t channel_hash = ::chat::meshtastic::computeChannelHash(channelNameFor(config_, out_channel),
                                                                  key,
                                                                  key_len);
    const uint8_t* wire_payload = data_pb;
    size_t wire_payload_len = data_pb_size;
    bool use_pki = false;
    bool track_ack = want_ack;
    if (shouldRequireDirectPki(encrypt_mode_, wire_dest, portnum))
    {
        if (!pki_ready_ || !allowPkiForPortnum(portnum) || !hasPkiKey(wire_dest))
        {
            return false;
        }

        uint8_t* pki_buf = scratch.aux_data.data();
        size_t pki_len = scratch.aux_data.size();
        if (!encryptPkiPayload(wire_dest, packet_id, data_pb, data_pb_size, pki_buf, &pki_len))
        {
            return false;
        }

        wire_payload = pki_buf;
        wire_payload_len = pki_len;
        key = nullptr;
        key_len = 0;
        channel_hash = 0;
        track_ack = true;
        want_ack = true;
        use_pki = true;
    }

    uint8_t* wire = scratch.wire.data();
    size_t wire_size = scratch.wire.size();
    if (!::chat::meshtastic::buildWirePacket(wire_payload,
                                             wire_payload_len,
                                             node_id_,
                                             packet_id,
                                             wire_dest,
                                             channel_hash,
                                             config_.hop_limit,
                                             want_ack,
                                             key,
                                             key_len,
                                             wire,
                                             &wire_size))
    {
        return false;
    }

    meshtastic_Data mqtt_data = meshtastic_Data_init_default;
    mqtt_data.portnum = static_cast<meshtastic_PortNum>(portnum);
    mqtt_data.want_response = effective_want_response;
    mqtt_data.dest = wire_dest;
    mqtt_data.source = node_id_;
    mqtt_data.has_bitfield = true;
    mqtt_data.payload.size = std::min(len, sizeof(mqtt_data.payload.bytes));
    if (mqtt_data.payload.size > 0)
    {
        std::memcpy(mqtt_data.payload.bytes, payload, mqtt_data.payload.size);
    }

    return transmitPreparedWire(wire,
                                wire_size,
                                out_channel,
                                use_pki ? nullptr : &mqtt_data,
                                track_ack,
                                true,
                                0,
                                want_ack);
}

bool MeshtasticRadioAdapter::pollIncomingData(::chat::MeshIncomingData* out)
{
    if (!out || data_queue_.empty())
    {
        return false;
    }
    *out = data_queue_.front();
    data_queue_.pop();
    return true;
}

bool MeshtasticRadioAdapter::requestNodeInfo(::chat::NodeId dest, bool want_response)
{
    return buildAndQueueNodeInfo(dest == 0 ? kBroadcastNode : dest, want_response, ::chat::ChannelId::PRIMARY);
}

bool MeshtasticRadioAdapter::startKeyVerification(::chat::NodeId node_id)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::Idle)
    {
        return false;
    }
    if (!pki_ready_ || node_public_keys_.find(node_id) == node_public_keys_.end())
    {
        return false;
    }

    kv_remote_node_ = node_id;
    kv_nonce_ = static_cast<uint64_t>(random());
    kv_nonce_ms_ = millis();
    kv_security_number_ = 0;
    kv_hash1_.fill(0);
    kv_hash2_.fill(0);

    meshtastic_KeyVerification init = meshtastic_KeyVerification_init_zero;
    init.nonce = kv_nonce_;
    if (!sendKeyVerificationPacket(kv_remote_node_, init, true))
    {
        resetKeyVerificationState();
        return false;
    }

    kv_state_ = KeyVerificationState::SenderInitiated;
    return true;
}

bool MeshtasticRadioAdapter::submitKeyVerificationNumber(::chat::NodeId node_id, uint64_t nonce, uint32_t number)
{
    return processKeyVerificationNumber(node_id, nonce, number);
}

bool MeshtasticRadioAdapter::isPkiReady() const
{
    return pki_ready_;
}

bool MeshtasticRadioAdapter::hasPkiKey(::chat::NodeId dest) const
{
    return node_public_keys_.find(dest) != node_public_keys_.end();
}

void MeshtasticRadioAdapter::applyConfig(const ::chat::MeshConfig& config)
{
    config_ = config;
}

void MeshtasticRadioAdapter::setUserInfo(const char* long_name, const char* short_name)
{
    long_name_ = long_name ? long_name : "";
    short_name_ = short_name ? short_name : "";
}

void MeshtasticRadioAdapter::setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent)
{
    (void)duty_cycle_enabled;
    (void)util_percent;
}

void MeshtasticRadioAdapter::setPrivacyConfig(uint8_t encrypt_mode)
{
    encrypt_mode_ = encrypt_mode;
    if (!pki_ready_)
    {
        pki_ready_ = initPkiKeys();
        loadPkiNodeKeys();
    }
}

bool MeshtasticRadioAdapter::isReady() const
{
    return ::platform::nrf52::arduino_common::chat::infra::radioPacketIo() != nullptr;
}

::chat::NodeId MeshtasticRadioAdapter::getNodeId() const
{
    return node_id_;
}

void MeshtasticRadioAdapter::setMqttProxySettings(const MqttProxySettings& settings)
{
    mqtt_proxy_settings_ = settings;
}

bool MeshtasticRadioAdapter::pollMqttProxyMessage(meshtastic_MqttClientProxyMessage* out)
{
    if (!out || mqtt_proxy_queue_.empty())
    {
        return false;
    }

    *out = mqtt_proxy_queue_.front();
    mqtt_proxy_queue_.pop();
    return true;
}

bool MeshtasticRadioAdapter::handleMqttProxyMessage(const meshtastic_MqttClientProxyMessage& msg)
{
    if (!mqtt_proxy_settings_.enabled || !mqtt_proxy_settings_.proxy_to_client_enabled)
    {
        return false;
    }
    if (msg.which_payload_variant != meshtastic_MqttClientProxyMessage_data_tag)
    {
        return false;
    }

    const auto* data_field = &msg.payload_variant.data;
    if (!data_field || data_field->size == 0)
    {
        return false;
    }

    auto& scratch = mqtt_scratch_;
    std::memset(&scratch.packet, 0, sizeof(scratch.packet));
    std::memset(scratch.channel_id, 0, sizeof(scratch.channel_id));
    std::memset(scratch.gateway_id, 0, sizeof(scratch.gateway_id));
    if (!decodeMqttServiceEnvelope(data_field->bytes, data_field->size,
                                   &scratch.packet,
                                   scratch.channel_id, sizeof(scratch.channel_id),
                                   scratch.gateway_id, sizeof(scratch.gateway_id)))
    {
        return false;
    }

    return injectMqttEnvelope(scratch.packet, scratch.channel_id, scratch.gateway_id);
}

bool MeshtasticRadioAdapter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    if (!has_pending_raw_packet_ || !out_data || max_len == 0)
    {
        out_len = 0;
        return false;
    }

    const size_t copy_len = (last_raw_packet_len_ < max_len) ? last_raw_packet_len_ : max_len;
    std::memcpy(out_data, last_raw_packet_, copy_len);
    out_len = copy_len;
    has_pending_raw_packet_ = false;
    return true;
}

void MeshtasticRadioAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    if (!data || size == 0)
    {
        return;
    }

    if (size <= sizeof(last_raw_packet_))
    {
        std::memcpy(last_raw_packet_, data, size);
        last_raw_packet_len_ = size;
        has_pending_raw_packet_ = true;
    }

    // Keep large protobuf scratch buffers off the task stack. This path can nest
    // MQTT proxy packaging and BLE forwarding on nRF52.
    auto& rx = rx_scratch_;
    std::memset(&rx.header, 0, sizeof(rx.header));
    std::fill(rx.payload.begin(), rx.payload.end(), 0);
    std::fill(rx.plain.begin(), rx.plain.end(), 0);
    std::memset(&rx.decoded, 0, sizeof(rx.decoded));

    auto& header = rx.header;
    uint8_t* payload = rx.payload.data();
    size_t payload_size = rx.payload.size();
    if (!::chat::meshtastic::parseWirePacket(data, size, &header, payload, &payload_size))
    {
        logMeshtasticRx("[gat562][mt] parse fail len=%u\n", static_cast<unsigned>(size));
        return;
    }

    HistoryResult history{};
    if (header.from != node_id_)
    {
        history = updatePacketHistory(header, true);
        if (history.seen_recently && !history.was_upgraded)
        {
            logMeshtasticRx("[gat562][mt] dedup from=%08lX id=%lu relay=%u ch=%u\n",
                            static_cast<unsigned long>(header.from),
                            static_cast<unsigned long>(header.id),
                            static_cast<unsigned>(header.relay_node),
                            static_cast<unsigned>(header.channel));
            return;
        }
    }

    uint8_t* plain = rx.plain.data();
    size_t plain_len = rx.plain.size();
    ::chat::ChannelId channel = ::chat::ChannelId::PRIMARY;
    size_t key_len = 0;
    const uint8_t* key = selectKeyByHash(config_, header.channel, &key_len, &channel);
    const uint8_t primary_hash = channelHashFor(config_, ::chat::ChannelId::PRIMARY);
    const uint8_t secondary_hash = channelHashFor(config_, ::chat::ChannelId::SECONDARY);
    const bool want_ack_flag = (header.flags & ::chat::meshtastic::PACKET_FLAGS_WANT_ACK_MASK) != 0;

    auto& decoded = rx.decoded;
    bool decoded_ok = false;

    if (header.channel == 0)
    {
        auto channel_it = node_last_channel_.find(header.from);
        if (channel_it != node_last_channel_.end())
        {
            channel = channel_it->second;
        }

        if (header.to != node_id_ || header.to == kBroadcastNode || payload_size <= 12)
        {
            return;
        }

        if (!pki_ready_)
        {
            (void)buildAndQueueNodeInfo(header.from, true, ::chat::ChannelId::PRIMARY);

            size_t primary_key_len = 0;
            const uint8_t* primary_key = selectKey(config_, ::chat::ChannelId::PRIMARY, &primary_key_len);
            (void)sendRoutingError(header.from,
                                   header.id,
                                   primary_hash,
                                   ::chat::ChannelId::PRIMARY,
                                   primary_key,
                                   primary_key_len,
                                   meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY,
                                   0);
            return;
        }

        if (!decryptPkiPayload(header.from, header.id, payload, payload_size, plain, &plain_len))
        {
            return;
        }
    }
    else if (!key)
    {
        if (!resolvePacketWithKnownKeys(config_,
                                        header,
                                        payload,
                                        payload_size,
                                        false,
                                        ::chat::ChannelId::PRIMARY,
                                        &channel,
                                        &key,
                                        &key_len,
                                        plain,
                                        &plain_len,
                                        &decoded))
        {
            logMeshtasticRx("[gat562][mt] unknown channel from=%08lX to=%08lX id=%lu ch=%u local[p=%u s=%u]\n",
                            static_cast<unsigned long>(header.from),
                            static_cast<unsigned long>(header.to),
                            static_cast<unsigned long>(header.id),
                            static_cast<unsigned>(header.channel),
                            static_cast<unsigned>(primary_hash),
                            static_cast<unsigned>(secondary_hash));
            return;
        }

        decoded_ok = true;
        logMeshtasticRx("[gat562][mt] channel fallback from=%08lX to=%08lX id=%lu rx_ch=%u local[p=%u s=%u] via=%s\n",
                        static_cast<unsigned long>(header.from),
                        static_cast<unsigned long>(header.to),
                        static_cast<unsigned long>(header.id),
                        static_cast<unsigned>(header.channel),
                        static_cast<unsigned>(primary_hash),
                        static_cast<unsigned>(secondary_hash),
                        channelDebugLabel(channel));
    }
    else if (!::chat::meshtastic::decryptPayload(header, payload, payload_size,
                                                 key, key_len, plain, &plain_len))
    {
        logMeshtasticRx("[gat562][mt] decrypt fail from=%08lX to=%08lX id=%lu ch=%u key=%u ack=%u\n",
                        static_cast<unsigned long>(header.from),
                        static_cast<unsigned long>(header.to),
                        static_cast<unsigned long>(header.id),
                        static_cast<unsigned>(header.channel),
                        static_cast<unsigned>(key_len),
                        static_cast<unsigned>(want_ack_flag ? 1U : 0U));

        size_t primary_key_len = 0;
        const uint8_t* primary_key = selectKey(config_, ::chat::ChannelId::PRIMARY, &primary_key_len);

        if (header.to == node_id_ && want_ack_flag)
        {
            if (header.channel == 0)
            {
                (void)buildAndQueueNodeInfo(header.from, true, ::chat::ChannelId::PRIMARY);
                (void)sendRoutingError(header.from,
                                       header.id,
                                       primary_hash,
                                       ::chat::ChannelId::PRIMARY,
                                       primary_key,
                                       primary_key_len,
                                       meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY,
                                       0);
            }
            else if (header.channel != primary_hash && header.channel != secondary_hash)
            {
                (void)buildAndQueueNodeInfo(header.from, true, ::chat::ChannelId::PRIMARY);
                (void)sendRoutingError(header.from,
                                       header.id,
                                       primary_hash,
                                       ::chat::ChannelId::PRIMARY,
                                       primary_key,
                                       primary_key_len,
                                       meshtastic_Routing_Error_NO_CHANNEL,
                                       0);
            }
        }
        return;
    }

    const uint8_t hop_limit = header.flags & ::chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
    const uint8_t hop_start =
        (header.flags & ::chat::meshtastic::PACKET_FLAGS_HOP_START_MASK) >>
        ::chat::meshtastic::PACKET_FLAGS_HOP_START_SHIFT;
    const bool is_broadcast = (header.to == kBroadcastNode);
    const bool to_us = (header.to == node_id_);
    const bool to_us_or_broadcast = to_us || is_broadcast;

    if (header.from == node_id_)
    {
        const auto pending_it = pending_retransmits_.find(pendingKey(header.from, header.id));
        if (is_broadcast && pending_it != pending_retransmits_.end())
        {
            logMeshtasticRx("[gat562][mt] implicit-ack observed self-broadcast id=%08lX relay=%u next=%u ch=%u\n",
                            static_cast<unsigned long>(header.id),
                            static_cast<unsigned>(header.relay_node),
                            static_cast<unsigned>(header.next_hop),
                            static_cast<unsigned>(header.channel));
            ::chat::RxMeta implicit_rx{};
            implicit_rx.rx_timestamp_ms = millis();
            implicit_rx.rx_timestamp_s = nowSeconds();
            implicit_rx.time_source = ::chat::RxTimeSource::DeviceUtc;
            implicit_rx.origin = ::chat::RxOrigin::Mesh;
            implicit_rx.channel_hash = header.channel;
            implicit_rx.next_hop = header.next_hop;
            implicit_rx.relay_node = header.relay_node;
            pending_retransmits_.erase(pending_it);
            emitRoutingResult(header.id,
                              meshtastic_Routing_Error_NONE,
                              node_id_,
                              node_id_,
                              channel,
                              header.channel,
                              &implicit_rx);
            return;
        }

        logMeshtasticRx("[gat562][mt] self drop from=%08lX id=%lu relay=%u ch=%u\n",
                        static_cast<unsigned long>(header.from),
                        static_cast<unsigned long>(header.id),
                        static_cast<unsigned>(header.relay_node),
                        static_cast<unsigned>(header.channel));
        return;
    }

    ::chat::RxMeta rx_meta{};
    rx_meta.rx_timestamp_ms = millis();
    rx_meta.rx_timestamp_s = nowSeconds();
    rx_meta.time_source = ::chat::RxTimeSource::DeviceUtc;
    rx_meta.origin = ::chat::RxOrigin::Mesh;
    rx_meta.direct = (::chat::meshtastic::computeHopsAway(header.flags) == 0);
    rx_meta.hop_count = (hop_start >= hop_limit) ? static_cast<uint8_t>(hop_start - hop_limit) : 0;
    rx_meta.hop_limit = hop_limit;
    rx_meta.channel_hash = header.channel;
    rx_meta.wire_flags = header.flags;
    rx_meta.rssi_dbm_x10 = static_cast<int16_t>(last_rx_rssi_ * 10.0f);
    rx_meta.snr_db_x10 = static_cast<int16_t>(last_rx_snr_ * 10.0f);
    rx_meta.next_hop = header.next_hop;
    rx_meta.relay_node = header.relay_node;

    if (!decoded_ok)
    {
        decoded_ok = decodeMeshtasticData(plain, plain_len, &decoded);
        if (!decoded_ok && header.channel != 0 && key)
        {
            size_t probe_plain_len = sizeof(plain);
            if (resolvePacketWithKnownKeys(config_,
                                           header,
                                           payload,
                                           payload_size,
                                           true,
                                           channel,
                                           &channel,
                                           &key,
                                           &key_len,
                                           plain,
                                           &probe_plain_len,
                                           &decoded))
            {
                plain_len = probe_plain_len;
                decoded_ok = true;
                logMeshtasticRx("[gat562][mt] hash collision fallback from=%08lX to=%08lX id=%lu rx_ch=%u via=%s\n",
                                static_cast<unsigned long>(header.from),
                                static_cast<unsigned long>(header.to),
                                static_cast<unsigned long>(header.id),
                                static_cast<unsigned>(header.channel),
                                channelDebugLabel(channel));
            }
        }
    }

    if (decoded_ok)
    {
        logMeshtasticRx("[gat562][mt] decoded from=%08lX to=%08lX id=%lu port=%u ch=%u hop=%u\n",
                        static_cast<unsigned long>(header.from),
                        static_cast<unsigned long>(header.to),
                        static_cast<unsigned long>(header.id),
                        static_cast<unsigned>(decoded.portnum),
                        static_cast<unsigned>(header.channel),
                        static_cast<unsigned>(hop_limit));
    }
    else
    {
        logMeshtasticRx("[gat562][mt] pb decode fail from=%08lX to=%08lX id=%lu plain=%u ch=%u\n",
                        static_cast<unsigned long>(header.from),
                        static_cast<unsigned long>(header.to),
                        static_cast<unsigned long>(header.id),
                        static_cast<unsigned>(plain_len),
                        static_cast<unsigned>(header.channel));
    }

    if (decoded_ok)
    {
        queueMqttProxyPublishFromWire(data, size, &decoded, channel);
    }

    updateNodeLastSeen(header.from, ::chat::meshtastic::computeHopsAway(header.flags), channel);

    if (decoded_ok && decoded.portnum == meshtastic_PortNum_ROUTING_APP && decoded.payload.size > 0)
    {
        handleRoutingPacket(header, decoded, channel, key, key_len, rx_meta);
    }

    maybeHandleObservedRelay(header);
    const bool duplicate = history.seen_recently && !history.was_upgraded;
    auto apply_observed_node_update = [&](::chat::NodeId node_id, const ::chat::contacts::NodeUpdate& update)
    {
        if (contact_service_)
        {
            contact_service_->applyNodeUpdate(node_id, update);
        }
        else if (node_store_)
        {
            node_store_->applyUpdate(node_id, update);
        }
    };

    // ------------------------------------------------------------
    // NODEINFO / USER: 淇濆畧鐗堬紝鍙洿鏂拌娴嬪瓧娈碉紝鍏堜笉瑕佹妸韬唤瀛楁鍐欒繘 store
    // ------------------------------------------------------------
    if (decoded_ok &&
        decoded.portnum == meshtastic_PortNum_NODEINFO_APP &&
        decoded.payload.size > 0 &&
        (node_store_ || contact_service_))
    {
        meshtastic_NodeInfo node = meshtastic_NodeInfo_init_default;
        pb_istream_t nstream = pb_istream_from_buffer(decoded.payload.bytes, decoded.payload.size);

        if (pb_decode(&nstream, meshtastic_NodeInfo_fields, &node))
        {
            const ::chat::NodeId effective_node_id = header.from;

            if (node.num != 0 && node.num != header.from)
            {
                logMeshtasticRx("[gat562][mt] reject nodeinfo mismatch from=%08lX claimed=%08lX\n",
                                static_cast<unsigned long>(header.from),
                                static_cast<unsigned long>(node.num));
            }
            else if (effective_node_id == node_id_ && header.from != node_id_)
            {
                logMeshtasticRx("[gat562][mt] reject foreign nodeinfo targeting self from=%08lX\n",
                                static_cast<unsigned long>(header.from));
            }
            else
            {
                const float snr = std::isnan(last_rx_snr_) ? node.snr : last_rx_snr_;
                const uint8_t hops_away =
                    node.has_hops_away ? node.hops_away
                                       : ::chat::meshtastic::computeHopsAway(header.flags);

                ::chat::contacts::NodeUpdate update{};
                update.has_last_seen = true;
                update.last_seen = nowSeconds();
                update.has_snr = !std::isnan(snr);
                update.snr = snr;
                update.has_rssi = !std::isnan(last_rx_rssi_);
                update.rssi = last_rx_rssi_;
                update.has_protocol = true;
                update.protocol = static_cast<uint8_t>(::chat::contacts::NodeProtocolType::Meshtastic);
                update.has_hops_away = true;
                update.hops_away = hops_away;
                update.has_channel = true;
                update.channel = static_cast<uint8_t>(channel);
                update.has_via_mqtt = true;
                update.via_mqtt = node.via_mqtt ||
                                  ((header.flags & ::chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK) != 0);
                update.has_is_ignored = true;
                update.is_ignored = node.is_ignored;
                if (node.has_position && ::chat::meshtastic::hasValidPosition(node.position))
                {
                    update.has_position = true;
                    update.position.valid = true;
                    update.position.latitude_i = node.position.latitude_i;
                    update.position.longitude_i = node.position.longitude_i;
                    update.position.has_altitude = node.position.has_altitude;
                    update.position.altitude = node.position.altitude;
                    update.position.timestamp =
                        node.position.timestamp != 0 ? node.position.timestamp : node.position.time;
                    update.position.precision_bits = node.position.precision_bits;
                    update.position.pdop = node.position.PDOP;
                    update.position.hdop = node.position.HDOP;
                    update.position.vdop = node.position.VDOP;
                    update.position.gps_accuracy_mm = node.position.gps_accuracy;
                }
                if (node.has_device_metrics)
                {
                    update.has_device_metrics = true;
                    update.device_metrics.has_battery_level = node.device_metrics.has_battery_level;
                    update.device_metrics.battery_level = node.device_metrics.battery_level;
                    update.device_metrics.has_voltage = node.device_metrics.has_voltage;
                    update.device_metrics.voltage = node.device_metrics.voltage;
                    update.device_metrics.has_channel_utilization = node.device_metrics.has_channel_utilization;
                    update.device_metrics.channel_utilization = node.device_metrics.channel_utilization;
                    update.device_metrics.has_air_util_tx = node.device_metrics.has_air_util_tx;
                    update.device_metrics.air_util_tx = node.device_metrics.air_util_tx;
                    update.device_metrics.has_uptime_seconds = node.device_metrics.has_uptime_seconds;
                    update.device_metrics.uptime_seconds = node.device_metrics.uptime_seconds;
                }

                if (node.has_user)
                {
                    if (node.user.short_name[0] != '\0')
                    {
                        update.short_name = node.user.short_name;
                    }
                    if (node.user.long_name[0] != '\0')
                    {
                        update.long_name = node.user.long_name;
                    }
                    update.has_role = true;
                    update.role = static_cast<uint8_t>(node.user.role);
                    if (node.user.hw_model != meshtastic_HardwareModel_UNSET)
                    {
                        update.has_hw_model = true;
                        update.hw_model = static_cast<uint8_t>(node.user.hw_model);
                    }

                    bool has_macaddr = false;
                    for (std::size_t idx = 0; idx < sizeof(update.macaddr); ++idx)
                    {
                        if (node.user.macaddr[idx] != 0)
                        {
                            has_macaddr = true;
                            break;
                        }
                    }
                    if (has_macaddr)
                    {
                        update.has_macaddr = true;
                        std::memcpy(update.macaddr, node.user.macaddr, sizeof(update.macaddr));
                    }

                    update.has_public_key = true;
                    update.public_key_present = (node.user.public_key.size > 0);
                }

                if (!duplicate || history.was_fallback)
                {
                    apply_observed_node_update(effective_node_id, update);
                }
                nodeinfo_last_seen_ms_[effective_node_id] = millis();

                if (node.has_user)
                {
                    logMeshtasticRx("[gat562][mt] nodeinfo observed from=%08lX owner=%08lX short=\"%s\" long=\"%s\"\n",
                                    static_cast<unsigned long>(header.from),
                                    static_cast<unsigned long>(effective_node_id),
                                    node.user.short_name,
                                    node.user.long_name);
                }
                else
                {
                    logMeshtasticRx("[gat562][mt] nodeinfo observed from=%08lX owner=%08lX\n",
                                    static_cast<unsigned long>(header.from),
                                    static_cast<unsigned long>(effective_node_id));
                }
            }
        }
        else
        {
            meshtastic_User user = meshtastic_User_init_default;
            pb_istream_t ustream = pb_istream_from_buffer(decoded.payload.bytes, decoded.payload.size);
            if (pb_decode(&ustream, meshtastic_User_fields, &user))
            {
                const ::chat::NodeId effective_node_id = header.from;

                if (effective_node_id == node_id_ && header.from != node_id_)
                {
                    logMeshtasticRx("[gat562][mt] reject foreign user payload targeting self from=%08lX\n",
                                    static_cast<unsigned long>(header.from));
                }
                else
                {
                    ::chat::contacts::NodeUpdate update{};
                    if (user.short_name[0] != '\0')
                    {
                        update.short_name = user.short_name;
                    }
                    if (user.long_name[0] != '\0')
                    {
                        update.long_name = user.long_name;
                    }
                    update.has_last_seen = true;
                    update.last_seen = nowSeconds();
                    update.has_snr = !std::isnan(last_rx_snr_);
                    update.snr = last_rx_snr_;
                    update.has_rssi = !std::isnan(last_rx_rssi_);
                    update.rssi = last_rx_rssi_;
                    update.has_protocol = true;
                    update.protocol = static_cast<uint8_t>(::chat::contacts::NodeProtocolType::Meshtastic);
                    update.has_hops_away = true;
                    update.hops_away = ::chat::meshtastic::computeHopsAway(header.flags);
                    update.has_channel = true;
                    update.channel = static_cast<uint8_t>(channel);
                    update.has_via_mqtt = true;
                    update.via_mqtt = ((header.flags & ::chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK) != 0);
                    update.has_role = true;
                    update.role = static_cast<uint8_t>(user.role);
                    if (user.hw_model != meshtastic_HardwareModel_UNSET)
                    {
                        update.has_hw_model = true;
                        update.hw_model = static_cast<uint8_t>(user.hw_model);
                    }

                    bool has_macaddr = false;
                    for (std::size_t idx = 0; idx < sizeof(update.macaddr); ++idx)
                    {
                        if (user.macaddr[idx] != 0)
                        {
                            has_macaddr = true;
                            break;
                        }
                    }
                    if (has_macaddr)
                    {
                        update.has_macaddr = true;
                        std::memcpy(update.macaddr, user.macaddr, sizeof(update.macaddr));
                    }

                    update.has_public_key = true;
                    update.public_key_present = (user.public_key.size > 0);

                    if (!duplicate || history.was_fallback)
                    {
                        apply_observed_node_update(effective_node_id, update);
                    }
                    nodeinfo_last_seen_ms_[effective_node_id] = millis();

                    logMeshtasticRx("[gat562][mt] user observed from=%08lX owner=%08lX short=\"%s\" long=\"%s\"\n",
                                    static_cast<unsigned long>(header.from),
                                    static_cast<unsigned long>(effective_node_id),
                                    user.short_name,
                                    user.long_name);
                }
            }
        }
    }

    if (decoded_ok &&
        decoded.portnum == meshtastic_PortNum_POSITION_APP &&
        decoded.payload.size > 0 &&
        contact_service_)
    {
        meshtastic_Position position_pb = meshtastic_Position_init_zero;
        pb_istream_t pstream = pb_istream_from_buffer(decoded.payload.bytes, decoded.payload.size);
        if (pb_decode(&pstream, meshtastic_Position_fields, &position_pb) &&
            ::chat::meshtastic::hasValidPosition(position_pb))
        {
            ::chat::contacts::NodePosition pos{};
            pos.valid = true;
            pos.latitude_i = position_pb.latitude_i;
            pos.longitude_i = position_pb.longitude_i;
            pos.has_altitude = position_pb.has_altitude;
            pos.altitude = position_pb.altitude;
            pos.timestamp = position_pb.timestamp != 0 ? position_pb.timestamp : position_pb.time;
            pos.precision_bits = position_pb.precision_bits;
            pos.pdop = position_pb.PDOP;
            pos.hdop = position_pb.HDOP;
            pos.vdop = position_pb.VDOP;
            pos.gps_accuracy_mm = position_pb.gps_accuracy;
            contact_service_->updateNodePosition(header.from, pos);
        }
    }

    if (decoded_ok && header.channel != 0 && header.from != node_id_)
    {
        node_last_channel_[header.from] = channel;
    }

    if (want_ack_flag && to_us && decoded_ok)
    {
        (void)sendRoutingAck(header.from, header.id, header.channel, channel, key, key_len, 0);
    }

    const bool want_response =
        decoded_ok &&
        (decoded.want_response ||
         (decoded.has_bitfield && ((decoded.bitfield & kBitfieldWantResponseMask) != 0)));

    if (decoded_ok && decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP)
    {
        (void)handleTraceRoutePacket(header, &decoded, &rx_meta, channel, want_ack_flag, want_response);
    }

    if (decoded_ok && decoded.portnum == meshtastic_PortNum_KEY_VERIFICATION_APP && decoded.payload.size > 0)
    {
        meshtastic_KeyVerification kv = meshtastic_KeyVerification_init_zero;
        bool handled = false;

        if (header.channel == 0 && ::chat::meshtastic::decodeKeyVerificationMessage(plain, plain_len, &kv))
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

        if (!handled)
        {
            logMeshtasticRx("[gat562][mt] key verification ignored from=%08lX stage=%s\n",
                            static_cast<unsigned long>(header.from),
                            keyVerificationStage(kv));
        }
    }

    if (decoded_ok && want_response && to_us_or_broadcast)
    {
        if (decoded.portnum == meshtastic_PortNum_NODEINFO_APP)
        {
            const uint32_t now_ms = millis();
            bool allow_reply = true;
            const auto it = nodeinfo_last_seen_ms_.find(header.from);
            if (it != nodeinfo_last_seen_ms_.end() && (now_ms - it->second) < kNodeInfoReplySuppressMs)
            {
                allow_reply = false;
            }
            nodeinfo_last_seen_ms_[header.from] = now_ms;
            if (allow_reply)
            {
                (void)buildAndQueueNodeInfo(header.from, false, channel);
            }
        }
        else if (decoded.portnum == meshtastic_PortNum_POSITION_APP)
        {
            const uint32_t now_ms = millis();
            const bool allow_reply =
                (last_position_reply_ms_ == 0) || ((now_ms - last_position_reply_ms_) >= kPositionReplySuppressMs);
            if (allow_reply && sendPositionTo(header.from, channel))
            {
                last_position_reply_ms_ = now_ms;
            }
        }
    }

    if (!duplicate || history.was_fallback)
    {
        (void)maybeRebroadcast(header, payload, payload_size, channel, decoded_ok ? &decoded : nullptr);
    }

    if (!to_us_or_broadcast || duplicate)
    {
        return;
    }

    const bool is_text_port =
        decoded_ok &&
        (decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
         decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP);
    const bool text_compressed =
        decoded_ok && (decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP);

    if (is_text_port)
    {
        logMeshtasticRx("[gat562][mt][text] rx from=%08lX to=%08lX id=%lu port=%u payload=%u plain=%u compressed=%u encrypted=%u\n",
                        static_cast<unsigned long>(header.from),
                        static_cast<unsigned long>(header.to),
                        static_cast<unsigned long>(header.id),
                        static_cast<unsigned>(decoded.portnum),
                        static_cast<unsigned>(decoded.payload.size),
                        static_cast<unsigned>(plain_len),
                        text_compressed ? 1U : 0U,
                        key_len > 0 ? 1U : 0U);
    }

    ::chat::MeshIncomingText incoming{};
    if (decoded_ok && ::chat::meshtastic::decodeTextPayload(decoded, &incoming))
    {
        incoming.from = header.from;
        incoming.to = header.to;
        incoming.msg_id = header.id;
        incoming.channel = channel;
        incoming.timestamp = (rx_meta.rx_timestamp_s != 0) ? rx_meta.rx_timestamp_s : nowSeconds();
        incoming.encrypted = key_len > 0;
        incoming.hop_limit = hop_limit;
        incoming.rx_meta = rx_meta;

        logMeshtasticRx("[gat562][mt] text queued from=%08lX to=%08lX id=%lu port=%u payload=%u compressed=%u len=%u text=\"%s\"\n",
                        static_cast<unsigned long>(incoming.from),
                        static_cast<unsigned long>(incoming.to),
                        static_cast<unsigned long>(incoming.msg_id),
                        static_cast<unsigned>(decoded.portnum),
                        static_cast<unsigned>(decoded.payload.size),
                        text_compressed ? 1U : 0U,
                        static_cast<unsigned>(incoming.text.size()),
                        incoming.text.c_str());

        text_queue_.push(std::move(incoming));
        return;
    }

    if (is_text_port)
    {
        logMeshtasticRx("[gat562][mt] text decode fail from=%08lX id=%lu port=%u payload=%u plain=%u compressed=%u\n",
                        static_cast<unsigned long>(header.from),
                        static_cast<unsigned long>(header.id),
                        static_cast<unsigned>(decoded.portnum),
                        static_cast<unsigned>(decoded.payload.size),
                        static_cast<unsigned>(plain_len),
                        text_compressed ? 1U : 0U);
    }

    ::chat::MeshIncomingData app_data{};
    if (decoded_ok && ::chat::meshtastic::decodeAppPayload(decoded, &app_data))
    {
        app_data.from = header.from;
        app_data.to = header.to;
        app_data.packet_id = header.id;
        app_data.request_id = decoded.request_id;
        app_data.channel = channel;
        app_data.channel_hash = header.channel;
        app_data.hop_limit = hop_limit;
        app_data.want_response = want_response;
        app_data.rx_meta = rx_meta;

        logMeshtasticRx("[gat562][mt] app queued from=%08lX to=%08lX id=%lu port=%u len=%u\n",
                        static_cast<unsigned long>(app_data.from),
                        static_cast<unsigned long>(app_data.to),
                        static_cast<unsigned long>(app_data.packet_id),
                        static_cast<unsigned>(app_data.portnum),
                        static_cast<unsigned>(app_data.payload.size()));

        data_queue_.push(std::move(app_data));
    }
}

void MeshtasticRadioAdapter::setLastRxStats(float rssi, float snr)
{
    last_rx_rssi_ = rssi;
    last_rx_snr_ = snr;
}

void MeshtasticRadioAdapter::processSendQueue()
{
    const uint32_t now_ms = millis();
    maybeBroadcastNodeInfo(now_ms);
    for (auto it = pending_retransmits_.begin(); it != pending_retransmits_.end();)
    {
        auto& pending = it->second;
        if (pending.next_tx_ms > now_ms)
        {
            ++it;
            continue;
        }

        auto* header = reinterpret_cast<::chat::meshtastic::PacketHeaderWire*>(pending.wire.data());
        if (pending.retries_left == 0)
        {
            if (pending.observe_only)
            {
                logMeshtasticRx("[gat562][mt] observe timeout id=%08lX dest=%08lX ch=%u local=%u want_ack=%u\n",
                                static_cast<unsigned long>(pending.packet_id),
                                static_cast<unsigned long>(pending.dest),
                                static_cast<unsigned>(pending.channel),
                                pending.local_origin ? 1U : 0U,
                                pending.want_ack ? 1U : 0U);
            }
            if (pending.local_origin && pending.want_ack)
            {
                emitRoutingResult(pending.packet_id,
                                  meshtastic_Routing_Error_MAX_RETRANSMIT,
                                  node_id_,
                                  pending.dest,
                                  pending.channel,
                                  pending.channel_hash,
                                  nullptr);
            }
            if (node_store_ && pending.dest != 0 && pending.dest != kBroadcastNode)
            {
                node_store_->setNextHop(pending.dest, 0, nowSeconds());
            }
            it = pending_retransmits_.erase(it);
            continue;
        }

        if (pending.observe_only)
        {
            ++it;
            continue;
        }

        if (pending.retries_left == 1 && header->next_hop != 0)
        {
            header->next_hop = 0;
            pending.fallback_sent = true;
            if (node_store_ && pending.dest != 0 && pending.dest != kBroadcastNode)
            {
                node_store_->setNextHop(pending.dest, 0, nowSeconds());
            }
        }

        if (transmitWire(pending.wire.data(), pending.wire.size()))
        {
            rememberLocalPacket(*header);
        }
        pending.retries_left--;
        pending.next_tx_ms = now_ms + kRetransmitIntervalMs;
        ++it;
    }
}

void MeshtasticRadioAdapter::flushDeferredPersistence(bool force)
{
    if (!pki_node_keys_dirty_)
    {
        return;
    }

    const uint32_t now_ms = millis();
    if (!force && static_cast<int32_t>(now_ms - pki_node_keys_save_due_ms_) < 0)
    {
        return;
    }

    if (!savePkiKeysToPrefs())
    {
        pki_node_keys_save_due_ms_ = millis() + kPkiNodeSaveDebounceMs;
    }
}

::chat::runtime::EffectiveSelfIdentity MeshtasticRadioAdapter::buildEffectiveIdentity() const
{
    ::chat::runtime::EffectiveSelfIdentity identity{};

    if (identity_provider_)
    {
        ::chat::runtime::SelfIdentityInput input{};
        if (identity_provider_->readSelfIdentityInput(&input))
        {
            if (!long_name_.empty())
            {
                input.configured_long_name = long_name_.c_str();
            }
            if (!short_name_.empty())
            {
                input.configured_short_name = short_name_.c_str();
            }
            (void)::chat::runtime::resolveEffectiveSelfIdentity(input, &identity);
            return identity;
        }
    }

    ::chat::runtime::SelfIdentityInput input{};
    input.node_id = node_id_;
    input.configured_long_name = long_name_.c_str();
    input.configured_short_name = short_name_.c_str();
    input.fallback_long_prefix = "node";
    input.fallback_ble_prefix = "node";
    input.allow_short_hex_fallback = true;
    (void)::chat::runtime::resolveEffectiveSelfIdentity(input, &identity);
    return identity;
}

bool MeshtasticRadioAdapter::transmitWire(const uint8_t* data, size_t size)
{
    auto* io = ::platform::nrf52::arduino_common::chat::infra::radioPacketIo();
    return io && io->transmit(data, size);
}

bool MeshtasticRadioAdapter::transmitPreparedWire(uint8_t* data, size_t size, ::chat::ChannelId channel,
                                                  const meshtastic_Data* decoded, bool track_retransmit,
                                                  bool local_origin, uint8_t retries_override,
                                                  bool observe_broadcast_ack)
{
    if (!data || size < sizeof(::chat::meshtastic::PacketHeaderWire))
    {
        return false;
    }

    auto* header = reinterpret_cast<::chat::meshtastic::PacketHeaderWire*>(data);
    logMeshtasticRx("[gat562][mt] tx from=%08lX to=%08lX id=%lu flags=0x%02X ch=%u next=%u relay=%u len=%u\n",
                    static_cast<unsigned long>(header->from),
                    static_cast<unsigned long>(header->to),
                    static_cast<unsigned long>(header->id),
                    static_cast<unsigned>(header->flags),
                    static_cast<unsigned>(header->channel),
                    static_cast<unsigned>(header->next_hop),
                    static_cast<unsigned>(header->relay_node),
                    static_cast<unsigned>(size));
    char wire_hex[768] = {};
    toHexString(data, size, wire_hex, sizeof(wire_hex), size);
    if (wire_hex[0] != '\0')
    {
        logMeshtasticRx("[gat562][mt] tx hex %s\n", wire_hex);
    }
    if (!transmitWire(data, size))
    {
        return false;
    }

    rememberLocalPacket(*header);
    if (decoded)
    {
        queueMqttProxyPublishFromWire(data, size, decoded, channel);
    }

    const bool is_unicast = header->to != kBroadcastNode;
    const bool wants_ack = (header->flags & ::chat::meshtastic::PACKET_FLAGS_WANT_ACK_MASK) != 0;
    if (local_origin && !is_unicast && !observe_broadcast_ack)
    {
        // Broadcast messages do not receive air ACKs, but the phone app still
        // expects a successful routing result to transition the local message
        // from "queued" to "sent".
        emitRoutingResult(header->id,
                          meshtastic_Routing_Error_NONE,
                          header->from,
                          header->to,
                          channel,
                          header->channel,
                          nullptr);
    }
    if (track_retransmit &&
        ((is_unicast && (header->next_hop != 0 || wants_ack)) ||
         (local_origin && !is_unicast && observe_broadcast_ack)))
    {
        queuePendingRetransmit(*header, data, size, channel, local_origin, retries_override, !is_unicast);
    }
    return true;
}

bool MeshtasticRadioAdapter::buildAndQueueNodeInfo(::chat::NodeId dest, bool want_response,
                                                   ::chat::ChannelId channel)
{
    auto mac = readMac();
    if (identity_provider_)
    {
        ::chat::runtime::SelfIdentityInput input{};
        if (identity_provider_->readSelfIdentityInput(&input) && input.mac_addr && input.mac_addr_len >= mac.size())
        {
            std::memcpy(mac.data(), input.mac_addr, mac.size());
        }
    }

    ::chat::runtime::MeshtasticAnnouncementRequest request{};
    request.identity = buildEffectiveIdentity();
    request.mesh_config = config_;
    request.channel = channel;
    request.packet_id = next_packet_id_++;
    request.dest_node = dest;
    request.hop_limit = config_.hop_limit;
    request.want_response = want_response;
    request.mac_addr = mac.data();
    if (pki_ready_)
    {
        request.public_key = pki_public_key_.data();
        request.public_key_len = pki_public_key_.size();
    }

    ::chat::runtime::MeshtasticAnnouncementPacket packet{};
    if (!::chat::runtime::MeshtasticSelfAnnouncementCore::buildNodeInfoPacket(request, &packet))
    {
        return false;
    }
    const bool ok = transmitWire(packet.wire, packet.wire_size);
    if (ok && dest == kBroadcastNode)
    {
        last_nodeinfo_ms_ = millis();
    }
    return ok;
}

bool MeshtasticRadioAdapter::buildAndQueueRoutingPacket(::chat::NodeId dest, uint32_t request_id,
                                                        uint8_t channel_hash, ::chat::ChannelId channel,
                                                        meshtastic_Routing_Error reason,
                                                        const uint8_t* key, size_t key_len,
                                                        uint8_t hop_limit)
{
    meshtastic_Routing routing = meshtastic_Routing_init_default;
    routing.which_variant = meshtastic_Routing_error_reason_tag;
    routing.error_reason = reason;

    uint8_t routing_buf[64] = {};
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
    data.payload.size = static_cast<pb_size_t>(rstream.bytes_written);
    std::memcpy(data.payload.bytes, routing_buf, data.payload.size);

    uint8_t data_buf[128] = {};
    pb_ostream_t dstream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
    if (!pb_encode(&dstream, meshtastic_Data_fields, &data))
    {
        return false;
    }

    uint8_t payload_buf[256] = {};
    const uint8_t* wire_payload = data_buf;
    size_t wire_payload_len = dstream.bytes_written;
    if (channel_hash == 0)
    {
        size_t pki_len = sizeof(payload_buf);
        const ::chat::MessageId packet_id = next_packet_id_;
        if (!encryptPkiPayload(dest, packet_id, data_buf, dstream.bytes_written, payload_buf, &pki_len))
        {
            return false;
        }
        wire_payload = payload_buf;
        wire_payload_len = pki_len;
        key = nullptr;
        key_len = 0;
    }

    uint8_t wire[256] = {};
    size_t wire_size = sizeof(wire);
    if (!::chat::meshtastic::buildWirePacket(wire_payload, wire_payload_len,
                                             node_id_, next_packet_id_++,
                                             dest, channel_hash, hop_limit, false,
                                             key, key_len, wire, &wire_size))
    {
        return false;
    }

    return transmitPreparedWire(wire, wire_size, channel, &data, false, true);
}

bool MeshtasticRadioAdapter::sendPositionTo(::chat::NodeId dest, ::chat::ChannelId channel)
{
    uint8_t payload[128] = {};
    size_t payload_len = sizeof(payload);
    if (!buildSelfPositionPayload(payload, &payload_len))
    {
        return false;
    }

    return sendAppData(channel,
                       meshtastic_PortNum_POSITION_APP,
                       payload,
                       payload_len,
                       dest,
                       false);
}

void MeshtasticRadioAdapter::maybeBroadcastNodeInfo(uint32_t now_ms)
{
    if (!config_.tx_enabled)
    {
        return;
    }
    if (last_nodeinfo_ms_ != 0 && (now_ms - last_nodeinfo_ms_) < kNodeInfoIntervalMs)
    {
        return;
    }
    (void)buildAndQueueNodeInfo(kBroadcastNode, false, ::chat::ChannelId::PRIMARY);
}

bool MeshtasticRadioAdapter::sendRoutingAck(::chat::NodeId dest, uint32_t request_id, uint8_t channel_hash,
                                            ::chat::ChannelId channel, const uint8_t* key, size_t key_len,
                                            uint8_t hop_limit)
{
    return buildAndQueueRoutingPacket(dest, request_id, channel_hash, channel,
                                      meshtastic_Routing_Error_NONE, key, key_len, hop_limit);
}

bool MeshtasticRadioAdapter::sendRoutingError(::chat::NodeId dest, uint32_t request_id, uint8_t channel_hash,
                                              ::chat::ChannelId channel, const uint8_t* key, size_t key_len,
                                              meshtastic_Routing_Error reason, uint8_t hop_limit)
{
    return buildAndQueueRoutingPacket(dest, request_id, channel_hash, channel,
                                      reason, key, key_len, hop_limit);
}

bool MeshtasticRadioAdapter::sendTraceRouteResponse(::chat::NodeId dest, uint32_t request_id,
                                                    const meshtastic_RouteDiscovery& route,
                                                    ::chat::ChannelId channel, bool want_ack)
{
    meshtastic_Data data = meshtastic_Data_init_default;
    data.portnum = meshtastic_PortNum_TRACEROUTE_APP;
    data.want_response = false;
    data.dest = dest;
    data.source = node_id_;
    data.request_id = request_id;
    data.has_bitfield = true;
    data.bitfield = 0;

    pb_ostream_t ostream = pb_ostream_from_buffer(data.payload.bytes, sizeof(data.payload.bytes));
    if (!pb_encode(&ostream, meshtastic_RouteDiscovery_fields, &route))
    {
        return false;
    }
    data.payload.size = static_cast<pb_size_t>(ostream.bytes_written);

    uint8_t data_buf[256] = {};
    pb_ostream_t dstream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
    if (!pb_encode(&dstream, meshtastic_Data_fields, &data))
    {
        return false;
    }

    size_t key_len = 0;
    const uint8_t* key = selectKey(config_, channel, &key_len);
    const uint8_t channel_hash = ::chat::meshtastic::computeChannelHash(channelNameFor(config_, channel),
                                                                        key,
                                                                        key_len);

    uint8_t wire[384] = {};
    size_t wire_size = sizeof(wire);
    if (!::chat::meshtastic::buildWirePacket(data_buf, dstream.bytes_written,
                                             node_id_, next_packet_id_++,
                                             dest, channel_hash, config_.hop_limit, want_ack,
                                             key, key_len, wire, &wire_size))
    {
        return false;
    }

    return transmitPreparedWire(wire, wire_size, channel, &data, true, true);
}

bool MeshtasticRadioAdapter::handleTraceRoutePacket(const ::chat::meshtastic::PacketHeaderWire& header,
                                                    meshtastic_Data* decoded,
                                                    const ::chat::RxMeta* rx_meta,
                                                    ::chat::ChannelId channel,
                                                    bool want_ack_flag,
                                                    bool want_response)
{
    if (!decoded || decoded->portnum != meshtastic_PortNum_TRACEROUTE_APP || decoded->payload.size == 0)
    {
        return false;
    }

    meshtastic_RouteDiscovery route = meshtastic_RouteDiscovery_init_zero;
    pb_istream_t istream = pb_istream_from_buffer(decoded->payload.bytes, decoded->payload.size);
    if (!pb_decode(&istream, meshtastic_RouteDiscovery_fields, &route))
    {
        return false;
    }

    const bool is_response = decoded->request_id != 0;
    const bool is_broadcast = header.to == kBroadcastNode;
    const bool to_us = header.to == node_id_;

    ::chat::meshtastic::insertTraceRouteUnknownHops(header.flags, &route, !is_response);
    ::chat::meshtastic::appendTraceRouteNodeAndSnr(&route, node_id_, rx_meta, !is_response, to_us);

    pb_ostream_t ostream = pb_ostream_from_buffer(decoded->payload.bytes, sizeof(decoded->payload.bytes));
    if (!pb_encode(&ostream, meshtastic_RouteDiscovery_fields, &route))
    {
        return false;
    }
    decoded->payload.size = static_cast<pb_size_t>(ostream.bytes_written);

    if (!is_response && want_response && (to_us || is_broadcast))
    {
        const uint8_t hop_limit = header.flags & ::chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
        const uint8_t hop_start =
            (header.flags & ::chat::meshtastic::PACKET_FLAGS_HOP_START_MASK) >>
            ::chat::meshtastic::PACKET_FLAGS_HOP_START_SHIFT;
        const bool ignore_broadcast_request = is_broadcast && hop_limit < hop_start;
        if (!ignore_broadcast_request)
        {
            (void)sendTraceRouteResponse(header.from, header.id, route, channel, want_ack_flag);
        }
    }

    return true;
}

void MeshtasticRadioAdapter::emitRoutingResult(uint32_t request_id, meshtastic_Routing_Error reason,
                                               ::chat::NodeId from, ::chat::NodeId to,
                                               ::chat::ChannelId channel, uint8_t channel_hash,
                                               const ::chat::RxMeta* rx_meta)
{
    if (request_id == 0)
    {
        return;
    }

    meshtastic_Routing routing = meshtastic_Routing_init_default;
    routing.which_variant = meshtastic_Routing_error_reason_tag;
    routing.error_reason = reason;

    uint8_t routing_buf[32] = {};
    pb_ostream_t rstream = pb_ostream_from_buffer(routing_buf, sizeof(routing_buf));
    if (!pb_encode(&rstream, meshtastic_Routing_fields, &routing))
    {
        return;
    }

    ::chat::MeshIncomingData incoming{};
    incoming.portnum = meshtastic_PortNum_ROUTING_APP;
    incoming.from = from;
    incoming.to = to;
    incoming.packet_id = 0;
    incoming.request_id = request_id;
    incoming.channel = channel;
    incoming.channel_hash = channel_hash;
    incoming.hop_limit = rx_meta ? rx_meta->hop_limit : 0;
    incoming.payload.assign(routing_buf, routing_buf + rstream.bytes_written);
    if (rx_meta)
    {
        incoming.rx_meta = *rx_meta;
    }
    else
    {
        incoming.rx_meta.rx_timestamp_ms = millis();
        incoming.rx_meta.rx_timestamp_s = nowSeconds();
        incoming.rx_meta.time_source = ::chat::RxTimeSource::DeviceUtc;
        incoming.rx_meta.origin = ::chat::RxOrigin::Mesh;
        incoming.rx_meta.channel_hash = channel_hash;
    }
    data_queue_.push(std::move(incoming));
}

uint8_t MeshtasticRadioAdapter::ourRelayId() const
{
    return static_cast<uint8_t>(node_id_ & 0xFFU);
}

uint8_t MeshtasticRadioAdapter::getLearnedNextHop(::chat::NodeId dest, uint8_t relay_node) const
{
    if (!node_store_ || dest == 0 || dest == kBroadcastNode)
    {
        return 0;
    }

    const uint8_t next_hop = node_store_->getNextHop(dest);
    if (next_hop == 0 || next_hop == relay_node)
    {
        return 0;
    }
    return next_hop;
}

void MeshtasticRadioAdapter::learnNextHop(::chat::NodeId dest, uint8_t next_hop)
{
    if (!node_store_ || dest == 0 || dest == kBroadcastNode || next_hop == 0 || next_hop == ourRelayId())
    {
        return;
    }
    (void)node_store_->setNextHop(dest, next_hop, nowSeconds());
}

MeshtasticRadioAdapter::PacketHistoryEntry* MeshtasticRadioAdapter::findHistory(::chat::NodeId sender, ::chat::MessageId packet_id)
{
    auto it = std::find_if(packet_history_.begin(), packet_history_.end(),
                           [sender, packet_id](const PacketHistoryEntry& entry)
                           {
                               return entry.sender == sender && entry.packet_id == packet_id;
                           });
    return (it != packet_history_.end()) ? &(*it) : nullptr;
}

const MeshtasticRadioAdapter::PacketHistoryEntry* MeshtasticRadioAdapter::findHistory(::chat::NodeId sender, ::chat::MessageId packet_id) const
{
    auto it = std::find_if(packet_history_.begin(), packet_history_.end(),
                           [sender, packet_id](const PacketHistoryEntry& entry)
                           {
                               return entry.sender == sender && entry.packet_id == packet_id;
                           });
    return (it != packet_history_.end()) ? &(*it) : nullptr;
}

bool MeshtasticRadioAdapter::hasRelayer(const PacketHistoryEntry& entry, uint8_t relayer, bool* sole)
{
    bool found = false;
    bool other_present = false;
    for (uint8_t value : entry.relayed_by)
    {
        if (value == relayer)
        {
            found = true;
        }
        else if (value != 0)
        {
            other_present = true;
        }
    }
    if (sole)
    {
        *sole = found && !other_present;
    }
    return found;
}

MeshtasticRadioAdapter::HistoryResult MeshtasticRadioAdapter::updatePacketHistory(const ::chat::meshtastic::PacketHeaderWire& header,
                                                                                  bool allow_update)
{
    HistoryResult result{};
    if (header.id == 0)
    {
        return result;
    }

    const uint32_t now_ms = millis();
    packet_history_.erase(
        std::remove_if(packet_history_.begin(), packet_history_.end(),
                       [now_ms](const PacketHistoryEntry& entry)
                       {
                           return entry.last_rx_ms != 0 &&
                                  (now_ms - entry.last_rx_ms) > kPacketHistoryTimeoutMs;
                       }),
        packet_history_.end());

    PacketHistoryEntry* existing = findHistory(header.from, header.id);
    const uint8_t hop_limit = header.flags & ::chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
    const uint8_t relay_id = ourRelayId();

    if (existing)
    {
        result.seen_recently = true;
        result.we_were_next_hop = (existing->next_hop == relay_id);
        result.was_upgraded = hop_limit > existing->highest_hop_limit;
        if (existing->next_hop != 0 && existing->next_hop != relay_id &&
            header.next_hop == 0 && header.relay_node != 0 &&
            hasRelayer(*existing, header.relay_node) &&
            !hasRelayer(*existing, relay_id) &&
            !hasRelayer(*existing, existing->next_hop))
        {
            result.was_fallback = true;
        }
    }

    if (!allow_update)
    {
        return result;
    }

    if (!existing)
    {
        if (packet_history_.size() >= kPacketHistoryCapacity)
        {
            auto oldest = std::min_element(packet_history_.begin(), packet_history_.end(),
                                           [](const PacketHistoryEntry& lhs, const PacketHistoryEntry& rhs)
                                           {
                                               return lhs.last_rx_ms < rhs.last_rx_ms;
                                           });
            if (oldest != packet_history_.end())
            {
                packet_history_.erase(oldest);
            }
        }
        packet_history_.push_back(PacketHistoryEntry{});
        existing = &packet_history_.back();
        existing->sender = header.from;
        existing->packet_id = header.id;
    }

    existing->last_rx_ms = now_ms;
    existing->highest_hop_limit = std::max(existing->highest_hop_limit, hop_limit);
    if (existing->next_hop == 0)
    {
        existing->next_hop = header.next_hop;
    }
    if (header.relay_node != 0 && !hasRelayer(*existing, header.relay_node))
    {
        auto slot = std::find(existing->relayed_by.begin(), existing->relayed_by.end(), 0);
        if (slot != existing->relayed_by.end())
        {
            *slot = header.relay_node;
        }
        else
        {
            existing->relayed_by[0] = header.relay_node;
        }
    }
    return result;
}

void MeshtasticRadioAdapter::rememberLocalPacket(const ::chat::meshtastic::PacketHeaderWire& header)
{
    if (header.id == 0)
    {
        return;
    }

    PacketHistoryEntry* entry = findHistory(header.from, header.id);
    if (!entry)
    {
        if (packet_history_.size() >= kPacketHistoryCapacity)
        {
            packet_history_.erase(packet_history_.begin());
        }
        packet_history_.push_back(PacketHistoryEntry{});
        entry = &packet_history_.back();
        entry->sender = header.from;
        entry->packet_id = header.id;
    }

    entry->last_rx_ms = millis();
    entry->next_hop = header.next_hop;
    entry->highest_hop_limit = header.flags & ::chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
    entry->our_tx_hop_limit = entry->highest_hop_limit;
    entry->relayed_by.fill(0);
    if (header.relay_node != 0)
    {
        entry->relayed_by[0] = header.relay_node;
    }
}

bool MeshtasticRadioAdapter::maybeRebroadcast(const ::chat::meshtastic::PacketHeaderWire& header,
                                              const uint8_t* payload, size_t payload_size,
                                              ::chat::ChannelId channel, const meshtastic_Data* decoded)
{
    if (!config_.enable_relay || !payload || payload_size == 0)
    {
        return false;
    }
    if (header.from == node_id_ || header.to == node_id_)
    {
        return false;
    }

    uint8_t hop_limit = header.flags & ::chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
    if (hop_limit == 0)
    {
        return false;
    }

    if (header.next_hop != 0 && header.next_hop != ourRelayId())
    {
        return false;
    }

    size_t key_len = 0;
    const uint8_t* key = selectKey(config_, channel, &key_len);
    uint8_t wire[384] = {};
    size_t wire_size = sizeof(wire);
    const bool want_ack = (header.flags & ::chat::meshtastic::PACKET_FLAGS_WANT_ACK_MASK) != 0;
    if (!::chat::meshtastic::buildWirePacket(payload, payload_size,
                                             header.from, header.id, header.to,
                                             header.channel, static_cast<uint8_t>(hop_limit - 1),
                                             want_ack, key, key_len, wire, &wire_size))
    {
        return false;
    }

    auto* out_header = reinterpret_cast<::chat::meshtastic::PacketHeaderWire*>(wire);
    out_header->relay_node = ourRelayId();
    out_header->next_hop = (header.next_hop == ourRelayId()) ? 0 : header.next_hop;
    return transmitPreparedWire(wire, wire_size, channel, decoded, header.next_hop != 0, false, 1);
}

void MeshtasticRadioAdapter::updateNodeLastSeen(::chat::NodeId node_id, uint8_t hops_away, ::chat::ChannelId channel)
{
    if (!node_store_ || node_id == 0 || node_id == node_id_)
    {
        return;
    }
    node_store_->upsert(node_id, "", "", nowSeconds(),
                        last_rx_snr_, last_rx_rssi_,
                        static_cast<uint8_t>(::chat::contacts::NodeProtocolType::Meshtastic),
                        ::chat::contacts::kNodeRoleUnknown,
                        hops_away, 0, static_cast<uint8_t>(channel));
}

void MeshtasticRadioAdapter::handleRoutingPacket(const ::chat::meshtastic::PacketHeaderWire& header,
                                                 const meshtastic_Data& decoded,
                                                 ::chat::ChannelId channel,
                                                 const uint8_t* key, size_t key_len,
                                                 const ::chat::RxMeta& rx_meta)
{
    (void)key;
    (void)key_len;

    if (decoded.payload.size == 0)
    {
        return;
    }

    meshtastic_Routing routing = meshtastic_Routing_init_default;
    pb_istream_t rstream = pb_istream_from_buffer(decoded.payload.bytes, decoded.payload.size);
    if (!pb_decode(&rstream, meshtastic_Routing_fields, &routing))
    {
        return;
    }

    if (decoded.request_id != 0 && header.to == node_id_)
    {
        const meshtastic_Routing_Error reason =
            (routing.which_variant == meshtastic_Routing_error_reason_tag)
                ? routing.error_reason
                : meshtastic_Routing_Error_NONE;
        (void)stopPendingRetransmit(node_id_, decoded.request_id);
        (void)stopPendingRetransmit(header.from, decoded.request_id);
        emitRoutingResult(decoded.request_id, reason, header.from, header.to, channel, header.channel, &rx_meta);

        bool we_were_sole_relayer = false;
        const PacketHistoryEntry* hist = findHistory(node_id_, decoded.request_id);
        const bool ack_relayer_seen = hist && hasRelayer(*hist, header.relay_node);
        const bool we_were_relayer = hist && hasRelayer(*hist, ourRelayId(), &we_were_sole_relayer);
        if (reason == meshtastic_Routing_Error_NONE)
        {
            if ((we_were_relayer && ack_relayer_seen) ||
                (rx_meta.direct && we_were_sole_relayer) ||
                header.relay_node != 0)
            {
                learnNextHop(header.from, header.relay_node != 0 ? header.relay_node : static_cast<uint8_t>(header.from & 0xFFU));
            }
        }
        else if (reason == meshtastic_Routing_Error_GOT_NAK || reason == meshtastic_Routing_Error_NO_ROUTE)
        {
            if (node_store_)
            {
                node_store_->setNextHop(header.from, 0, nowSeconds());
            }
        }

        if (reason == meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY ||
            reason == meshtastic_Routing_Error_NO_CHANNEL)
        {
            (void)buildAndQueueNodeInfo(header.from, true);
        }
        return;
    }

    if ((decoded.request_id != 0 || decoded.reply_id != 0) && header.relay_node != 0)
    {
        learnNextHop(header.from, header.relay_node);
    }
}

void MeshtasticRadioAdapter::queuePendingRetransmit(const ::chat::meshtastic::PacketHeaderWire& header,
                                                    const uint8_t* wire, size_t wire_size,
                                                    ::chat::ChannelId channel,
                                                    bool local_origin, uint8_t retries_override,
                                                    bool observe_only)
{
    if (!wire || wire_size < sizeof(::chat::meshtastic::PacketHeaderWire) || header.id == 0)
    {
        return;
    }

    PendingRetransmit pending{};
    pending.wire.assign(wire, wire + wire_size);
    pending.original_from = header.from;
    pending.dest = header.to;
    pending.packet_id = header.id;
    pending.channel = channel;
    pending.channel_hash = header.channel;
    pending.want_ack = (header.flags & ::chat::meshtastic::PACKET_FLAGS_WANT_ACK_MASK) != 0;
    pending.local_origin = local_origin;
    pending.observe_only = observe_only;
    if (observe_only)
    {
        pending.retries_left = 0;
        pending.next_tx_ms = millis() + kImplicitAckTimeoutMs;
    }
    else
    {
        pending.retries_left = retries_override != 0
                                   ? retries_override
                                   : (pending.want_ack ? kDefaultAckRetries : kDefaultNextHopRetries);
        pending.next_tx_ms = millis() + kRetransmitIntervalMs;
    }
    logMeshtasticRx("[gat562][mt] watch pending id=%08lX from=%08lX dest=%08lX observe=%u local=%u want_ack=%u next=%lu\n",
                    static_cast<unsigned long>(pending.packet_id),
                    static_cast<unsigned long>(pending.original_from),
                    static_cast<unsigned long>(pending.dest),
                    pending.observe_only ? 1U : 0U,
                    pending.local_origin ? 1U : 0U,
                    pending.want_ack ? 1U : 0U,
                    static_cast<unsigned long>(pending.next_tx_ms));
    pending_retransmits_[pendingKey(header.from, header.id)] = std::move(pending);
}

bool MeshtasticRadioAdapter::stopPendingRetransmit(::chat::NodeId from, ::chat::MessageId packet_id)
{
    return pending_retransmits_.erase(pendingKey(from, packet_id)) > 0;
}

void MeshtasticRadioAdapter::maybeHandleObservedRelay(const ::chat::meshtastic::PacketHeaderWire& header)
{
    if (header.from == 0 || header.id == 0 || header.relay_node == 0)
    {
        return;
    }

    const PacketHistoryEntry* hist = findHistory(node_id_, header.id);
    if (!hist)
    {
        return;
    }

    if (hist->next_hop != 0 && hist->next_hop == header.relay_node)
    {
        learnNextHop(header.to, header.relay_node);
        (void)stopPendingRetransmit(node_id_, header.id);
    }
}

uint64_t MeshtasticRadioAdapter::pendingKey(::chat::NodeId from, ::chat::MessageId packet_id)
{
    return (static_cast<uint64_t>(from) << 32) | packet_id;
}

bool MeshtasticRadioAdapter::initPkiKeys()
{
    std::vector<uint8_t> pub_blob;
    std::vector<uint8_t> priv_blob;
    const bool have_pub = platform::ui::settings_store::get_blob(kPkiPublicNs, kPkiPublicKey, pub_blob);
    const bool have_priv = platform::ui::settings_store::get_blob(kPkiPublicNs, kPkiPrivateKey, priv_blob);
    bool have_keys = have_pub && have_priv &&
                     pub_blob.size() == pki_public_key_.size() &&
                     priv_blob.size() == pki_private_key_.size();
    if (have_keys)
    {
        std::memcpy(pki_public_key_.data(), pub_blob.data(), pki_public_key_.size());
        std::memcpy(pki_private_key_.data(), priv_blob.data(), pki_private_key_.size());
        have_keys = !isZeroKey(pki_private_key_.data(), pki_private_key_.size());
    }

    if (!have_keys)
    {
        RNG.begin("trail-mate");
        auto mac = readMac();
        RNG.stir(mac.data(), mac.size());
        uint32_t noise = random();
        RNG.stir(reinterpret_cast<const uint8_t*>(&noise), sizeof(noise));
        Curve25519::dh1(pki_public_key_.data(), pki_private_key_.data());
        have_keys = !isZeroKey(pki_private_key_.data(), pki_private_key_.size());
        if (have_keys)
        {
            (void)platform::ui::settings_store::put_blob(kPkiPublicNs, kPkiPublicKey,
                                                         pki_public_key_.data(), pki_public_key_.size());
            (void)platform::ui::settings_store::put_blob(kPkiPublicNs, kPkiPrivateKey,
                                                         pki_private_key_.data(), pki_private_key_.size());
        }
    }

    return have_keys;
}

void MeshtasticRadioAdapter::loadPkiNodeKeys()
{
    struct PkiKeyEntry
    {
        uint32_t node_id;
        uint32_t last_seen;
        uint8_t key[32];
    } __attribute__((packed));

    std::vector<uint8_t> blob;
    if (!platform::ui::settings_store::get_blob(kPkiNodeNs, kPkiNodeKey, blob) ||
        blob.size() < sizeof(PkiKeyEntry))
    {
        return;
    }

    const size_t count = std::min(blob.size() / sizeof(PkiKeyEntry), kMaxPkiNodes);
    const auto* entries = reinterpret_cast<const PkiKeyEntry*>(blob.data());
    node_public_keys_.clear();
    node_key_last_seen_.clear();
    for (size_t i = 0; i < count; ++i)
    {
        if (entries[i].node_id == 0)
        {
            continue;
        }
        std::array<uint8_t, 32> key{};
        std::memcpy(key.data(), entries[i].key, sizeof(entries[i].key));
        node_public_keys_[entries[i].node_id] = key;
        node_key_last_seen_[entries[i].node_id] = entries[i].last_seen;
    }
}

void MeshtasticRadioAdapter::savePkiNodeKey(::chat::NodeId node_id,
                                            const uint8_t* key,
                                            size_t key_len)
{
    if (node_id == 0)
    {
        return;
    }

    if (!key || key_len != 32)
    {
        logMeshtasticRx("[gat562][mt] ignore invalid pki key node=%08lX len=%u\n",
                        static_cast<unsigned long>(node_id),
                        static_cast<unsigned>(key_len));
        return;
    }

    std::array<uint8_t, 32> key_copy{};
    std::memcpy(key_copy.data(), key, key_copy.size());

    auto it = node_public_keys_.find(node_id);
    const bool existed = (it != node_public_keys_.end());
    const bool changed = !existed || (it->second != key_copy);

    if (changed)
    {
        node_public_keys_[node_id] = key_copy;
        markPkiKeysDirty();

        logMeshtasticRx("[gat562][mt] pki key updated node=%08lX\n",
                        static_cast<unsigned long>(node_id));
    }

    // 鏃犺 key 鏄惁鍙樺寲锛岄兘鍒锋柊鏈€杩戠湅鍒拌鑺傜偣鍏挜鐨勬椂闂?    touchPkiNodeKey(node_id);
}

void MeshtasticRadioAdapter::markPkiKeysDirty()
{
    pki_node_keys_dirty_ = true;
    pki_node_keys_save_due_ms_ = millis() + kPkiNodeSaveDebounceMs;
}

bool MeshtasticRadioAdapter::savePkiKeysToPrefs()
{
    struct PkiKeyEntry
    {
        uint32_t node_id;
        uint32_t last_seen;
        uint8_t key[32];
    } __attribute__((packed));

    std::vector<PkiKeyEntry> entries;
    entries.reserve(node_public_keys_.size());
    for (const auto& item : node_public_keys_)
    {
        PkiKeyEntry entry{};
        entry.node_id = item.first;
        auto seen_it = node_key_last_seen_.find(item.first);
        entry.last_seen = (seen_it != node_key_last_seen_.end()) ? seen_it->second : 0;
        std::memcpy(entry.key, item.second.data(), sizeof(entry.key));
        entries.push_back(entry);
    }

    if (entries.size() > kMaxPkiNodes)
    {
        std::sort(entries.begin(), entries.end(),
                  [](const PkiKeyEntry& a, const PkiKeyEntry& b)
                  {
                      return a.last_seen < b.last_seen;
                  });
        const size_t drop = entries.size() - kMaxPkiNodes;
        for (size_t i = 0; i < drop; ++i)
        {
            node_public_keys_.erase(entries[i].node_id);
            node_key_last_seen_.erase(entries[i].node_id);
        }
        entries.erase(entries.begin(), entries.begin() + static_cast<long>(drop));
    }

    const bool persisted = platform::ui::settings_store::put_blob(kPkiNodeNs, kPkiNodeKey,
                                                                  entries.empty() ? nullptr : entries.data(),
                                                                  entries.size() * sizeof(PkiKeyEntry));
    if (persisted)
    {
        pki_node_keys_dirty_ = false;
    }
    return persisted;
}

void MeshtasticRadioAdapter::touchPkiNodeKey(::chat::NodeId node_id)
{
    node_key_last_seen_[node_id] = nowSeconds();
}

bool MeshtasticRadioAdapter::decryptPkiPayload(::chat::NodeId from, ::chat::MessageId packet_id,
                                               const uint8_t* cipher, size_t cipher_len,
                                               uint8_t* out_plain, size_t* out_plain_len)
{
    if (!cipher || cipher_len <= 12 || !out_plain || !out_plain_len || !pki_ready_)
    {
        return false;
    }

    auto it = node_public_keys_.find(from);
    if (it == node_public_keys_.end())
    {
        (void)buildAndQueueNodeInfo(from, true, ::chat::ChannelId::PRIMARY);
        size_t primary_key_len = 0;
        const uint8_t* primary_key = selectKey(config_, ::chat::ChannelId::PRIMARY, &primary_key_len);
        (void)sendRoutingError(from, packet_id, channelHashFor(config_, ::chat::ChannelId::PRIMARY),
                               ::chat::ChannelId::PRIMARY,
                               primary_key, primary_key_len,
                               meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY, 0);
        return false;
    }

    touchPkiNodeKey(from);
    uint8_t shared[32] = {};
    uint8_t local_priv[32] = {};
    std::memcpy(shared, it->second.data(), sizeof(shared));
    std::memcpy(local_priv, pki_private_key_.data(), sizeof(local_priv));
    if (!Curve25519::dh2(shared, local_priv))
    {
        return false;
    }
    hashSharedKey(shared, sizeof(shared));

    const uint8_t* auth = cipher + (cipher_len - 12);
    uint32_t extra_nonce = 0;
    std::memcpy(&extra_nonce, auth + 8, sizeof(extra_nonce));

    uint8_t nonce[::chat::meshtastic::kPkiNonceSize] = {};
    initPkiNonce(from, static_cast<uint64_t>(packet_id), extra_nonce, nonce);

    const size_t plain_len = cipher_len - 12;
    if (*out_plain_len < plain_len)
    {
        *out_plain_len = plain_len;
        return false;
    }
    if (!::chat::meshtastic::decryptPkiAesCcm(shared, sizeof(shared), nonce, 8,
                                              cipher, plain_len, nullptr, 0, auth, out_plain))
    {
        return false;
    }

    *out_plain_len = plain_len;
    return true;
}

bool MeshtasticRadioAdapter::encryptPkiPayload(::chat::NodeId dest, ::chat::MessageId packet_id,
                                               const uint8_t* plain, size_t plain_len,
                                               uint8_t* out_cipher, size_t* out_cipher_len)
{
    if (!plain || !out_cipher || !out_cipher_len || !pki_ready_)
    {
        return false;
    }

    auto it = node_public_keys_.find(dest);
    if (it == node_public_keys_.end())
    {
        return false;
    }

    touchPkiNodeKey(dest);
    uint8_t shared[32] = {};
    uint8_t local_priv[32] = {};
    std::memcpy(shared, it->second.data(), sizeof(shared));
    std::memcpy(local_priv, pki_private_key_.data(), sizeof(local_priv));
    if (!Curve25519::dh2(shared, local_priv))
    {
        return false;
    }
    hashSharedKey(shared, sizeof(shared));

    const uint32_t extra_nonce = static_cast<uint32_t>(random());
    uint8_t nonce[::chat::meshtastic::kPkiNonceSize] = {};
    initPkiNonce(node_id_, static_cast<uint64_t>(packet_id), extra_nonce, nonce);

    const size_t needed = plain_len + 8 + sizeof(extra_nonce);
    if (*out_cipher_len < needed)
    {
        *out_cipher_len = needed;
        return false;
    }

    uint8_t auth[16] = {};
    if (!::chat::meshtastic::encryptPkiAesCcm(shared, sizeof(shared), nonce, 8,
                                              nullptr, 0,
                                              plain, plain_len,
                                              out_cipher, auth))
    {
        return false;
    }

    std::memcpy(out_cipher + plain_len, auth, 8);
    std::memcpy(out_cipher + plain_len + 8, &extra_nonce, sizeof(extra_nonce));
    *out_cipher_len = needed;
    return true;
}

void MeshtasticRadioAdapter::updateKeyVerificationState()
{
    if (kv_state_ == KeyVerificationState::Idle)
    {
        return;
    }
    const uint32_t now_ms = millis();
    if (kv_nonce_ms_ != 0 && (now_ms - kv_nonce_ms_) > kKeyVerificationTimeoutMs)
    {
        resetKeyVerificationState();
        return;
    }
    kv_nonce_ms_ = now_ms;
}

void MeshtasticRadioAdapter::resetKeyVerificationState()
{
    kv_state_ = KeyVerificationState::Idle;
    kv_nonce_ = 0;
    kv_nonce_ms_ = 0;
    kv_security_number_ = 0;
    kv_remote_node_ = 0;
    kv_hash1_.fill(0);
    kv_hash2_.fill(0);
}

void MeshtasticRadioAdapter::buildVerificationCode(char* out, size_t out_len) const
{
    if (!out || out_len < 10)
    {
        if (out && out_len > 0)
        {
            out[0] = '\0';
        }
        return;
    }
    for (int i = 0; i < 4; ++i)
    {
        out[i] = static_cast<char>((kv_hash1_[i] >> 2) + '0');
        out[i + 5] = static_cast<char>((kv_hash1_[i + 4] >> 2) + '0');
    }
    out[4] = ' ';
    out[9] = '\0';
}

bool MeshtasticRadioAdapter::handleKeyVerificationInit(const ::chat::meshtastic::PacketHeaderWire& header,
                                                       const meshtastic_KeyVerification& kv)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::Idle || header.to != node_id_ || header.to == kBroadcastNode || !pki_ready_)
    {
        return false;
    }
    auto key_it = node_public_keys_.find(header.from);
    if (key_it == node_public_keys_.end())
    {
        return false;
    }

    kv_nonce_ = kv.nonce;
    kv_nonce_ms_ = millis();
    kv_remote_node_ = header.from;
    kv_security_number_ = static_cast<uint32_t>(random(1, 1000000));
    if (!computeKeyVerificationHashes(kv_security_number_,
                                      kv_nonce_,
                                      kv_remote_node_,
                                      node_id_,
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
    reply.hash2.size = static_cast<pb_size_t>(kv_hash2_.size());
    std::memcpy(reply.hash2.bytes, kv_hash2_.data(), kv_hash2_.size());
    if (!sendKeyVerificationPacket(kv_remote_node_, reply, false))
    {
        resetKeyVerificationState();
        return false;
    }

    kv_state_ = KeyVerificationState::ReceiverAwaitingHash1;
    sys::EventBus::publish(new sys::KeyVerificationNumberInformEvent(kv_remote_node_, kv_nonce_, kv_security_number_), 0);
    return true;
}

bool MeshtasticRadioAdapter::handleKeyVerificationReply(const ::chat::meshtastic::PacketHeaderWire& header,
                                                        const meshtastic_KeyVerification& kv)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::SenderInitiated ||
        header.to != node_id_ || header.to == kBroadcastNode ||
        kv.nonce != kv_nonce_ || header.from != kv_remote_node_ ||
        kv.hash1.size != 0 || kv.hash2.size != 32)
    {
        return false;
    }

    std::memcpy(kv_hash2_.data(), kv.hash2.bytes, kv_hash2_.size());
    kv_state_ = KeyVerificationState::SenderAwaitingNumber;
    kv_nonce_ms_ = millis();
    sys::EventBus::publish(new sys::KeyVerificationNumberRequestEvent(kv_remote_node_, kv_nonce_), 0);
    return true;
}

bool MeshtasticRadioAdapter::processKeyVerificationNumber(::chat::NodeId remote_node, uint64_t nonce, uint32_t number)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::SenderAwaitingNumber ||
        kv_remote_node_ != remote_node || kv_nonce_ != nonce)
    {
        return false;
    }

    auto key_it = node_public_keys_.find(remote_node);
    if (key_it == node_public_keys_.end())
    {
        resetKeyVerificationState();
        return false;
    }

    std::array<uint8_t, 32> scratch_hash{};
    kv_security_number_ = number;
    if (!computeKeyVerificationHashes(kv_security_number_,
                                      kv_nonce_,
                                      node_id_,
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
    if (std::memcmp(scratch_hash.data(), kv_hash2_.data(), kv_hash2_.size()) != 0)
    {
        return false;
    }

    meshtastic_KeyVerification response = meshtastic_KeyVerification_init_zero;
    response.nonce = kv_nonce_;
    response.hash1.size = static_cast<pb_size_t>(kv_hash1_.size());
    std::memcpy(response.hash1.bytes, kv_hash1_.data(), kv_hash1_.size());
    if (!sendKeyVerificationPacket(kv_remote_node_, response, true))
    {
        return false;
    }

    kv_state_ = KeyVerificationState::SenderAwaitingUser;
    kv_nonce_ms_ = millis();
    char code[12] = {};
    buildVerificationCode(code, sizeof(code));
    sys::EventBus::publish(new sys::KeyVerificationFinalEvent(kv_remote_node_, kv_nonce_, true, code), 0);
    return true;
}

bool MeshtasticRadioAdapter::handleKeyVerificationFinal(const ::chat::meshtastic::PacketHeaderWire& header,
                                                        const meshtastic_KeyVerification& kv)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::ReceiverAwaitingHash1 ||
        header.to != node_id_ || header.to == kBroadcastNode ||
        kv.nonce != kv_nonce_ || header.from != kv_remote_node_ ||
        kv.hash1.size != 32 || kv.hash2.size != 0)
    {
        return false;
    }
    if (std::memcmp(kv.hash1.bytes, kv_hash1_.data(), kv_hash1_.size()) != 0)
    {
        return false;
    }

    kv_state_ = KeyVerificationState::ReceiverAwaitingUser;
    kv_nonce_ms_ = millis();
    char code[12] = {};
    buildVerificationCode(code, sizeof(code));
    sys::EventBus::publish(new sys::KeyVerificationFinalEvent(kv_remote_node_, kv_nonce_, false, code), 0);
    return true;
}

bool MeshtasticRadioAdapter::sendKeyVerificationPacket(::chat::NodeId dest, const meshtastic_KeyVerification& kv,
                                                       bool want_response)
{
    if (!pki_ready_ || !hasPkiKey(dest))
    {
        return false;
    }

    uint8_t kv_buf[96] = {};
    pb_ostream_t kv_stream = pb_ostream_from_buffer(kv_buf, sizeof(kv_buf));
    if (!pb_encode(&kv_stream, meshtastic_KeyVerification_fields, &kv))
    {
        return false;
    }

    uint8_t data_buf[160] = {};
    size_t data_size = sizeof(data_buf);
    if (!::chat::meshtastic::encodeAppData(meshtastic_PortNum_KEY_VERIFICATION_APP,
                                           kv_buf,
                                           kv_stream.bytes_written,
                                           want_response,
                                           data_buf,
                                           &data_size))
    {
        return false;
    }

    uint8_t pki_buf[256] = {};
    size_t pki_len = sizeof(pki_buf);
    const ::chat::MessageId packet_id = next_packet_id_++;
    if (!encryptPkiPayload(dest, packet_id, data_buf, data_size, pki_buf, &pki_len))
    {
        return false;
    }

    uint8_t wire[384] = {};
    size_t wire_size = sizeof(wire);
    if (!::chat::meshtastic::buildWirePacket(pki_buf, pki_len,
                                             node_id_, packet_id,
                                             dest, 0, config_.hop_limit, false,
                                             nullptr, 0, wire, &wire_size))
    {
        return false;
    }

    return transmitPreparedWire(wire, wire_size, ::chat::ChannelId::PRIMARY, nullptr, false, true);
}

std::string MeshtasticRadioAdapter::mqttNodeIdString() const
{
    char node_id[16];
    snprintf(node_id, sizeof(node_id), "!%08lx", static_cast<unsigned long>(node_id_));
    return std::string(node_id);
}

const char* MeshtasticRadioAdapter::mqttChannelIdFor(::chat::ChannelId channel) const
{
    if (channel == ::chat::ChannelId::SECONDARY && !mqtt_proxy_settings_.secondary_channel_id.empty())
    {
        return mqtt_proxy_settings_.secondary_channel_id.c_str();
    }
    if (!mqtt_proxy_settings_.primary_channel_id.empty())
    {
        return mqtt_proxy_settings_.primary_channel_id.c_str();
    }
    return nullptr;
}

bool MeshtasticRadioAdapter::hasAnyMqttDownlinkEnabled() const
{
    return mqtt_proxy_settings_.primary_downlink_enabled ||
           mqtt_proxy_settings_.secondary_downlink_enabled;
}

bool MeshtasticRadioAdapter::shouldPublishToMqtt(::chat::ChannelId channel, bool from_mqtt, bool is_pki) const
{
    if (!mqtt_proxy_settings_.enabled || !mqtt_proxy_settings_.proxy_to_client_enabled || from_mqtt)
    {
        return false;
    }
    if (is_pki)
    {
        return true;
    }
    if (channel == ::chat::ChannelId::SECONDARY)
    {
        return mqtt_proxy_settings_.secondary_uplink_enabled;
    }
    return mqtt_proxy_settings_.primary_uplink_enabled;
}

uint8_t MeshtasticRadioAdapter::mqttChannelHashForId(const char* channel_id, bool* out_known,
                                                     ::chat::ChannelId* out_channel) const
{
    bool known = false;
    ::chat::ChannelId channel = ::chat::ChannelId::PRIMARY;
    size_t key_len = 0;
    const uint8_t* key = nullptr;
    uint8_t hash = 0;

    if (channel_id && std::strcmp(channel_id, "PKI") == 0)
    {
        known = hasAnyMqttDownlinkEnabled();
        hash = 0;
    }
    else if (channel_id && !mqtt_proxy_settings_.primary_channel_id.empty() &&
             std::strcmp(channel_id, mqtt_proxy_settings_.primary_channel_id.c_str()) == 0)
    {
        known = mqtt_proxy_settings_.primary_downlink_enabled;
        key = selectKey(config_, ::chat::ChannelId::PRIMARY, &key_len);
        hash = ::chat::meshtastic::computeChannelHash(
            channelNameFor(config_, ::chat::ChannelId::PRIMARY),
            key,
            key_len);
    }
    else if (channel_id && !mqtt_proxy_settings_.secondary_channel_id.empty() &&
             std::strcmp(channel_id, mqtt_proxy_settings_.secondary_channel_id.c_str()) == 0)
    {
        known = mqtt_proxy_settings_.secondary_downlink_enabled;
        channel = ::chat::ChannelId::SECONDARY;
        key = selectKey(config_, ::chat::ChannelId::SECONDARY, &key_len);
        hash = ::chat::meshtastic::computeChannelHash(
            channelNameFor(config_, ::chat::ChannelId::SECONDARY),
            key,
            key_len);
    }

    if (out_known)
    {
        *out_known = known;
    }
    if (out_channel)
    {
        *out_channel = channel;
    }
    return hash;
}

bool MeshtasticRadioAdapter::decodeMqttServiceEnvelope(const uint8_t* payload, size_t payload_len,
                                                       meshtastic_MeshPacket* out_packet,
                                                       char* out_channel_id, size_t channel_id_len,
                                                       char* out_gateway_id, size_t gateway_id_len) const
{
    if (!payload || payload_len == 0 || !out_packet || !out_channel_id || !out_gateway_id ||
        channel_id_len == 0 || gateway_id_len == 0)
    {
        return false;
    }

    *out_packet = meshtastic_MeshPacket_init_zero;
    out_channel_id[0] = '\0';
    out_gateway_id[0] = '\0';

    pb_istream_t stream = pb_istream_from_buffer(payload, payload_len);
    while (stream.bytes_left > 0)
    {
        pb_wire_type_t wire_type = PB_WT_VARINT;
        uint32_t tag = 0;
        bool eof = false;
        if (!pb_decode_tag(&stream, &wire_type, &tag, &eof))
        {
            return false;
        }
        if (eof)
        {
            break;
        }

        if (tag == meshtastic_ServiceEnvelope_packet_tag)
        {
            if (wire_type != PB_WT_STRING)
            {
                return false;
            }
            pb_istream_t substream;
            if (!pb_make_string_substream(&stream, &substream))
            {
                return false;
            }
            bool ok = pb_decode(&substream, meshtastic_MeshPacket_fields, out_packet);
            pb_close_string_substream(&stream, &substream);
            if (!ok)
            {
                return false;
            }
        }
        else if (tag == meshtastic_ServiceEnvelope_channel_id_tag)
        {
            if (wire_type != PB_WT_STRING)
            {
                return false;
            }
            pb_istream_t substream;
            if (!pb_make_string_substream(&stream, &substream))
            {
                return false;
            }
            bool ok = readPbString(&substream, out_channel_id, channel_id_len);
            pb_close_string_substream(&stream, &substream);
            if (!ok)
            {
                return false;
            }
        }
        else if (tag == meshtastic_ServiceEnvelope_gateway_id_tag)
        {
            if (wire_type != PB_WT_STRING)
            {
                return false;
            }
            pb_istream_t substream;
            if (!pb_make_string_substream(&stream, &substream))
            {
                return false;
            }
            bool ok = readPbString(&substream, out_gateway_id, gateway_id_len);
            pb_close_string_substream(&stream, &substream);
            if (!ok)
            {
                return false;
            }
        }
        else if (!pb_skip_field(&stream, wire_type))
        {
            return false;
        }
    }

    return out_packet->which_payload_variant == meshtastic_MeshPacket_decoded_tag ||
           out_packet->which_payload_variant == meshtastic_MeshPacket_encrypted_tag;
}

bool MeshtasticRadioAdapter::injectMqttEnvelope(const meshtastic_MeshPacket& packet,
                                                const char* channel_id,
                                                const char* gateway_id)
{
    (void)gateway_id;

    if (!mqtt_proxy_settings_.enabled || !mqtt_proxy_settings_.proxy_to_client_enabled)
    {
        return false;
    }

    bool known_channel = false;
    ::chat::ChannelId channel_index = ::chat::ChannelId::PRIMARY;
    const uint8_t channel_hash = mqttChannelHashForId(channel_id, &known_channel, &channel_index);
    if (!known_channel)
    {
        return false;
    }

    if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag)
    {
        if (mqtt_proxy_settings_.encryption_enabled)
        {
            return false;
        }
        if (packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP)
        {
            return false;
        }
    }

    auto& scratch = mqtt_scratch_;
    std::fill(scratch.wire.begin(), scratch.wire.end(), 0);
    uint8_t* wire_buffer = scratch.wire.data();
    size_t wire_size = sizeof(::chat::meshtastic::PacketHeaderWire);

    if (packet.which_payload_variant == meshtastic_MeshPacket_encrypted_tag)
    {
        ::chat::meshtastic::PacketHeaderWire header{};
        header.to = packet.to;
        header.from = packet.from;
        header.id = packet.id;
        header.flags = (packet.hop_limit & ::chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK) |
                       ((packet.hop_start << ::chat::meshtastic::PACKET_FLAGS_HOP_START_SHIFT) &
                        ::chat::meshtastic::PACKET_FLAGS_HOP_START_MASK) |
                       ::chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK;
        if (packet.want_ack)
        {
            header.flags |= ::chat::meshtastic::PACKET_FLAGS_WANT_ACK_MASK;
        }
        header.channel = packet.channel != 0 ? packet.channel : channel_hash;
        header.next_hop = packet.next_hop;
        header.relay_node = packet.relay_node;
        std::memcpy(wire_buffer, &header, sizeof(header));

        const size_t enc_size =
            std::min(static_cast<size_t>(packet.encrypted.size), sizeof(packet.encrypted.bytes));
        if (wire_size + enc_size > scratch.wire.size())
        {
            return false;
        }
        std::memcpy(wire_buffer + wire_size, packet.encrypted.bytes, enc_size);
        wire_size += enc_size;
    }
    else
    {
        std::memset(&scratch.decoded, 0, sizeof(scratch.decoded));
        scratch.decoded = packet.decoded;
        meshtastic_Data& decoded = scratch.decoded;
        decoded.dest = packet.to;
        decoded.source = packet.from;
        decoded.has_bitfield = true;

        std::fill(scratch.buffer.begin(), scratch.buffer.end(), 0);
        pb_ostream_t dstream = pb_ostream_from_buffer(scratch.buffer.data(), scratch.buffer.size());
        if (!pb_encode(&dstream, meshtastic_Data_fields, &decoded))
        {
            return false;
        }

        size_t psk_len = 0;
        const uint8_t* psk = selectKey(config_, channel_index, &psk_len);
        size_t rebuilt_size = scratch.wire.size();
        if (!::chat::meshtastic::buildWirePacket(scratch.buffer.data(), dstream.bytes_written,
                                                 packet.from, packet.id,
                                                 packet.to, channel_hash, packet.hop_limit, packet.want_ack,
                                                 psk, psk_len, wire_buffer, &rebuilt_size))
        {
            return false;
        }
        wire_size = rebuilt_size;
        auto* rebuilt_header = reinterpret_cast<::chat::meshtastic::PacketHeaderWire*>(wire_buffer);
        rebuilt_header->flags |= ::chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK;
        if (packet.hop_start != 0)
        {
            rebuilt_header->flags &= ~::chat::meshtastic::PACKET_FLAGS_HOP_START_MASK;
            rebuilt_header->flags |= ((packet.hop_start << ::chat::meshtastic::PACKET_FLAGS_HOP_START_SHIFT) &
                                      ::chat::meshtastic::PACKET_FLAGS_HOP_START_MASK);
        }
        rebuilt_header->next_hop = packet.next_hop;
        rebuilt_header->relay_node = packet.relay_node;
    }

    handleRawPacket(wire_buffer, wire_size);
    return true;
}

bool MeshtasticRadioAdapter::queueMqttProxyPublish(const meshtastic_MeshPacket& packet,
                                                   const char* channel_id)
{
    if (!mqtt_proxy_settings_.enabled || !mqtt_proxy_settings_.proxy_to_client_enabled ||
        !channel_id || *channel_id == '\0')
    {
        return false;
    }

    auto& scratch = mqtt_scratch_;
    std::memset(&scratch.proxy, 0, sizeof(scratch.proxy));
    std::string node_id = mqttNodeIdString();
    meshtastic_ServiceEnvelope env = meshtastic_ServiceEnvelope_init_zero;
    env.packet = const_cast<meshtastic_MeshPacket*>(&packet);
    env.channel_id = const_cast<char*>(channel_id);
    env.gateway_id = const_cast<char*>(node_id.c_str());

    pb_ostream_t estream = pb_ostream_from_buffer(scratch.proxy.payload_variant.data.bytes,
                                                  sizeof(scratch.proxy.payload_variant.data.bytes));
    if (!pb_encode(&estream, meshtastic_ServiceEnvelope_fields, &env))
    {
        return false;
    }

    meshtastic_MqttClientProxyMessage& proxy = scratch.proxy;
    proxy.which_payload_variant = meshtastic_MqttClientProxyMessage_data_tag;
    const std::string root = mqtt_proxy_settings_.root.empty() ? std::string("msh") : mqtt_proxy_settings_.root;
    const std::string topic = root + "/2/e/" + channel_id + "/" + node_id;
    std::strncpy(proxy.topic, topic.c_str(), sizeof(proxy.topic) - 1);
    proxy.topic[sizeof(proxy.topic) - 1] = '\0';
    proxy.payload_variant.data.size = static_cast<pb_size_t>(estream.bytes_written);
    proxy.retained = false;

    while (mqtt_proxy_queue_.size() >= kMaxMqttProxyQueue)
    {
        mqtt_proxy_queue_.pop();
    }
    mqtt_proxy_queue_.push(proxy);
    logMeshtasticRx("[gat562][mt][mqtt] queued topic=%s env=%u variant=%u port=%u q=%u\n",
                    proxy.topic,
                    static_cast<unsigned>(proxy.payload_variant.data.size),
                    static_cast<unsigned>(packet.which_payload_variant),
                    packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag
                        ? static_cast<unsigned>(packet.decoded.portnum)
                        : 0U,
                    static_cast<unsigned>(mqtt_proxy_queue_.size()));
    return true;
}

bool MeshtasticRadioAdapter::queueMqttProxyPublishFromWire(const uint8_t* wire_data,
                                                           size_t wire_size,
                                                           const meshtastic_Data* decoded,
                                                           ::chat::ChannelId channel_index)
{
    if (!wire_data || wire_size < sizeof(::chat::meshtastic::PacketHeaderWire))
    {
        return false;
    }

    ::chat::meshtastic::PacketHeaderWire header{};
    auto& scratch = mqtt_scratch_;
    std::fill(scratch.buffer.begin(), scratch.buffer.end(), 0);
    size_t payload_size = scratch.buffer.size();
    if (!::chat::meshtastic::parseWirePacket(wire_data, wire_size, &header, scratch.buffer.data(), &payload_size))
    {
        return false;
    }

    const bool from_mqtt = (header.flags & ::chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK) != 0;
    const bool is_pki = (header.channel == 0);
    if (!shouldPublishToMqtt(channel_index, from_mqtt, is_pki))
    {
        return false;
    }

    const char* channel_id = is_pki ? "PKI" : mqttChannelIdFor(channel_index);
    if (!channel_id || *channel_id == '\0')
    {
        return false;
    }

    if (mqtt_proxy_settings_.encryption_enabled)
    {
        std::memset(&scratch.packet, 0, sizeof(scratch.packet));
        if (!makeEncryptedPacketFromWire(wire_data, wire_size, &scratch.packet))
        {
            return false;
        }
        return queueMqttProxyPublish(scratch.packet, channel_id);
    }

    if (!decoded)
    {
        return false;
    }

    std::memset(&scratch.packet, 0, sizeof(scratch.packet));
    fillDecodedPacketCommon(&scratch.packet, *decoded, header, channel_index);
    return queueMqttProxyPublish(scratch.packet, channel_id);
}

} // namespace platform::nrf52::arduino_common::chat::meshtastic
