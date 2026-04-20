/**
 * @file mt_adapter.cpp
 * @brief Meshtastic mesh adapter implementation
 */

#include "platform/esp/arduino_common/chat/infra/meshtastic/mt_adapter.h"
#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/domain/contact_types.h"
#include "chat/ports/i_node_store.h"
#include "chat/time_utils.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/arduino_common/gps/gps_service_api.h"
#include "sys/event_bus.h"
#include "team/protocol/team_portnum.h"
#include <Arduino.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#define TEST_CURVE25519_FIELD_OPS
#include "../../internal/blob_store_io.h"
#include "board/TLoRaPagerTypes.h"
#include "chat/infra/meshtastic/mt_pki_crypto.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/mqtt.pb.h"
#include <Curve25519.h>
#include <RNG.h>
#include <RadioLib.h>
#include <vector>

#ifndef LORA_LOG_ENABLE
#define LORA_LOG_ENABLE 0
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
constexpr uint8_t kDefaultPskIndex = 1;
constexpr const char* kSecondaryChannelName = "Squad";
constexpr uint8_t kLoraSyncWord = 0x2b;
constexpr uint16_t kLoraPreambleLen = 16;
constexpr uint8_t kBitfieldWantResponseMask = 0x02;
constexpr size_t kMaxMqttProxyQueue = 12;
constexpr uint32_t kBroadcastNodeId = 0xFFFFFFFFu;

using chat::meshtastic::allowPkiForPortnum;
using chat::meshtastic::appendTraceRouteNodeAndSnr;
using chat::meshtastic::computeHopsAway;
using chat::meshtastic::computeKeyVerificationHashes;
using chat::meshtastic::decryptPkiAesCcm;
using chat::meshtastic::djb2HashText;
using chat::meshtastic::encryptPkiAesCcm;
using chat::meshtastic::fillDecodedPacketCommon;
using chat::meshtastic::hashSharedKey;
using chat::meshtastic::initPkiNonce;
using chat::meshtastic::insertTraceRouteUnknownHops;
using chat::meshtastic::makeEncryptedPacketFromWire;
using chat::meshtastic::modemPresetToParams;
using chat::meshtastic::readPbString;
using chat::meshtastic::shouldSetAirWantAck;

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
    case meshtastic_PortNum_KEY_VERIFICATION_APP:
        return "KEY_VERIFY";
    case team::proto::TEAM_MGMT_APP:
        return "TEAM_MGMT";
    case team::proto::TEAM_POSITION_APP:
        return "TEAM_POS";
    case team::proto::TEAM_WAYPOINT_APP:
        return "TEAM_WP";
    case team::proto::TEAM_TRACK_APP:
        return "TEAM_TRACK";
    case team::proto::TEAM_CHAT_APP:
        return "TEAM_CHAT";
    default:
        return "UNKNOWN";
    }
}

bool shouldRequireDirectPki(uint8_t encrypt_mode, uint32_t dest_node, uint32_t portnum)
{
    // Match upstream Meshtastic: direct unicast app traffic uses PKI outside
    // cleartext/Ham-style operation, and must not silently fall back to channel crypto.
    return encrypt_mode != 0 &&
           dest_node != kBroadcastNodeId &&
           allowPkiForPortnum(portnum);
}

void mt_diag_log(const char* fmt, ...)
{
    char buf[192] = {};
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.print(buf);
}

void mt_diag_dropf(const chat::meshtastic::PacketHeaderWire* header,
                   const char* reason,
                   const char* fmt = nullptr,
                   ...)
{
    char detail[96] = {};
    if (fmt && fmt[0] != '\0')
    {
        va_list args;
        va_start(args, fmt);
        vsnprintf(detail, sizeof(detail), fmt, args);
        va_end(args);
    }

    if (header)
    {
        mt_diag_log("[MT][RX_DROP] reason=%s from=%08lX to=%08lX id=%08lX ch=%u%s%s\n",
                    reason ? reason : "unknown",
                    static_cast<unsigned long>(header->from),
                    static_cast<unsigned long>(header->to),
                    static_cast<unsigned long>(header->id),
                    static_cast<unsigned>(header->channel),
                    detail[0] ? " " : "",
                    detail);
    }
    else
    {
        mt_diag_log("[MT][RX_DROP] reason=%s%s%s\n",
                    reason ? reason : "unknown",
                    detail[0] ? " " : "",
                    detail);
    }
}

using chat::meshtastic::computeChannelHash;
using chat::meshtastic::expandShortPsk;
using chat::meshtastic::hasValidPosition;
using chat::meshtastic::isZeroKey;
using chat::meshtastic::keyVerificationStage;
using chat::meshtastic::routingErrorName;
using chat::meshtastic::toHex;

static bool build_self_position_payload(uint8_t* out_buf, size_t* out_len)
{
    if (!out_buf || !out_len || *out_len == 0)
    {
        return false;
    }

    gps::GpsState gps_state = gps::gps_get_data();
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
        if (course < 0.0) course = 0.0;
        uint32_t cdeg = static_cast<uint32_t>(lround(course * 100.0));
        if (cdeg >= 36000U) cdeg = 35999U;
        pos.has_ground_track = true;
        pos.ground_track = cdeg;
    }
    if (gps_state.satellites > 0)
    {
        pos.sats_in_view = gps_state.satellites;
    }

    uint32_t ts = static_cast<uint32_t>(time(nullptr));
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

static void publishPositionEvent(uint32_t node_id, const meshtastic_Position& pos)
{
    if (node_id == 0 || !hasValidPosition(pos))
    {
        return;
    }

    bool has_altitude = pos.has_altitude || pos.has_altitude_hae;
    int32_t altitude = pos.has_altitude ? pos.altitude : (pos.has_altitude_hae ? pos.altitude_hae : 0);
    uint32_t ts = pos.timestamp ? pos.timestamp : pos.time;

    sys::NodePositionUpdateEvent* event = new sys::NodePositionUpdateEvent(
        node_id,
        pos.latitude_i,
        pos.longitude_i,
        has_altitude,
        altitude,
        ts,
        pos.precision_bits,
        pos.PDOP,
        pos.HDOP,
        pos.VDOP,
        pos.gps_accuracy);
    sys::EventBus::publish(event, 0);
}

} // namespace

// Use protobuf codec and wire packet functions
using chat::meshtastic::buildWirePacket;
using chat::meshtastic::decodeKeyVerificationMessage;
using chat::meshtastic::decodeTextMessage;
using chat::meshtastic::decryptPayload;
using chat::meshtastic::encodeAppData;
using chat::meshtastic::encodeNodeInfoMessage;
using chat::meshtastic::encodeTextMessage;
using chat::meshtastic::PacketHeaderWire;
using chat::meshtastic::parseWirePacket;

namespace chat
{
namespace meshtastic
{

MeshCapabilities MtAdapter::getCapabilities() const
{
    MeshCapabilities caps;
    caps.supports_unicast_text = true;
    caps.supports_unicast_appdata = true;
    caps.supports_broadcast_appdata = true;
    caps.supports_appdata_ack = true;
    caps.provides_appdata_sender = true;
    caps.supports_node_info = true;
    caps.supports_pki = true;
    return caps;
}

MtAdapter::MtAdapter(LoraBoard& board)
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
      last_position_reply_ms_(0),
      last_rx_rssi_(std::numeric_limits<float>::quiet_NaN()),
      last_rx_snr_(std::numeric_limits<float>::quiet_NaN()),
      kv_state_(KeyVerificationState::Idle),
      kv_nonce_(0),
      kv_nonce_ms_(0),
      kv_security_number_(0),
      kv_remote_node_(0),
      kv_hash1_{},
      kv_hash2_{},
      last_raw_packet_len_(0),
      has_pending_raw_packet_(false)
{
    config_ = MeshConfig(); // Default config
    initNodeIdentity();
    next_packet_id_ = static_cast<MessageId>(random(1, 0x7FFFFFFF));
    LORA_LOG("[LORA] packet id start=%lu\n", static_cast<unsigned long>(next_packet_id_));
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
    return sendTextWithId(channel, text, 0, out_msg_id, peer);
}

bool MtAdapter::sendTextWithId(ChannelId channel, const std::string& text,
                               MessageId forced_msg_id,
                               MessageId* out_msg_id, NodeId peer)
{
    if (!ready_ || text.empty() || !config_.tx_enabled)
    {
        return false;
    }

    ChannelId out_channel = channel;
    if (encrypt_mode_ == 0 || encrypt_mode_ == 2)
    {
        out_channel = ChannelId::PRIMARY;
    }

    PendingSend pending;
    pending.channel = out_channel;
    pending.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    pending.text = text;
    pending.msg_id = (forced_msg_id != 0) ? forced_msg_id : next_packet_id_++;
    if (forced_msg_id != 0 && forced_msg_id >= next_packet_id_)
    {
        next_packet_id_ = forced_msg_id + 1;
        if (next_packet_id_ == 0)
        {
            next_packet_id_ = 1;
        }
    }
    pending.dest = (peer != 0) ? peer : 0xFFFFFFFF;
    pending.retry_count = 0;
    pending.last_attempt = 0;

    send_queue_.push(pending);
    mt_diag_log("[MT][TX] queue text id=%08lX dest=%08lX logical_ch=%u len=%u\n",
                static_cast<unsigned long>(pending.msg_id),
                static_cast<unsigned long>(pending.dest),
                static_cast<unsigned>(out_channel),
                static_cast<unsigned>(text.size()));
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
                            NodeId dest, bool want_ack,
                            MessageId packet_id,
                            bool want_response)
{
    if (!ready_ || !config_.tx_enabled)
    {
        return false;
    }

    uint32_t now_ms = millis();
    if (min_tx_interval_ms_ > 0 && last_tx_ms_ > 0 &&
        (now_ms - last_tx_ms_) < min_tx_interval_ms_)
    {
        return false;
    }

    ChannelId out_channel = channel;
    if (encrypt_mode_ == 0 || encrypt_mode_ == 2)
    {
        out_channel = ChannelId::PRIMARY;
    }

    uint8_t data_buffer[256];
    size_t data_size = sizeof(data_buffer);
    // Keep legacy behavior for existing callers: want_ack implied want_response.
    bool effective_want_response = want_response || want_ack;
    if (!encodeAppData(portnum, payload, len, effective_want_response, data_buffer, &data_size))
    {
        return false;
    }

    uint8_t wire_buffer[512];
    size_t wire_size = sizeof(wire_buffer);

    uint8_t channel_hash =
        (out_channel == ChannelId::SECONDARY) ? secondary_channel_hash_ : primary_channel_hash_;
    const uint8_t* psk =
        (out_channel == ChannelId::SECONDARY) ? secondary_psk_ : primary_psk_;
    size_t psk_len =
        (out_channel == ChannelId::SECONDARY) ? secondary_psk_len_ : primary_psk_len_;
    uint8_t hop_limit = config_.hop_limit;
    uint32_t dest_node = (dest != 0) ? dest : kBroadcastNodeId;
    bool track_ack = want_ack;
    bool air_want_ack = shouldSetAirWantAck(dest_node, track_ack);
    MessageId msg_id = (packet_id != 0) ? packet_id : next_packet_id_++;
    if (packet_id != 0 && packet_id >= next_packet_id_)
    {
        next_packet_id_ = packet_id + 1;
        if (next_packet_id_ == 0)
        {
            next_packet_id_ = 1;
        }
    }

    const uint8_t* out_payload = data_buffer;
    size_t out_len = data_size;
    bool use_pki = false;
    if (shouldRequireDirectPki(encrypt_mode_, dest_node, portnum))
    {
        const bool have_dest_key = node_public_keys_.find(dest_node) != node_public_keys_.end();
        if (!pki_ready_ || !have_dest_key)
        {
            const char* reason = !pki_ready_ ? "pki_not_ready" : "pki_key_missing";
            mt_diag_log("[MT][TX_BLOCK] id=%08lX dest=%08lX port=%u reason=%s path=PKI\n",
                        static_cast<unsigned long>(msg_id),
                        static_cast<unsigned long>(dest_node),
                        static_cast<unsigned>(portnum),
                        reason);
            LORA_LOG("[LORA] TX app PKI required but unavailable dest=%08lX port=%u\n",
                     (unsigned long)dest_node,
                     (unsigned)portnum);
            return false;
        }

        uint8_t pki_buf[256];
        size_t pki_len = sizeof(pki_buf);
        if (!encryptPkiPayload(dest_node, msg_id, data_buffer, data_size, pki_buf, &pki_len))
        {
            mt_diag_log("[MT][TX_BLOCK] id=%08lX dest=%08lX port=%u reason=pki_encrypt_fail path=PKI\n",
                        static_cast<unsigned long>(msg_id),
                        static_cast<unsigned long>(dest_node),
                        static_cast<unsigned>(portnum));
            LORA_LOG("[LORA] TX app PKI encrypt failed dest=%08lX port=%u\n",
                     (unsigned long)dest_node,
                     (unsigned)portnum);
            return false;
        }
        out_payload = pki_buf;
        out_len = pki_len;
        channel_hash = 0; // PKI channel
        track_ack = true;
        air_want_ack = true;
        psk = nullptr;
        psk_len = 0;
        use_pki = true;
    }

    if (!use_pki)
    {
        if (out_channel == ChannelId::SECONDARY)
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

    mt_diag_log("[MT][TX_ROUTE] id=%08lX dest=%08lX port=%u logical_ch=%u wire_ch=%u path=%s payload=%u\n",
                static_cast<unsigned long>(msg_id),
                static_cast<unsigned long>(dest_node),
                static_cast<unsigned>(portnum),
                static_cast<unsigned>(out_channel),
                static_cast<unsigned>(channel_hash),
                use_pki ? "PKI" : "CHANNEL",
                static_cast<unsigned>(out_len));
    if (!buildWirePacket(out_payload, out_len, node_id_, msg_id,
                         dest_node, channel_hash, hop_limit, air_want_ack,
                         psk, psk_len, wire_buffer, &wire_size))
    {
        return false;
    }

    if (!board_.isRadioOnline())
    {
        return false;
    }

    bool ok = transmitWirePacket(wire_buffer, wire_size);
    mt_diag_log("[MT][TX] app id=%08lX dest=%08lX port=%u ok=%u air_ack=%u track_ack=%u len=%u\n",
                static_cast<unsigned long>(msg_id),
                static_cast<unsigned long>(dest_node),
                static_cast<unsigned>(portnum),
                ok ? 1U : 0U,
                air_want_ack ? 1U : 0U,
                track_ack ? 1U : 0U,
                static_cast<unsigned>(wire_size));
    LORA_LOG("[LORA] TX app port=%u len=%u want_resp=%u air_ack=%u track_ack=%u ok=%d\n",
             (unsigned)portnum,
             (unsigned)wire_size,
             effective_want_response ? 1U : 0U,
             air_want_ack ? 1U : 0U,
             track_ack ? 1U : 0U,
             ok ? 1 : 0);
    if (ok)
    {
        last_tx_ms_ = now_ms;
        if (track_ack)
        {
            trackPendingAck(msg_id, dest_node, out_channel, channel_hash, wire_buffer, wire_size);
        }
        meshtastic_Data mqtt_data = meshtastic_Data_init_default;
        mqtt_data.portnum = static_cast<meshtastic_PortNum>(portnum);
        mqtt_data.want_response = effective_want_response;
        mqtt_data.dest = dest_node;
        mqtt_data.source = node_id_;
        mqtt_data.has_bitfield = true;
        mqtt_data.payload.size = std::min(len, sizeof(mqtt_data.payload.bytes));
        if (mqtt_data.payload.size > 0)
        {
            memcpy(mqtt_data.payload.bytes, payload, mqtt_data.payload.size);
        }
        queueMqttProxyPublishFromWire(wire_buffer, wire_size,
                                      use_pki ? nullptr : &mqtt_data,
                                      out_channel);
    }
    return ok;
}

bool MtAdapter::sendMeshPacket(const meshtastic_MeshPacket& packet)
{
    last_send_error_ = meshtastic_Routing_Error_NONE;
    if (!ready_ || !config_.tx_enabled || !board_.isRadioOnline())
    {
        last_send_error_ = meshtastic_Routing_Error_NO_INTERFACE;
        return false;
    }
    if (packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
    {
        last_send_error_ = meshtastic_Routing_Error_BAD_REQUEST;
        return false;
    }

    uint32_t now_ms = millis();
    if (min_tx_interval_ms_ > 0 && last_tx_ms_ > 0 &&
        (now_ms - last_tx_ms_) < min_tx_interval_ms_)
    {
        last_send_error_ = meshtastic_Routing_Error_DUTY_CYCLE_LIMIT;
        return false;
    }

    uint32_t msg_id = (packet.id != 0) ? packet.id : next_packet_id_++;
    if (packet.id != 0 && packet.id >= next_packet_id_)
    {
        next_packet_id_ = packet.id + 1;
        if (next_packet_id_ == 0)
        {
            next_packet_id_ = 1;
        }
    }

    meshtastic_Data data = packet.decoded;
    data.dest = (packet.to != 0) ? packet.to : kBroadcastNodeId;
    data.source = node_id_;
    data.has_bitfield = true;

    uint8_t data_buf[256];
    pb_ostream_t dstream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
    if (!pb_encode(&dstream, meshtastic_Data_fields, &data))
    {
        last_send_error_ = meshtastic_Routing_Error_TOO_LARGE;
        return false;
    }

    const uint32_t dest = data.dest;
    const uint8_t hop_limit = (packet.hop_limit > 0) ? packet.hop_limit : config_.hop_limit;
    bool track_ack = packet.want_ack;
    bool air_want_ack = shouldSetAirWantAck(dest, track_ack);
    uint8_t channel_hash = primary_channel_hash_;
    const uint8_t* psk = primary_psk_;
    size_t psk_len = primary_psk_len_;
    const uint8_t* out_payload = data_buf;
    size_t out_len = dstream.bytes_written;

    if (packet.pki_encrypted)
    {
        if (dest == 0 || dest == 0xFFFFFFFF)
        {
            last_send_error_ = meshtastic_Routing_Error_BAD_REQUEST;
            return false;
        }
        if (packet.public_key.size > 0 && packet.public_key.size != pki_public_key_.size())
        {
            last_send_error_ = meshtastic_Routing_Error_BAD_REQUEST;
            return false;
        }
        if (packet.public_key.size == pki_public_key_.size())
        {
            savePkiNodeKey(dest, packet.public_key.bytes, packet.public_key.size);
        }
        if (!pki_ready_)
        {
            last_send_error_ = meshtastic_Routing_Error_PKI_FAILED;
            return false;
        }
        if (node_public_keys_.find(dest) == node_public_keys_.end())
        {
            last_send_error_ = meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY;
            return false;
        }

        uint8_t pki_buf[256];
        size_t pki_len = sizeof(pki_buf);
        if (!encryptPkiPayload(dest, msg_id, data_buf, dstream.bytes_written, pki_buf, &pki_len))
        {
            last_send_error_ = meshtastic_Routing_Error_PKI_FAILED;
            return false;
        }

        out_payload = pki_buf;
        out_len = pki_len;
        channel_hash = 0;
        psk = nullptr;
        psk_len = 0;
        track_ack = true;
        air_want_ack = true;
    }
    else if (packet.channel == 1)
    {
        channel_hash = secondary_channel_hash_;
        psk = secondary_psk_;
        psk_len = secondary_psk_len_;
    }
    else if (packet.channel != 0)
    {
        last_send_error_ = meshtastic_Routing_Error_NO_CHANNEL;
        return false;
    }

    uint8_t wire_buffer[512];
    size_t wire_size = sizeof(wire_buffer);
    if (!buildWirePacket(out_payload, out_len, node_id_, msg_id,
                         dest, channel_hash, hop_limit, air_want_ack,
                         psk, psk_len, wire_buffer, &wire_size))
    {
        last_send_error_ = meshtastic_Routing_Error_TOO_LARGE;
        return false;
    }

    if (transmitWirePacket(wire_buffer, wire_size))
    {
        last_tx_ms_ = now_ms;
        last_send_error_ = meshtastic_Routing_Error_NONE;
        if (track_ack)
        {
            trackPendingAck(msg_id, dest, (packet.channel == 1) ? ChannelId::SECONDARY : ChannelId::PRIMARY,
                            channel_hash, wire_buffer, wire_size);
        }
        ChannelId mqtt_channel = (packet.channel == 1) ? ChannelId::SECONDARY : ChannelId::PRIMARY;
        queueMqttProxyPublishFromWire(wire_buffer, wire_size,
                                      (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag) ? &data : nullptr,
                                      mqtt_channel);
        return true;
    }
    last_send_error_ = meshtastic_Routing_Error_NO_INTERFACE;
    return false;
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

bool MtAdapter::requestNodeInfo(NodeId dest, bool want_response)
{
    if (!ready_)
    {
        return false;
    }
    uint32_t target = (dest == 0) ? 0xFFFFFFFF : dest;
    ChannelId channel = ChannelId::PRIMARY;
    if (target != 0xFFFFFFFF)
    {
        auto it = node_last_channel_.find(target);
        if (it != node_last_channel_.end())
        {
            channel = it->second;
        }
    }
    return sendNodeInfoTo(target, want_response, channel);
}

bool MtAdapter::isPkiReady() const
{
    return pki_ready_;
}

bool MtAdapter::hasPkiKey(NodeId dest) const
{
    return node_public_keys_.find(dest) != node_public_keys_.end();
}

bool MtAdapter::getNodePublicKey(NodeId node_id, uint8_t out_key[32]) const
{
    if (!out_key)
    {
        return false;
    }
    auto it = node_public_keys_.find(node_id);
    if (it == node_public_keys_.end())
    {
        return false;
    }
    memcpy(out_key, it->second.data(), 32);
    return true;
}

bool MtAdapter::getOwnPublicKey(uint8_t out_key[32]) const
{
    if (!out_key || !pki_ready_)
    {
        return false;
    }
    memcpy(out_key, pki_public_key_.data(), 32);
    return true;
}

void MtAdapter::rememberNodePublicKey(NodeId node_id, const uint8_t* key, size_t key_len)
{
    if (node_id == 0 || !key || key_len != pki_public_key_.size())
    {
        return;
    }
    savePkiNodeKey(node_id, key, key_len);
}

void MtAdapter::forgetNodePublicKey(NodeId node_id)
{
    if (node_id == 0)
    {
        return;
    }
    node_public_keys_.erase(node_id);
    node_key_last_seen_.erase(node_id);
    node_last_channel_.erase(node_id);
    nodeinfo_last_seen_ms_.erase(node_id);
    node_long_names_.erase(node_id);
    savePkiKeysToPrefs();
}

meshtastic_Routing_Error MtAdapter::getLastRoutingError() const
{
    return last_send_error_;
}

void MtAdapter::setMqttProxySettings(const MqttProxySettings& settings)
{
    mqtt_proxy_settings_ = settings;
}

bool MtAdapter::pollMqttProxyMessage(meshtastic_MqttClientProxyMessage* out)
{
    if (!out || mqtt_proxy_queue_.empty())
    {
        return false;
    }
    *out = mqtt_proxy_queue_.front();
    mqtt_proxy_queue_.pop();
    return true;
}

std::string MtAdapter::mqttNodeIdString() const
{
    char node_id[16];
    snprintf(node_id, sizeof(node_id), "!%08lx", static_cast<unsigned long>(node_id_));
    return std::string(node_id);
}

const char* MtAdapter::mqttChannelIdFor(ChannelId channel) const
{
    if (channel == ChannelId::SECONDARY && !mqtt_proxy_settings_.secondary_channel_id.empty())
    {
        return mqtt_proxy_settings_.secondary_channel_id.c_str();
    }
    if (!mqtt_proxy_settings_.primary_channel_id.empty())
    {
        return mqtt_proxy_settings_.primary_channel_id.c_str();
    }
    return nullptr;
}

bool MtAdapter::hasAnyMqttDownlinkEnabled() const
{
    return mqtt_proxy_settings_.primary_downlink_enabled ||
           mqtt_proxy_settings_.secondary_downlink_enabled;
}

bool MtAdapter::shouldPublishToMqtt(ChannelId channel, bool from_mqtt, bool is_pki) const
{
    if (!mqtt_proxy_settings_.enabled || !mqtt_proxy_settings_.proxy_to_client_enabled || from_mqtt)
    {
        return false;
    }
    if (is_pki)
    {
        return true;
    }
    if (channel == ChannelId::SECONDARY)
    {
        return mqtt_proxy_settings_.secondary_uplink_enabled;
    }
    return mqtt_proxy_settings_.primary_uplink_enabled;
}

uint8_t MtAdapter::mqttChannelHashForId(const char* channel_id, bool* out_known,
                                        ChannelId* out_channel) const
{
    bool known = false;
    ChannelId channel = ChannelId::PRIMARY;
    uint8_t hash = primary_channel_hash_;
    if (channel_id && strcmp(channel_id, "PKI") == 0)
    {
        known = hasAnyMqttDownlinkEnabled();
        hash = 0;
        channel = ChannelId::PRIMARY;
    }
    else if (channel_id && !mqtt_proxy_settings_.primary_channel_id.empty() &&
             strcmp(channel_id, mqtt_proxy_settings_.primary_channel_id.c_str()) == 0)
    {
        known = mqtt_proxy_settings_.primary_downlink_enabled;
        hash = primary_channel_hash_;
        channel = ChannelId::PRIMARY;
    }
    else if (channel_id && !mqtt_proxy_settings_.secondary_channel_id.empty() &&
             strcmp(channel_id, mqtt_proxy_settings_.secondary_channel_id.c_str()) == 0)
    {
        known = mqtt_proxy_settings_.secondary_downlink_enabled;
        hash = secondary_channel_hash_;
        channel = ChannelId::SECONDARY;
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

bool MtAdapter::decodeMqttServiceEnvelope(const uint8_t* payload, size_t payload_len,
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

bool MtAdapter::injectMqttEnvelope(const meshtastic_MeshPacket& packet,
                                   const char* channel_id,
                                   const char* gateway_id)
{
    if (!mqtt_proxy_settings_.enabled || !mqtt_proxy_settings_.proxy_to_client_enabled)
    {
        return false;
    }

    bool known_channel = false;
    ChannelId channel_index = ChannelId::PRIMARY;
    uint8_t channel_hash = mqttChannelHashForId(channel_id, &known_channel, &channel_index);
    if (!known_channel)
    {
        LORA_LOG("[MQTT] downlink ignored unknown/disabled channel id='%s'\n",
                 channel_id ? channel_id : "");
        return false;
    }

    if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag)
    {
        if (mqtt_proxy_settings_.encryption_enabled)
        {
            LORA_LOG("[MQTT] downlink ignore decoded payload while mqtt encryption enabled\n");
            return false;
        }
        if (packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP)
        {
            LORA_LOG("[MQTT] downlink ignore admin payload\n");
            return false;
        }
    }

    PacketHeaderWire header{};
    header.to = packet.to;
    header.from = packet.from;
    header.id = packet.id;
    header.flags = (packet.hop_limit & PACKET_FLAGS_HOP_LIMIT_MASK) |
                   ((packet.hop_start << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK) |
                   PACKET_FLAGS_VIA_MQTT_MASK;
    if (packet.want_ack)
    {
        header.flags |= PACKET_FLAGS_WANT_ACK_MASK;
    }
    header.channel = (packet.which_payload_variant == meshtastic_MeshPacket_encrypted_tag)
                         ? packet.channel
                         : channel_hash;
    if (header.channel == 0 && packet.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
    {
        header.channel = channel_hash;
    }
    header.next_hop = packet.next_hop;
    header.relay_node = packet.relay_node;

    uint8_t wire_buffer[sizeof(PacketHeaderWire) + 256];
    size_t wire_size = sizeof(PacketHeaderWire);
    memcpy(wire_buffer, &header, sizeof(header));

    if (packet.which_payload_variant == meshtastic_MeshPacket_encrypted_tag)
    {
        size_t payload_size = std::min(static_cast<size_t>(packet.encrypted.size), sizeof(packet.encrypted.bytes));
        if (wire_size + payload_size > sizeof(wire_buffer))
        {
            return false;
        }
        memcpy(wire_buffer + wire_size, packet.encrypted.bytes, payload_size);
        wire_size += payload_size;
    }
    else
    {
        meshtastic_Data decoded = packet.decoded;
        decoded.dest = packet.to;
        decoded.source = packet.from;
        decoded.has_bitfield = true;
        uint8_t data_buffer[256];
        pb_ostream_t dstream = pb_ostream_from_buffer(data_buffer, sizeof(data_buffer));
        if (!pb_encode(&dstream, meshtastic_Data_fields, &decoded))
        {
            return false;
        }
        const uint8_t* psk = nullptr;
        size_t psk_len = 0;
        if (channel_hash == secondary_channel_hash_)
        {
            psk = secondary_psk_;
            psk_len = secondary_psk_len_;
        }
        else if (channel_hash == primary_channel_hash_)
        {
            psk = primary_psk_;
            psk_len = primary_psk_len_;
        }
        uint8_t rebuilt[sizeof(wire_buffer)];
        size_t rebuilt_size = sizeof(rebuilt);
        if (!buildWirePacket(data_buffer, dstream.bytes_written, packet.from, packet.id,
                             packet.to, channel_hash, packet.hop_limit, packet.want_ack,
                             psk, psk_len, rebuilt, &rebuilt_size))
        {
            return false;
        }
        if (rebuilt_size > sizeof(wire_buffer))
        {
            return false;
        }
        memcpy(wire_buffer, rebuilt, rebuilt_size);
        wire_size = rebuilt_size;
        auto* rebuilt_header = reinterpret_cast<PacketHeaderWire*>(wire_buffer);
        rebuilt_header->flags |= PACKET_FLAGS_VIA_MQTT_MASK;
        if (packet.hop_start != 0)
        {
            rebuilt_header->flags &= ~PACKET_FLAGS_HOP_START_MASK;
            rebuilt_header->flags |= ((packet.hop_start << PACKET_FLAGS_HOP_START_SHIFT) &
                                      PACKET_FLAGS_HOP_START_MASK);
        }
        rebuilt_header->next_hop = packet.next_hop;
        rebuilt_header->relay_node = packet.relay_node;
    }

    LORA_LOG("[MQTT] downlink inject topic_ch='%s' gateway='%s' from=%08lX to=%08lX id=%08lX\n",
             channel_id ? channel_id : "",
             gateway_id ? gateway_id : "",
             (unsigned long)packet.from,
             (unsigned long)packet.to,
             (unsigned long)packet.id);
    processReceivedPacket(wire_buffer, wire_size);
    return true;
}

bool MtAdapter::handleMqttProxyMessage(const meshtastic_MqttClientProxyMessage& msg)
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

    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    char channel_id[32] = {0};
    char gateway_id[16] = {0};
    if (!decodeMqttServiceEnvelope(data_field->bytes, data_field->size,
                                   &packet,
                                   channel_id, sizeof(channel_id),
                                   gateway_id, sizeof(gateway_id)))
    {
        LORA_LOG("[MQTT] downlink decode fail topic='%s' len=%u\n",
                 msg.topic,
                 static_cast<unsigned>(data_field->size));
        return false;
    }
    return injectMqttEnvelope(packet, channel_id, gateway_id);
}

bool MtAdapter::queueMqttProxyPublish(const meshtastic_MeshPacket& packet,
                                      const char* channel_id)
{
    if (!mqtt_proxy_settings_.enabled || !mqtt_proxy_settings_.proxy_to_client_enabled ||
        !channel_id || *channel_id == '\0')
    {
        return false;
    }

    uint8_t env_buf[435];
    meshtastic_MeshPacket packet_copy = packet;
    std::string node_id = mqttNodeIdString();
    meshtastic_ServiceEnvelope env = meshtastic_ServiceEnvelope_init_zero;
    env.packet = &packet_copy;
    env.channel_id = const_cast<char*>(channel_id);
    env.gateway_id = const_cast<char*>(node_id.c_str());

    pb_ostream_t estream = pb_ostream_from_buffer(env_buf, sizeof(env_buf));
    if (!pb_encode(&estream, meshtastic_ServiceEnvelope_fields, &env))
    {
        LORA_LOG("[MQTT] uplink encode fail ch='%s' err=%s\n",
                 channel_id,
                 PB_GET_ERROR(&estream));
        return false;
    }

    meshtastic_MqttClientProxyMessage proxy = meshtastic_MqttClientProxyMessage_init_zero;
    proxy.which_payload_variant = meshtastic_MqttClientProxyMessage_data_tag;
    std::string root = mqtt_proxy_settings_.root.empty() ? std::string("msh") : mqtt_proxy_settings_.root;
    std::string topic = root + "/2/e/" + channel_id + "/" + node_id;
    strncpy(proxy.topic, topic.c_str(), sizeof(proxy.topic) - 1);
    proxy.topic[sizeof(proxy.topic) - 1] = '\0';
    proxy.payload_variant.data.size = static_cast<pb_size_t>(estream.bytes_written);
    memcpy(proxy.payload_variant.data.bytes, env_buf, estream.bytes_written);
    proxy.retained = false;

    while (mqtt_proxy_queue_.size() >= kMaxMqttProxyQueue)
    {
        mqtt_proxy_queue_.pop();
    }
    mqtt_proxy_queue_.push(proxy);
    LORA_LOG("[MQTT] uplink queue topic='%s' bytes=%u q=%u\n",
             proxy.topic,
             static_cast<unsigned>(proxy.payload_variant.data.size),
             static_cast<unsigned>(mqtt_proxy_queue_.size()));
    return true;
}

bool MtAdapter::queueMqttProxyPublishFromWire(const uint8_t* wire_data,
                                              size_t wire_size,
                                              const meshtastic_Data* decoded,
                                              ChannelId channel_index)
{
    if (!wire_data || wire_size < sizeof(PacketHeaderWire))
    {
        return false;
    }

    PacketHeaderWire header{};
    uint8_t payload[256];
    size_t payload_size = sizeof(payload);
    if (!parseWirePacket(wire_data, wire_size, &header, payload, &payload_size))
    {
        return false;
    }

    const bool from_mqtt = (header.flags & PACKET_FLAGS_VIA_MQTT_MASK) != 0;
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
        meshtastic_MeshPacket encrypted_packet = meshtastic_MeshPacket_init_zero;
        if (!makeEncryptedPacketFromWire(wire_data, wire_size, &encrypted_packet))
        {
            return false;
        }
        return queueMqttProxyPublish(encrypted_packet, channel_id);
    }

    if (!decoded)
    {
        return false;
    }

    meshtastic_MeshPacket decoded_packet = meshtastic_MeshPacket_init_zero;
    fillDecodedPacketCommon(&decoded_packet, *decoded, header, channel_index);
    return queueMqttProxyPublish(decoded_packet, channel_id);
}

void MtAdapter::applyConfig(const MeshConfig& config)
{
    config_ = config;
    updateChannelKeys();
    configureRadio();
}

void MtAdapter::setUserInfo(const char* long_name, const char* short_name)
{
    std::string new_long = (long_name && long_name[0]) ? long_name : "";
    std::string new_short = (short_name && short_name[0]) ? short_name : "";
    if (new_short.size() > 4)
    {
        new_short.resize(4);
    }

    if (new_long == user_long_name_ && new_short == user_short_name_)
    {
        return;
    }

    user_long_name_ = new_long;
    user_short_name_ = new_short;
    last_nodeinfo_ms_ = 0;
}

void MtAdapter::setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent)
{
    if (config_.override_duty_cycle)
    {
        min_tx_interval_ms_ = 0;
        return;
    }

    meshtastic_Config_LoRaConfig_RegionCode region_code =
        static_cast<meshtastic_Config_LoRaConfig_RegionCode>(config_.region);
    if (region_code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
    {
        region_code = meshtastic_Config_LoRaConfig_RegionCode_CN;
    }
    const chat::meshtastic::RegionInfo* region = chat::meshtastic::findRegion(region_code);
    if (!region || region->duty_cycle_percent >= 100.0f)
    {
        min_tx_interval_ms_ = 0;
        return;
    }

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

void MtAdapter::setPrivacyConfig(uint8_t encrypt_mode)
{
    encrypt_mode_ = encrypt_mode;
}

void MtAdapter::setLastRxStats(float rssi, float snr)
{
    last_rx_rssi_ = rssi;
    last_rx_snr_ = snr;
}

bool MtAdapter::isReady() const
{
    return ready_ && board_.isRadioOnline();
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
        mt_diag_log("[MT][RX_DROP] reason=parse_fail len=%u\n",
                    static_cast<unsigned>(size));
        LORA_LOG("[LORA] RX parse fail len=%u hex=%s\n",
                 (unsigned)size,
                 raw_hex.c_str());
        return;
    }

    const bool matches_primary_channel = (header.channel == primary_channel_hash_);
    const bool matches_secondary_channel =
        (secondary_psk_len_ > 0 && header.channel == secondary_channel_hash_);

    std::string full_hex = toHex(data, size, size);
    LORA_LOG("[LORA] RX wire from=%08lX to=%08lX id=%08lX ch=0x%02X flags=0x%02X len=%u\n",
             (unsigned long)header.from,
             (unsigned long)header.to,
             (unsigned long)header.id,
             header.channel,
             header.flags,
             (unsigned)payload_size);
    const char* channel_kind = "UNKNOWN";
    if (matches_primary_channel)
    {
        channel_kind = "PRIMARY";
    }
    else if (matches_secondary_channel)
    {
        channel_kind = "SECONDARY";
    }
    else if (header.channel == 0)
    {
        channel_kind = "ZERO_UNMATCHED";
    }
    LORA_LOG("[LORA] RX channel kind=%s hash=0x%02X\n", channel_kind, header.channel);
    LORA_LOG("[LORA] RX full packet hex: %s\n", full_hex.c_str());

    // Check for duplicates
    if (dedup_.isDuplicate(header.from, header.id))
    {
        mt_diag_dropf(&header, "dedup");
        LORA_LOG("[LORA] RX dedup from=%08lX id=%08lX\n",
                 (unsigned long)header.from,
                 (unsigned long)header.id);
        return; // Duplicate, ignore
    }

    chat::RxMeta rx_meta;
    rx_meta.rx_timestamp_ms = millis();
    uint32_t epoch_s = chat::now_epoch_seconds();
    if (chat::is_valid_epoch(epoch_s))
    {
        rx_meta.rx_timestamp_s = epoch_s;
        rx_meta.time_source = chat::RxTimeSource::DeviceUtc;
    }
    else
    {
        rx_meta.time_source = chat::RxTimeSource::Uptime;
        rx_meta.rx_timestamp_s = rx_meta.rx_timestamp_ms / 1000U;
    }
    const bool from_is = (header.flags & chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK) != 0;
    rx_meta.from_is = from_is;
    rx_meta.origin = from_is ? chat::RxOrigin::External : chat::RxOrigin::Mesh;
    rx_meta.channel_hash = header.channel;
    rx_meta.wire_flags = header.flags;
    rx_meta.next_hop = header.next_hop;
    rx_meta.relay_node = header.relay_node;
    uint8_t hop_limit = header.flags & chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
    uint8_t hop_count = computeHopsAway(header.flags);
    rx_meta.hop_count = hop_count;
    rx_meta.hop_limit = (hop_count == 0xFF) ? 0xFF : hop_limit;
    rx_meta.direct = (hop_count == 0);
    if (!std::isnan(last_rx_rssi_))
    {
        rx_meta.rssi_dbm_x10 = static_cast<int16_t>(std::lround(last_rx_rssi_ * 10.0f));
    }
    if (!std::isnan(last_rx_snr_))
    {
        rx_meta.snr_db_x10 = static_cast<int16_t>(std::lround(last_rx_snr_ * 10.0f));
    }
    rx_meta.freq_hz = radio_freq_hz_;
    rx_meta.bw_hz = radio_bw_hz_;
    rx_meta.sf = radio_sf_;
    rx_meta.cr = radio_cr_;

    mt_diag_log("[MT][RX] from=%08lX to=%08lX id=%08lX flags=0x%02X ch=%u next=%u relay=%u len=%u\n",
                static_cast<unsigned long>(header.from),
                static_cast<unsigned long>(header.to),
                static_cast<unsigned long>(header.id),
                static_cast<unsigned>(header.flags),
                static_cast<unsigned>(header.channel),
                static_cast<unsigned>(header.next_hop),
                static_cast<unsigned>(header.relay_node),
                static_cast<unsigned>(payload_size));

    if (header.from == node_id_)
    {
        auto pending_it = pending_ack_states_.find(header.id);
        if (header.to == kBroadcastNodeId && pending_it != pending_ack_states_.end())
        {
            const ChannelId channel_id = pending_it->second.channel;
            const uint8_t channel_hash = pending_it->second.channel_hash;
            mt_diag_log("[MT][IMPLICIT_ACK] observed self-broadcast id=%08lX relay=%08lX next=%08lX ch=%u\n",
                        static_cast<unsigned long>(header.id),
                        static_cast<unsigned long>(header.relay_node),
                        static_cast<unsigned long>(header.next_hop),
                        static_cast<unsigned>(header.channel));
            pending_ack_states_.erase(pending_it);
            emitRoutingResultToPhone(header.id,
                                     meshtastic_Routing_Error_NONE,
                                     node_id_,
                                     node_id_,
                                     channel_id,
                                     channel_hash,
                                     &rx_meta);
            return;
        }

        mt_diag_dropf(&header, "self_echo");
        LORA_LOG("[LORA] RX self drop id=%08lX to=%08lX relay=%08lX ch=%u\n",
                 static_cast<unsigned long>(header.id),
                 static_cast<unsigned long>(header.to),
                 static_cast<unsigned long>(header.relay_node),
                 static_cast<unsigned>(header.channel));
        return;
    }

    // Decrypt payload if needed
    uint8_t plaintext[256];
    size_t plaintext_len = sizeof(plaintext);

    const uint8_t* psk = nullptr;
    size_t psk_len = 0;

    meshtastic_Data decoded = meshtastic_Data_init_default;
    ChannelId decoded_channel_id = ChannelId::PRIMARY;
    bool used_pki_transport = false;
    const bool can_try_pki =
        (header.to == node_id_ && header.to != kBroadcastNodeId && payload_size > 12);
    const char* last_drop_reason = nullptr;
    char last_drop_detail[96] = {};

    auto note_drop = [&](const char* reason, const char* detail = nullptr)
    {
        last_drop_reason = reason;
        if (detail && detail[0] != '\0')
        {
            std::snprintf(last_drop_detail, sizeof(last_drop_detail), "%s", detail);
        }
        else
        {
            last_drop_detail[0] = '\0';
        }
    };

    auto try_decode_candidate = [&](const char* path_name,
                                    const uint8_t* candidate_psk,
                                    size_t candidate_psk_len,
                                    bool candidate_pki,
                                    ChannelId candidate_channel) -> bool
    {
        uint8_t candidate_plaintext[256];
        size_t candidate_plaintext_len = sizeof(candidate_plaintext);

        if (candidate_pki)
        {
            if (!pki_ready_)
            {
                char detail[64];
                std::snprintf(detail, sizeof(detail), "path=%s", path_name);
                note_drop("pki_not_ready", detail);
                return false;
            }
            if (!decryptPkiPayload(header.from,
                                   header.id,
                                   payload,
                                   payload_size,
                                   candidate_plaintext,
                                   &candidate_plaintext_len))
            {
                char detail[64];
                std::snprintf(detail, sizeof(detail), "path=%s", path_name);
                note_drop("pki_decrypt_fail", detail);
                return false;
            }
        }
        else if (candidate_psk && candidate_psk_len > 0)
        {
            if (!decryptPayload(header,
                                payload,
                                payload_size,
                                candidate_psk,
                                candidate_psk_len,
                                candidate_plaintext,
                                &candidate_plaintext_len))
            {
                char detail[64];
                std::snprintf(detail, sizeof(detail), "path=%s", path_name);
                note_drop("channel_decrypt_fail", detail);
                return false;
            }
        }
        else
        {
            if (payload_size > sizeof(candidate_plaintext))
            {
                char detail[64];
                std::snprintf(detail,
                              sizeof(detail),
                              "path=%s len=%u",
                              path_name,
                              static_cast<unsigned>(payload_size));
                note_drop("payload_too_large", detail);
                return false;
            }
            memcpy(candidate_plaintext, payload, payload_size);
            candidate_plaintext_len = payload_size;
        }

        meshtastic_Data candidate_decoded = meshtastic_Data_init_default;
        pb_istream_t candidate_stream =
            pb_istream_from_buffer(candidate_plaintext, candidate_plaintext_len);
        if (!pb_decode(&candidate_stream, meshtastic_Data_fields, &candidate_decoded))
        {
            char detail[64];
            std::snprintf(detail, sizeof(detail), "path=%s", path_name);
            note_drop("data_decode_fail", detail);
            return false;
        }

        memcpy(plaintext, candidate_plaintext, candidate_plaintext_len);
        plaintext_len = candidate_plaintext_len;
        decoded = candidate_decoded;
        psk = candidate_psk;
        psk_len = candidate_psk_len;
        decoded_channel_id = candidate_channel;
        used_pki_transport = candidate_pki;
        if (header.channel == 0)
        {
            mt_diag_log("[MT][RX_ROUTE] id=%08lX ch=0 path=%s port=%u\n",
                        static_cast<unsigned long>(header.id),
                        path_name,
                        static_cast<unsigned>(decoded.portnum));
        }
        return true;
    };

    bool decoded_ok = false;
    if (matches_primary_channel)
    {
        decoded_ok = try_decode_candidate("PRIMARY",
                                          primary_psk_,
                                          primary_psk_len_,
                                          false,
                                          ChannelId::PRIMARY);
    }
    if (!decoded_ok && matches_secondary_channel)
    {
        decoded_ok = try_decode_candidate("SECONDARY",
                                          secondary_psk_,
                                          secondary_psk_len_,
                                          false,
                                          ChannelId::SECONDARY);
    }
    if (!decoded_ok && can_try_pki)
    {
        decoded_ok = try_decode_candidate("PKI",
                                          nullptr,
                                          0,
                                          true,
                                          matches_secondary_channel ? ChannelId::SECONDARY
                                                                    : ChannelId::PRIMARY);
    }

    if (!decoded_ok)
    {
        if (!(matches_primary_channel || matches_secondary_channel) && !can_try_pki)
        {
            char detail[64];
            std::snprintf(detail,
                          sizeof(detail),
                          "primary=0x%02X secondary=0x%02X",
                          static_cast<unsigned>(primary_channel_hash_),
                          static_cast<unsigned>(secondary_channel_hash_));
            note_drop("unknown_channel", detail);
        }

        if (last_drop_reason)
        {
            mt_diag_dropf(&header, last_drop_reason, "%s", last_drop_detail);
        }
        else
        {
            mt_diag_dropf(&header, "decode_failed");
        }

        std::string cipher_hex = toHex(payload, payload_size, payload_size);
        if (!(matches_primary_channel || matches_secondary_channel) && !can_try_pki)
        {
            LORA_LOG("[LORA] RX unknown channel hash=0x%02X from=%08lX id=%08lX len=%u hex=%s (skip decode)\n",
                     header.channel,
                     (unsigned long)header.from,
                     (unsigned long)header.id,
                     (unsigned)payload_size,
                     cipher_hex.c_str());
        }
        else if (last_drop_reason && std::strcmp(last_drop_reason, "pki_not_ready") == 0)
        {
            LORA_LOG("[LORA] RX PKI drop (not ready) from=%08lX id=%08lX len=%u\n",
                     (unsigned long)header.from,
                     (unsigned long)header.id,
                     (unsigned)payload_size);
            sendNodeInfoTo(header.from, true, ChannelId::PRIMARY);
            sendRoutingError(header.from, header.id, primary_channel_hash_,
                             primary_psk_, primary_psk_len_,
                             meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY);
        }
        else if (last_drop_reason && std::strcmp(last_drop_reason, "pki_decrypt_fail") == 0)
        {
            forgetNodePublicKey(header.from);
            sendNodeInfoTo(header.from, true, ChannelId::PRIMARY);
            sendRoutingError(header.from, header.id, primary_channel_hash_,
                             primary_psk_, primary_psk_len_,
                             meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY);
            mt_diag_log("[MT][PKI_RESYNC] node=%08lX action=forget_key+request_nodeinfo\n",
                        static_cast<unsigned long>(header.from));
            LORA_LOG("[LORA] RX PKI decrypt fail from=%08lX id=%08lX len=%u hex=%s\n",
                     (unsigned long)header.from,
                     (unsigned long)header.id,
                     (unsigned)payload_size,
                     cipher_hex.c_str());
        }
        else if (last_drop_reason && std::strcmp(last_drop_reason, "channel_decrypt_fail") == 0)
        {
            LORA_LOG("[LORA] RX decrypt fail id=%08lX ch=0x%02X psk=%u len=%u hex=%s\n",
                     (unsigned long)header.id,
                     header.channel,
                     (unsigned)psk_len,
                     (unsigned)payload_size,
                     cipher_hex.c_str());
        }
        else
        {
            LORA_LOG("[LORA] RX data decode fail id=%08lX len=%u hex=%s\n",
                     (unsigned long)header.id,
                     (unsigned)payload_size,
                     cipher_hex.c_str());
        }
        return;
    }

    // Only mark packets as seen after we have successfully identified and decoded them.
    // This avoids poisoning dedup for retries when a packet failed due to stale PKI state.
    dedup_.markSeen(header.from, header.id);

    if (plaintext_len > 0)
    {
        std::string protobuf_hex = toHex(plaintext, plaintext_len, plaintext_len);
        LORA_LOG("[LORA] RX protobuf hex: %s\n", protobuf_hex.c_str());
    }

    {
        const bool decoded_is_broadcast = (header.to == kBroadcastNodeId);
        const bool decoded_want_response = decoded.want_response ||
                                           (decoded.has_bitfield &&
                                            ((decoded.bitfield & kBitfieldWantResponseMask) != 0));
        mt_diag_log("[MT][RX_DECODE] from=%08lX to=%08lX id=%08lX ch=%u port=%u(%s) payload=%u bcast=%u resp=%u pki=%u\n",
                    static_cast<unsigned long>(header.from),
                    static_cast<unsigned long>(header.to),
                    static_cast<unsigned long>(header.id),
                    static_cast<unsigned>(header.channel),
                    static_cast<unsigned>(decoded.portnum),
                    portName(decoded.portnum),
                    static_cast<unsigned>(decoded.payload.size),
                    decoded_is_broadcast ? 1U : 0U,
                    decoded_want_response ? 1U : 0U,
                    used_pki_transport ? 1U : 0U);

        LORA_LOG("[LORA] RX data portnum=%u (%s) payload=%u\n",
                 (unsigned)decoded.portnum,
                 portName(decoded.portnum),
                 (unsigned)decoded.payload.size);
        LORA_LOG("[LORA] RX data plain port=%u dest=%08lX src=%08lX req=%08lX want_resp=%u bitfield=%u has_bitfield=%u payload=%u\n",
                 (unsigned)decoded.portnum,
                 (unsigned long)decoded.dest,
                 (unsigned long)decoded.source,
                 (unsigned long)decoded.request_id,
                 decoded.want_response ? 1U : 0U,
                 (unsigned)decoded.bitfield,
                 decoded.has_bitfield ? 1U : 0U,
                 (unsigned)decoded.payload.size);
        if (decoded.payload.size > 0)
        {
            std::string payload_hex = toHex(decoded.payload.bytes, decoded.payload.size, decoded.payload.size);
            LORA_LOG("[LORA] RX data payload hex: %s\n", payload_hex.c_str());
        }

        bool nodeinfo_decoded = false;
        uint8_t channel_index = 0xFF;
        if (header.channel == primary_channel_hash_)
        {
            channel_index = 0;
        }
        else if (header.channel == secondary_channel_hash_)
        {
            channel_index = 1;
        }
        auto publish_link_stats = [&](uint32_t node_id)
        {
            float snr = last_rx_snr_;
            float rssi = last_rx_rssi_;
            uint8_t hops_away = computeHopsAway(header.flags);
            if (std::isnan(snr) && std::isnan(rssi) && hops_away == 0xFF)
            {
                return;
            }
            uint32_t now_secs = time(nullptr);
            sys::NodeInfoUpdateEvent* event = new sys::NodeInfoUpdateEvent(
                node_id, "", "", snr, rssi, now_secs, 0,
                chat::contacts::kNodeRoleUnknown,
                hops_away, 0, channel_index);
            sys::EventBus::publish(event, 0);
        };

        if (decoded.portnum == meshtastic_PortNum_NODEINFO_APP && decoded.payload.size > 0)
        {
            meshtastic_NodeInfo node = meshtastic_NodeInfo_init_default;
            pb_istream_t nstream = pb_istream_from_buffer(decoded.payload.bytes, decoded.payload.size);
            if (pb_decode(&nstream, meshtastic_NodeInfo_fields, &node))
            {
                uint32_t node_id = node.num ? node.num : header.from;
                const char* short_name = node.has_user ? node.user.short_name : "";
                const char* long_name = node.has_user ? node.user.long_name : "";
                uint8_t role = chat::contacts::kNodeRoleUnknown;
                if (node.has_user && node.user.role <= meshtastic_Config_DeviceConfig_Role_CLIENT_BASE)
                {
                    role = static_cast<uint8_t>(node.user.role);
                }

                float snr = last_rx_snr_;
                if (std::isnan(snr))
                {
                    snr = node.snr;
                }
                float rssi = last_rx_rssi_;
                uint8_t hops_away = node.has_hops_away ? node.hops_away : computeHopsAway(header.flags);

                LORA_LOG("[LORA] RX NodeInfo from %08lX short='%s' long='%s' snr=%.1f\n",
                         (unsigned long)node_id, short_name, long_name, snr);

                if (long_name[0])
                {
                    node_long_names_[node_id] = long_name;
                }
                if (node.has_user && node.user.public_key.size == 32)
                {
                    std::array<uint8_t, 32> key{};
                    memcpy(key.data(), node.user.public_key.bytes, 32);
                    node_public_keys_[node_id] = key;
                    savePkiNodeKey(node_id, key.data(), key.size());
                    std::string key_fp = toHex(key.data(), key.size(), 8);
                    LORA_LOG("[LORA] PKI key stored for %08lX fp=%s\n",
                             (unsigned long)node_id, key_fp.c_str());
                    LORA_LOG("[LORA] PKI key updated for %08lX\n",
                             (unsigned long)node_id);
                }

                uint32_t now_secs = time(nullptr);
                chat::contacts::NodeDeviceMetrics metrics{};
                const bool has_metrics = node.has_device_metrics;
                if (has_metrics)
                {
                    metrics.has_battery_level = node.device_metrics.has_battery_level;
                    metrics.battery_level = node.device_metrics.battery_level;
                    metrics.has_voltage = node.device_metrics.has_voltage;
                    metrics.voltage = node.device_metrics.voltage;
                    metrics.has_channel_utilization = node.device_metrics.has_channel_utilization;
                    metrics.channel_utilization = node.device_metrics.channel_utilization;
                    metrics.has_air_util_tx = node.device_metrics.has_air_util_tx;
                    metrics.air_util_tx = node.device_metrics.air_util_tx;
                    metrics.has_uptime_seconds = node.device_metrics.has_uptime_seconds;
                    metrics.uptime_seconds = node.device_metrics.uptime_seconds;
                }
                sys::NodeInfoUpdateEvent* event = new sys::NodeInfoUpdateEvent(
                    node_id, short_name, long_name, snr, rssi, now_secs,
                    static_cast<uint8_t>(chat::contacts::NodeProtocolType::Meshtastic), role,
                    hops_away, static_cast<uint8_t>(node.user.hw_model), channel_index,
                    node.has_user, node.has_user ? node.user.macaddr : nullptr,
                    node.via_mqtt || ((header.flags & chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK) != 0),
                    node.is_ignored,
                    node.has_user && node.user.public_key.size == 32,
                    node.is_key_manually_verified,
                    has_metrics, has_metrics ? &metrics : nullptr);
                bool published = sys::EventBus::publish(event, 0);
                if (published)
                {
                    mt_diag_log("[MT][RX_NODEINFO] from=%08lX node=%08lX mode=nodeinfo published=1\n",
                                static_cast<unsigned long>(header.from),
                                static_cast<unsigned long>(node_id));
                    LORA_LOG("[LORA] NodeInfo event published node=%08lX\n",
                             (unsigned long)node_id);
                }
                else
                {
                    mt_diag_dropf(&header,
                                  "nodeinfo_event_drop",
                                  "node=%08lX pending=%u",
                                  static_cast<unsigned long>(node_id),
                                  static_cast<unsigned>(sys::EventBus::pendingCount()));
                    LORA_LOG("[LORA] NodeInfo event dropped node=%08lX pending=%u\n",
                             (unsigned long)node_id,
                             static_cast<unsigned>(sys::EventBus::pendingCount()));
                }
                if (node.has_position)
                {
                    publishPositionEvent(node_id, node.position);
                }
                nodeinfo_decoded = true;
            }
            else
            {
                meshtastic_User user = meshtastic_User_init_default;
                pb_istream_t ustream = pb_istream_from_buffer(decoded.payload.bytes, decoded.payload.size);
                if (pb_decode(&ustream, meshtastic_User_fields, &user))
                {
                    const uint32_t node_id = header.from;
                    const char* short_name = user.short_name[0] ? user.short_name : "";
                    const char* long_name = user.long_name[0] ? user.long_name : "";
                    uint8_t role = chat::contacts::kNodeRoleUnknown;
                    if (user.role <= meshtastic_Config_DeviceConfig_Role_CLIENT_BASE)
                    {
                        role = static_cast<uint8_t>(user.role);
                    }
                    LORA_LOG("[LORA] RX User from %08lX id='%s' short='%s' long='%s'\n",
                             (unsigned long)node_id, user.id, short_name, long_name);
                    if (long_name[0])
                    {
                        node_long_names_[node_id] = long_name;
                    }
                    if (user.public_key.size == 32)
                    {
                        std::array<uint8_t, 32> key{};
                        memcpy(key.data(), user.public_key.bytes, 32);
                        node_public_keys_[node_id] = key;
                        savePkiNodeKey(node_id, key.data(), key.size());
                        std::string key_fp = toHex(key.data(), key.size(), 8);
                        LORA_LOG("[LORA] PKI key stored for %08lX fp=%s\n",
                                 (unsigned long)node_id, key_fp.c_str());
                        LORA_LOG("[LORA] PKI key updated for %08lX\n",
                                 (unsigned long)node_id);
                    }

                    float snr = last_rx_snr_;
                    float rssi = last_rx_rssi_;
                    uint8_t hops_away = computeHopsAway(header.flags);

                    uint32_t now_secs = time(nullptr);
                    sys::NodeInfoUpdateEvent* event = new sys::NodeInfoUpdateEvent(
                        node_id, short_name, long_name, snr, rssi, now_secs,
                        static_cast<uint8_t>(chat::contacts::NodeProtocolType::Meshtastic), role,
                        hops_away, static_cast<uint8_t>(user.hw_model), channel_index,
                        true, user.macaddr,
                        ((header.flags & chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK) != 0),
                        false,
                        user.public_key.size == 32,
                        false,
                        false, nullptr);
                    bool published = sys::EventBus::publish(event, 0);
                    if (published)
                    {
                        mt_diag_log("[MT][RX_NODEINFO] from=%08lX node=%08lX mode=user published=1\n",
                                    static_cast<unsigned long>(header.from),
                                    static_cast<unsigned long>(node_id));
                        LORA_LOG("[LORA] NodeInfo event published node=%08lX\n",
                                 (unsigned long)node_id);
                    }
                    else
                    {
                        mt_diag_dropf(&header,
                                      "user_event_drop",
                                      "node=%08lX pending=%u",
                                      static_cast<unsigned long>(node_id),
                                      static_cast<unsigned>(sys::EventBus::pendingCount()));
                        LORA_LOG("[LORA] NodeInfo event dropped node=%08lX pending=%u\n",
                                 (unsigned long)node_id,
                                 static_cast<unsigned>(sys::EventBus::pendingCount()));
                    }
                    nodeinfo_decoded = true;
                }
                else
                {
                    LORA_LOG("[LORA] RX NodeInfo decode fail from=%08lX err=%s\n",
                             (unsigned long)header.from,
                             PB_GET_ERROR(&nstream));
                    LORA_LOG("[LORA] RX User decode fail from=%08lX err=%s\n",
                             (unsigned long)header.from,
                             PB_GET_ERROR(&ustream));
                }
            }
        }

        if (!nodeinfo_decoded)
        {
            publish_link_stats(header.from);
        }

        if (decoded.portnum == meshtastic_PortNum_POSITION_APP && decoded.payload.size > 0)
        {
            meshtastic_Position pos = meshtastic_Position_init_default;
            pb_istream_t pstream =
                pb_istream_from_buffer(decoded.payload.bytes, decoded.payload.size);
            if (pb_decode(&pstream, meshtastic_Position_fields, &pos))
            {
                mt_diag_log("[MT][RX_POSITION] from=%08lX id=%08lX payload=%u\n",
                            static_cast<unsigned long>(header.from),
                            static_cast<unsigned long>(header.id),
                            static_cast<unsigned>(decoded.payload.size));
                publishPositionEvent(header.from, pos);
            }
            else
            {
                mt_diag_dropf(&header, "position_decode_fail");
                LORA_LOG("[LORA] RX Position decode fail from=%08lX err=%s\n",
                         (unsigned long)header.from,
                         PB_GET_ERROR(&pstream));
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
                        sendNodeInfoTo(header.from, true,
                                       (header.channel == secondary_channel_hash_)
                                           ? ChannelId::SECONDARY
                                           : ChannelId::PRIMARY);
                        LORA_LOG("[LORA] TX nodeinfo after routing err from=%08lX reason=%s\n",
                                 (unsigned long)header.from,
                                 routingErrorName(routing.error_reason));
                    }
                    clearPendingAck(decoded.request_id);
                    mt_diag_log("[MT][ACK] req=%08lX from=%08lX reason=%u ok=%u\n",
                                static_cast<unsigned long>(decoded.request_id),
                                static_cast<unsigned long>(header.from),
                                static_cast<unsigned>(routing.error_reason),
                                ok ? 1U : 0U);
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

        if (decoded.portnum == meshtastic_PortNum_KEY_VERIFICATION_APP && decoded.payload.size > 0)
        {
            meshtastic_KeyVerification kv = meshtastic_KeyVerification_init_default;
            if (decodeKeyVerificationMessage(plaintext, plaintext_len, &kv))
            {
                mt_diag_log("[MT][KEY_VERIFY] from=%08lX id=%08lX stage=%s hash1=%u hash2=%u\n",
                            static_cast<unsigned long>(header.from),
                            static_cast<unsigned long>(header.id),
                            keyVerificationStage(kv),
                            static_cast<unsigned>(kv.hash1.size),
                            static_cast<unsigned>(kv.hash2.size));
                LORA_LOG("[LORA] RX key verification from=%08lX nonce=%llu hash1=%u hash2=%u stage=%s\n",
                         (unsigned long)header.from,
                         static_cast<unsigned long long>(kv.nonce),
                         static_cast<unsigned>(kv.hash1.size),
                         static_cast<unsigned>(kv.hash2.size),
                         keyVerificationStage(kv));
                bool handled = false;
                if (header.channel != 0)
                {
                    LORA_LOG("[LORA] RX key verification ignored non-PKI channel=0x%02X\n",
                             header.channel);
                }
                else if (kv.hash1.size == 0 && kv.hash2.size == 0)
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
                if (!handled)
                {
                    mt_diag_log("[MT][KEY_VERIFY] from=%08lX id=%08lX handled=0 stage=%s\n",
                                static_cast<unsigned long>(header.from),
                                static_cast<unsigned long>(header.id),
                                keyVerificationStage(kv));
                    LORA_LOG("[LORA] RX key verification ignored stage=%s\n",
                             keyVerificationStage(kv));
                }
            }
            else
            {
                mt_diag_dropf(&header, "key_verify_decode_fail");
                LORA_LOG("[LORA] RX key verification decode fail from=%08lX\n",
                         (unsigned long)header.from);
            }
        }

        bool want_ack_flag = (header.flags & PACKET_FLAGS_WANT_ACK_MASK) != 0;
        bool want_response = decoded.want_response ||
                             (decoded.has_bitfield && ((decoded.bitfield & kBitfieldWantResponseMask) != 0));
        bool is_broadcast = (header.to == 0xFFFFFFFF);
        bool to_us = (header.to == node_id_);
        bool to_us_or_broadcast = to_us || is_broadcast;
        bool is_text_port = (decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
                             decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP);
        bool is_nodeinfo_port = (decoded.portnum == meshtastic_PortNum_NODEINFO_APP);
        bool is_position_port = (decoded.portnum == meshtastic_PortNum_POSITION_APP);
        bool is_traceroute_port = (decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP);
        ChannelId channel_id = decoded_channel_id;
        if (header.channel != 0 && header.from != node_id_)
        {
            node_last_channel_[header.from] = channel_id;
        }
        if (want_ack_flag && to_us)
        {
            if (sendRoutingAck(header.from, header.id, header.channel, psk, psk_len))
            {
                LORA_LOG("[LORA] TX ack to=%08lX req=%08lX port=%u\n",
                         (unsigned long)header.from,
                         (unsigned long)header.id,
                         static_cast<unsigned>(decoded.portnum));
            }
            else
            {
                LORA_LOG("[LORA] TX ack fail to=%08lX req=%08lX port=%u\n",
                         (unsigned long)header.from,
                         (unsigned long)header.id,
                         static_cast<unsigned>(decoded.portnum));
            }
        }

        if (is_traceroute_port)
        {
            handleTraceRoutePacket(header,
                                   &decoded,
                                   &rx_meta,
                                   channel_id,
                                   want_ack_flag,
                                   want_response);
        }

        if (want_response && to_us_or_broadcast)
        {
            if (is_nodeinfo_port)
            {
                uint32_t now_ms = millis();
                bool allow_reply = true;
                auto it = nodeinfo_last_seen_ms_.find(header.from);
                if (it != nodeinfo_last_seen_ms_.end())
                {
                    uint32_t since = now_ms - it->second;
                    if (since < NODEINFO_REPLY_SUPPRESS_MS)
                    {
                        allow_reply = false;
                    }
                }
                nodeinfo_last_seen_ms_[header.from] = now_ms;
                if (allow_reply)
                {
                    if (sendNodeInfoTo(header.from, false, channel_id))
                    {
                        LORA_LOG("[LORA] TX nodeinfo reply to=%08lX\n",
                                 (unsigned long)header.from);
                    }
                    else
                    {
                        LORA_LOG("[LORA] TX nodeinfo reply fail to=%08lX\n",
                                 (unsigned long)header.from);
                    }
                }
                else
                {
                    LORA_LOG("[LORA] TX nodeinfo reply suppressed to=%08lX\n",
                             (unsigned long)header.from);
                }
            }
            else if (is_position_port)
            {
                uint32_t now_ms = millis();
                bool allow_reply = true;
                if (last_position_reply_ms_ != 0)
                {
                    uint32_t since = now_ms - last_position_reply_ms_;
                    if (since < POSITION_REPLY_SUPPRESS_MS)
                    {
                        allow_reply = false;
                    }
                }
                if (allow_reply)
                {
                    if (sendPositionTo(header.from, channel_id))
                    {
                        last_position_reply_ms_ = now_ms;
                        LORA_LOG("[LORA] TX position reply to=%08lX\n",
                                 (unsigned long)header.from);
                    }
                    else
                    {
                        LORA_LOG("[LORA] TX position reply skip/fail to=%08lX\n",
                                 (unsigned long)header.from);
                    }
                }
                else
                {
                    LORA_LOG("[LORA] TX position reply suppressed to=%08lX\n",
                             (unsigned long)header.from);
                }
            }
        }

        queueMqttProxyPublishFromWire(data, size, &decoded, channel_id);

        if (!is_text_port && decoded.payload.size > 0)
        {
            MeshIncomingData incoming;
            incoming.portnum = decoded.portnum;
            incoming.from = header.from;
            incoming.to = header.to;
            incoming.packet_id = header.id;
            incoming.request_id = decoded.request_id;
            incoming.channel = channel_id;
            incoming.channel_hash = header.channel;
            incoming.hop_limit = header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
            incoming.want_response = want_response;
            incoming.payload.assign(decoded.payload.bytes,
                                    decoded.payload.bytes + decoded.payload.size);
            incoming.rx_meta = rx_meta;
            if (app_receive_queue_.size() < MAX_APP_QUEUE)
            {
                app_receive_queue_.push(incoming);
                mt_diag_log("[MT][RX_APP] from=%08lX to=%08lX id=%08lX port=%u(%s) queued=1 depth=%u\n",
                            static_cast<unsigned long>(incoming.from),
                            static_cast<unsigned long>(incoming.to),
                            static_cast<unsigned long>(incoming.packet_id),
                            static_cast<unsigned>(incoming.portnum),
                            portName(incoming.portnum),
                            static_cast<unsigned>(app_receive_queue_.size()));
            }
            else
            {
                mt_diag_dropf(&header,
                              "app_queue_full",
                              "port=%u depth=%u",
                              static_cast<unsigned>(incoming.portnum),
                              static_cast<unsigned>(app_receive_queue_.size()));
            }
        }

        if (is_text_port)
        {
            MeshIncomingText incoming;
            if (decodeTextPayload(decoded, &incoming))
            {
                incoming.from = header.from;
                incoming.to = header.to;
                incoming.msg_id = header.id;
                incoming.channel = decoded_channel_id;
                incoming.hop_limit = header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
                incoming.encrypted = used_pki_transport || (psk != nullptr && psk_len > 0);
                incoming.rx_meta = rx_meta;

                receive_queue_.push(incoming);
                mt_diag_log("[MT][RX_TEXT] from=%08lX to=%08lX id=%08lX ch=%u len=%u\n",
                            static_cast<unsigned long>(incoming.from),
                            static_cast<unsigned long>(incoming.to),
                            static_cast<unsigned long>(incoming.msg_id),
                            static_cast<unsigned>(incoming.channel),
                            static_cast<unsigned>(incoming.text.size()));
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
            else
            {
                mt_diag_dropf(&header,
                              "text_decode_fail",
                              "port=%u payload=%u",
                              static_cast<unsigned>(decoded.portnum),
                              static_cast<unsigned>(decoded.payload.size));
                LORA_LOG("[LORA] RX text decode fail from=%08lX id=%08lX port=%u payload=%u\n",
                         (unsigned long)header.from,
                         (unsigned long)header.id,
                         static_cast<unsigned>(decoded.portnum),
                         static_cast<unsigned>(decoded.payload.size));
            }
        }
    }
}

void MtAdapter::processSendQueue()
{
    uint32_t now = millis();

    maybeBroadcastNodeInfo(now);

    for (auto it = pending_ack_states_.begin(); it != pending_ack_states_.end();)
    {
        PendingAckState& pending = it->second;
        if (now - pending.last_attempt_ms >= ACK_TIMEOUT_MS)
        {
            if (pending.retransmit_count < MAX_ACK_RETRIES)
            {
                retryPendingAck(it->first, pending);
                ++it;
                continue;
            }

            mt_diag_log("[MT][ACK_TIMEOUT] req=%08lX dest=%08lX age_ms=%lu retries=%u\n",
                        static_cast<unsigned long>(it->first),
                        static_cast<unsigned long>(pending.dest),
                        static_cast<unsigned long>(now - pending.last_attempt_ms),
                        static_cast<unsigned>(pending.retransmit_count));
            LORA_LOG("[LORA] RX ack timeout req=%08lX dest=%08lX retries=%u\n",
                     static_cast<unsigned long>(it->first),
                     static_cast<unsigned long>(pending.dest),
                     static_cast<unsigned>(pending.retransmit_count));
            last_send_error_ = meshtastic_Routing_Error_MAX_RETRANSMIT;
            emitRoutingResultToPhone(it->first,
                                     meshtastic_Routing_Error_MAX_RETRANSMIT,
                                     node_id_,
                                     pending.dest,
                                     pending.channel,
                                     pending.channel_hash,
                                     nullptr);
            it = pending_ack_states_.erase(it);
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

        if (min_tx_interval_ms_ > 0 && last_tx_ms_ > 0 &&
            (now - last_tx_ms_) < min_tx_interval_ms_)
        {
            break;
        }

        // Check if ready to send
        if (now - pending.last_attempt < RETRY_DELAY_MS && pending.retry_count > 0)
        {
            break; // Wait before retry
        }

        // Try to send
        if (sendPacket(pending))
        {
            // Success, remove from queue
            last_tx_ms_ = now;
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
                const uint8_t channel_hash =
                    (pending.channel == ChannelId::SECONDARY) ? secondary_channel_hash_ : primary_channel_hash_;
                last_send_error_ = meshtastic_Routing_Error_NO_INTERFACE;
                emitRoutingResultToPhone(pending.msg_id,
                                         meshtastic_Routing_Error_NO_INTERFACE,
                                         node_id_,
                                         pending.dest,
                                         pending.channel,
                                         channel_hash,
                                         nullptr);
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
    if (!config_.tx_enabled)
    {
        return false;
    }

    // Create Data message payload
    uint8_t data_buffer[256];
    size_t data_size = sizeof(data_buffer);

    NodeId from_node = node_id_;
    if (!encodeTextMessage(pending.channel, pending.text, from_node,
                           pending.msg_id, pending.dest, data_buffer, &data_size))
    {
        return false;
    }
    meshtastic_Data decoded = meshtastic_Data_init_default;
    bool decoded_ok = false;
    {
        pb_istream_t stream = pb_istream_from_buffer(data_buffer, data_size);
        if (pb_decode(&stream, meshtastic_Data_fields, &decoded))
        {
            decoded_ok = true;
            LORA_LOG("[LORA] TX data plain port=%u dest=%08lX src=%08lX req=%08lX want_resp=%u bitfield=%u has_bitfield=%u payload=%u\n",
                     (unsigned)decoded.portnum,
                     (unsigned long)decoded.dest,
                     (unsigned long)decoded.source,
                     (unsigned long)decoded.request_id,
                     decoded.want_response ? 1U : 0U,
                     (unsigned)decoded.bitfield,
                     decoded.has_bitfield ? 1U : 0U,
                     (unsigned)decoded.payload.size);
        }
        else
        {
            LORA_LOG("[LORA] TX data plain decode fail err=%s\n", PB_GET_ERROR(&stream));
        }
    }
    std::string data_hex = toHex(data_buffer, data_size, data_size);
    LORA_LOG("[LORA] TX data protobuf hex: %s\n", data_hex.c_str());

    // Build a full Meshtastic-compatible wire packet
    uint8_t wire_buffer[512];
    size_t wire_size = sizeof(wire_buffer);

    ChannelId channel = pending.channel;
    uint8_t channel_hash =
        (channel == ChannelId::SECONDARY) ? secondary_channel_hash_ : primary_channel_hash_;
    uint8_t hop_limit = config_.hop_limit;
    uint32_t dest = (pending.dest != 0) ? pending.dest : kBroadcastNodeId;
    bool track_ack = true;
    bool air_want_ack = shouldSetAirWantAck(dest, track_ack);

    // Upstream Meshtastic requires PKI for direct unicast traffic on
    // non-infrastructure ports and rejects legacy channel-encrypted DMs.
    const uint8_t* payload = data_buffer;
    size_t payload_len = data_size;
    const uint8_t* psk = nullptr;
    size_t psk_len = 0;
    bool use_pki = false;
    if (shouldRequireDirectPki(encrypt_mode_, dest, pending.portnum))
    {
        const bool have_dest_key = node_public_keys_.find(dest) != node_public_keys_.end();
        if (!pki_ready_ || !have_dest_key)
        {
            const char* reason = !pki_ready_ ? "pki_not_ready" : "pki_key_missing";
            mt_diag_log("[MT][TX_BLOCK] id=%08lX dest=%08lX port=%u reason=%s path=PKI\n",
                        static_cast<unsigned long>(pending.msg_id),
                        static_cast<unsigned long>(dest),
                        static_cast<unsigned>(pending.portnum),
                        reason);
            LORA_LOG("[LORA] TX text PKI required but unavailable dest=%08lX\n",
                     (unsigned long)dest);
            return false;
        }
        uint8_t pki_buf[256];
        size_t pki_len = sizeof(pki_buf);
        if (!encryptPkiPayload(dest, pending.msg_id, data_buffer, data_size, pki_buf, &pki_len))
        {
            mt_diag_log("[MT][TX_BLOCK] id=%08lX dest=%08lX port=%u reason=pki_encrypt_fail path=PKI\n",
                        static_cast<unsigned long>(pending.msg_id),
                        static_cast<unsigned long>(dest),
                        static_cast<unsigned>(pending.portnum));
            LORA_LOG("[LORA] TX text PKI encrypt failed dest=%08lX\n",
                     (unsigned long)dest);
            return false;
        }
        payload = pki_buf;
        payload_len = pki_len;
        channel_hash = 0; // PKI channel
        track_ack = true;
        air_want_ack = true;
        use_pki = true;
    }

    if (!use_pki)
    {
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

    const char* channel_name = kSecondaryChannelName;
    if (channel != ChannelId::SECONDARY)
    {
        auto preset =
            static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(config_.modem_preset);
        channel_name = config_.use_preset ? chat::meshtastic::presetDisplayName(preset) : "Custom";
    }
    LORA_LOG("[LORA] TX channel name='%s' hash=0x%02X psk=%u pki=%u dest=%08lX\n",
             channel_name,
             channel_hash,
             (unsigned)psk_len,
             (channel_hash == 0) ? 1U : 0U,
             (unsigned long)dest);
    mt_diag_log("[MT][TX_ROUTE] id=%08lX dest=%08lX port=%u logical_ch=%u wire_ch=%u path=%s payload=%u\n",
                static_cast<unsigned long>(pending.msg_id),
                static_cast<unsigned long>(dest),
                static_cast<unsigned>(pending.portnum),
                static_cast<unsigned>(channel),
                static_cast<unsigned>(channel_hash),
                use_pki ? "PKI" : "CHANNEL",
                static_cast<unsigned>(payload_len));

    if (!buildWirePacket(payload, payload_len, from_node, pending.msg_id,
                         dest, channel_hash, hop_limit, air_want_ack,
                         psk, psk_len, wire_buffer, &wire_size))
    {
        return false;
    }
    LORA_LOG("[LORA] TX wire ch=0x%02X hop=%u air_ack=%d track_ack=%d psk=%u wire=%u dest=%08lX\n",
             channel_hash,
             hop_limit,
             air_want_ack ? 1 : 0,
             track_ack ? 1 : 0,
             (unsigned)psk_len,
             (unsigned)wire_size,
             (unsigned long)dest);
    std::string tx_full_hex = toHex(wire_buffer, wire_size, wire_size);
    LORA_LOG("[LORA] TX full packet hex: %s\n", tx_full_hex.c_str());

    if (!board_.isRadioOnline())
    {
        return false;
    }

    bool ok = transmitWirePacket(wire_buffer, wire_size);
    LORA_LOG("[LORA] TX text id=%08lX ch=%u len=%u ok=%d\n",
             (unsigned long)pending.msg_id,
             static_cast<unsigned>(channel),
             (unsigned)wire_size,
             ok ? 1 : 0);
    if (ok && track_ack)
    {
        trackPendingAck(pending.msg_id, dest, channel, channel_hash, wire_buffer, wire_size);
    }
    if (ok)
    {
        queueMqttProxyPublishFromWire(wire_buffer, wire_size,
                                      decoded_ok ? &decoded : nullptr,
                                      channel);
    }
    return ok;
}

bool MtAdapter::sendNodeInfo()
{
    if (!ready_)
    {
        return false;
    }

    return sendNodeInfoTo(0xFFFFFFFF, false, ChannelId::PRIMARY);
}

bool MtAdapter::sendNodeInfoTo(uint32_t dest, bool want_response, ChannelId channel)
{
    uint8_t data_buffer[256];
    size_t data_size = sizeof(data_buffer);

    char user_id[16];
    snprintf(user_id, sizeof(user_id), "!%08lX", (unsigned long)node_id_);
    const app::AppConfig& cfg = app::configFacade().getConfig();
    if (cfg.aprs.self_enable && cfg.aprs.self_callsign[0] != '\0')
    {
        strncpy(user_id, cfg.aprs.self_callsign, sizeof(user_id) - 1);
        user_id[sizeof(user_id) - 1] = '\0';
    }

    char long_name[32];
    char short_name[5];
    uint16_t suffix = static_cast<uint16_t>(node_id_ & 0x0ffff);
    if (!user_long_name_.empty())
    {
        strncpy(long_name, user_long_name_.c_str(), sizeof(long_name) - 1);
        long_name[sizeof(long_name) - 1] = '\0';
    }
    else
    {
        snprintf(long_name, sizeof(long_name), "lilygo-%04X", suffix);
    }
    if (!user_short_name_.empty())
    {
        strncpy(short_name, user_short_name_.c_str(), sizeof(short_name) - 1);
        short_name[sizeof(short_name) - 1] = '\0';
    }
    else
    {
        snprintf(short_name, sizeof(short_name), "%04X", suffix);
    }

    meshtastic_HardwareModel hw_model = meshtastic_HardwareModel_UNSET;
#if defined(ARDUINO_T_DECK)
    hw_model = meshtastic_HardwareModel_T_DECK;
#elif defined(ARDUINO_LILYGO_TWATCH_S3)
    hw_model = meshtastic_HardwareModel_T_WATCH_S3;
#elif defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    hw_model = meshtastic_HardwareModel_T_LORA_PAGER;
#endif

    if (!encodeNodeInfoMessage(
            user_id,
            long_name,
            short_name,
            hw_model,
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

    uint8_t channel_hash =
        (channel == ChannelId::SECONDARY) ? secondary_channel_hash_ : primary_channel_hash_;
    uint8_t hop_limit = config_.hop_limit;
    bool want_ack = want_response && (dest != 0xFFFFFFFF);
    const uint8_t* psk =
        (channel == ChannelId::SECONDARY) ? secondary_psk_ : primary_psk_;
    size_t psk_len =
        (channel == ChannelId::SECONDARY) ? secondary_psk_len_ : primary_psk_len_;
    const uint32_t msg_id = next_packet_id_++;

    if (!buildWirePacket(data_buffer, data_size, node_id_, msg_id,
                         dest, channel_hash, hop_limit, want_ack,
                         psk, psk_len, wire_buffer, &wire_size))
    {
        return false;
    }
    LORA_LOG("[LORA] TX nodeinfo wire ch=0x%02X idx=%u hop=%u wire=%u\n",
             channel_hash,
             (unsigned)(channel == ChannelId::SECONDARY ? 1 : 0),
             hop_limit,
             (unsigned)wire_size);
    std::string nodeinfo_full_hex = toHex(wire_buffer, wire_size, wire_size);
    LORA_LOG("[LORA] TX nodeinfo full packet hex: %s\n", nodeinfo_full_hex.c_str());

    if (!board_.isRadioOnline())
    {
        return false;
    }

    bool ok = transmitWirePacket(wire_buffer, wire_size);
    LORA_LOG("[LORA] TX nodeinfo id=%08lX len=%u ok=%d\n",
             (unsigned long)msg_id,
             (unsigned)wire_size,
             ok ? 1 : 0);
    return ok;
}

bool MtAdapter::sendPositionTo(uint32_t dest, ChannelId channel)
{
    uint8_t payload[128];
    size_t payload_len = sizeof(payload);
    if (!build_self_position_payload(payload, &payload_len))
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

bool MtAdapter::sendTraceRouteResponse(uint32_t dest,
                                       uint32_t request_id,
                                       const meshtastic_RouteDiscovery& route,
                                       ChannelId channel,
                                       bool want_ack)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.to = dest;
    packet.channel = (channel == ChannelId::SECONDARY) ? 1 : 0;
    packet.hop_limit = config_.hop_limit;
    packet.want_ack = want_ack;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded = meshtastic_Data_init_default;
    packet.decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP;
    packet.decoded.want_response = false;
    packet.decoded.request_id = request_id;
    packet.decoded.has_bitfield = true;
    packet.decoded.bitfield = 0;

    pb_ostream_t ostream =
        pb_ostream_from_buffer(packet.decoded.payload.bytes, sizeof(packet.decoded.payload.bytes));
    if (!pb_encode(&ostream, meshtastic_RouteDiscovery_fields, &route))
    {
        LORA_LOG("[LORA] traceroute response encode fail req=%08lX err=%s\n",
                 (unsigned long)request_id,
                 PB_GET_ERROR(&ostream));
        return false;
    }

    packet.decoded.payload.size = static_cast<pb_size_t>(ostream.bytes_written);
    return sendMeshPacket(packet);
}

bool MtAdapter::handleTraceRoutePacket(const PacketHeaderWire& header,
                                       meshtastic_Data* decoded,
                                       const chat::RxMeta* rx_meta,
                                       ChannelId channel,
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
        LORA_LOG("[LORA] traceroute decode fail from=%08lX err=%s\n",
                 (unsigned long)header.from,
                 PB_GET_ERROR(&istream));
        return false;
    }

    const bool is_response = decoded->request_id != 0;
    const bool is_broadcast = header.to == kBroadcastNodeId;
    const bool to_us = header.to == node_id_;

    insertTraceRouteUnknownHops(header.flags, &route, !is_response);
    appendTraceRouteNodeAndSnr(&route, node_id_, rx_meta, !is_response, to_us);

    pb_ostream_t ostream = pb_ostream_from_buffer(decoded->payload.bytes, sizeof(decoded->payload.bytes));
    if (!pb_encode(&ostream, meshtastic_RouteDiscovery_fields, &route))
    {
        LORA_LOG("[LORA] traceroute re-encode fail from=%08lX err=%s\n",
                 (unsigned long)header.from,
                 PB_GET_ERROR(&ostream));
        return false;
    }
    decoded->payload.size = static_cast<pb_size_t>(ostream.bytes_written);

    if (!is_response && want_response && (to_us || is_broadcast))
    {
        const uint8_t hop_limit = header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
        const uint8_t hop_start =
            (header.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
        const bool ignore_broadcast_request = is_broadcast && hop_limit < hop_start;
        if (ignore_broadcast_request)
        {
            LORA_LOG("[LORA] traceroute reply ignored broadcast req=%08lX hop=%u/%u\n",
                     (unsigned long)header.id,
                     static_cast<unsigned>(hop_limit),
                     static_cast<unsigned>(hop_start));
        }
        else if (sendTraceRouteResponse(header.from, header.id, route, channel, want_ack_flag))
        {
            LORA_LOG("[LORA] TX traceroute reply to=%08lX req=%08lX\n",
                     (unsigned long)header.from,
                     (unsigned long)header.id);
        }
        else
        {
            LORA_LOG("[LORA] TX traceroute reply fail to=%08lX req=%08lX\n",
                     (unsigned long)header.from,
                     (unsigned long)header.id);
        }
    }

    return true;
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
    if (!board_.isRadioOnline())
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
    bool using_preset = config_.use_preset;
    if (using_preset)
    {
        modemPresetToParams(preset, region->wide_lora, bw_khz, sf, cr_denom);
    }
    else
    {
        bw_khz = config_.bandwidth_khz;
        sf = config_.spread_factor;
        cr_denom = config_.coding_rate;

        if (bw_khz == 31.0f) bw_khz = 31.25f;
        if (bw_khz == 62.0f) bw_khz = 62.5f;
        if (bw_khz == 200.0f) bw_khz = 203.125f;
        if (bw_khz == 400.0f) bw_khz = 406.25f;
        if (bw_khz == 800.0f) bw_khz = 812.5f;
        if (bw_khz == 1600.0f) bw_khz = 1625.0f;

        if (bw_khz < 7.0f) bw_khz = 7.8f;
        if (!region->wide_lora && bw_khz > 500.0f) bw_khz = 500.0f;
        if (region->wide_lora && bw_khz > 1625.0f) bw_khz = 1625.0f;
        if (sf < 5) sf = 5;
        if (sf > 12) sf = 12;
        if (cr_denom < 5) cr_denom = 5;
        if (cr_denom > 8) cr_denom = 8;
    }

    const float region_span_khz = (region->freq_end_mhz - region->freq_start_mhz) * 1000.0f;
    if (region_span_khz < bw_khz)
    {
        using_preset = true;
        preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
        modemPresetToParams(preset, region->wide_lora, bw_khz, sf, cr_denom);
        config_.use_preset = true;
        config_.modem_preset = static_cast<uint8_t>(preset);
    }

    const char* channel_name =
        using_preset ? chat::meshtastic::presetDisplayName(preset) : "Custom";

    float span_mhz = region->freq_end_mhz - region->freq_start_mhz;
    float spacing_mhz = region->spacing_khz / 1000.0f;
    float bw_mhz = bw_khz / 1000.0f;
    uint32_t num_channels = static_cast<uint32_t>(floor(span_mhz / (spacing_mhz + bw_mhz)));
    if (num_channels < 1)
    {
        num_channels = 1;
    }

    uint32_t channel_slot = 0;
    if (config_.channel_num > 0)
    {
        channel_slot = static_cast<uint32_t>((config_.channel_num - 1) % num_channels);
    }
    else
    {
        channel_slot = djb2HashText(channel_name) % num_channels;
    }

    float freq_mhz = region->freq_start_mhz + (bw_khz / 2000.0f) + (channel_slot * bw_mhz);
    if (config_.override_frequency_mhz > 0.0f)
    {
        freq_mhz = config_.override_frequency_mhz;
    }
    freq_mhz += config_.frequency_offset_mhz;

    if (config_.override_frequency_mhz <= 0.0f)
    {
        float min_center = region->freq_start_mhz + (bw_khz / 2000.0f);
        float max_center = region->freq_end_mhz - (bw_khz / 2000.0f);
        if (min_center > max_center)
        {
            min_center = region->freq_start_mhz;
            max_center = region->freq_end_mhz;
        }
        if (freq_mhz < min_center) freq_mhz = min_center;
        if (freq_mhz > max_center) freq_mhz = max_center;
    }

    int8_t tx_power = config_.tx_power;
    if (region->power_limit_dbm > 0)
    {
        if (tx_power == 0)
        {
            tx_power = static_cast<int8_t>(region->power_limit_dbm);
        }
        if (tx_power > static_cast<int8_t>(region->power_limit_dbm))
        {
            tx_power = static_cast<int8_t>(region->power_limit_dbm);
        }
    }
    if (tx_power == 0)
    {
        tx_power = 17;
    }
    if (tx_power < -9)
    {
        tx_power = -9;
    }
    config_.tx_power = tx_power;

    radio_freq_hz_ = static_cast<uint32_t>(std::lround(freq_mhz * 1000000.0f));
    radio_bw_hz_ = static_cast<uint32_t>(std::lround(bw_khz * 1000.0f));
    radio_sf_ = sf;
    radio_cr_ = cr_denom;

#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    board_.configureLoraRadio(freq_mhz, bw_khz, sf, cr_denom, tx_power,
                              kLoraPreambleLen, kLoraSyncWord, 2);
#endif

    ready_ = true;
    // Suppress auto NodeInfo broadcast at boot; wait for interval to elapse.
    last_nodeinfo_ms_ = millis();
    LORA_LOG("[LORA] adapter ready, node_id=%08lX\n", (unsigned long)node_id_);
    LORA_LOG("[LORA] radio config region=%u preset=%u use_preset=%u freq=%.3fMHz sf=%u bw=%.1f cr=4/%u tx=%d ch=%lu sync=0x%02X preamble=%u tx_en=%u\n",
             static_cast<unsigned>(region_code),
             static_cast<unsigned>(preset),
             using_preset ? 1U : 0U,
             freq_mhz,
             sf,
             bw_khz,
             cr_denom,
             static_cast<int>(tx_power),
             static_cast<unsigned long>(channel_slot),
             kLoraSyncWord,
             kLoraPreambleLen,
             config_.tx_enabled ? 1U : 0U);
    startRadioReceive();
}

void MtAdapter::initNodeIdentity()
{
    const uint64_t raw = ESP.getEfuseMac();

    // Important: getEfuseMac() writes the 6-byte MAC into the low bytes of a uint64_t.
    // ESP32 is little-endian, so the raw integer value is NOT the same as the MAC string.
    // Correct approach: interpret as bytes.
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&raw);

    // p[0..5] are exactly the 6 bytes that esp_efuse_mac_get_default writes (same order as API).
    // To avoid any ambiguity, copy them explicitly into our MAC buffer.
    mac_addr_[0] = p[0];
    mac_addr_[1] = p[1];
    mac_addr_[2] = p[2];
    mac_addr_[3] = p[3];
    mac_addr_[4] = p[4];
    mac_addr_[5] = p[5];

    LORA_LOG("[LORA] ESP.getEfuseMac raw=0x%016llX\n",
             static_cast<unsigned long long>(raw));
    LORA_LOG("[LORA] eFuse MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
             mac_addr_[0], mac_addr_[1], mac_addr_[2],
             mac_addr_[3], mac_addr_[4], mac_addr_[5]);

    // Derive node_id from the last 4 bytes of the MAC (as in the original logic).
    node_id_ = (static_cast<uint32_t>(mac_addr_[2]) << 24) |
               (static_cast<uint32_t>(mac_addr_[3]) << 16) |
               (static_cast<uint32_t>(mac_addr_[4]) << 8) |
               (static_cast<uint32_t>(mac_addr_[5]) << 0);

    LORA_LOG("[LORA] node_id=0x%08X\n", static_cast<unsigned>(node_id_));
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

    auto preset =
        static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(config_.modem_preset);
    const char* primary_name = config_.use_preset ? chat::meshtastic::presetDisplayName(preset) : "Custom";
    if (!primary_name || primary_name[0] == '\0')
    {
        primary_name = "Custom";
    }
    primary_channel_hash_ = computeChannelHash(primary_name, primary_psk_, primary_psk_len_);
    secondary_channel_hash_ =
        computeChannelHash(kSecondaryChannelName,
                           (secondary_psk_len_ > 0) ? secondary_psk_ : nullptr,
                           secondary_psk_len_);
    std::string primary_psk_hex = toHex(primary_psk_, primary_psk_len_, primary_psk_len_);
    std::string secondary_psk_hex = toHex(secondary_psk_, secondary_psk_len_, secondary_psk_len_);
    if (primary_psk_hex.empty()) primary_psk_hex = "-";
    if (secondary_psk_hex.empty()) secondary_psk_hex = "-";
    LORA_LOG("[LORA] channel primary='%s' hash=0x%02X psk=%u hex=%s\n",
             primary_name,
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
    if (!board_.isRadioOnline())
    {
        app::AppTasks::requestRadioReceiveRestart();
        return;
    }
#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    app::AppTasks::requestRadioReceiveRestart();
    int state = board_.startRadioReceive();
    if (state == RADIOLIB_ERR_NONE)
    {
        app::AppTasks::setRadioReceiveActive(true);
    }
    else
    {
        app::AppTasks::requestRadioReceiveRestart();
        LORA_LOG("[LORA] RX start fail state=%d\n", state);
    }
#endif
}

bool MtAdapter::transmitWirePacket(const uint8_t* wire_data, size_t wire_size)
{
    if (!board_.isRadioOnline())
    {
        return false;
    }

    app::AppTasks::requestRadioReceiveRestart();

#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    const int state = board_.transmitRadio(wire_data, wire_size);
#else
    const int state = RADIOLIB_ERR_UNSUPPORTED;
#endif
    const bool ok = (state == RADIOLIB_ERR_NONE);
    if (ok)
    {
        startRadioReceive();
    }
    else
    {
        app::AppTasks::requestRadioReceiveRestart();
    }
    return ok;
}

void MtAdapter::trackPendingAck(uint32_t msg_id, uint32_t dest, ChannelId channel, uint8_t channel_hash,
                                const uint8_t* wire_data, size_t wire_size)
{
    PendingAckState state;
    state.dest = dest;
    state.channel = channel;
    state.channel_hash = channel_hash;
    state.last_attempt_ms = millis();
    state.retransmit_count = 0;
    state.wire_packet.assign(wire_data, wire_data + wire_size);
    pending_ack_states_[msg_id] = std::move(state);
}

void MtAdapter::clearPendingAck(uint32_t msg_id)
{
    pending_ack_states_.erase(msg_id);
}

void MtAdapter::retryPendingAck(uint32_t msg_id, PendingAckState& pending)
{
    pending.last_attempt_ms = millis();
    ++pending.retransmit_count;
    mt_diag_log("[MT][RETX] req=%08lX dest=%08lX try=%u len=%u\n",
                static_cast<unsigned long>(msg_id),
                static_cast<unsigned long>(pending.dest),
                static_cast<unsigned>(pending.retransmit_count),
                static_cast<unsigned>(pending.wire_packet.size()));
    LORA_LOG("[LORA] TX retry req=%08lX dest=%08lX try=%u len=%u\n",
             static_cast<unsigned long>(msg_id),
             static_cast<unsigned long>(pending.dest),
             static_cast<unsigned>(pending.retransmit_count),
             static_cast<unsigned>(pending.wire_packet.size()));
    if (transmitWirePacket(pending.wire_packet.data(), pending.wire_packet.size()))
    {
        last_tx_ms_ = pending.last_attempt_ms;
        return;
    }
    LORA_LOG("[LORA] TX retry immediate fail req=%08lX try=%u\n",
             static_cast<unsigned long>(msg_id),
             static_cast<unsigned>(pending.retransmit_count));
}

bool MtAdapter::initPkiKeys()
{
    std::array<uint8_t, 32> pub_before{};
    std::vector<uint8_t> pub_blob;
    std::vector<uint8_t> priv_blob;
    const bool have_pub_blob = chat::infra::loadRawBlobFromPreferences("chat", "pki_pub", pub_blob);
    const bool have_priv_blob = chat::infra::loadRawBlobFromPreferences("chat", "pki_priv", priv_blob);
    const size_t pub_len = have_pub_blob ? pub_blob.size() : 0;
    const size_t priv_len = have_priv_blob ? priv_blob.size() : 0;
    if (pub_len == pub_before.size())
    {
        memcpy(pub_before.data(), pub_blob.data(), pub_before.size());
    }
    if (priv_len == pki_private_key_.size())
    {
        memcpy(pki_private_key_.data(), priv_blob.data(), pki_private_key_.size());
    }

    if (pub_len > 0)
    {
        std::string stored_fp = toHex(pub_before.data(), pub_len, 8);
        LORA_LOG("[LORA] PKI stored pub len=%u fp=%s\n",
                 static_cast<unsigned>(pub_len), stored_fp.c_str());
    }
    else
    {
        LORA_LOG("[LORA] PKI stored pub len=0\n");
    }
    LORA_LOG("[LORA] PKI stored priv len=%u\n", static_cast<unsigned>(priv_len));
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
            std::string gen_fp = toHex(pki_public_key_.data(), pki_public_key_.size(), 8);
            LORA_LOG("[LORA] PKI keys generated pub fp=%s\n", gen_fp.c_str());
            const bool pub_ok = chat::infra::saveRawBlobToPreferences("chat",
                                                                      "pki_pub",
                                                                      pki_public_key_.data(),
                                                                      pki_public_key_.size());
            const bool priv_ok = chat::infra::saveRawBlobToPreferences("chat",
                                                                       "pki_priv",
                                                                       pki_private_key_.data(),
                                                                       pki_private_key_.size());
            if (!pub_ok || !priv_ok)
            {
                LORA_LOG("[LORA] PKI key persist failed pub_ok=%u priv_ok=%u\n",
                         pub_ok ? 1U : 0U,
                         priv_ok ? 1U : 0U);
            }
        }
    }
    else
    {
        memcpy(pki_public_key_.data(), pub_before.data(), pki_public_key_.size());
        std::string loaded_fp = toHex(pki_public_key_.data(), pki_public_key_.size(), 8);
        LORA_LOG("[LORA] PKI keys loaded pub fp=%s\n", loaded_fp.c_str());
    }

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
    struct PkiKeyEntry
    {
        uint32_t node_id;
        uint8_t key[32];
    };
    struct PkiKeyEntryV2
    {
        uint32_t node_id;
        uint32_t last_seen;
        uint8_t key[32];
    } __attribute__((packed));

    std::vector<uint8_t> blob;
    chat::infra::PreferencesBlobMetadata meta;
    if (!chat::infra::loadRawBlobFromPreferencesWithMetadata(kPkiPrefsNs,
                                                             kPkiPrefsKey,
                                                             kPkiPrefsKeyVer,
                                                             nullptr,
                                                             blob,
                                                             &meta))
    {
        LORA_LOG("[LORA] PKI prefs open failed ns=%s\n", kPkiPrefsNs);
        return;
    }
    if (meta.len < sizeof(PkiKeyEntry))
    {
        return;
    }

    if (meta.has_version && meta.version == kPkiPrefsVersion &&
        (meta.len % sizeof(PkiKeyEntryV2) == 0))
    {
        size_t count = meta.len / sizeof(PkiKeyEntryV2);
        if (count > kMaxPkiNodes)
        {
            count = kMaxPkiNodes;
        }
        const auto* entries_v2 = reinterpret_cast<const PkiKeyEntryV2*>(blob.data());
        for (size_t i = 0; i < count; ++i)
        {
            if (entries_v2[i].node_id == 0)
            {
                continue;
            }
            std::array<uint8_t, 32> key{};
            memcpy(key.data(), entries_v2[i].key, sizeof(entries_v2[i].key));
            node_public_keys_[entries_v2[i].node_id] = key;
            node_key_last_seen_[entries_v2[i].node_id] = entries_v2[i].last_seen;
            LORA_LOG("[LORA] PKI key loaded for %08lX\n",
                     static_cast<unsigned long>(entries_v2[i].node_id));
        }
        LORA_LOG("[LORA] PKI keys loaded=%u ns=%s\n",
                 static_cast<unsigned>(count),
                 kPkiPrefsNs);
        return;
    }

    if ((meta.len % sizeof(PkiKeyEntry)) != 0)
    {
        return;
    }
    size_t count = meta.len / sizeof(PkiKeyEntry);
    if (count > kMaxPkiNodes)
    {
        count = kMaxPkiNodes;
    }
    const auto* entries = reinterpret_cast<const PkiKeyEntry*>(blob.data());
    for (size_t i = 0; i < count; ++i)
    {
        if (entries[i].node_id == 0)
        {
            continue;
        }
        std::array<uint8_t, 32> key{};
        memcpy(key.data(), entries[i].key, sizeof(entries[i].key));
        node_public_keys_[entries[i].node_id] = key;
        node_key_last_seen_[entries[i].node_id] = 0;
        LORA_LOG("[LORA] PKI key loaded for %08lX\n",
                 static_cast<unsigned long>(entries[i].node_id));
    }
    LORA_LOG("[LORA] PKI keys loaded=%u ns=%s\n",
             static_cast<unsigned>(count),
             kPkiPrefsNs);
    if (!node_public_keys_.empty())
    {
        savePkiKeysToPrefs();
    }
}

void MtAdapter::savePkiNodeKey(uint32_t node_id, const uint8_t* key, size_t key_len)
{
    if (node_id == 0 || !key || key_len != 32)
    {
        return;
    }
    std::array<uint8_t, 32> key_copy{};
    memcpy(key_copy.data(), key, 32);
    node_public_keys_[node_id] = key_copy;
    touchPkiNodeKey(node_id);
    savePkiKeysToPrefs();
}

void MtAdapter::savePkiKeysToPrefs()
{
    struct PkiKeyEntryV2
    {
        uint32_t node_id;
        uint32_t last_seen;
        uint8_t key[32];
    } __attribute__((packed));

    std::vector<PkiKeyEntryV2> entries;
    entries.reserve(node_public_keys_.size());
    for (const auto& kv : node_public_keys_)
    {
        PkiKeyEntryV2 entry{};
        entry.node_id = kv.first;
        auto seen_it = node_key_last_seen_.find(kv.first);
        entry.last_seen = (seen_it != node_key_last_seen_.end()) ? seen_it->second : 0;
        memcpy(entry.key, kv.second.data(), sizeof(entry.key));
        entries.push_back(entry);
    }
    if (entries.size() > kMaxPkiNodes)
    {
        std::sort(entries.begin(), entries.end(),
                  [](const PkiKeyEntryV2& a, const PkiKeyEntryV2& b)
                  {
                      return a.last_seen < b.last_seen;
                  });
        size_t drop = entries.size() - kMaxPkiNodes;
        for (size_t i = 0; i < drop; ++i)
        {
            node_public_keys_.erase(entries[i].node_id);
            node_key_last_seen_.erase(entries[i].node_id);
        }
        entries.erase(entries.begin(), entries.begin() + static_cast<long>(drop));
    }

    chat::infra::PreferencesBlobMetadata meta;
    if (!entries.empty())
    {
        meta.len = entries.size() * sizeof(PkiKeyEntryV2);
        meta.has_version = true;
        meta.version = kPkiPrefsVersion;
    }

    const bool ok = chat::infra::saveRawBlobToPreferencesWithMetadata(
        kPkiPrefsNs,
        kPkiPrefsKey,
        kPkiPrefsKeyVer,
        nullptr,
        entries.empty() ? nullptr : reinterpret_cast<const uint8_t*>(entries.data()),
        entries.size() * sizeof(PkiKeyEntryV2),
        &meta,
        false);
    if (!ok)
    {
        LORA_LOG("[LORA] PKI key save failed open ns=%s\n", kPkiPrefsNs);
        return;
    }
    if (!entries.empty())
    {
        LORA_LOG("[LORA] PKI key saved (total=%u ns=%s)\n",
                 static_cast<unsigned>(entries.size()),
                 kPkiPrefsNs);
    }
}

void MtAdapter::touchPkiNodeKey(uint32_t node_id)
{
    uint32_t now_secs = static_cast<uint32_t>(time(nullptr));
    node_key_last_seen_[node_id] = now_secs;
}

bool MtAdapter::decryptPkiPayload(uint32_t from, uint32_t packet_id,
                                  const uint8_t* cipher, size_t cipher_len,
                                  uint8_t* out_plain, size_t* out_plain_len)
{
    if (!cipher || cipher_len <= 12 || !out_plain || !out_plain_len)
    {
        mt_diag_log("[MT][PKI] decrypt_skip from=%08lX id=%08lX reason=bad_args len=%u\n",
                    static_cast<unsigned long>(from),
                    static_cast<unsigned long>(packet_id),
                    static_cast<unsigned>(cipher_len));
        return false;
    }
    if (!pki_ready_)
    {
        mt_diag_log("[MT][PKI] decrypt_skip from=%08lX id=%08lX reason=not_ready len=%u\n",
                    static_cast<unsigned long>(from),
                    static_cast<unsigned long>(packet_id),
                    static_cast<unsigned>(cipher_len));
        return false;
    }
    auto it = node_public_keys_.find(from);
    if (it == node_public_keys_.end())
    {
        mt_diag_log("[MT][PKI] decrypt_skip from=%08lX id=%08lX reason=key_missing\n",
                    static_cast<unsigned long>(from),
                    static_cast<unsigned long>(packet_id));
        LORA_LOG("[LORA] PKI key missing for %08lX\n", (unsigned long)from);
        sendNodeInfoTo(from, true, ChannelId::PRIMARY);
        sendRoutingError(from, packet_id, primary_channel_hash_,
                         primary_psk_, primary_psk_len_,
                         meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY);
        LORA_LOG("[LORA] PKI unknown for %08lX, sent nodeinfo\n",
                 (unsigned long)from);
        return false;
    }
    touchPkiNodeKey(from);

    uint8_t shared[32];
    uint8_t local_priv[32];
    memcpy(shared, it->second.data(), sizeof(shared));
    memcpy(local_priv, pki_private_key_.data(), sizeof(local_priv));
    if (!Curve25519::dh2(shared, local_priv))
    {
        mt_diag_log("[MT][PKI] decrypt_fail from=%08lX id=%08lX reason=dh2\n",
                    static_cast<unsigned long>(from),
                    static_cast<unsigned long>(packet_id));
        return false;
    }

    hashSharedKey(shared, sizeof(shared));

    const uint8_t* auth = cipher + (cipher_len - 12);
    uint32_t extra_nonce = 0;
    memcpy(&extra_nonce, auth + 8, sizeof(extra_nonce));
    std::string key_fp = toHex(it->second.data(), it->second.size(), 8);

    uint8_t nonce[16];
    uint64_t packet_id64 = static_cast<uint64_t>(packet_id);
    initPkiNonce(from, packet_id64, extra_nonce, nonce);

    size_t plain_len = cipher_len - 12;
    if (*out_plain_len < plain_len)
    {
        *out_plain_len = plain_len;
        mt_diag_log("[MT][PKI] decrypt_skip from=%08lX id=%08lX reason=buffer_small need=%u\n",
                    static_cast<unsigned long>(from),
                    static_cast<unsigned long>(packet_id),
                    static_cast<unsigned>(plain_len));
        return false;
    }

    if (!decryptPkiAesCcm(shared, sizeof(shared), nonce, 8,
                          cipher, plain_len, nullptr, 0, auth, out_plain))
    {
        mt_diag_log("[MT][PKI] decrypt_fail from=%08lX id=%08lX reason=ccm_auth key=%s extra_nonce=%08lX len=%u\n",
                    static_cast<unsigned long>(from),
                    static_cast<unsigned long>(packet_id),
                    key_fp.c_str(),
                    static_cast<unsigned long>(extra_nonce),
                    static_cast<unsigned>(cipher_len));
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
    std::string key_fp = toHex(it->second.data(), it->second.size(), 8);
    LORA_LOG("[LORA] PKI encrypt dest=%08lX key_fp=%s\n",
             (unsigned long)dest, key_fp.c_str());
    touchPkiNodeKey(dest);

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
    LORA_LOG("[LORA] PKI encrypt packet_id=%08lX extra_nonce=%08lX plain_len=%u\n",
             (unsigned long)packet_id,
             (unsigned long)extra_nonce,
             (unsigned)plain_len);
    uint8_t nonce[16];
    uint64_t packet_id64 = static_cast<uint64_t>(packet_id);
    initPkiNonce(node_id_, packet_id64, extra_nonce, nonce);

    const size_t m = 8;
    size_t needed = plain_len + m + sizeof(extra_nonce);
    if (*out_cipher_len < needed)
    {
        *out_cipher_len = needed;
        return false;
    }

    uint8_t auth[16];
    if (!encryptPkiAesCcm(shared, sizeof(shared), nonce, m,
                          nullptr, 0,
                          plain, plain_len,
                          out_cipher, auth))
    {
        return false;
    }
    memcpy(out_cipher + plain_len, auth, m);
    memcpy(out_cipher + plain_len + m, &extra_nonce, sizeof(extra_nonce));
    *out_cipher_len = needed;
    return true;
}

bool MtAdapter::startKeyVerification(NodeId node_id)
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

bool MtAdapter::submitKeyVerificationNumber(NodeId node_id, uint64_t nonce, uint32_t number)
{
    return processKeyVerificationNumber(node_id, nonce, number);
}

void MtAdapter::updateKeyVerificationState()
{
    if (kv_state_ == KeyVerificationState::Idle)
    {
        return;
    }

    uint32_t now_ms = millis();
    if (kv_nonce_ms_ != 0 && (now_ms - kv_nonce_ms_) > 60000)
    {
        resetKeyVerificationState();
        return;
    }
    kv_nonce_ms_ = now_ms;
}

void MtAdapter::resetKeyVerificationState()
{
    kv_state_ = KeyVerificationState::Idle;
    kv_nonce_ = 0;
    kv_nonce_ms_ = 0;
    kv_security_number_ = 0;
    kv_remote_node_ = 0;
    kv_hash1_.fill(0);
    kv_hash2_.fill(0);
}

void MtAdapter::buildVerificationCode(char* out, size_t out_len) const
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (out_len < 10)
    {
        out[0] = '\0';
        return;
    }
    for (int i = 0; i < 4; ++i)
    {
        out[i] = static_cast<char>((kv_hash1_[i] >> 2) + 48);
    }
    out[4] = ' ';
    for (int i = 0; i < 4; ++i)
    {
        out[i + 5] = static_cast<char>((kv_hash1_[i + 4] >> 2) + 48);
    }
    out[9] = '\0';
}

bool MtAdapter::handleKeyVerificationInit(const PacketHeaderWire& header,
                                          const meshtastic_KeyVerification& kv)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::Idle)
    {
        return false;
    }
    if (header.to != node_id_ || header.to == 0xFFFFFFFF)
    {
        return false;
    }
    if (!pki_ready_)
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
    memcpy(reply.hash2.bytes, kv_hash2_.data(), kv_hash2_.size());
    reply.hash1.size = 0;

    if (!sendKeyVerificationPacket(kv_remote_node_, reply, false))
    {
        resetKeyVerificationState();
        return false;
    }

    kv_state_ = KeyVerificationState::ReceiverAwaitingHash1;
    sys::EventBus::publish(
        new sys::KeyVerificationNumberInformEvent(kv_remote_node_, kv_nonce_, kv_security_number_), 0);
    return true;
}

bool MtAdapter::handleKeyVerificationReply(const PacketHeaderWire& header,
                                           const meshtastic_KeyVerification& kv)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::SenderInitiated)
    {
        return false;
    }
    if (header.to != node_id_ || header.to == 0xFFFFFFFF)
    {
        return false;
    }
    if (kv.nonce != kv_nonce_ || header.from != kv_remote_node_)
    {
        return false;
    }
    if (kv.hash1.size != 0 || kv.hash2.size != 32)
    {
        return false;
    }

    memcpy(kv_hash2_.data(), kv.hash2.bytes, 32);
    kv_state_ = KeyVerificationState::SenderAwaitingNumber;
    kv_nonce_ms_ = millis();

    sys::EventBus::publish(
        new sys::KeyVerificationNumberRequestEvent(kv_remote_node_, kv_nonce_), 0);
    return true;
}

bool MtAdapter::processKeyVerificationNumber(uint32_t remote_node, uint64_t nonce, uint32_t number)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::SenderAwaitingNumber)
    {
        return false;
    }
    if (kv_remote_node_ != remote_node || kv_nonce_ != nonce)
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

    if (memcmp(scratch_hash.data(), kv_hash2_.data(), kv_hash2_.size()) != 0)
    {
        return false;
    }

    meshtastic_KeyVerification response = meshtastic_KeyVerification_init_zero;
    response.nonce = kv_nonce_;
    response.hash1.size = static_cast<pb_size_t>(kv_hash1_.size());
    memcpy(response.hash1.bytes, kv_hash1_.data(), kv_hash1_.size());
    response.hash2.size = 0;

    if (!sendKeyVerificationPacket(kv_remote_node_, response, true))
    {
        return false;
    }

    kv_state_ = KeyVerificationState::SenderAwaitingUser;
    kv_nonce_ms_ = millis();

    char code[12];
    buildVerificationCode(code, sizeof(code));
    sys::EventBus::publish(
        new sys::KeyVerificationFinalEvent(kv_remote_node_, kv_nonce_, true, code), 0);
    return true;
}

bool MtAdapter::handleKeyVerificationFinal(const PacketHeaderWire& header,
                                           const meshtastic_KeyVerification& kv)
{
    updateKeyVerificationState();
    if (kv_state_ != KeyVerificationState::ReceiverAwaitingHash1)
    {
        return false;
    }
    if (header.to != node_id_ || header.to == 0xFFFFFFFF)
    {
        return false;
    }
    if (kv.nonce != kv_nonce_ || header.from != kv_remote_node_)
    {
        return false;
    }
    if (kv.hash1.size != 32 || kv.hash2.size != 0)
    {
        return false;
    }
    if (memcmp(kv.hash1.bytes, kv_hash1_.data(), kv_hash1_.size()) != 0)
    {
        return false;
    }

    kv_state_ = KeyVerificationState::ReceiverAwaitingUser;
    kv_nonce_ms_ = millis();

    char code[12];
    buildVerificationCode(code, sizeof(code));
    sys::EventBus::publish(
        new sys::KeyVerificationFinalEvent(kv_remote_node_, kv_nonce_, false, code), 0);
    return true;
}

bool MtAdapter::sendKeyVerificationPacket(uint32_t dest, const meshtastic_KeyVerification& kv,
                                          bool want_response)
{
    if (!pki_ready_ || node_public_keys_.find(dest) == node_public_keys_.end())
    {
        return false;
    }

    uint8_t kv_buf[96];
    pb_ostream_t kv_stream = pb_ostream_from_buffer(kv_buf, sizeof(kv_buf));
    if (!pb_encode(&kv_stream, meshtastic_KeyVerification_fields, &kv))
    {
        return false;
    }

    uint8_t data_buf[160];
    size_t data_size = sizeof(data_buf);
    if (!encodeAppData(meshtastic_PortNum_KEY_VERIFICATION_APP,
                       kv_buf, kv_stream.bytes_written,
                       want_response, data_buf, &data_size))
    {
        return false;
    }

    uint8_t pki_buf[256];
    size_t pki_len = sizeof(pki_buf);
    MessageId msg_id = next_packet_id_++;
    if (!encryptPkiPayload(dest, msg_id, data_buf, data_size, pki_buf, &pki_len))
    {
        return false;
    }

    uint8_t wire_buffer[512];
    size_t wire_size = sizeof(wire_buffer);
    uint8_t hop_limit = config_.hop_limit;
    uint8_t channel_hash = 0;
    bool want_ack = false;
    if (!buildWirePacket(pki_buf, pki_len, node_id_, msg_id,
                         dest, channel_hash, hop_limit, want_ack,
                         nullptr, 0, wire_buffer, &wire_size))
    {
        return false;
    }

    if (transmitWirePacket(wire_buffer, wire_size))
    {
        return true;
    }
    return false;
}

bool MtAdapter::sendRoutingAck(uint32_t dest, uint32_t request_id, uint8_t channel_hash,
                               const uint8_t* psk, size_t psk_len)
{
    if (!board_.isRadioOnline())
    {
        return false;
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

    if (channel_hash == 0)
    {
        if (!pki_ready_ || node_public_keys_.find(dest) == node_public_keys_.end())
        {
            return false;
        }

        uint8_t pki_buf[256];
        size_t pki_len = sizeof(pki_buf);
        MessageId msg_id = next_packet_id_++;
        if (!encryptPkiPayload(dest, msg_id, data_buf, dstream.bytes_written, pki_buf, &pki_len))
        {
            return false;
        }

        uint8_t wire_buffer[256];
        size_t wire_size = sizeof(wire_buffer);
        uint8_t hop_limit = config_.hop_limit;
        bool want_ack = false;
        if (!buildWirePacket(pki_buf, pki_len, node_id_, msg_id,
                             dest, channel_hash, hop_limit, want_ack,
                             nullptr, 0, wire_buffer, &wire_size))
        {
            return false;
        }

        std::string ack_full_hex = toHex(wire_buffer, wire_size, wire_size);
        LORA_LOG("[LORA] TX ack full packet hex: %s\n", ack_full_hex.c_str());

        if (transmitWirePacket(wire_buffer, wire_size))
        {
            return true;
        }
        return false;
    }

    uint8_t wire_buffer[256];
    size_t wire_size = sizeof(wire_buffer);
    uint8_t hop_limit = config_.hop_limit;
    bool want_ack = false;
    if (!buildWirePacket(data_buf, dstream.bytes_written, node_id_, next_packet_id_++,
                         dest, channel_hash, hop_limit, want_ack,
                         psk, psk_len, wire_buffer, &wire_size))
    {
        return false;
    }

    std::string ack_full_hex = toHex(wire_buffer, wire_size, wire_size);
    LORA_LOG("[LORA] TX ack full packet hex: %s\n", ack_full_hex.c_str());

    if (transmitWirePacket(wire_buffer, wire_size))
    {
        return true;
    }
    return false;
}

bool MtAdapter::sendRoutingError(uint32_t dest, uint32_t request_id, uint8_t channel_hash,
                                 const uint8_t* psk, size_t psk_len,
                                 meshtastic_Routing_Error reason)
{
    if (!board_.isRadioOnline())
    {
        return false;
    }

    meshtastic_Routing routing = meshtastic_Routing_init_default;
    routing.which_variant = meshtastic_Routing_error_reason_tag;
    routing.error_reason = reason;

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

    if (channel_hash == 0)
    {
        if (!pki_ready_ || node_public_keys_.find(dest) == node_public_keys_.end())
        {
            return false;
        }

        uint8_t pki_buf[256];
        size_t pki_len = sizeof(pki_buf);
        MessageId msg_id = next_packet_id_++;
        if (!encryptPkiPayload(dest, msg_id, data_buf, dstream.bytes_written, pki_buf, &pki_len))
        {
            return false;
        }

        uint8_t wire_buffer[256];
        size_t wire_size = sizeof(wire_buffer);
        uint8_t hop_limit = config_.hop_limit;
        bool want_ack = false;
        if (!buildWirePacket(pki_buf, pki_len, node_id_, msg_id,
                             dest, channel_hash, hop_limit, want_ack,
                             nullptr, 0, wire_buffer, &wire_size))
        {
            return false;
        }

        std::string err_full_hex = toHex(wire_buffer, wire_size, wire_size);
        LORA_LOG("[LORA] TX routing error full packet hex: %s\n", err_full_hex.c_str());

        if (transmitWirePacket(wire_buffer, wire_size))
        {
            return true;
        }
        return false;
    }

    uint8_t wire_buffer[256];
    size_t wire_size = sizeof(wire_buffer);
    uint8_t hop_limit = config_.hop_limit;
    bool want_ack = false;
    if (!buildWirePacket(data_buf, dstream.bytes_written, node_id_, next_packet_id_++,
                         dest, channel_hash, hop_limit, want_ack,
                         psk, psk_len, wire_buffer, &wire_size))
    {
        return false;
    }

    std::string err_full_hex = toHex(wire_buffer, wire_size, wire_size);
    LORA_LOG("[LORA] TX routing error full packet hex: %s\n", err_full_hex.c_str());

    if (transmitWirePacket(wire_buffer, wire_size))
    {
        return true;
    }
    return false;
}

void MtAdapter::emitRoutingResultToPhone(uint32_t request_id,
                                         meshtastic_Routing_Error reason,
                                         uint32_t from,
                                         uint32_t to,
                                         ChannelId channel,
                                         uint8_t channel_hash,
                                         const chat::RxMeta* rx_meta)
{
    if (request_id == 0)
    {
        return;
    }

    mt_diag_log("[MT][ACK->BLE] req=%08lX from=%08lX to=%08lX reason=%u\n",
                static_cast<unsigned long>(request_id),
                static_cast<unsigned long>(from),
                static_cast<unsigned long>(to),
                static_cast<unsigned>(reason));

    meshtastic_Routing routing = meshtastic_Routing_init_default;
    routing.which_variant = meshtastic_Routing_error_reason_tag;
    routing.error_reason = reason;

    uint8_t routing_buf[32];
    pb_ostream_t rstream = pb_ostream_from_buffer(routing_buf, sizeof(routing_buf));
    if (!pb_encode(&rstream, meshtastic_Routing_fields, &routing))
    {
        LORA_LOG("[LORA] synthetic routing encode fail req=%08lX\n",
                 (unsigned long)request_id);
        return;
    }

    MeshIncomingData incoming;
    incoming.portnum = meshtastic_PortNum_ROUTING_APP;
    incoming.from = from;
    incoming.to = to;
    incoming.packet_id = 0;
    incoming.request_id = request_id;
    incoming.channel = channel;
    incoming.channel_hash = channel_hash;
    incoming.hop_limit = rx_meta ? rx_meta->hop_limit : 0;
    incoming.want_response = false;
    incoming.payload.assign(routing_buf, routing_buf + rstream.bytes_written);

    if (rx_meta)
    {
        incoming.rx_meta = *rx_meta;
    }
    else
    {
        incoming.rx_meta.rx_timestamp_ms = millis();
        incoming.rx_meta.rx_timestamp_s = incoming.rx_meta.rx_timestamp_ms / 1000U;
        incoming.rx_meta.time_source = chat::RxTimeSource::Uptime;
        incoming.rx_meta.origin = chat::RxOrigin::Mesh;
        incoming.rx_meta.channel_hash = channel_hash;
    }

    if (app_receive_queue_.size() < MAX_APP_QUEUE)
    {
        app_receive_queue_.push(incoming);
    }
    else
    {
        LORA_LOG("[LORA] synthetic routing drop req=%08lX queue full\n",
                 (unsigned long)request_id);
    }

    sys::EventBus::publish(
        new sys::ChatSendResultEvent(request_id, reason == meshtastic_Routing_Error_NONE), 0);
}

} // namespace meshtastic
} // namespace chat
