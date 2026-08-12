/**
 * @file mt_adapter.cpp
 * @brief Meshtastic mesh adapter implementation
 */

#include "platform/esp/arduino_common/chat/infra/meshtastic/mt_adapter.h"
#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/domain/contact_types.h"
#include "chat/infra/voice/vmp_private_crypto.h"
#include "chat/time_utils.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/arduino_common/gps/gps_service_api.h"
#include "platform/esp/arduino_common/mesh/esp_preferences_mesh_identity_store.h"
#include "sys/event_bus.h"
#include "team/protocol/team_portnum.h"
#include <Arduino.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <ctime>
#include <esp_heap_caps.h>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#define TEST_CURVE25519_FIELD_OPS
#include "board/TLoRaPagerTypes.h"
#include "chat/infra/meshtastic/mt_node_payload.h"
#include "chat/infra/meshtastic/mt_pki_crypto.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "chat/runtime/meshtastic_position_core.h"
#include "chat/runtime/meshtastic_protocol_policy.h"
#include "chat/runtime/meshtastic_self_announcement_core.h"
#include "chat/runtime/self_identity_policy.h"
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
constexpr uint8_t kBitfieldWantResponseMask = 0x02;
constexpr uint32_t kBroadcastNodeId = 0xFFFFFFFFu;

using chat::meshtastic::computeHopsAway;
using chat::meshtastic::computeKeyVerificationHashes;
using chat::meshtastic::decryptPkiAesCcm;
using chat::meshtastic::encryptPkiAesCcm;
using chat::meshtastic::fillDecodedPacketCommon;
using chat::meshtastic::hashSharedKey;
using chat::meshtastic::initPkiNonce;
using chat::meshtastic::makeEncryptedPacketFromWire;
using chat::meshtastic::readPbString;
using chat::meshtastic::requirePkiForDirectPort;
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

bool isLowPriorityMqttUplink(const meshtastic_MeshPacket& packet)
{
    if (packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
    {
        // An encrypted packet has no inspectable port number here. Preserve it
        // rather than risking the loss of an encrypted user message.
        return false;
    }

    switch (packet.decoded.portnum)
    {
    case meshtastic_PortNum_NODEINFO_APP:
    case meshtastic_PortNum_POSITION_APP:
    case meshtastic_PortNum_TELEMETRY_APP:
    case meshtastic_PortNum_TRACEROUTE_APP:
        return true;
    default:
        return false;
    }
}

chat::delivery::SendFailureKind failureKindFromRoutingError(
    meshtastic_Routing_Error reason)
{
    switch (reason)
    {
    case meshtastic_Routing_Error_NONE:
        return chat::delivery::SendFailureKind::None;
    case meshtastic_Routing_Error_TIMEOUT:
    case meshtastic_Routing_Error_MAX_RETRANSMIT:
    case meshtastic_Routing_Error_NO_RESPONSE:
    case meshtastic_Routing_Error_NO_ROUTE:
        return chat::delivery::SendFailureKind::AckTimeout;
    case meshtastic_Routing_Error_NO_INTERFACE:
    case meshtastic_Routing_Error_DUTY_CYCLE_LIMIT:
    case meshtastic_Routing_Error_RATE_LIMIT_EXCEEDED:
        return chat::delivery::SendFailureKind::RadioSendFailed;
    case meshtastic_Routing_Error_NO_CHANNEL:
        return chat::delivery::SendFailureKind::ChannelKeyMissing;
    case meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY:
    case meshtastic_Routing_Error_PKI_FAILED:
        return chat::delivery::SendFailureKind::PeerKeyMissing;
    case meshtastic_Routing_Error_GOT_NAK:
    case meshtastic_Routing_Error_BAD_REQUEST:
    case meshtastic_Routing_Error_NOT_AUTHORIZED:
    case meshtastic_Routing_Error_ADMIN_BAD_SESSION_KEY:
    case meshtastic_Routing_Error_ADMIN_PUBLIC_KEY_UNAUTHORIZED:
    case meshtastic_Routing_Error_TOO_LARGE:
        return chat::delivery::SendFailureKind::Rejected;
    }
    return chat::delivery::SendFailureKind::Unknown;
}

bool isPermanentQueuedTextFailure(meshtastic_Routing_Error reason)
{
    switch (reason)
    {
    case meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY:
    case meshtastic_Routing_Error_PKI_FAILED:
    case meshtastic_Routing_Error_NO_CHANNEL:
    case meshtastic_Routing_Error_BAD_REQUEST:
    case meshtastic_Routing_Error_NOT_AUTHORIZED:
    case meshtastic_Routing_Error_ADMIN_BAD_SESSION_KEY:
    case meshtastic_Routing_Error_ADMIN_PUBLIC_KEY_UNAUTHORIZED:
    case meshtastic_Routing_Error_TOO_LARGE:
        return true;
    case meshtastic_Routing_Error_NONE:
    case meshtastic_Routing_Error_NO_ROUTE:
    case meshtastic_Routing_Error_GOT_NAK:
    case meshtastic_Routing_Error_TIMEOUT:
    case meshtastic_Routing_Error_NO_INTERFACE:
    case meshtastic_Routing_Error_MAX_RETRANSMIT:
    case meshtastic_Routing_Error_NO_RESPONSE:
    case meshtastic_Routing_Error_DUTY_CYCLE_LIMIT:
    case meshtastic_Routing_Error_RATE_LIMIT_EXCEEDED:
        return false;
    }
    return false;
}

int16_t coreRadioRssi(float rssi)
{
    if (!std::isfinite(rssi))
    {
        return 0;
    }
    return static_cast<int16_t>(std::lround(
        std::max(-32768.0f, std::min(32767.0f, rssi))));
}

int8_t coreRadioSnr(float snr)
{
    if (!std::isfinite(snr))
    {
        return 0;
    }
    return static_cast<int8_t>(std::lround(
        std::max(-128.0f, std::min(127.0f, snr))));
}

void mt_diag_log(const char* fmt, ...)
{
    char buf[160] = {};
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
using chat::meshtastic::isZeroKey;
using chat::meshtastic::keyVerificationStage;
using chat::meshtastic::routingErrorName;
using chat::meshtastic::toHex;

static chat::runtime::MeshtasticPositionInput build_self_position_input()
{
    gps::GpsState gps_state = gps::gps_get_data();

    chat::runtime::MeshtasticPositionInput input{};
    input.valid = gps_state.valid;
    input.latitude_deg = gps_state.lat;
    input.longitude_deg = gps_state.lng;
    input.has_altitude = gps_state.has_alt;
    input.altitude_m = gps_state.alt_m;
    input.has_speed = gps_state.has_speed;
    input.speed_mps = gps_state.speed_mps;
    input.has_course = gps_state.has_course;
    input.course_deg = gps_state.course_deg;
    input.satellites = gps_state.satellites;
    input.timestamp_s = static_cast<uint32_t>(time(nullptr));
    return input;
}

static void publishPositionEvent(uint32_t node_id,
                                 const chat::contacts::NodePosition& pos)
{
    if (node_id == 0 || !pos.valid)
    {
        return;
    }

    sys::EventBus::publish(
        new sys::NodePositionUpdateEvent(node_id,
                                         pos.latitude_i,
                                         pos.longitude_i,
                                         pos.has_altitude,
                                         pos.altitude,
                                         pos.timestamp,
                                         pos.precision_bits,
                                         pos.pdop,
                                         pos.hdop,
                                         pos.vdop,
                                         pos.gps_accuracy_mm),
        0);
}

} // namespace

// Use protobuf codec and wire packet functions
using chat::meshtastic::buildWirePacket;
using chat::meshtastic::decodeKeyVerificationMessage;
using chat::meshtastic::decodeTextMessage;
using chat::meshtastic::decryptPayload;
using chat::meshtastic::encodeAppData;
using chat::meshtastic::encodeTextMessage;
using chat::meshtastic::PacketHeaderWire;
using chat::meshtastic::parseWirePacket;

namespace chat
{
namespace meshtastic
{

using ::chat::infra::IncomingQueuePriority;
using ::chat::infra::IncomingQueuePushReport;

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
    caps.supports_node_info_query = true;
    caps.supports_node_info_reply = true;
    caps.supports_node_info_reannounce = true;
    caps.supports_position_request = true;
    caps.supports_position_reply = true;
    caps.supports_trace_route_request = true;
    caps.supports_trace_route_reply = true;
    caps.supports_protocol_app_response = true;
    caps.supports_protocol_ack_tracking = true;
    return caps;
}

MtAdapter::MtAdapter(LoraBoard& board, IMeshPeerDirectory* peer_directory)
    : board_(board),
      peer_directory_(peer_directory),
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
    core_bridge_.reset(
        new ::platform::esp::arduino_common::mesh::EspMeshtasticAdapterBridge(
            board_,
            peer_directory_));
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

void* MtAdapter::operator new(std::size_t size)
{
    void* ptr = heap_caps_malloc_prefer(size,
                                        2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return ptr != nullptr ? ptr : ::operator new(size);
}

void MtAdapter::operator delete(void* ptr) noexcept
{
    heap_caps_free(ptr);
}

void MtAdapter::operator delete(void* ptr, std::size_t) noexcept
{
    operator delete(ptr);
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
    if (text.size() > ::chat::infra::kIncomingTextMaxLen)
    {
        mt_diag_log("[MT][TX] reject text len=%u cap=%u reason=too_long\n",
                    static_cast<unsigned>(text.size()),
                    static_cast<unsigned>(::chat::infra::kIncomingTextMaxLen));
        return false;
    }
    if (send_queue_.isFull())
    {
        mt_diag_log("[MT][TX] reject text len=%u reason=send_queue_full depth=%u\n",
                    static_cast<unsigned>(text.size()),
                    static_cast<unsigned>(send_queue_.size()));
        LORA_LOG("[LORA] TX queue full drop new text len=%u\n",
                 static_cast<unsigned>(text.size()));
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
    pending.text_len = text.size();
    memcpy(pending.text.data(), text.data(), pending.text_len);
    pending.text[pending.text_len] = '\0';
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
    pending.key_exchange_count = 0;
    pending.waiting_for_peer_key = false;

    send_queue_.append(pending);
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
    return receive_queue_.pop(out);
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

    runtime::SendPacketEffect packet{};
    packet.protocol = MeshProtocol::Meshtastic;
    packet.channel = channel;
    packet.dest = dest;
    packet.portnum = portnum;
    packet.request_id = (packet_id != 0) ? packet_id : next_packet_id_++;
    if (packet_id != 0 && packet_id >= next_packet_id_)
    {
        next_packet_id_ = packet_id + 1;
        if (next_packet_id_ == 0)
        {
            next_packet_id_ = 1;
        }
    }
    packet.want_ack = want_ack;
    packet.want_response = want_response;
    if (!packet.payload.assign(payload, len))
    {
        return false;
    }
    return enqueueSendPacketAction(packet);
}

bool MtAdapter::sendAppDataNow(ChannelId channel, uint32_t portnum,
                               const uint8_t* payload, size_t len,
                               NodeId dest, bool want_ack,
                               MessageId packet_id,
                               bool want_response)
{
    last_send_error_ = meshtastic_Routing_Error_NONE;
    if (!ready_ || !config_.tx_enabled)
    {
        last_send_error_ = meshtastic_Routing_Error_NO_INTERFACE;
        return false;
    }

    uint32_t now_ms = millis();
    if (min_tx_interval_ms_ > 0 && last_tx_ms_ > 0 &&
        (now_ms - last_tx_ms_) < min_tx_interval_ms_)
    {
        last_send_error_ = meshtastic_Routing_Error_DUTY_CYCLE_LIMIT;
        return false;
    }

    ChannelId out_channel = channel;
    if (encrypt_mode_ == 0 || encrypt_mode_ == 2)
    {
        out_channel = ChannelId::PRIMARY;
    }

    auto& scratch = tx_scratch_;
    auto& data_buffer = scratch.data;
    auto& wire_buffer = scratch.wire;
    size_t data_size = data_buffer.size();
    const auto send_policy =
        chat::runtime::resolveMeshtasticAppDataSendPolicy(dest, want_ack, want_response);
    bool effective_want_response = send_policy.effective_want_response;
    if (!encodeAppData(portnum,
                       payload,
                       len,
                       effective_want_response,
                       data_buffer.data(),
                       &data_size,
                       &scratch.decoded))
    {
        last_send_error_ = meshtastic_Routing_Error_BAD_REQUEST;
        return false;
    }

    size_t wire_size = wire_buffer.size();

    uint8_t channel_hash =
        (out_channel == ChannelId::SECONDARY) ? secondary_channel_hash_ : primary_channel_hash_;
    const uint8_t* psk =
        (out_channel == ChannelId::SECONDARY) ? secondary_psk_ : primary_psk_;
    size_t psk_len =
        (out_channel == ChannelId::SECONDARY) ? secondary_psk_len_ : primary_psk_len_;
    uint8_t hop_limit = config_.hop_limit;
    uint32_t dest_node = send_policy.wire_dest;
    bool track_ack = send_policy.track_ack;
    bool air_want_ack = send_policy.wire_want_ack;
    MessageId msg_id = (packet_id != 0) ? packet_id : next_packet_id_++;
    if (packet_id != 0 && packet_id >= next_packet_id_)
    {
        next_packet_id_ = packet_id + 1;
        if (next_packet_id_ == 0)
        {
            next_packet_id_ = 1;
        }
    }

    const uint8_t* out_payload = data_buffer.data();
    size_t out_len = data_size;
    bool use_pki = false;
    if (requirePkiForDirectPort(dest_node, portnum))
    {
        const bool have_dest_key = findPkiNodeKey(dest_node) != nullptr;
        if (!pki_ready_ || !have_dest_key)
        {
            const char* reason = !pki_ready_ ? "pki_not_ready" : "pki_key_missing";
            last_send_error_ = !pki_ready_ ? meshtastic_Routing_Error_PKI_FAILED
                                           : meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY;
            mt_diag_log("[MT][TX_BLOCK] id=%08lX dest=%08lX port=%u reason=%s path=PKI\n",
                        static_cast<unsigned long>(msg_id),
                        static_cast<unsigned long>(dest_node),
                        static_cast<unsigned>(portnum),
                        reason);
            LORA_LOG("[LORA] TX app PKI required but unavailable dest=%08lX port=%u\n",
                     (unsigned long)dest_node,
                     (unsigned)portnum);
            executePkiResync(!pki_ready_
                                 ? runtime::MeshtasticPkiResyncCause::LocalPkiNotReady
                                 : runtime::MeshtasticPkiResyncCause::PeerKeyMissing,
                             dest_node,
                             msg_id,
                             out_channel);
            return false;
        }

        auto& pki_buffer = scratch.pki;
        size_t pki_len = pki_buffer.size();
        if (!encryptPkiPayload(dest_node, msg_id, data_buffer.data(), data_size, pki_buffer.data(), &pki_len))
        {
            last_send_error_ = meshtastic_Routing_Error_PKI_FAILED;
            mt_diag_log("[MT][TX_BLOCK] id=%08lX dest=%08lX port=%u reason=pki_encrypt_fail path=PKI\n",
                        static_cast<unsigned long>(msg_id),
                        static_cast<unsigned long>(dest_node),
                        static_cast<unsigned>(portnum));
            LORA_LOG("[LORA] TX app PKI encrypt failed dest=%08lX port=%u\n",
                     (unsigned long)dest_node,
                     (unsigned)portnum);
            return false;
        }
        out_payload = pki_buffer.data();
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
    bool ok = false;
    if (use_pki)
    {
        if (!buildWirePacket(out_payload, out_len, node_id_, msg_id,
                             dest_node, channel_hash, hop_limit, air_want_ack,
                             psk, psk_len, wire_buffer.data(), &wire_size))
        {
            last_send_error_ = meshtastic_Routing_Error_TOO_LARGE;
            return false;
        }
        if (!board_.isRadioOnline())
        {
            last_send_error_ = meshtastic_Routing_Error_NO_INTERFACE;
            return false;
        }
        ok = transmitWirePacket(wire_buffer.data(), wire_size);
    }
    else if (dest_node == kBroadcastNodeId)
    {
        if (!buildWirePacket(out_payload, out_len, node_id_, msg_id,
                             dest_node, channel_hash, hop_limit, air_want_ack,
                             psk, psk_len, wire_buffer.data(), &wire_size))
        {
            last_send_error_ = meshtastic_Routing_Error_TOO_LARGE;
            return false;
        }
        if (!board_.isRadioOnline())
        {
            last_send_error_ = meshtastic_Routing_Error_NO_INTERFACE;
            return false;
        }
        ok = transmitWirePacket(wire_buffer.data(), wire_size);
    }
    else
    {
        ok = sendChannelAppDataViaCore(portnum,
                                       payload,
                                       len,
                                       dest_node,
                                       effective_want_response,
                                       msg_id,
                                       channel_hash,
                                       psk,
                                       psk_len,
                                       hop_limit,
                                       air_want_ack,
                                       wire_buffer.data(),
                                       &wire_size);
    }
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
            trackPendingAck(msg_id, dest_node, out_channel, channel_hash, wire_buffer.data(), wire_size);
        }
        auto& mqtt_data = scratch.decoded;
        std::memset(&mqtt_data, 0, sizeof(mqtt_data));
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
        queueMqttProxyPublishFromWire(wire_buffer.data(), wire_size,
                                      use_pki ? nullptr : &mqtt_data,
                                      out_channel);
    }
    else
    {
        last_send_error_ = board_.isRadioOnline()
                               ? meshtastic_Routing_Error_RATE_LIMIT_EXCEEDED
                               : meshtastic_Routing_Error_NO_INTERFACE;
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

    auto& scratch = tx_scratch_;
    auto& data = scratch.decoded;
    auto& data_buf = scratch.data;
    auto& wire_buffer = scratch.wire;
    data = packet.decoded;
    data.dest = (packet.to != 0) ? packet.to : kBroadcastNodeId;
    data.source = node_id_;
    data.has_bitfield = true;

    pb_ostream_t dstream = pb_ostream_from_buffer(data_buf.data(), data_buf.size());
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
    const uint8_t* out_payload = data_buf.data();
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
        if (!findPkiNodeKey(dest))
        {
            last_send_error_ = meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY;
            return false;
        }

        auto& pki_buffer = scratch.pki;
        size_t pki_len = pki_buffer.size();
        if (!encryptPkiPayload(dest, msg_id, data_buf.data(), dstream.bytes_written, pki_buffer.data(), &pki_len))
        {
            last_send_error_ = meshtastic_Routing_Error_PKI_FAILED;
            return false;
        }

        out_payload = pki_buffer.data();
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

    size_t wire_size = wire_buffer.size();
    if (!buildWirePacket(out_payload, out_len, node_id_, msg_id,
                         dest, channel_hash, hop_limit, air_want_ack,
                         psk, psk_len, wire_buffer.data(), &wire_size))
    {
        last_send_error_ = meshtastic_Routing_Error_TOO_LARGE;
        return false;
    }

    if (transmitWirePacket(wire_buffer.data(), wire_size))
    {
        last_tx_ms_ = now_ms;
        last_send_error_ = meshtastic_Routing_Error_NONE;
        if (track_ack)
        {
            trackPendingAck(msg_id, dest, (packet.channel == 1) ? ChannelId::SECONDARY : ChannelId::PRIMARY,
                            channel_hash, wire_buffer.data(), wire_size);
        }
        ChannelId mqtt_channel = (packet.channel == 1) ? ChannelId::SECONDARY : ChannelId::PRIMARY;
        queueMqttProxyPublishFromWire(wire_buffer.data(), wire_size,
                                      (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag) ? &data : nullptr,
                                      mqtt_channel);
        return true;
    }
    last_send_error_ = meshtastic_Routing_Error_NO_INTERFACE;
    return false;
}

bool MtAdapter::pollIncomingData(MeshIncomingData* out)
{
    return app_receive_queue_.pop(out);
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
        (void)getNodeLastChannel(target, &channel);
    }
    return enqueueNodeInfoAction(target, want_response, channel);
}

bool MtAdapter::isPkiReady() const
{
    return pki_ready_;
}

bool MtAdapter::hasPkiKey(NodeId dest) const
{
    if (findPkiNodeKey(dest))
    {
        return true;
    }
    uint8_t key[32] = {};
    return readPkiNodeKeyFromDirectory(dest, key, nullptr);
}

bool MtAdapter::getNodePublicKey(NodeId node_id, uint8_t out_key[32]) const
{
    if (!out_key)
    {
        return false;
    }
    const auto* entry = findPkiNodeKey(node_id);
    if (entry)
    {
        memcpy(out_key, entry->key.data(), 32);
        return true;
    }
    return readPkiNodeKeyFromDirectory(node_id, out_key, nullptr);
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

bool MtAdapter::deriveVmpContactSecret(NodeId peer_id, uint8_t out_secret[32])
{
    if (!out_secret || peer_id == 0U || peer_id == kBroadcastNodeId || !pki_ready_)
    {
        return false;
    }
    const PkiNodeKeyEntry* const peer_key = findPkiNodeKey(peer_id);
    if (!peer_key)
    {
        return false;
    }

    uint8_t identity_shared_secret[32] = {};
    uint8_t local_private_key[32] = {};
    std::memcpy(identity_shared_secret,
                peer_key->key.data(),
                sizeof(identity_shared_secret));
    std::memcpy(local_private_key,
                pki_private_key_.data(),
                sizeof(local_private_key));
    const bool derived = Curve25519::dh2(identity_shared_secret, local_private_key) &&
                         ::chat::voice::vmp::deriveVmpContactSecret(
                             identity_shared_secret,
                             ::chat::voice::vmp::ContactSecretIdentityFamily::Meshtastic,
                             node_id_,
                             peer_id,
                             out_secret);
    volatile uint8_t* cursor = identity_shared_secret;
    for (std::size_t index = 0U; index < sizeof(identity_shared_secret); ++index)
    {
        cursor[index] = 0U;
    }
    cursor = local_private_key;
    for (std::size_t index = 0U; index < sizeof(local_private_key); ++index)
    {
        cursor[index] = 0U;
    }
    return derived;
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
    (void)erasePkiNodeKey(node_id);
    eraseNodeRuntime(node_id);
    if (peer_directory_)
    {
        (void)peer_directory_->remove(
            makeMeshPeerNodeIdentity(MeshProtocol::Meshtastic, node_id));
    }
}

meshtastic_Routing_Error MtAdapter::getLastRoutingError() const
{
    return last_send_error_;
}

void MtAdapter::setMqttProxySettings(const MqttProxySettings& settings)
{
    const bool root_changed = mqtt_proxy_settings_.root != settings.root;
    const bool downlink_policy_changed =
        mqtt_proxy_settings_.enabled != settings.enabled ||
        mqtt_proxy_settings_.proxy_to_client_enabled != settings.proxy_to_client_enabled ||
        mqtt_proxy_settings_.encryption_enabled != settings.encryption_enabled ||
        mqtt_proxy_settings_.primary_downlink_enabled != settings.primary_downlink_enabled ||
        mqtt_proxy_settings_.secondary_downlink_enabled != settings.secondary_downlink_enabled ||
        mqtt_proxy_settings_.root != settings.root ||
        mqtt_proxy_settings_.primary_channel_id != settings.primary_channel_id ||
        mqtt_proxy_settings_.secondary_channel_id != settings.secondary_channel_id;
    const bool changed = mqtt_proxy_settings_.enabled != settings.enabled ||
                         mqtt_proxy_settings_.proxy_to_client_enabled != settings.proxy_to_client_enabled ||
                         mqtt_proxy_settings_.encryption_enabled != settings.encryption_enabled ||
                         mqtt_proxy_settings_.primary_uplink_enabled != settings.primary_uplink_enabled ||
                         mqtt_proxy_settings_.primary_downlink_enabled != settings.primary_downlink_enabled ||
                         mqtt_proxy_settings_.secondary_uplink_enabled != settings.secondary_uplink_enabled ||
                         mqtt_proxy_settings_.secondary_downlink_enabled != settings.secondary_downlink_enabled ||
                         mqtt_proxy_settings_.root != settings.root ||
                         mqtt_proxy_settings_.primary_channel_id != settings.primary_channel_id ||
                         mqtt_proxy_settings_.secondary_channel_id != settings.secondary_channel_id;
    if (root_changed && !mqtt_proxy_queue_.empty())
    {
        const size_t dropped = mqtt_proxy_queue_.size();
        mqtt_proxy_queue_.clear();
        LORA_LOG("[MQTT] settings root changed drop queued=%u old_root=%s new_root=%s\n",
                 static_cast<unsigned>(dropped),
                 mqtt_proxy_settings_.root.c_str(),
                 settings.root.c_str());
    }
    if (downlink_policy_changed)
    {
        const size_t dropped = mqtt_downlink_tx_queue_.size();
        mqtt_downlink_tx_queue_.clear();
        for (auto& entry : mqtt_downlink_seen_)
        {
            entry = MqttDownlinkSeenEntry{};
        }
        mqtt_downlink_seen_next_ = 0;
        if (dropped > 0)
        {
            LORA_LOG("[MQTT][DownlinkTX] settings changed drop pending=%u\n",
                     static_cast<unsigned>(dropped));
        }
    }
    mqtt_proxy_settings_ = settings;
    if (changed)
    {
        LORA_LOG("[MQTT] settings enabled=%u proxy=%u enc=%u root=%s "
                 "primary='%s' up=%u down=%u secondary='%s' up=%u down=%u\n",
                 mqtt_proxy_settings_.enabled ? 1U : 0U,
                 mqtt_proxy_settings_.proxy_to_client_enabled ? 1U : 0U,
                 mqtt_proxy_settings_.encryption_enabled ? 1U : 0U,
                 mqtt_proxy_settings_.root.c_str(),
                 mqtt_proxy_settings_.primary_channel_id.c_str(),
                 mqtt_proxy_settings_.primary_uplink_enabled ? 1U : 0U,
                 mqtt_proxy_settings_.primary_downlink_enabled ? 1U : 0U,
                 mqtt_proxy_settings_.secondary_channel_id.c_str(),
                 mqtt_proxy_settings_.secondary_uplink_enabled ? 1U : 0U,
                 mqtt_proxy_settings_.secondary_downlink_enabled ? 1U : 0U);
    }
}

bool MtAdapter::pollMqttProxyMessage(meshtastic_MqttClientProxyMessage* out)
{
    return mqtt_proxy_queue_.popOldest(out);
}

bool MtAdapter::hasMqttProxyMessage() const
{
    return !mqtt_proxy_queue_.empty();
}

std::string MtAdapter::mqttNodeIdString() const
{
    char node_id[16];
    snprintf(node_id, sizeof(node_id), "!%08lx", static_cast<unsigned long>(node_id_));
    return std::string(node_id);
}

const char* MtAdapter::mqttChannelIdFor(ChannelId channel) const
{
    return ::chat::meshtastic::mqttChannelIdFor(mqtt_proxy_settings_, channel);
}

bool MtAdapter::hasAnyMqttDownlinkEnabled() const
{
    return ::chat::meshtastic::hasAnyMqttDownlinkEnabled(mqtt_proxy_settings_);
}

bool MtAdapter::shouldPublishToMqtt(ChannelId channel, bool from_mqtt, bool is_pki) const
{
    return ::chat::meshtastic::shouldPublishToMqtt(
        mqtt_proxy_settings_, channel, from_mqtt, is_pki);
}

uint8_t MtAdapter::mqttChannelHashForId(const char* channel_id, bool* out_known,
                                        ChannelId* out_channel) const
{
    const auto match =
        ::chat::meshtastic::resolveMqttProxyDownlinkChannel(mqtt_proxy_settings_, channel_id);
    bool known = match.known;
    ChannelId channel = match.channel;
    uint8_t hash = primary_channel_hash_;
    if (match.pki)
    {
        hash = 0;
    }
    else if (match.known && match.channel == ChannelId::PRIMARY)
    {
        hash = primary_channel_hash_;
    }
    else if (match.known && match.channel == ChannelId::SECONDARY)
    {
        hash = secondary_channel_hash_;
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
    if (!::chat::meshtastic::mqttProxyRuntimeEnabled(mqtt_proxy_settings_))
    {
        LORA_LOG("[MQTT] downlink reject reason=%s enabled=%u proxy=%u\n",
                 ::chat::meshtastic::mqttProxyRejectReasonName(
                     ::chat::meshtastic::MqttProxyRejectReason::ProxyDisabled),
                 mqtt_proxy_settings_.enabled ? 1U : 0U,
                 mqtt_proxy_settings_.proxy_to_client_enabled ? 1U : 0U);
        return false;
    }

    bool known_channel = false;
    ChannelId channel_index = ChannelId::PRIMARY;
    uint8_t channel_hash = mqttChannelHashForId(channel_id, &known_channel, &channel_index);
    const auto channel_reason = ::chat::meshtastic::validateMqttDownlinkChannel(known_channel);
    if (channel_reason != ::chat::meshtastic::MqttProxyRejectReason::None)
    {
        LORA_LOG("[MQTT] downlink reject reason=%s channel='%s' primary='%s' p_down=%u secondary='%s' s_down=%u\n",
                 ::chat::meshtastic::mqttProxyRejectReasonName(channel_reason),
                 channel_id ? channel_id : "",
                 mqtt_proxy_settings_.primary_channel_id.c_str(),
                 mqtt_proxy_settings_.primary_downlink_enabled ? 1U : 0U,
                 mqtt_proxy_settings_.secondary_channel_id.c_str(),
                 mqtt_proxy_settings_.secondary_downlink_enabled ? 1U : 0U);
        return false;
    }

    const auto accept_policy = chat::runtime::resolveMeshtasticMqttDownlinkPolicy(
        gateway_id,
        node_id_,
        packet.from,
        packet.to,
        config_.tx_enabled);
    if (!accept_policy.accept_locally)
    {
        LORA_LOG("[MQTT] downlink ignore reason=%s gateway='%s' from=%08lX id=%08lX\n",
                 chat::runtime::meshtasticMqttDownlinkReasonName(accept_policy.reason),
                 gateway_id ? gateway_id : "",
                 (unsigned long)packet.from,
                 (unsigned long)packet.id);
        return false;
    }

    const bool is_decoded_payload = packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag;
    const meshtastic_PortNum decoded_portnum =
        is_decoded_payload ? packet.decoded.portnum : meshtastic_PortNum_UNKNOWN_APP;
    const bool is_metadata_only_payload =
        is_decoded_payload &&
        ::chat::meshtastic::isMqttMetadataOnlyDownlinkPort(decoded_portnum);
    const auto decoded_reason = ::chat::meshtastic::validateMqttDecodedDownlinkPayload(
        mqtt_proxy_settings_, is_decoded_payload, decoded_portnum);
    if (decoded_reason != ::chat::meshtastic::MqttProxyRejectReason::None)
    {
        LORA_LOG("[MQTT] downlink reject reason=%s port=%u enc=%u\n",
                 ::chat::meshtastic::mqttProxyRejectReasonName(decoded_reason),
                 is_decoded_payload ? static_cast<unsigned>(decoded_portnum) : 0U,
                 mqtt_proxy_settings_.encryption_enabled ? 1U : 0U);
        return false;
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

    auto& tx = tx_scratch_;
    auto& wire_storage = tx.wire;
    uint8_t* wire_buffer = wire_storage.data();
    size_t wire_size = sizeof(PacketHeaderWire);
    memcpy(wire_buffer, &header, sizeof(header));

    if (packet.which_payload_variant == meshtastic_MeshPacket_encrypted_tag)
    {
        size_t payload_size = std::min(static_cast<size_t>(packet.encrypted.size), sizeof(packet.encrypted.bytes));
        if (wire_size + payload_size > wire_storage.size())
        {
            LORA_LOG("[MQTT] downlink reject reason=%s encrypted=%u wire_cap=%u\n",
                     ::chat::meshtastic::mqttProxyRejectReasonName(
                         ::chat::meshtastic::MqttProxyRejectReason::PayloadTooLarge),
                     static_cast<unsigned>(payload_size),
                     static_cast<unsigned>(wire_storage.size()));
            return false;
        }
        memcpy(wire_buffer + wire_size, packet.encrypted.bytes, payload_size);
        wire_size += payload_size;
    }
    else
    {
        auto& decoded = tx.decoded;
        auto& data_buffer = tx.data;
        decoded = packet.decoded;
        decoded.dest = packet.to;
        decoded.source = packet.from;
        decoded.has_bitfield = true;
        pb_ostream_t dstream = pb_ostream_from_buffer(data_buffer.data(), data_buffer.size());
        if (!pb_encode(&dstream, meshtastic_Data_fields, &decoded))
        {
            LORA_LOG("[MQTT] downlink reject reason=%s port=%u\n",
                     ::chat::meshtastic::mqttProxyRejectReasonName(
                         ::chat::meshtastic::MqttProxyRejectReason::DataEncodeFailed),
                     static_cast<unsigned>(decoded.portnum));
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
        wire_size = wire_storage.size();
        if (!buildWirePacket(data_buffer.data(), dstream.bytes_written, packet.from, packet.id,
                             packet.to, channel_hash, packet.hop_limit, packet.want_ack,
                             psk, psk_len, wire_buffer, &wire_size))
        {
            LORA_LOG("[MQTT] downlink reject reason=%s id=%08lX ch=0x%02X data=%u\n",
                     ::chat::meshtastic::mqttProxyRejectReasonName(
                         ::chat::meshtastic::MqttProxyRejectReason::WireBuildFailed),
                     (unsigned long)packet.id,
                     (unsigned)channel_hash,
                     (unsigned)dstream.bytes_written);
            return false;
        }
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
    PacketHeaderWire* tx_header = reinterpret_cast<PacketHeaderWire*>(wire_buffer);
    if (is_metadata_only_payload)
    {
        LORA_LOG("[MQTT] downlink mesh tx skipped reason=metadata_only port=%u id=%08lX\n",
                 static_cast<unsigned>(decoded_portnum),
                 (unsigned long)tx_header->id);
    }
    else
    {
        const auto tx_policy = chat::runtime::resolveMeshtasticMqttDownlinkPolicy(
            gateway_id,
            node_id_,
            tx_header->from,
            tx_header->to,
            config_.tx_enabled);
        if (!tx_policy.transmit_to_mesh)
        {
            LORA_LOG("[MQTT] downlink mesh tx skipped reason=%s id=%08lX\n",
                     chat::runtime::meshtasticMqttDownlinkReasonName(tx_policy.reason),
                     (unsigned long)tx_header->id);
        }
        else
        {
            const bool tx_ok = enqueueMqttDownlinkTx(wire_buffer, wire_size, *tx_header);
            LORA_LOG("[MQTT] downlink mesh tx scheduled id=%08lX ch=0x%02X len=%u ok=%u\n",
                     (unsigned long)tx_header->id,
                     (unsigned)tx_header->channel,
                     (unsigned)wire_size,
                     tx_ok ? 1U : 0U);
        }
    }

    processReceivedPacket(wire_buffer, wire_size);
    LORA_LOG("[MQTT] downlink local rx complete id=%08lX origin=mqtt\n",
             (unsigned long)tx_header->id);
    return true;
}

bool MtAdapter::isMqttDownlinkRecentlySeen(NodeId from,
                                           MessageId msg_id,
                                           uint8_t channel_hash,
                                           uint32_t now_ms)
{
    for (auto& entry : mqtt_downlink_seen_)
    {
        if (!entry.used)
        {
            continue;
        }
        if (now_ms - entry.seen_ms > kMqttDownlinkSeenTtlMs)
        {
            entry = MqttDownlinkSeenEntry{};
            continue;
        }
        if (entry.from == from &&
            entry.msg_id == msg_id &&
            entry.channel_hash == channel_hash)
        {
            return true;
        }
    }
    return false;
}

void MtAdapter::rememberMqttDownlinkSeen(NodeId from,
                                         MessageId msg_id,
                                         uint8_t channel_hash,
                                         uint32_t now_ms)
{
    for (auto& entry : mqtt_downlink_seen_)
    {
        if (entry.used &&
            entry.from == from &&
            entry.msg_id == msg_id &&
            entry.channel_hash == channel_hash)
        {
            entry.seen_ms = now_ms;
            return;
        }
    }

    for (auto& entry : mqtt_downlink_seen_)
    {
        if (!entry.used || now_ms - entry.seen_ms > kMqttDownlinkSeenTtlMs)
        {
            entry.used = true;
            entry.from = from;
            entry.msg_id = msg_id;
            entry.channel_hash = channel_hash;
            entry.seen_ms = now_ms;
            return;
        }
    }

    MqttDownlinkSeenEntry& entry = mqtt_downlink_seen_[mqtt_downlink_seen_next_];
    mqtt_downlink_seen_next_ = (mqtt_downlink_seen_next_ + 1U) % mqtt_downlink_seen_.size();
    entry.used = true;
    entry.from = from;
    entry.msg_id = msg_id;
    entry.channel_hash = channel_hash;
    entry.seen_ms = now_ms;
}

bool MtAdapter::enqueueMqttDownlinkTx(const uint8_t* wire_data,
                                      size_t wire_size,
                                      const PacketHeaderWire& header)
{
    if (!wire_data || wire_size == 0 || wire_size > kMqttDownlinkWireMaxLen)
    {
        LORA_LOG("[MQTT][DownlinkTX] drop reason=invalid_wire id=%08lX len=%u\n",
                 static_cast<unsigned long>(header.id),
                 static_cast<unsigned>(wire_size));
        return false;
    }

    const uint32_t now_ms = millis();
    if (isMqttDownlinkRecentlySeen(header.from, header.id, header.channel, now_ms))
    {
        LORA_LOG("[MQTT][DownlinkTX] skip reason=duplicate from=%08lX id=%08lX ch=0x%02X depth=%u\n",
                 static_cast<unsigned long>(header.from),
                 static_cast<unsigned long>(header.id),
                 static_cast<unsigned>(header.channel),
                 static_cast<unsigned>(mqtt_downlink_tx_queue_.size()));
        return true;
    }

    for (size_t i = 0; i < mqtt_downlink_tx_queue_.size(); ++i)
    {
        const PendingMqttDownlinkTx* pending = mqtt_downlink_tx_queue_.get(i);
        if (pending &&
            pending->from == header.from &&
            pending->msg_id == header.id &&
            pending->channel_hash == header.channel)
        {
            rememberMqttDownlinkSeen(header.from, header.id, header.channel, now_ms);
            LORA_LOG("[MQTT][DownlinkTX] skip reason=already_queued from=%08lX id=%08lX ch=0x%02X depth=%u\n",
                     static_cast<unsigned long>(header.from),
                     static_cast<unsigned long>(header.id),
                     static_cast<unsigned>(header.channel),
                     static_cast<unsigned>(mqtt_downlink_tx_queue_.size()));
            return true;
        }
    }

    if (mqtt_downlink_tx_queue_.isFull())
    {
        rememberMqttDownlinkSeen(header.from, header.id, header.channel, now_ms);
        LORA_LOG("[MQTT][DownlinkTX] drop reason=pending_queue_full from=%08lX to=%08lX id=%08lX ch=0x%02X depth=%u\n",
                 static_cast<unsigned long>(header.from),
                 static_cast<unsigned long>(header.to),
                 static_cast<unsigned long>(header.id),
                 static_cast<unsigned>(header.channel),
                 static_cast<unsigned>(mqtt_downlink_tx_queue_.size()));
        return false;
    }

    PendingMqttDownlinkTx pending{};
    std::memcpy(pending.wire.data(), wire_data, wire_size);
    pending.wire_size = wire_size;
    pending.from = header.from;
    pending.to = header.to;
    pending.msg_id = header.id;
    pending.channel_hash = header.channel;
    pending.first_seen_ms = now_ms;

    mqtt_downlink_tx_queue_.append(pending);
    rememberMqttDownlinkSeen(header.from, header.id, header.channel, now_ms);
    LORA_LOG("[MQTT][DownlinkTX] queued from=%08lX to=%08lX id=%08lX ch=0x%02X len=%u depth=%u\n",
             static_cast<unsigned long>(header.from),
             static_cast<unsigned long>(header.to),
             static_cast<unsigned long>(header.id),
             static_cast<unsigned>(header.channel),
             static_cast<unsigned>(wire_size),
             static_cast<unsigned>(mqtt_downlink_tx_queue_.size()));
    return true;
}

bool MtAdapter::handleMqttProxyMessage(const meshtastic_MqttClientProxyMessage& msg)
{
    const bool is_data = msg.which_payload_variant == meshtastic_MqttClientProxyMessage_data_tag;
    const size_t data_size = is_data ? static_cast<size_t>(msg.payload_variant.data.size) : 0U;
    const auto inbound_reason = ::chat::meshtastic::validateMqttProxyInbound(
        mqtt_proxy_settings_, is_data, data_size > 0);
    if (inbound_reason != ::chat::meshtastic::MqttProxyRejectReason::None)
    {
        LORA_LOG("[MQTT] proxy reject reason=%s variant=%u len=%u enabled=%u proxy=%u\n",
                 ::chat::meshtastic::mqttProxyRejectReasonName(inbound_reason),
                 (unsigned)msg.which_payload_variant,
                 (unsigned)data_size,
                 mqtt_proxy_settings_.enabled ? 1U : 0U,
                 mqtt_proxy_settings_.proxy_to_client_enabled ? 1U : 0U);
        return false;
    }

    const auto* data_field = &msg.payload_variant.data;
    auto& scratch = mqtt_downlink_scratch_;
    auto& packet = scratch.packet;
    std::memset(&packet, 0, sizeof(packet));
    std::memset(scratch.channel_id, 0, sizeof(scratch.channel_id));
    std::memset(scratch.gateway_id, 0, sizeof(scratch.gateway_id));
    if (!decodeMqttServiceEnvelope(data_field->bytes, data_field->size,
                                   &packet,
                                   scratch.channel_id, sizeof(scratch.channel_id),
                                   scratch.gateway_id, sizeof(scratch.gateway_id)))
    {
        LORA_LOG("[MQTT] proxy reject reason=%s topic='%s' len=%u\n",
                 ::chat::meshtastic::mqttProxyRejectReasonName(
                     ::chat::meshtastic::MqttProxyRejectReason::DecodeFailed),
                 msg.topic,
                 static_cast<unsigned>(data_field->size));
        return false;
    }
    return injectMqttEnvelope(packet, scratch.channel_id, scratch.gateway_id);
}

bool MtAdapter::queueMqttProxyPublish(const meshtastic_MeshPacket& packet,
                                      const char* channel_id)
{
    if (!::chat::meshtastic::mqttProxyRuntimeEnabled(mqtt_proxy_settings_) ||
        !channel_id || *channel_id == '\0')
    {
        return false;
    }

    auto& scratch = mqtt_publish_scratch_;
    std::memset(&scratch.proxy, 0, sizeof(scratch.proxy));
    std::memset(&scratch.envelope, 0, sizeof(scratch.envelope));
    std::string node_id = mqttNodeIdString();
    auto& env = scratch.envelope;
    env.packet = const_cast<meshtastic_MeshPacket*>(&packet);
    env.channel_id = const_cast<char*>(channel_id);
    env.gateway_id = const_cast<char*>(node_id.c_str());

    meshtastic_MqttClientProxyMessage& proxy = scratch.proxy;
    pb_ostream_t estream = pb_ostream_from_buffer(proxy.payload_variant.data.bytes,
                                                  sizeof(proxy.payload_variant.data.bytes));
    if (!pb_encode(&estream, meshtastic_ServiceEnvelope_fields, &env))
    {
        LORA_LOG("[MQTT] uplink encode fail ch='%s' err=%s\n",
                 channel_id,
                 PB_GET_ERROR(&estream));
        return false;
    }

    proxy.which_payload_variant = meshtastic_MqttClientProxyMessage_data_tag;
    std::string root = mqtt_proxy_settings_.root.empty() ? std::string("msh") : mqtt_proxy_settings_.root;
    std::string topic = root + "/2/e/" + channel_id + "/" + node_id;
    strncpy(proxy.topic, topic.c_str(), sizeof(proxy.topic) - 1);
    proxy.topic[sizeof(proxy.topic) - 1] = '\0';
    proxy.payload_variant.data.size = static_cast<pb_size_t>(estream.bytes_written);
    proxy.retained = false;

    // A broker outage must not let periodic position/telemetry traffic evict
    // user messages already waiting for uplink. Text, control and encrypted
    // traffic remain admissible; lower-priority reports are simply skipped
    // once the bounded queue is full.
    if (mqtt_proxy_queue_.isFull() && isLowPriorityMqttUplink(packet))
    {
        LORA_LOG("[MQTT] uplink queue drop newest priority=low port=%s depth=%u\n",
                 portName(packet.decoded.portnum),
                 static_cast<unsigned>(mqtt_proxy_queue_.size()));
        return false;
    }

    bool dropped = false;
    mqtt_proxy_queue_.pushDropOldest(proxy, &dropped);
    LORA_LOG("[MQTT] uplink queue root='%s' topic='%s' bytes=%u q=%u\n",
             root.c_str(),
             proxy.topic,
             static_cast<unsigned>(proxy.payload_variant.data.size),
             static_cast<unsigned>(mqtt_proxy_queue_.size()));
    if (dropped)
    {
        LORA_LOG("[MQTT] uplink queue dropped oldest depth=%u\n",
                 static_cast<unsigned>(mqtt_proxy_queue_.size()));
    }
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
    auto& scratch = mqtt_publish_scratch_;
    std::fill(scratch.payload.begin(), scratch.payload.end(), 0);
    size_t payload_size = scratch.payload.size();
    if (!parseWirePacket(wire_data, wire_size, &header, scratch.payload.data(), &payload_size))
    {
        return false;
    }

    const bool from_mqtt = (header.flags & PACKET_FLAGS_VIA_MQTT_MASK) != 0;
    const bool is_pki = (header.channel == 0);
    const auto publish_reason = ::chat::meshtastic::validateMqttProxyPublish(
        mqtt_proxy_settings_, channel_index, from_mqtt, is_pki);
    if (publish_reason != ::chat::meshtastic::MqttProxyRejectReason::None)
    {
        if (publish_reason != ::chat::meshtastic::MqttProxyRejectReason::MqttLoopback &&
            (publish_reason != ::chat::meshtastic::MqttProxyRejectReason::ProxyDisabled ||
             mqtt_proxy_settings_.enabled || mqtt_proxy_settings_.proxy_to_client_enabled))
        {
            LORA_LOG("[MQTT] uplink reject reason=%s from=%08lX id=%08lX ch=%u idx=%u "
                     "enabled=%u proxy=%u up=%u/%u\n",
                     ::chat::meshtastic::mqttProxyRejectReasonName(publish_reason),
                     static_cast<unsigned long>(header.from),
                     static_cast<unsigned long>(header.id),
                     static_cast<unsigned>(header.channel),
                     static_cast<unsigned>(channel_index),
                     mqtt_proxy_settings_.enabled ? 1U : 0U,
                     mqtt_proxy_settings_.proxy_to_client_enabled ? 1U : 0U,
                     mqtt_proxy_settings_.primary_uplink_enabled ? 1U : 0U,
                     mqtt_proxy_settings_.secondary_uplink_enabled ? 1U : 0U);
        }
        return false;
    }

    const char* channel_id = is_pki ? "PKI" : mqttChannelIdFor(channel_index);
    if (!channel_id || *channel_id == '\0')
    {
        return false;
    }

    if (mqtt_proxy_settings_.encryption_enabled || is_pki)
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

    PacketHeaderWire header;
    if (size < sizeof(header))
    {
        mt_diag_log("[MT][RX_DROP] reason=parse_fail len=%u\n",
                    static_cast<unsigned>(size));
#if LORA_LOG_ENABLE
        std::string raw_hex = toHex(data, size);
        LORA_LOG("[LORA] RX parse fail len=%u hex=%s\n",
                 (unsigned)size,
                 raw_hex.c_str());
#endif
        return;
    }

    memcpy(&header, data, sizeof(header));
    const size_t wire_payload_size = size - sizeof(header);
    const bool matches_primary_channel = (header.channel == primary_channel_hash_);
    const bool matches_secondary_channel =
        (secondary_psk_len_ > 0 && header.channel == secondary_channel_hash_);
    const bool can_try_pki =
        (header.to == node_id_ && header.to != kBroadcastNodeId && wire_payload_size > 12);
    const bool from_self = (header.from == node_id_);
    if (!from_self && !(matches_primary_channel || matches_secondary_channel) && !can_try_pki)
    {
        char detail[64];
        std::snprintf(detail,
                      sizeof(detail),
                      "primary=0x%02X secondary=0x%02X",
                      static_cast<unsigned>(primary_channel_hash_),
                      static_cast<unsigned>(secondary_channel_hash_));
        mt_diag_dropf(&header, "unknown_channel", "%s", detail);
        LORA_LOG("[LORA] RX unknown channel hash=0x%02X from=%08lX id=%08lX len=%u (early skip)\n",
                 header.channel,
                 (unsigned long)header.from,
                 (unsigned long)header.id,
                 (unsigned)wire_payload_size);
        return;
    }

    // Store raw packet data for protocol detection
    if (size <= sizeof(last_raw_packet_))
    {
        memcpy(last_raw_packet_, data, size);
        last_raw_packet_len_ = size;
        has_pending_raw_packet_ = true;
    }

    if (core_bridge_)
    {
        core_bridge_->onRadioPacket(data,
                                    size,
                                    coreRadioRssi(last_rx_rssi_),
                                    coreRadioSnr(last_rx_snr_));
    }

    // Parse wire packet header
    auto& scratch = rx_scratch_;
    uint8_t* payload = scratch.payload.data();
    size_t payload_size = scratch.payload.size();

    if (!parseWirePacket(data, size, &header, payload, &payload_size))
    {
        mt_diag_log("[MT][RX_DROP] reason=parse_fail len=%u\n",
                    static_cast<unsigned>(size));
#if LORA_LOG_ENABLE
        std::string raw_hex = toHex(data, size);
        LORA_LOG("[LORA] RX parse fail len=%u hex=%s\n",
                 (unsigned)size,
                 raw_hex.c_str());
#endif
        return;
    }

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
#if LORA_LOG_ENABLE
    std::string full_hex = toHex(data, size, size);
    LORA_LOG("[LORA] RX full packet hex: %s\n", full_hex.c_str());
#endif

    // Check for duplicates
    if (dedup_.isDuplicate(header.from, header.id, header.channel))
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
        auto* pending_slot = pending_ack_states_.find(header.id);
        if (header.to == kBroadcastNodeId && pending_slot && !from_is)
        {
            const ChannelId channel_id = pending_slot->meta.channel;
            const uint8_t channel_hash = pending_slot->meta.channel_hash;
            mt_diag_log("[MT][IMPLICIT_ACK] observed self-broadcast id=%08lX relay=%08lX next=%08lX ch=%u\n",
                        static_cast<unsigned long>(header.id),
                        static_cast<unsigned long>(header.relay_node),
                        static_cast<unsigned long>(header.next_hop),
                        static_cast<unsigned>(header.channel));
            pending_ack_states_.erase(header.id);
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
    uint8_t* plaintext = scratch.plaintext.data();
    size_t plaintext_len = scratch.plaintext.size();

    const uint8_t* psk = nullptr;
    size_t psk_len = 0;

    auto& decoded = scratch.decoded;
    std::memset(&decoded, 0, sizeof(decoded));
    ChannelId decoded_channel_id = ChannelId::PRIMARY;
    bool used_pki_transport = false;
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
        uint8_t* candidate_plaintext = scratch.candidate_plaintext.data();
        size_t candidate_plaintext_len = scratch.candidate_plaintext.size();

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
            if (payload_size > scratch.candidate_plaintext.size())
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

        auto& candidate_decoded = scratch.candidate_decoded;
        std::memset(&candidate_decoded, 0, sizeof(candidate_decoded));
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

        if (!(matches_primary_channel || matches_secondary_channel) && !can_try_pki)
        {
            LORA_LOG("[LORA] RX unknown channel hash=0x%02X from=%08lX id=%08lX len=%u (skip decode)\n",
                     header.channel,
                     (unsigned long)header.from,
                     (unsigned long)header.id,
                     (unsigned)payload_size);
        }
        else if (last_drop_reason && std::strcmp(last_drop_reason, "pki_not_ready") == 0)
        {
            LORA_LOG("[LORA] RX PKI drop (not ready) from=%08lX id=%08lX len=%u\n",
                     (unsigned long)header.from,
                     (unsigned long)header.id,
                     (unsigned)payload_size);
            executePkiResync(runtime::MeshtasticPkiResyncCause::LocalPkiNotReady,
                             header.from,
                             header.id,
                             ChannelId::PRIMARY);
        }
        else if (last_drop_reason && std::strcmp(last_drop_reason, "pki_decrypt_fail") == 0)
        {
            if (findPkiNodeKey(header.from))
            {
                executePkiResync(runtime::MeshtasticPkiResyncCause::PeerKeyStale,
                                 header.from,
                                 header.id,
                                 ChannelId::PRIMARY);
                mt_diag_log("[MT][PKI_RESYNC] node=%08lX action=forget_key+request_nodeinfo\n",
                            static_cast<unsigned long>(header.from));
            }
            LORA_LOG("[LORA] RX PKI decrypt fail from=%08lX id=%08lX len=%u\n",
                     (unsigned long)header.from,
                     (unsigned long)header.id,
                     (unsigned)payload_size);
        }
        else if (last_drop_reason && std::strcmp(last_drop_reason, "channel_decrypt_fail") == 0)
        {
            LORA_LOG("[LORA] RX decrypt fail id=%08lX ch=0x%02X psk=%u len=%u\n",
                     (unsigned long)header.id,
                     header.channel,
                     (unsigned)psk_len,
                     (unsigned)payload_size);
        }
        else
        {
            LORA_LOG("[LORA] RX data decode fail id=%08lX len=%u\n",
                     (unsigned long)header.id,
                     (unsigned)payload_size);
        }
        return;
    }

    // Only mark packets as seen after we have successfully identified and decoded them.
    // This avoids poisoning dedup for retries when a packet failed due to stale PKI state.
    dedup_.markSeen(header.from, header.id, header.channel);

    if (plaintext_len > 0)
    {
#if LORA_LOG_ENABLE
        std::string protobuf_hex = toHex(plaintext, plaintext_len, plaintext_len);
        LORA_LOG("[LORA] RX protobuf hex: %s\n", protobuf_hex.c_str());
#endif
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
#if LORA_LOG_ENABLE
            std::string payload_hex = toHex(decoded.payload.bytes, decoded.payload.size, decoded.payload.size);
            LORA_LOG("[LORA] RX data payload hex: %s\n", payload_hex.c_str());
#endif
        }

        bool node_metadata_decoded = false;
        uint8_t channel_index = 0xFF;
        if (header.channel == primary_channel_hash_)
        {
            channel_index = 0;
        }
        else if (header.channel == secondary_channel_hash_)
        {
            channel_index = 1;
        }
        const uint32_t packet_timestamp = static_cast<uint32_t>(time(nullptr));
        auto publish_link_stats = [&](uint32_t node_id)
        {
            float snr = last_rx_snr_;
            float rssi = last_rx_rssi_;
            uint8_t hops_away = computeHopsAway(header.flags);
            if (std::isnan(snr) && std::isnan(rssi) && hops_away == 0xFF)
            {
                return;
            }
            sys::NodeInfoUpdateEvent* event = new sys::NodeInfoUpdateEvent(
                node_id, "", "", snr, rssi, packet_timestamp, 0,
                chat::contacts::kNodeRoleUnknown,
                hops_away, 0, channel_index);
            sys::EventBus::publish(event, 0);
        };

        if (chat::meshtastic::isNodeMetadataPayload(decoded.portnum) &&
            decoded.payload.size > 0)
        {
            chat::meshtastic::NodePayloadDecodeContext context{};
            context.fallback_node_id = header.from;
            context.snr = last_rx_snr_;
            context.rssi = last_rx_rssi_;
            context.timestamp = packet_timestamp;
            context.hops_away = computeHopsAway(header.flags);
            context.channel = channel_index;
            context.via_mqtt =
                (header.flags & chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK) != 0;

            chat::meshtastic::DecodedNodePayload node{};
            if (chat::meshtastic::decodeNodeMetadataPayload(decoded, context, &node))
            {
                LORA_LOG("[LORA] RX node metadata port=%u from %08lX short='%s' long='%s' snr=%.1f\n",
                         static_cast<unsigned>(decoded.portnum),
                         (unsigned long)node.node_id,
                         node.short_name.c_str(),
                         node.long_name.c_str(),
                         node.snr);

                if (node.has_public_key)
                {
                    savePkiNodeKey(node.node_id,
                                   node.public_key.data(),
                                   node.public_key.size());
                    std::string key_fp =
                        toHex(node.public_key.data(), node.public_key.size(), 8);
                    LORA_LOG("[LORA] PKI key stored for %08lX fp=%s\n",
                             (unsigned long)node.node_id, key_fp.c_str());
                    LORA_LOG("[LORA] PKI key updated for %08lX\n",
                             (unsigned long)node.node_id);
                }

                sys::NodeInfoUpdateEvent* event = new sys::NodeInfoUpdateEvent(
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
                    node.has_device_metrics ? &node.device_metrics : nullptr,
                    node.has_public_key_state);
                bool published = sys::EventBus::publish(event, 0);
                if (published)
                {
                    mt_diag_log("[MT][RX_NODEMETA] from=%08lX node=%08lX port=%u published=1\n",
                                static_cast<unsigned long>(header.from),
                                static_cast<unsigned long>(node.node_id),
                                static_cast<unsigned>(decoded.portnum));
                    LORA_LOG("[LORA] node metadata event published node=%08lX\n",
                             (unsigned long)node.node_id);
                }
                else
                {
                    mt_diag_dropf(&header,
                                  "node_metadata_event_drop",
                                  "node=%08lX port=%u pending=%u",
                                  static_cast<unsigned long>(node.node_id),
                                  static_cast<unsigned>(decoded.portnum),
                                  static_cast<unsigned>(sys::EventBus::pendingCount()));
                    LORA_LOG("[LORA] node metadata event dropped node=%08lX pending=%u\n",
                             (unsigned long)node.node_id,
                             static_cast<unsigned>(sys::EventBus::pendingCount()));
                }
                if (node.has_position)
                {
                    publishPositionEvent(node.node_id, node.position);
                }
                node_metadata_decoded = true;
            }
            else
            {
                mt_diag_dropf(&header, "node_metadata_decode_fail");
                LORA_LOG("[LORA] RX node metadata decode fail port=%u from=%08lX\n",
                         static_cast<unsigned>(decoded.portnum),
                         (unsigned long)header.from);
            }
        }

        if (!node_metadata_decoded)
        {
            publish_link_stats(header.from);
        }

        if (decoded.portnum == meshtastic_PortNum_POSITION_APP && decoded.payload.size > 0)
        {
            chat::meshtastic::DecodedPositionPayload position{};
            if (chat::meshtastic::decodePositionPayload(
                    decoded,
                    header.from,
                    packet_timestamp,
                    &position))
            {
                mt_diag_log("[MT][RX_POSITION] from=%08lX id=%08lX payload=%u\n",
                            static_cast<unsigned long>(header.from),
                            static_cast<unsigned long>(header.id),
                            static_cast<unsigned>(decoded.payload.size));
                publishPositionEvent(position.node_id, position.position);
            }
            else
            {
                mt_diag_dropf(&header, "position_decode_fail");
                LORA_LOG("[LORA] RX Position decode fail from=%08lX\n",
                         (unsigned long)header.from);
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
                    meshtastic_Routing_Error routing_reason =
                        meshtastic_Routing_Error_NONE;
                    if (routing.which_variant == meshtastic_Routing_error_reason_tag &&
                        routing.error_reason != meshtastic_Routing_Error_NONE)
                    {
                        ok = false;
                        routing_reason = routing.error_reason;
                    }
                    if (routing.which_variant == meshtastic_Routing_error_reason_tag &&
                        (routing.error_reason == meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY ||
                         routing.error_reason == meshtastic_Routing_Error_NO_CHANNEL))
                    {
                        const ChannelId resync_channel = (header.channel == secondary_channel_hash_)
                                                             ? ChannelId::SECONDARY
                                                             : ChannelId::PRIMARY;
                        const auto cause =
                            (routing.error_reason == meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY)
                                ? runtime::MeshtasticPkiResyncCause::PeerReportsUnknownPubkey
                                : runtime::MeshtasticPkiResyncCause::PeerReportsNoChannel;
                        executePkiResync(cause, header.from, 0, resync_channel);
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
                        new sys::ChatSendResultEvent(
                            decoded.request_id,
                            ok ? chat::MessageStatus::Delivered
                               : chat::MessageStatus::Failed,
                            chat::MeshProtocol::Meshtastic,
                            failureKindFromRoutingError(routing_reason)),
                        0);
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
            if (decodeKeyVerificationMessage(plaintext,
                                             plaintext_len,
                                             &kv,
                                             &scratch.candidate_decoded))
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
        bool is_traceroute_port = (decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP);
        ChannelId channel_id = decoded_channel_id;
        if (header.channel != 0 && header.from != node_id_)
        {
            rememberNodeRuntimeRx(header.from,
                                  channel_id,
                                  rx_meta.origin == chat::RxOrigin::External,
                                  millis());
        }
        if (node_metadata_decoded)
        {
            maybeBroadcastNodeInfoAfterPeerAnnouncement(
                header.from,
                millis(),
                channel_id,
                (header.flags & chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK) != 0);
        }
        if (want_ack_flag && to_us)
        {
            (void)psk;
            (void)psk_len;
            if (enqueueRoutingAckAction(header.from, header.id, header.channel))
            {
                LORA_LOG("[LORA] TX ack queued to=%08lX req=%08lX port=%u\n",
                         (unsigned long)header.from,
                         (unsigned long)header.id,
                         static_cast<unsigned>(decoded.portnum));
            }
            else
            {
                LORA_LOG("[LORA] TX ack queue fail to=%08lX req=%08lX port=%u\n",
                         (unsigned long)header.from,
                         (unsigned long)header.id,
                         static_cast<unsigned>(decoded.portnum));
            }
        }

        runtime::IncomingPacket runtime_packet{};
        runtime_packet.protocol = MeshProtocol::Meshtastic;
        runtime_packet.channel = channel_id;
        runtime_packet.from = header.from;
        runtime_packet.to = header.to;
        runtime_packet.packet_id = header.id;
        runtime_packet.request_id = decoded.request_id;
        runtime_packet.portnum = decoded.portnum;
        runtime_packet.want_response = want_response;
        runtime_packet.encrypted = used_pki_transport || (psk != nullptr && psk_len > 0);
        if (runtime_packet.payload.assign(decoded.payload.bytes, decoded.payload.size))
        {
            runtime_packet.rx_meta = rx_meta;
            protocol_effect_workspace_.primary.clear();
            protocol_runtime_.handleIncomingPacket(runtime_packet,
                                                   buildProtocolRuntimeContext(),
                                                   protocol_effect_workspace_.primary);
            (void)executeProtocolEffects(protocol_effect_workspace_.primary);
        }
        else
        {
            LORA_LOG("[LORA] runtime payload drop port=%u len=%u cap=%u\n",
                     static_cast<unsigned>(decoded.portnum),
                     static_cast<unsigned>(decoded.payload.size),
                     static_cast<unsigned>(runtime_packet.payload.capacity()));
        }

        if (is_nodeinfo_port)
        {
            uint32_t now_ms = millis();
            const uint32_t last_reply_ms = getNodeInfoReplyMs(header.from);
            const auto reply_policy = chat::runtime::resolveMeshtasticNodeInfoReplyPolicy(
                want_response, to_us_or_broadcast, now_ms, last_reply_ms);
            if (reply_policy.should_reply)
            {
                if (enqueueNodeInfoAction(header.from, false, channel_id, true, now_ms))
                {
                    LORA_LOG("[LORA] TX nodeinfo reply queued to=%08lX\n",
                             (unsigned long)header.from);
                }
                else
                {
                    LORA_LOG("[LORA] TX nodeinfo reply queue fail to=%08lX\n",
                             (unsigned long)header.from);
                }
            }
            else if (reply_policy.reason == chat::runtime::MeshtasticReplyReason::Suppressed)
            {
                LORA_LOG("[LORA] TX nodeinfo reply suppressed to=%08lX age=%lu\n",
                         (unsigned long)header.from,
                         (unsigned long)reply_policy.age_ms);
            }
        }

        queueMqttProxyPublishFromWire(data, size, &decoded, channel_id);

        if (!is_text_port && !is_traceroute_port && decoded.payload.size > 0)
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
            incoming.rx_meta = rx_meta;
            IncomingQueuePushReport report{};
            if (app_receive_queue_.push(incoming,
                                        decoded.payload.bytes,
                                        decoded.payload.size,
                                        IncomingQueuePriority::P1User,
                                        &report))
            {
                if (report.dropped_existing)
                {
                    mt_diag_dropf(&header,
                                  "app_queue_pressure",
                                  "evicted_prio=%u depth=%u",
                                  static_cast<unsigned>(report.dropped_priority),
                                  static_cast<unsigned>(app_receive_queue_.size()));
                }
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
                              "app_queue_drop_new",
                              "port=%u payload=%u depth=%u",
                              static_cast<unsigned>(incoming.portnum),
                              static_cast<unsigned>(decoded.payload.size),
                              static_cast<unsigned>(app_receive_queue_.size()));
            }
        }

        if (is_text_port)
        {
            MeshIncomingText incoming;
            char* text_buf = reinterpret_cast<char*>(rx_scratch_.plaintext.data());
            size_t text_len = 0;
            if (decodeTextPayloadToBuffer(decoded, text_buf, rx_scratch_.plaintext.size(), &text_len))
            {
                incoming.from = header.from;
                incoming.to = header.to;
                incoming.msg_id = header.id;
                incoming.channel = decoded_channel_id;
                incoming.timestamp = (rx_meta.rx_timestamp_s != 0) ? rx_meta.rx_timestamp_s : (millis() / 1000U);
                incoming.hop_limit = header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
                incoming.encrypted = used_pki_transport || (psk != nullptr && psk_len > 0);
                incoming.rx_meta = rx_meta;

                IncomingQueuePushReport report{};
                if (receive_queue_.push(incoming,
                                        text_buf,
                                        text_len,
                                        IncomingQueuePriority::P1User,
                                        &report))
                {
                    if (report.dropped_existing)
                    {
                        mt_diag_dropf(&header,
                                      "text_queue_pressure",
                                      "evicted_prio=%u depth=%u",
                                      static_cast<unsigned>(report.dropped_priority),
                                      static_cast<unsigned>(receive_queue_.size()));
                    }
                    mt_diag_log("[MT][RX_TEXT] from=%08lX to=%08lX id=%08lX ch=%u len=%u\n",
                                static_cast<unsigned long>(incoming.from),
                                static_cast<unsigned long>(incoming.to),
                                static_cast<unsigned long>(incoming.msg_id),
                                static_cast<unsigned>(incoming.channel),
                                static_cast<unsigned>(text_len));
                    LORA_LOG("[LORA] RX text from=%08lX id=%08lX ch=%u len=%u\n",
                             (unsigned long)incoming.from,
                             (unsigned long)incoming.msg_id,
                             static_cast<unsigned>(incoming.channel),
                             static_cast<unsigned>(text_len));
                    if (text_len > 0)
                    {
                        LORA_LOG("[LORA] RX text msg='%.*s'\n",
                                 static_cast<int>(text_len),
                                 text_buf);
                    }
                }
                else
                {
                    mt_diag_dropf(&header,
                                  "text_queue_drop_new",
                                  "len=%u depth=%u",
                                  static_cast<unsigned>(text_len),
                                  static_cast<unsigned>(receive_queue_.size()));
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
    uint8_t tx_budget_remaining = kLoRaAirTxBudgetPerTick;

    maybeBroadcastNodeInfo(now);
    const bool protocol_tx_queued =
        processProtocolActionQueue(now, tx_budget_remaining);

    for (std::size_t index = 0; index < pending_ack_states_.capacity();)
    {
        PendingAckSlot* slot = pending_ack_states_.slotAt(index);
        if (!slot || !slot->used)
        {
            ++index;
            continue;
        }

        PendingAckState& pending = slot->meta;
        const uint32_t msg_id = static_cast<uint32_t>(slot->key);
        if (now - pending.last_attempt_ms >= ACK_TIMEOUT_MS)
        {
            if (pending.retransmit_count < MAX_ACK_RETRIES)
            {
                if (retryPendingAck(msg_id, *slot, now, tx_budget_remaining))
                {
                    ++index;
                    continue;
                }
                ++index;
                continue;
            }

            mt_diag_log("[MT][ACK_TIMEOUT] req=%08lX dest=%08lX age_ms=%lu retries=%u\n",
                        static_cast<unsigned long>(msg_id),
                        static_cast<unsigned long>(pending.dest),
                        static_cast<unsigned long>(now - pending.last_attempt_ms),
                        static_cast<unsigned>(pending.retransmit_count));
            LORA_LOG("[LORA] RX ack timeout req=%08lX dest=%08lX retries=%u\n",
                     static_cast<unsigned long>(msg_id),
                     static_cast<unsigned long>(pending.dest),
                     static_cast<unsigned>(pending.retransmit_count));
            last_send_error_ = meshtastic_Routing_Error_MAX_RETRANSMIT;
            emitRoutingResultToPhone(msg_id,
                                     meshtastic_Routing_Error_MAX_RETRANSMIT,
                                     node_id_,
                                     pending.dest,
                                     pending.channel,
                                     pending.channel_hash,
                                     nullptr);
            pending_ack_states_.eraseAt(index);
            continue;
        }
        ++index;
    }

    uint8_t drained = 0;
    uint8_t inspected = 0;
    const uint8_t queue_snapshot =
        static_cast<uint8_t>(std::min<std::size_t>(send_queue_.size(), kPendingSendQueueDepth));
    while (!send_queue_.empty() &&
           drained < kSendQueueDrainPerTick &&
           tx_budget_remaining > 0 &&
           inspected < queue_snapshot)
    {
        PendingSend pending{};
        if (!send_queue_.popOldest(&pending))
        {
            break;
        }
        ++inspected;

        if (min_tx_interval_ms_ > 0 && last_tx_ms_ > 0 &&
            (now - last_tx_ms_) < min_tx_interval_ms_)
        {
            send_queue_.append(pending);
            break;
        }

        if (pending.waiting_for_peer_key &&
            pending.dest != 0 &&
            pending.dest != kBroadcastNodeId &&
            findPkiNodeKey(pending.dest))
        {
            pending.waiting_for_peer_key = false;
        }

        // Check if ready to send
        const uint32_t retry_delay_ms =
            pending.waiting_for_peer_key ? PKI_KEY_EXCHANGE_RETRY_MS
                                         : RETRY_DELAY_MS;
        if (now - pending.last_attempt < retry_delay_ms &&
            (pending.retry_count > 0 || pending.waiting_for_peer_key))
        {
            send_queue_.append(pending);
            continue;
        }

        // Try to send
        if (sendPacket(pending))
        {
            // Success, remove from queue
            last_tx_ms_ = now;
            --tx_budget_remaining;
            ++drained;
        }
        else
        {
            const meshtastic_Routing_Error failure_reason =
                last_send_error_ != meshtastic_Routing_Error_NONE
                    ? last_send_error_
                    : meshtastic_Routing_Error_NO_INTERFACE;
            if (failure_reason == meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY &&
                pending.dest != 0 &&
                pending.dest != kBroadcastNodeId &&
                pending.key_exchange_count < MAX_PKI_KEY_EXCHANGE_RETRIES)
            {
                ++pending.key_exchange_count;
                pending.waiting_for_peer_key = true;
                pending.last_attempt = now;
                executePkiResync(runtime::MeshtasticPkiResyncCause::PeerKeyMissing,
                                 pending.dest,
                                 pending.msg_id,
                                 pending.channel);
                LORA_LOG("[LORA] TX text waiting peer key dest=%08lX id=%08lX exchange=%u\n",
                         static_cast<unsigned long>(pending.dest),
                         static_cast<unsigned long>(pending.msg_id),
                         static_cast<unsigned>(pending.key_exchange_count));
                send_queue_.append(pending);
                continue;
            }

            if (isPermanentQueuedTextFailure(failure_reason))
            {
                const uint8_t channel_hash =
                    (pending.channel == ChannelId::SECONDARY) ? secondary_channel_hash_ : primary_channel_hash_;
                emitRoutingResultToPhone(pending.msg_id,
                                         failure_reason,
                                         node_id_,
                                         pending.dest,
                                         pending.channel,
                                         channel_hash,
                                         nullptr);
                continue;
            }

            // Failed, retry or drop
            pending.retry_count++;
            pending.last_attempt = now;

            if (pending.retry_count > MAX_RETRIES)
            {
                // Max retries reached, drop
                const uint8_t channel_hash =
                    (pending.channel == ChannelId::SECONDARY) ? secondary_channel_hash_ : primary_channel_hash_;
                last_send_error_ = failure_reason;
                emitRoutingResultToPhone(pending.msg_id,
                                         failure_reason,
                                         node_id_,
                                         pending.dest,
                                         pending.channel,
                                         channel_hash,
                                         nullptr);
            }
            else
            {
                // Will retry later
                send_queue_.append(pending);
                break;
            }
        }
    }

    const bool downlink_tx_queued =
        processMqttDownlinkTxQueue(now, tx_budget_remaining);
    if (protocol_tx_queued || downlink_tx_queued)
    {
        LORA_LOG("[LORA] airtime tick_budget protocol=%u downlink=%u remaining=%u\n",
                 protocol_tx_queued ? 1U : 0U,
                 downlink_tx_queued ? 1U : 0U,
                 static_cast<unsigned>(tx_budget_remaining));
    }
}

bool MtAdapter::processMqttDownlinkTxQueue(uint32_t now_ms,
                                           uint8_t& tx_budget_remaining)
{
    bool tx_queued = false;
    uint8_t drained = 0;
    while (!mqtt_downlink_tx_queue_.empty() &&
           drained < kSendQueueDrainPerTick &&
           tx_budget_remaining > 0)
    {
        PendingMqttDownlinkTx* pending = mqtt_downlink_tx_queue_.get(0);
        if (!pending)
        {
            break;
        }

        if (!config_.tx_enabled)
        {
            LORA_LOG("[MQTT][DownlinkTX] drop reason=tx_disabled from=%08lX id=%08lX ch=0x%02X depth=%u\n",
                     static_cast<unsigned long>(pending->from),
                     static_cast<unsigned long>(pending->msg_id),
                     static_cast<unsigned>(pending->channel_hash),
                     static_cast<unsigned>(mqtt_downlink_tx_queue_.size()));
            PendingMqttDownlinkTx discarded{};
            mqtt_downlink_tx_queue_.popOldest(&discarded);
            continue;
        }

        if (min_tx_interval_ms_ > 0 && last_tx_ms_ > 0 &&
            (now_ms - last_tx_ms_) < min_tx_interval_ms_)
        {
            LORA_LOG("[MQTT][DownlinkTX] deferred reason=airtime_budget id=%08lX wait_ms=%lu depth=%u\n",
                     static_cast<unsigned long>(pending->msg_id),
                     static_cast<unsigned long>(min_tx_interval_ms_ - (now_ms - last_tx_ms_)),
                     static_cast<unsigned>(mqtt_downlink_tx_queue_.size()));
            break;
        }

        if (pending->retry_count > 0 &&
            (now_ms - pending->last_attempt_ms) < RETRY_DELAY_MS)
        {
            break;
        }

        pending->last_attempt_ms = now_ms;
        if (transmitWirePacket(pending->wire.data(), pending->wire_size))
        {
            last_tx_ms_ = now_ms;
            --tx_budget_remaining;
            tx_queued = true;
            LORA_LOG("[MQTT][DownlinkTX] sent_to_radio from=%08lX to=%08lX id=%08lX ch=0x%02X len=%u retries=%u age_ms=%lu depth=%u\n",
                     static_cast<unsigned long>(pending->from),
                     static_cast<unsigned long>(pending->to),
                     static_cast<unsigned long>(pending->msg_id),
                     static_cast<unsigned>(pending->channel_hash),
                     static_cast<unsigned>(pending->wire_size),
                     static_cast<unsigned>(pending->retry_count),
                     static_cast<unsigned long>(now_ms - pending->first_seen_ms),
                     static_cast<unsigned>(mqtt_downlink_tx_queue_.size()));
            PendingMqttDownlinkTx discarded{};
            mqtt_downlink_tx_queue_.popOldest(&discarded);
            ++drained;
            continue;
        }

        ++pending->retry_count;
        const char* reason = board_.isRadioOnline() ? "radio_queue_full" : "radio_offline";
        if (pending->retry_count > kMqttDownlinkTxMaxRetries)
        {
            LORA_LOG("[MQTT][DownlinkTX] drop reason=%s from=%08lX to=%08lX id=%08lX ch=0x%02X retries=%u age_ms=%lu depth=%u\n",
                     reason,
                     static_cast<unsigned long>(pending->from),
                     static_cast<unsigned long>(pending->to),
                     static_cast<unsigned long>(pending->msg_id),
                     static_cast<unsigned>(pending->channel_hash),
                     static_cast<unsigned>(pending->retry_count),
                     static_cast<unsigned long>(now_ms - pending->first_seen_ms),
                     static_cast<unsigned>(mqtt_downlink_tx_queue_.size()));
            PendingMqttDownlinkTx discarded{};
            mqtt_downlink_tx_queue_.popOldest(&discarded);
            continue;
        }

        LORA_LOG("[MQTT][DownlinkTX] deferred reason=%s from=%08lX id=%08lX ch=0x%02X retries=%u depth=%u\n",
                 reason,
                 static_cast<unsigned long>(pending->from),
                 static_cast<unsigned long>(pending->msg_id),
                 static_cast<unsigned>(pending->channel_hash),
                 static_cast<unsigned>(pending->retry_count),
                 static_cast<unsigned>(mqtt_downlink_tx_queue_.size()));
        break;
    }
    return tx_queued;
}

bool MtAdapter::sendPacket(const PendingSend& pending)
{
    last_send_error_ = meshtastic_Routing_Error_NONE;
    if (!config_.tx_enabled)
    {
        last_send_error_ = meshtastic_Routing_Error_NO_INTERFACE;
        return false;
    }

    auto& scratch = tx_scratch_;
    auto& data_buffer = scratch.data;
    auto& wire_buffer = scratch.wire;

    // Create Data message payload
    size_t data_size = data_buffer.size();

    NodeId from_node = node_id_;
    if (!encodeTextMessageBytes(pending.channel,
                                pending.text.data(),
                                pending.text_len,
                                from_node,
                                pending.msg_id,
                                pending.dest,
                                data_buffer.data(),
                                &data_size,
                                &scratch.decoded))
    {
        last_send_error_ = meshtastic_Routing_Error_BAD_REQUEST;
        return false;
    }
    auto& decoded = scratch.decoded;
    std::memset(&decoded, 0, sizeof(decoded));
    bool decoded_ok = false;
    {
        pb_istream_t stream = pb_istream_from_buffer(data_buffer.data(), data_size);
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
    // Build a full Meshtastic-compatible wire packet
    size_t wire_size = wire_buffer.size();

    ChannelId channel = pending.channel;
    uint8_t channel_hash =
        (channel == ChannelId::SECONDARY) ? secondary_channel_hash_ : primary_channel_hash_;
    uint8_t hop_limit = config_.hop_limit;
    uint32_t dest = (pending.dest != 0) ? pending.dest : kBroadcastNodeId;
    const bool is_broadcast = dest == kBroadcastNodeId;
    const bool dest_last_seen_via_mqtt =
        !is_broadcast && nodeLastSeenViaMqtt(dest);
    bool track_ack = !is_broadcast && !dest_last_seen_via_mqtt;
    bool air_want_ack = shouldSetAirWantAck(dest, track_ack);

    // Upstream Meshtastic requires PKI for direct unicast traffic on
    // non-infrastructure ports and rejects legacy channel-encrypted DMs.
    const uint8_t* payload = data_buffer.data();
    size_t payload_len = data_size;
    const uint8_t* psk = nullptr;
    size_t psk_len = 0;
    bool use_pki = false;
    if (requirePkiForDirectPort(dest, pending.portnum))
    {
        const bool have_dest_key = findPkiNodeKey(dest) != nullptr;
        if (!pki_ready_ || !have_dest_key)
        {
            const char* reason = !pki_ready_ ? "pki_not_ready" : "pki_key_missing";
            last_send_error_ = !pki_ready_ ? meshtastic_Routing_Error_PKI_FAILED
                                           : meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY;
            mt_diag_log("[MT][TX_BLOCK] id=%08lX dest=%08lX port=%u reason=%s path=PKI\n",
                        static_cast<unsigned long>(pending.msg_id),
                        static_cast<unsigned long>(dest),
                        static_cast<unsigned>(pending.portnum),
                        reason);
            LORA_LOG("[LORA] TX text PKI required but unavailable dest=%08lX\n",
                     (unsigned long)dest);
            return false;
        }
        auto& pki_buffer = scratch.pki;
        size_t pki_len = pki_buffer.size();
        if (!encryptPkiPayload(dest, pending.msg_id, data_buffer.data(), data_size, pki_buffer.data(), &pki_len))
        {
            last_send_error_ = meshtastic_Routing_Error_PKI_FAILED;
            mt_diag_log("[MT][TX_BLOCK] id=%08lX dest=%08lX port=%u reason=pki_encrypt_fail path=PKI\n",
                        static_cast<unsigned long>(pending.msg_id),
                        static_cast<unsigned long>(dest),
                        static_cast<unsigned>(pending.portnum));
            LORA_LOG("[LORA] TX text PKI encrypt failed dest=%08lX\n",
                     (unsigned long)dest);
            return false;
        }
        payload = pki_buffer.data();
        payload_len = pki_len;
        channel_hash = 0; // PKI channel
        track_ack = !dest_last_seen_via_mqtt;
        air_want_ack = shouldSetAirWantAck(dest, track_ack);
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

    const char* channel_name = chat::meshtastic::channelName(config_, channel);
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
                         psk, psk_len, wire_buffer.data(), &wire_size))
    {
        last_send_error_ = meshtastic_Routing_Error_TOO_LARGE;
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
    bool tx_ok = false;
    if (board_.isRadioOnline())
    {
        tx_ok = transmitWirePacket(wire_buffer.data(), wire_size);
    }
    else if (!dest_last_seen_via_mqtt && !is_broadcast)
    {
        last_send_error_ = meshtastic_Routing_Error_NO_INTERFACE;
        return false;
    }
    bool mqtt_ok = false;
    if (tx_ok || dest_last_seen_via_mqtt || is_broadcast)
    {
        mqtt_ok = queueMqttProxyPublishFromWire(wire_buffer.data(),
                                                wire_size,
                                                use_pki
                                                    ? nullptr
                                                    : (decoded_ok ? &decoded
                                                                  : nullptr),
                                                channel);
    }
    const bool ok = (dest_last_seen_via_mqtt || is_broadcast) ? (tx_ok || mqtt_ok) : tx_ok;
    if (!ok)
    {
        last_send_error_ = board_.isRadioOnline()
                               ? meshtastic_Routing_Error_RATE_LIMIT_EXCEEDED
                               : meshtastic_Routing_Error_NO_INTERFACE;
    }
    LORA_LOG("[LORA] TX text id=%08lX ch=%u len=%u ok=%d\n",
             (unsigned long)pending.msg_id,
             static_cast<unsigned>(channel),
             (unsigned)wire_size,
             ok ? 1 : 0);
    if (ok && track_ack)
    {
        trackPendingAck(pending.msg_id, dest, channel, channel_hash, wire_buffer.data(), wire_size);
    }
    else if (ok)
    {
        sys::EventBus::publish(
            new sys::ChatSendResultEvent(pending.msg_id,
                                         chat::MessageStatus::Sent,
                                         chat::MeshProtocol::Meshtastic),
            0);
    }
    return ok;
}

bool MtAdapter::sendNodeInfoTo(uint32_t dest, bool want_response, ChannelId channel)
{
    chat::runtime::EffectiveSelfIdentity identity{};
    chat::runtime::SelfIdentityInput identity_input{};
    identity_input.node_id = node_id_;
    identity_input.configured_long_name = user_long_name_.c_str();
    identity_input.configured_short_name = user_short_name_.c_str();
    identity_input.fallback_long_prefix = "lilygo";
    identity_input.fallback_ble_prefix = "lilygo";
    identity_input.allow_short_hex_fallback = true;
    (void)chat::runtime::resolveEffectiveSelfIdentity(identity_input, &identity);

    char user_id_override[16] = {};
    const app::AppConfig& cfg = app::configFacade().readConfig();
    if (cfg.aprs.self_enable && cfg.aprs.self_callsign[0] != '\0')
    {
        strncpy(user_id_override, cfg.aprs.self_callsign, sizeof(user_id_override) - 1);
        user_id_override[sizeof(user_id_override) - 1] = '\0';
    }

    meshtastic_HardwareModel hw_model = meshtastic_HardwareModel_UNSET;
#if defined(ARDUINO_T_DECK_PRO)
    hw_model = meshtastic_HardwareModel_T_DECK_PRO;
#elif defined(ARDUINO_T_DECK)
    hw_model = meshtastic_HardwareModel_T_DECK;
#elif defined(ARDUINO_LILYGO_TWATCH_S3)
    hw_model = meshtastic_HardwareModel_T_WATCH_S3;
#elif defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280) || \
    defined(ARDUINO_LILYGO_LORA_LR1121)
    hw_model = meshtastic_HardwareModel_T_LORA_PAGER;
#endif

    chat::runtime::MeshtasticAnnouncementRequest request{};
    request.identity = identity;
    request.mesh_config = config_;
    request.channel = channel;
    request.packet_id = next_packet_id_++;
    request.dest_node = dest;
    request.hop_limit = config_.hop_limit;
    request.want_response = want_response;
    request.want_ack = want_response && (dest != kBroadcastNodeId);
    request.user_id_override = user_id_override[0] != '\0' ? user_id_override : nullptr;
    request.hw_model = hw_model;
    request.mac_addr = mac_addr_;
    if (pki_ready_)
    {
        request.public_key = pki_public_key_.data();
        request.public_key_len = pki_public_key_.size();
    }

    if (!chat::runtime::MeshtasticSelfAnnouncementCore::buildNodeInfoPacket(
            request, &node_info_packet_scratch_))
    {
        return false;
    }

    const char* logged_user_id = request.user_id_override ? request.user_id_override : "";
    char default_user_id[16] = {};
    if (!request.user_id_override)
    {
        snprintf(default_user_id, sizeof(default_user_id), "!%08lX", (unsigned long)node_id_);
        logged_user_id = default_user_id;
    }
    LORA_LOG("[LORA] NodeInfo user_id=%s short=%s long=%s\n",
             logged_user_id, identity.short_name, identity.long_name);
    LORA_LOG("[LORA] TX nodeinfo wire ch=0x%02X idx=%u hop=%u wire=%u\n",
             node_info_packet_scratch_.channel_hash,
             (unsigned)(channel == ChannelId::SECONDARY ? 1 : 0),
             request.hop_limit,
             (unsigned)node_info_packet_scratch_.wire_size);
    if (!board_.isRadioOnline())
    {
        return false;
    }

    bool ok = transmitWirePacket(node_info_packet_scratch_.wire,
                                 node_info_packet_scratch_.wire_size);
    if (ok && dest == kBroadcastNodeId)
    {
        last_nodeinfo_ms_ = millis();
    }
    LORA_LOG("[LORA] TX nodeinfo id=%08lX len=%u ok=%d\n",
             (unsigned long)request.packet_id,
             (unsigned)node_info_packet_scratch_.wire_size,
             ok ? 1 : 0);
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
        if (enqueueNodeInfoAction(kBroadcastNodeId, false, ChannelId::PRIMARY))
        {
            LORA_LOG("[LORA] TX nodeinfo periodic queued\n");
        }
    }
}

void MtAdapter::maybeBroadcastNodeInfoAfterPeerAnnouncement(uint32_t from_node,
                                                            uint32_t now_ms,
                                                            ChannelId channel,
                                                            bool from_mqtt)
{
    const auto policy = chat::runtime::resolveMeshtasticNodeInfoReannouncePolicy(
        ready_,
        config_.tx_enabled,
        from_mqtt,
        from_node,
        node_id_,
        now_ms,
        last_nodeinfo_ms_);
    if (!policy.should_announce)
    {
        if (policy.reason == chat::runtime::MeshtasticNodeInfoReannounceReason::Suppressed)
        {
            LORA_LOG("[LORA] TX nodeinfo announce suppressed from=%08lX age=%lu\n",
                     (unsigned long)from_node,
                     (unsigned long)policy.age_ms);
        }
        return;
    }

    if (enqueueNodeInfoAction(kBroadcastNodeId, false, channel))
    {
        LORA_LOG("[LORA] TX nodeinfo announce queued after peer from=%08lX ch=%u\n",
                 (unsigned long)from_node,
                 static_cast<unsigned>(channel));
    }
    else
    {
        LORA_LOG("[LORA] TX nodeinfo announce queue fail after peer from=%08lX ch=%u\n",
                 (unsigned long)from_node,
                 static_cast<unsigned>(channel));
    }
}

void MtAdapter::configureRadio()
{
    if (!board_.isRadioOnline())
    {
        ready_ = false;
        return;
    }

    const chat::meshtastic::RadioConfig radio =
        chat::meshtastic::deriveRadioConfig(config_);
    if (radio.using_preset != config_.use_preset ||
        config_.modem_preset != static_cast<uint8_t>(radio.modem_preset))
    {
        config_.use_preset = radio.using_preset;
        config_.modem_preset = static_cast<uint8_t>(radio.modem_preset);
    }
    config_.tx_power = radio.tx_power_dbm;

    radio_freq_hz_ = static_cast<uint32_t>(std::lround(radio.freq_mhz * 1000000.0f));
    radio_bw_hz_ = static_cast<uint32_t>(std::lround(radio.bw_khz * 1000.0f));
    radio_sf_ = radio.sf;
    radio_cr_ = radio.cr_denom;

#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280) || \
    defined(ARDUINO_LILYGO_LORA_LR1121)
    board_.configureLoraRadio(radio.freq_mhz,
                              radio.bw_khz,
                              radio.sf,
                              radio.cr_denom,
                              radio.tx_power_dbm,
                              radio.preamble_len,
                              radio.sync_word,
                              radio.crc_len);
#endif

    ready_ = true;
    // Suppress auto NodeInfo broadcast at boot; wait for interval to elapse.
    last_nodeinfo_ms_ = millis();
    LORA_LOG("[LORA] adapter ready, node_id=%08lX\n", (unsigned long)node_id_);
    LORA_LOG("[LORA] radio config region=%u preset=%u use_preset=%u freq=%.3fMHz sf=%u bw=%.1f cr=4/%u tx=%d ch=%lu sync=0x%02X preamble=%u tx_en=%u\n",
             static_cast<unsigned>(radio.region_code),
             static_cast<unsigned>(radio.modem_preset),
             radio.using_preset ? 1U : 0U,
             radio.freq_mhz,
             radio.sf,
             radio.bw_khz,
             radio.cr_denom,
             static_cast<int>(radio.tx_power_dbm),
             static_cast<unsigned long>(radio.channel_slot),
             radio.sync_word,
             radio.preamble_len,
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
        primary_psk_len_ = chat::normalizeMeshtasticChannelKeyLen(config_.primary_key,
                                                                  sizeof(config_.primary_key),
                                                                  config_.primary_key_len);
        memcpy(primary_psk_, config_.primary_key, primary_psk_len_);
    }

    if (isZeroKey(config_.secondary_key, sizeof(config_.secondary_key)))
    {
        secondary_psk_len_ = 0;
        memset(secondary_psk_, 0, sizeof(secondary_psk_));
    }
    else
    {
        secondary_psk_len_ = chat::normalizeMeshtasticChannelKeyLen(config_.secondary_key,
                                                                    sizeof(config_.secondary_key),
                                                                    config_.secondary_key_len);
        memcpy(secondary_psk_, config_.secondary_key, secondary_psk_len_);
    }

    const char* primary_name = chat::meshtastic::primaryChannelName(config_);
    const char* secondary_name = chat::meshtastic::secondaryChannelName(config_);
    primary_channel_hash_ = computeChannelHash(primary_name, primary_psk_, primary_psk_len_);
    secondary_channel_hash_ =
        computeChannelHash(secondary_name,
                           (secondary_psk_len_ > 0) ? secondary_psk_ : nullptr,
                           secondary_psk_len_);
    LORA_LOG("[LORA] channel primary='%s' hash=0x%02X psk=%u key=<redacted>\n",
             primary_name,
             primary_channel_hash_,
             (unsigned)primary_psk_len_);
    LORA_LOG("[LORA] channel secondary='%s' hash=0x%02X psk=%u key=<redacted>\n",
             secondary_name,
             secondary_channel_hash_,
             (unsigned)secondary_psk_len_);
}

void MtAdapter::startRadioReceive()
{
    if (!board_.isRadioOnline())
    {
        app::AppTasks::requestRadioReceiveRestart();
        return;
    }
#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280) || \
    defined(ARDUINO_LILYGO_LORA_LR1121)
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
    if (!wire_data || wire_size == 0 || wire_size > MAX_PACKET_SIZE)
    {
        return false;
    }
    if (!board_.isRadioOnline())
    {
        return false;
    }
    const bool queued = app::AppTasks::enqueueRadioTransmit(wire_data, wire_size);
    LORA_LOG("[LORA] TX enqueue len=%u ok=%u\n",
             static_cast<unsigned>(wire_size),
             queued ? 1U : 0U);
    return queued;
}

bool MtAdapter::enqueueProtocolAction(const PendingProtocolAction& action)
{
    if (action.type == PendingProtocolActionType::None)
    {
        return false;
    }
    if (protocol_action_count_ >= protocol_action_queue_.size())
    {
        LORA_LOG("[LORA] protocol action queue full type=%u peer=%08lX req=%08lX\n",
                 static_cast<unsigned>(action.type),
                 static_cast<unsigned long>(action.peer),
                 static_cast<unsigned long>(action.request_id));
        return false;
    }
    const size_t index =
        (protocol_action_head_ + protocol_action_count_) % protocol_action_queue_.size();
    protocol_action_queue_[index] = action;
    ++protocol_action_count_;
    return true;
}

bool MtAdapter::enqueueNodeInfoAction(NodeId peer, bool want_response, ChannelId channel,
                                      bool mark_reply, uint32_t reply_ms)
{
    for (size_t i = 0; i < protocol_action_count_; ++i)
    {
        const size_t index = (protocol_action_head_ + i) % protocol_action_queue_.size();
        PendingProtocolAction& action = protocol_action_queue_[index];
        if (action.type == PendingProtocolActionType::SendNodeInfo &&
            action.peer == peer &&
            action.channel == channel)
        {
            action.want_response = action.want_response || want_response;
            if (mark_reply)
            {
                action.mark_nodeinfo_reply = true;
                action.nodeinfo_reply_ms = reply_ms;
            }
            return true;
        }
    }
    PendingProtocolAction action{};
    action.type = PendingProtocolActionType::SendNodeInfo;
    action.peer = peer;
    action.want_response = want_response;
    action.channel = channel;
    action.mark_nodeinfo_reply = mark_reply;
    action.nodeinfo_reply_ms = reply_ms;
    return enqueueProtocolAction(action);
}

bool MtAdapter::enqueueRoutingAckAction(NodeId peer, MessageId request_id, uint8_t channel_hash)
{
    PendingProtocolAction action{};
    action.type = PendingProtocolActionType::SendRoutingAck;
    action.peer = peer;
    action.request_id = request_id;
    action.channel_hash = channel_hash;
    return enqueueProtocolAction(action);
}

bool MtAdapter::enqueueRoutingErrorAction(NodeId peer, MessageId request_id, ChannelId channel,
                                          meshtastic_Routing_Error reason)
{
    PendingProtocolAction action{};
    action.type = PendingProtocolActionType::SendRoutingError;
    action.peer = peer;
    action.request_id = request_id;
    action.channel = channel;
    action.routing_error = reason;
    return enqueueProtocolAction(action);
}

bool MtAdapter::enqueueSendPacketAction(const runtime::SendPacketEffect& packet)
{
    PendingProtocolAction action{};
    action.type = PendingProtocolActionType::SendPacket;
    action.packet = packet;
    action.peer = packet.dest;
    action.request_id = packet.request_id;
    action.channel = packet.channel;
    return enqueueProtocolAction(action);
}

bool MtAdapter::popProtocolAction()
{
    if (protocol_action_count_ == 0)
    {
        return false;
    }
    protocol_action_queue_[protocol_action_head_] = PendingProtocolAction{};
    protocol_action_head_ = (protocol_action_head_ + 1) % protocol_action_queue_.size();
    --protocol_action_count_;
    return true;
}

bool MtAdapter::resolvePskForChannelHash(uint8_t channel_hash,
                                         const uint8_t** out_psk,
                                         size_t* out_psk_len) const
{
    if (!out_psk || !out_psk_len)
    {
        return false;
    }
    *out_psk = nullptr;
    *out_psk_len = 0;
    if (channel_hash == 0)
    {
        return true;
    }
    if (channel_hash == primary_channel_hash_)
    {
        *out_psk = primary_psk_;
        *out_psk_len = primary_psk_len_;
        return true;
    }
    if (channel_hash == secondary_channel_hash_)
    {
        *out_psk = secondary_psk_;
        *out_psk_len = secondary_psk_len_;
        return true;
    }
    return false;
}

bool MtAdapter::executeProtocolAction(const PendingProtocolAction& action)
{
    switch (action.type)
    {
    case PendingProtocolActionType::SendNodeInfo:
        return sendNodeInfoTo(static_cast<uint32_t>(action.peer),
                              action.want_response,
                              action.channel);
    case PendingProtocolActionType::SendRoutingAck:
    {
        const uint8_t* psk = nullptr;
        size_t psk_len = 0;
        if (!resolvePskForChannelHash(action.channel_hash, &psk, &psk_len))
        {
            return false;
        }
        return sendRoutingAck(static_cast<uint32_t>(action.peer),
                              action.request_id,
                              action.channel_hash,
                              psk,
                              psk_len);
    }
    case PendingProtocolActionType::SendRoutingError:
    {
        const bool use_secondary = action.channel == ChannelId::SECONDARY;
        const uint8_t channel_hash = use_secondary ? secondary_channel_hash_ : primary_channel_hash_;
        const uint8_t* psk = use_secondary ? secondary_psk_ : primary_psk_;
        const size_t psk_len = use_secondary ? secondary_psk_len_ : primary_psk_len_;
        return sendRoutingError(static_cast<uint32_t>(action.peer),
                                action.request_id,
                                channel_hash,
                                psk,
                                psk_len,
                                action.routing_error);
    }
    case PendingProtocolActionType::SendPacket:
        return sendProtocolPacketEffect(action.packet);
    case PendingProtocolActionType::None:
    default:
        return true;
    }
}

bool MtAdapter::processProtocolActionQueue(uint32_t now_ms,
                                           uint8_t& tx_budget_remaining)
{
    bool tx_queued = false;
    size_t processed = 0;
    while (protocol_action_count_ > 0 &&
           processed < protocol_action_queue_.size() &&
           tx_budget_remaining > 0)
    {
        PendingProtocolAction& action = protocol_action_queue_[protocol_action_head_];
        if (min_tx_interval_ms_ > 0 && last_tx_ms_ > 0 &&
            (now_ms - last_tx_ms_) < min_tx_interval_ms_)
        {
            break;
        }
        if (action.retry_count > 0 && (now_ms - action.last_attempt) < RETRY_DELAY_MS)
        {
            break;
        }
        if (executeProtocolAction(action))
        {
            if (action.mark_nodeinfo_reply)
            {
                setNodeInfoReplyMs(action.peer, action.nodeinfo_reply_ms);
            }
            last_tx_ms_ = now_ms;
            --tx_budget_remaining;
            tx_queued = true;
            popProtocolAction();
        }
        else
        {
            ++action.retry_count;
            action.last_attempt = now_ms;
            if (action.retry_count > MAX_RETRIES)
            {
                LORA_LOG("[LORA] protocol action drop type=%u peer=%08lX req=%08lX\n",
                         static_cast<unsigned>(action.type),
                         static_cast<unsigned long>(action.peer),
                         static_cast<unsigned long>(action.request_id));
                popProtocolAction();
            }
            break;
        }
        ++processed;
    }
    return tx_queued;
}

bool MtAdapter::sendChannelAppDataViaCore(uint32_t portnum,
                                          const uint8_t* payload,
                                          size_t len,
                                          uint32_t dest_node,
                                          bool effective_want_response,
                                          MessageId msg_id,
                                          uint8_t channel_hash,
                                          const uint8_t* psk,
                                          size_t psk_len,
                                          uint8_t hop_limit,
                                          bool air_want_ack,
                                          uint8_t* out_wire_data,
                                          size_t* inout_wire_size)
{
    if (!core_bridge_ || !payload || len == 0 || !out_wire_data || !inout_wire_size)
    {
        return false;
    }

    ::mesh::DirectMessageCommand command{
        ::mesh::NodeId{dest_node},
        ::mesh::ByteView{payload, len},
        effective_want_response};
    command.from = ::mesh::NodeId{node_id_};
    command.application_port = portnum;
    command.packet_id = msg_id;
    command.channel_hash = channel_hash;
    command.channel_key = ::mesh::ByteView{psk, psk_len};
    command.hop_limit = hop_limit;
    command.has_air_want_ack = true;
    command.air_want_ack = air_want_ack;
    command.include_payload_dest = false;
    command.require_local_identity = false;
    command.require_peer_key = false;

    auto sent = core_bridge_->sendDirect(command);
    if (!sent.ok)
    {
        mt_diag_log("[MT][TX_BLOCK] id=%08lX dest=%08lX port=%u reason=core_send_fail code=%u path=CHANNEL\n",
                    static_cast<unsigned long>(msg_id),
                    static_cast<unsigned long>(dest_node),
                    static_cast<unsigned>(portnum),
                    static_cast<unsigned>(sent.failure));
        return false;
    }
    return core_bridge_->copyLastSentPacket(out_wire_data, *inout_wire_size, *inout_wire_size);
}

void MtAdapter::trackPendingAck(uint32_t msg_id, uint32_t dest, ChannelId channel, uint8_t channel_hash,
                                const uint8_t* wire_data, size_t wire_size)
{
    if (!wire_data || wire_size == 0)
    {
        return;
    }

    PendingAckState state;
    state.dest = dest;
    state.channel = channel;
    state.channel_hash = channel_hash;
    state.last_attempt_ms = millis();
    state.retransmit_count = 0;

    ::chat::meshtastic::PendingWirePushReport report{};
    auto* slot = pending_ack_states_.upsert(msg_id,
                                            ::chat::meshtastic::PendingWirePriority::P0,
                                            wire_data,
                                            wire_size,
                                            state,
                                            &report);
    if (!slot)
    {
        mt_diag_log("[MT][ACK_TRACK_DROP] req=%08lX dest=%08lX wire=%u max=%u depth=%u\n",
                    static_cast<unsigned long>(msg_id),
                    static_cast<unsigned long>(dest),
                    static_cast<unsigned>(wire_size),
                    static_cast<unsigned>(pending_ack_states_.maxWireLen()),
                    static_cast<unsigned>(pending_ack_states_.size()));
        LORA_LOG("[LORA] TX ack track drop req=%08lX dest=%08lX wire=%u depth=%u\n",
                 static_cast<unsigned long>(msg_id),
                 static_cast<unsigned long>(dest),
                 static_cast<unsigned>(wire_size),
                 static_cast<unsigned>(pending_ack_states_.size()));
        last_send_error_ = meshtastic_Routing_Error_MAX_RETRANSMIT;
        emitRoutingResultToPhone(msg_id,
                                 meshtastic_Routing_Error_MAX_RETRANSMIT,
                                 node_id_,
                                 dest,
                                 channel,
                                 channel_hash,
                                 nullptr);
        return;
    }

    if (report.result == ::chat::meshtastic::PendingWirePushReport::Result::DroppedExisting)
    {
        mt_diag_log("[MT][ACK_TRACK_DROP_OLD] dropped_key=%016llX dropped_priority=%u req=%08lX depth=%u\n",
                    static_cast<unsigned long long>(report.dropped_key),
                    static_cast<unsigned>(report.dropped_priority),
                    static_cast<unsigned long>(msg_id),
                    static_cast<unsigned>(pending_ack_states_.size()));
    }
}

void MtAdapter::clearPendingAck(uint32_t msg_id)
{
    pending_ack_states_.erase(msg_id);
}

bool MtAdapter::retryPendingAck(uint32_t msg_id,
                                PendingAckSlot& slot,
                                uint32_t now_ms,
                                uint8_t& tx_budget_remaining)
{
    if (tx_budget_remaining == 0)
    {
        return false;
    }
    if (min_tx_interval_ms_ > 0 && last_tx_ms_ > 0 &&
        (now_ms - last_tx_ms_) < min_tx_interval_ms_)
    {
        return false;
    }
    PendingAckState& pending = slot.meta;
    pending.last_attempt_ms = now_ms;
    ++pending.retransmit_count;
    mt_diag_log("[MT][RETX] req=%08lX dest=%08lX try=%u len=%u\n",
                static_cast<unsigned long>(msg_id),
                static_cast<unsigned long>(pending.dest),
                static_cast<unsigned>(pending.retransmit_count),
                static_cast<unsigned>(slot.wire_size));
    LORA_LOG("[LORA] TX retry req=%08lX dest=%08lX try=%u len=%u\n",
             static_cast<unsigned long>(msg_id),
             static_cast<unsigned long>(pending.dest),
             static_cast<unsigned>(pending.retransmit_count),
             static_cast<unsigned>(slot.wire_size));
    if (transmitWirePacket(slot.wire.data(), slot.wire_size))
    {
        last_tx_ms_ = pending.last_attempt_ms;
        --tx_budget_remaining;
        return true;
    }
    LORA_LOG("[LORA] TX retry immediate fail req=%08lX try=%u\n",
             static_cast<unsigned long>(msg_id),
             static_cast<unsigned>(pending.retransmit_count));
    return false;
}

bool MtAdapter::initPkiKeys()
{
    ::platform::esp::arduino_common::mesh::EspPreferencesLocalIdentityStore store;
    ::mesh::LocalIdentity stored_identity{};
    auto loaded = store.load(stored_identity);
    bool have_keys = loaded.ok &&
                     !isZeroKey(stored_identity.private_key,
                                sizeof(stored_identity.private_key));
    if (have_keys)
    {
        memcpy(pki_public_key_.data(), stored_identity.public_key, pki_public_key_.size());
        memcpy(pki_private_key_.data(), stored_identity.private_key, pki_private_key_.size());
        std::string loaded_fp = toHex(pki_public_key_.data(), pki_public_key_.size(), 8);
        LORA_LOG("[LORA] PKI keys loaded pub fp=%s\n", loaded_fp.c_str());
    }
    else
    {
        LORA_LOG("[LORA] PKI stored identity unavailable status=%u\n",
                 static_cast<unsigned>(loaded.failure));
    }

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
            ::mesh::LocalIdentity generated{};
            memcpy(generated.public_key, pki_public_key_.data(), sizeof(generated.public_key));
            memcpy(generated.private_key, pki_private_key_.data(), sizeof(generated.private_key));
            generated.valid = true;
            auto saved = store.save(generated);
            if (!saved.ok)
            {
                LORA_LOG("[LORA] PKI key persist failed status=%u\n",
                         static_cast<unsigned>(saved.failure));
            }
        }
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

MtAdapter::PkiNodeKeyEntry* MtAdapter::findCachedPkiNodeKey(uint32_t node_id)
{
    for (auto& entry : pki_node_keys_)
    {
        if (entry.used && entry.node_id == node_id)
        {
            return &entry;
        }
    }
    return nullptr;
}

const MtAdapter::PkiNodeKeyEntry* MtAdapter::findCachedPkiNodeKey(uint32_t node_id) const
{
    for (const auto& entry : pki_node_keys_)
    {
        if (entry.used && entry.node_id == node_id)
        {
            return &entry;
        }
    }
    return nullptr;
}

MtAdapter::PkiNodeKeyEntry* MtAdapter::findPkiNodeKey(uint32_t node_id)
{
    if (auto* entry = findCachedPkiNodeKey(node_id))
    {
        return entry;
    }
    if (loadPkiNodeKeyFromDirectory(node_id))
    {
        return findCachedPkiNodeKey(node_id);
    }
    return nullptr;
}

const MtAdapter::PkiNodeKeyEntry* MtAdapter::findPkiNodeKey(uint32_t node_id) const
{
    return findCachedPkiNodeKey(node_id);
}

MtAdapter::PkiNodeKeyEntry* MtAdapter::upsertPkiNodeKey(uint32_t node_id,
                                                        const uint8_t* key,
                                                        uint32_t last_seen_s,
                                                        bool* out_changed,
                                                        bool* out_evicted)
{
    if (out_changed)
    {
        *out_changed = false;
    }
    if (out_evicted)
    {
        *out_evicted = false;
    }
    if (node_id == 0 || !key)
    {
        return nullptr;
    }

    const uint32_t seen = last_seen_s != 0
                              ? last_seen_s
                              : static_cast<uint32_t>(time(nullptr));
    if (auto* existing = findCachedPkiNodeKey(node_id))
    {
        const bool changed = memcmp(existing->key.data(), key, existing->key.size()) != 0;
        if (changed)
        {
            memcpy(existing->key.data(), key, existing->key.size());
        }
        existing->last_seen_s = seen;
        if (out_changed)
        {
            *out_changed = changed;
        }
        return existing;
    }

    PkiNodeKeyEntry* slot = nullptr;
    for (auto& entry : pki_node_keys_)
    {
        if (!entry.used)
        {
            slot = &entry;
            break;
        }
    }
    if (!slot)
    {
        slot = &pki_node_keys_[0];
        for (auto& entry : pki_node_keys_)
        {
            if (entry.last_seen_s < slot->last_seen_s)
            {
                slot = &entry;
            }
        }
        if (out_evicted)
        {
            *out_evicted = true;
        }
    }

    *slot = PkiNodeKeyEntry{};
    slot->used = true;
    slot->node_id = node_id;
    slot->last_seen_s = seen;
    memcpy(slot->key.data(), key, slot->key.size());
    if (out_changed)
    {
        *out_changed = true;
    }
    return slot;
}

void MtAdapter::clearPkiNodeKeys()
{
    for (auto& entry : pki_node_keys_)
    {
        entry = PkiNodeKeyEntry{};
    }
}

bool MtAdapter::erasePkiNodeKey(uint32_t node_id)
{
    if (auto* entry = findPkiNodeKey(node_id))
    {
        *entry = PkiNodeKeyEntry{};
        return true;
    }
    return false;
}

size_t MtAdapter::pkiNodeKeyCount() const
{
    size_t count = 0;
    for (const auto& entry : pki_node_keys_)
    {
        if (entry.used)
        {
            ++count;
        }
    }
    return count;
}

MtAdapter::NodeRuntimeEntry* MtAdapter::findNodeRuntime(uint32_t node_id)
{
    for (auto& entry : node_runtime_)
    {
        if (entry.used && entry.node_id == node_id)
        {
            return &entry;
        }
    }
    return nullptr;
}

const MtAdapter::NodeRuntimeEntry* MtAdapter::findNodeRuntime(uint32_t node_id) const
{
    for (const auto& entry : node_runtime_)
    {
        if (entry.used && entry.node_id == node_id)
        {
            return &entry;
        }
    }
    return nullptr;
}

MtAdapter::NodeRuntimeEntry* MtAdapter::upsertNodeRuntime(uint32_t node_id, uint32_t now_ms)
{
    if (node_id == 0)
    {
        return nullptr;
    }
    if (auto* existing = findNodeRuntime(node_id))
    {
        existing->last_touch_ms = now_ms;
        return existing;
    }

    NodeRuntimeEntry* slot = nullptr;
    for (auto& entry : node_runtime_)
    {
        if (!entry.used)
        {
            slot = &entry;
            break;
        }
    }
    if (!slot)
    {
        slot = &node_runtime_[0];
        for (auto& entry : node_runtime_)
        {
            if (entry.last_touch_ms < slot->last_touch_ms)
            {
                slot = &entry;
            }
        }
    }

    *slot = NodeRuntimeEntry{};
    slot->used = true;
    slot->node_id = node_id;
    slot->last_touch_ms = now_ms;
    return slot;
}

void MtAdapter::eraseNodeRuntime(uint32_t node_id)
{
    if (auto* entry = findNodeRuntime(node_id))
    {
        *entry = NodeRuntimeEntry{};
    }
}

bool MtAdapter::getNodeLastChannel(uint32_t node_id, ChannelId* out) const
{
    const auto* entry = findNodeRuntime(node_id);
    if (!entry || !entry->has_last_channel)
    {
        return false;
    }
    if (out)
    {
        *out = entry->last_channel;
    }
    return true;
}

void MtAdapter::rememberNodeLastChannel(uint32_t node_id, ChannelId channel, uint32_t now_ms)
{
    if (auto* entry = upsertNodeRuntime(node_id, now_ms))
    {
        entry->last_channel = channel;
        entry->has_last_channel = true;
    }
}

void MtAdapter::rememberNodeRuntimeRx(uint32_t node_id,
                                      ChannelId channel,
                                      bool via_mqtt,
                                      uint32_t now_ms)
{
    if (auto* entry = upsertNodeRuntime(node_id, now_ms))
    {
        entry->last_channel = channel;
        entry->has_last_channel = true;
        entry->last_seen_via_mqtt = via_mqtt;
    }
}

bool MtAdapter::nodeLastSeenViaMqtt(uint32_t node_id) const
{
    const auto* entry = findNodeRuntime(node_id);
    return entry && entry->last_seen_via_mqtt;
}

uint32_t MtAdapter::getNodeInfoReplyMs(uint32_t node_id) const
{
    const auto* entry = findNodeRuntime(node_id);
    return entry ? entry->nodeinfo_reply_ms : 0;
}

void MtAdapter::setNodeInfoReplyMs(uint32_t node_id, uint32_t now_ms)
{
    if (auto* entry = upsertNodeRuntime(node_id, now_ms))
    {
        entry->nodeinfo_reply_ms = now_ms;
    }
}

void MtAdapter::loadPkiNodeKeys()
{
    if (!peer_directory_)
    {
        LORA_LOG("[LORA] PKI peer directory unavailable, hot cache empty\n");
        return;
    }

    clearPkiNodeKeys();
    size_t count = 0;
    const auto loaded =
        peer_directory_->loadRecent(MeshProtocol::Meshtastic,
                                    pki_directory_load_entries_.data(),
                                    pki_directory_load_entries_.size(),
                                    &count);
    if (!loaded.succeeded())
    {
        LORA_LOG("[LORA] PKI peer directory load failed status=%u\n",
                 static_cast<unsigned>(loaded.code));
        return;
    }

    size_t loaded_keys = 0;
    for (size_t index = 0; index < count; ++index)
    {
        const MeshPeerRecord& peer = pki_directory_load_entries_[index];
        if (peer.identity.kind != MeshPeerIdentityKind::NodeId ||
            peer.identity.node_id == 0 ||
            !peer.meshtastic.has_public_key)
        {
            continue;
        }
        bool evicted = false;
        (void)upsertPkiNodeKey(peer.identity.node_id,
                               peer.meshtastic.public_key,
                               peer.last_seen_s,
                               nullptr,
                               &evicted);
        LORA_LOG("[LORA] PKI key loaded for %08lX\n",
                 static_cast<unsigned long>(peer.identity.node_id));
        ++loaded_keys;
        if (evicted)
        {
            LORA_LOG("[LORA] PKI key load evicted oldest cap=%u\n",
                     static_cast<unsigned>(pki_node_keys_.size()));
        }
    }
    LORA_LOG("[LORA] PKI hot keys loaded=%u directory=mesh_peer_directory\n",
             static_cast<unsigned>(loaded_keys));
}

bool MtAdapter::readPkiNodeKeyFromDirectory(uint32_t node_id,
                                            uint8_t out_key[32],
                                            uint32_t* out_last_seen_s) const
{
    if (!peer_directory_ || node_id == 0 || node_id == kBroadcastNodeId || !out_key)
    {
        return false;
    }

    MeshPeerRecord record{};
    const auto status =
        peer_directory_->findByNodeId(MeshProtocol::Meshtastic, node_id, record);
    if (!status.succeeded() ||
        !record.meshtastic.has_public_key ||
        !meshPeerHasNonZeroBytes(record.meshtastic.public_key,
                                 sizeof(record.meshtastic.public_key)))
    {
        return false;
    }

    memcpy(out_key, record.meshtastic.public_key, sizeof(record.meshtastic.public_key));
    if (out_last_seen_s)
    {
        *out_last_seen_s = record.last_seen_s;
    }
    return true;
}

bool MtAdapter::loadPkiNodeKeyFromDirectory(uint32_t node_id)
{
    uint8_t key[32] = {};
    uint32_t last_seen_s = 0;
    if (!readPkiNodeKeyFromDirectory(node_id, key, &last_seen_s))
    {
        return false;
    }
    bool evicted = false;
    if (!upsertPkiNodeKey(node_id, key, last_seen_s, nullptr, &evicted))
    {
        return false;
    }
    LORA_LOG("[LORA] PKI key loaded on demand for %08lX%s\n",
             static_cast<unsigned long>(node_id),
             evicted ? " evicted=1" : "");
    return true;
}

void MtAdapter::savePkiNodeKey(uint32_t node_id, const uint8_t* key, size_t key_len)
{
    if (node_id == 0 || !key || key_len != 32)
    {
        return;
    }
    const uint32_t seen = static_cast<uint32_t>(time(nullptr));
    if (!upsertPkiNodeKey(node_id, key, seen))
    {
        return;
    }
    touchPkiNodeKey(node_id);
    savePkiNodeKeyToDirectory(node_id, key, seen);
}

void MtAdapter::savePkiNodeKeyToDirectory(uint32_t node_id,
                                          const uint8_t* key,
                                          uint32_t last_seen_s)
{
    if (!peer_directory_ || node_id == 0 || !key)
    {
        return;
    }

    MeshPeerRecord record{};
    record.valid = true;
    record.identity = makeMeshPeerNodeIdentity(MeshProtocol::Meshtastic, node_id);
    record.source = MeshPeerSource::RuntimeRx;
    record.first_seen_s = last_seen_s;
    record.last_seen_s = last_seen_s;
    record.meshtastic.has_public_key = true;
    memcpy(record.meshtastic.public_key,
           key,
           sizeof(record.meshtastic.public_key));

    const auto saved = peer_directory_->record(record);
    if (!saved.succeeded())
    {
        LORA_LOG("[LORA] PKI key directory save failed status=%u\n",
                 static_cast<unsigned>(saved.code));
        return;
    }
    LORA_LOG("[LORA] PKI key saved directory=mesh_peer_directory node=%08lX\n",
             static_cast<unsigned long>(node_id));
}

void MtAdapter::touchPkiNodeKey(uint32_t node_id)
{
    if (auto* entry = findPkiNodeKey(node_id))
    {
        entry->last_seen_s = static_cast<uint32_t>(time(nullptr));
    }
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
    auto* key_entry = findPkiNodeKey(from);
    if (!key_entry)
    {
        mt_diag_log("[MT][PKI] decrypt_skip from=%08lX id=%08lX reason=key_missing\n",
                    static_cast<unsigned long>(from),
                    static_cast<unsigned long>(packet_id));
        LORA_LOG("[LORA] PKI key missing for %08lX\n", (unsigned long)from);
        executePkiResync(runtime::MeshtasticPkiResyncCause::PeerKeyMissing,
                         from,
                         packet_id,
                         ChannelId::PRIMARY);
        LORA_LOG("[LORA] PKI unknown for %08lX, sent nodeinfo\n",
                 (unsigned long)from);
        return false;
    }
    touchPkiNodeKey(from);

    uint8_t shared[32];
    uint8_t local_priv[32];
    memcpy(shared, key_entry->key.data(), sizeof(shared));
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
    std::string key_fp = toHex(key_entry->key.data(), key_entry->key.size(), 8);

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
    auto* key_entry = findPkiNodeKey(dest);
    if (!key_entry)
    {
        LORA_LOG("[LORA] PKI key missing for %08lX\n", (unsigned long)dest);
        return false;
    }
    std::string key_fp = toHex(key_entry->key.data(), key_entry->key.size(), 8);
    LORA_LOG("[LORA] PKI encrypt dest=%08lX key_fp=%s\n",
             (unsigned long)dest, key_fp.c_str());
    touchPkiNodeKey(dest);

    uint8_t shared[32];
    uint8_t local_priv[32];
    memcpy(shared, key_entry->key.data(), sizeof(shared));
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
    if (!pki_ready_ || !findPkiNodeKey(node_id))
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
    auto* key_entry = findPkiNodeKey(header.from);
    if (!key_entry)
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
                                      key_entry->key.data(),
                                      key_entry->key.size(),
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
    auto* key_entry = findPkiNodeKey(remote_node);
    if (!key_entry)
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
                                      key_entry->key.data(),
                                      key_entry->key.size(),
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
    if (!pki_ready_ || !findPkiNodeKey(dest))
    {
        return false;
    }

    uint8_t kv_buf[96];
    pb_ostream_t kv_stream = pb_ostream_from_buffer(kv_buf, sizeof(kv_buf));
    if (!pb_encode(&kv_stream, meshtastic_KeyVerification_fields, &kv))
    {
        return false;
    }

    runtime::SendPacketEffect packet{};
    packet.protocol = MeshProtocol::Meshtastic;
    packet.channel = ChannelId::PRIMARY;
    packet.dest = dest;
    packet.portnum = meshtastic_PortNum_KEY_VERIFICATION_APP;
    packet.request_id = next_packet_id_++;
    packet.want_ack = false;
    packet.want_response = want_response;
    if (!packet.payload.assign(kv_buf, kv_stream.bytes_written))
    {
        return false;
    }
    return enqueueSendPacketAction(packet);
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

    auto& tx = tx_scratch_;
    auto& data = tx.decoded;
    auto& data_buf = tx.data;
    auto& pki_buf = tx.pki;
    auto& wire_buffer = tx.wire;
    std::memset(&data, 0, sizeof(data));
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

    pb_ostream_t dstream = pb_ostream_from_buffer(data_buf.data(), data_buf.size());
    if (!pb_encode(&dstream, meshtastic_Data_fields, &data))
    {
        return false;
    }

    if (channel_hash == 0)
    {
        if (!pki_ready_ || !findPkiNodeKey(dest))
        {
            return false;
        }

        size_t pki_len = pki_buf.size();
        MessageId msg_id = next_packet_id_++;
        if (!encryptPkiPayload(dest, msg_id, data_buf.data(), dstream.bytes_written, pki_buf.data(), &pki_len))
        {
            return false;
        }

        size_t wire_size = wire_buffer.size();
        uint8_t hop_limit = config_.hop_limit;
        bool want_ack = false;
        if (!buildWirePacket(pki_buf.data(), pki_len, node_id_, msg_id,
                             dest, channel_hash, hop_limit, want_ack,
                             nullptr, 0, wire_buffer.data(), &wire_size))
        {
            return false;
        }

        if (transmitWirePacket(wire_buffer.data(), wire_size))
        {
            return true;
        }
        return false;
    }

    size_t wire_size = wire_buffer.size();
    uint8_t hop_limit = config_.hop_limit;
    bool want_ack = false;
    if (!buildWirePacket(data_buf.data(), dstream.bytes_written, node_id_, next_packet_id_++,
                         dest, channel_hash, hop_limit, want_ack,
                         psk, psk_len, wire_buffer.data(), &wire_size))
    {
        return false;
    }

    if (transmitWirePacket(wire_buffer.data(), wire_size))
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

    auto& tx = tx_scratch_;
    auto& data = tx.decoded;
    auto& data_buf = tx.data;
    auto& pki_buf = tx.pki;
    auto& wire_buffer = tx.wire;
    std::memset(&data, 0, sizeof(data));
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

    pb_ostream_t dstream = pb_ostream_from_buffer(data_buf.data(), data_buf.size());
    if (!pb_encode(&dstream, meshtastic_Data_fields, &data))
    {
        return false;
    }

    if (channel_hash == 0)
    {
        if (!pki_ready_ || !findPkiNodeKey(dest))
        {
            return false;
        }

        size_t pki_len = pki_buf.size();
        MessageId msg_id = next_packet_id_++;
        if (!encryptPkiPayload(dest, msg_id, data_buf.data(), dstream.bytes_written, pki_buf.data(), &pki_len))
        {
            return false;
        }

        size_t wire_size = wire_buffer.size();
        uint8_t hop_limit = config_.hop_limit;
        bool want_ack = false;
        if (!buildWirePacket(pki_buf.data(), pki_len, node_id_, msg_id,
                             dest, channel_hash, hop_limit, want_ack,
                             nullptr, 0, wire_buffer.data(), &wire_size))
        {
            return false;
        }

        if (transmitWirePacket(wire_buffer.data(), wire_size))
        {
            return true;
        }
        return false;
    }

    size_t wire_size = wire_buffer.size();
    uint8_t hop_limit = config_.hop_limit;
    bool want_ack = false;
    if (!buildWirePacket(data_buf.data(), dstream.bytes_written, node_id_, next_packet_id_++,
                         dest, channel_hash, hop_limit, want_ack,
                         psk, psk_len, wire_buffer.data(), &wire_size))
    {
        return false;
    }

    if (transmitWirePacket(wire_buffer.data(), wire_size))
    {
        return true;
    }
    return false;
}

runtime::RuntimeContext MtAdapter::buildProtocolRuntimeContext() const
{
    runtime::RuntimeContext context{};
    context.protocol = MeshProtocol::Meshtastic;
    context.self_node = node_id_;
    context.now_ms = millis();

    const runtime::MeshtasticPositionInput position = build_self_position_input();
    context.self_position_valid = position.valid;
    context.self_latitude_deg = position.latitude_deg;
    context.self_longitude_deg = position.longitude_deg;
    context.self_has_altitude = position.has_altitude;
    context.self_altitude_m = position.altitude_m;
    context.self_has_speed = position.has_speed;
    context.self_speed_mps = position.speed_mps;
    context.self_has_course = position.has_course;
    context.self_course_deg = position.course_deg;
    context.self_satellites = position.satellites;
    context.self_position_timestamp_s = position.timestamp_s;
    return context;
}

bool MtAdapter::sendProtocolPacketEffect(const runtime::SendPacketEffect& packet)
{
    const uint8_t* payload = packet.payload.empty() ? nullptr : packet.payload.data();
    const size_t payload_len = packet.payload.size();
    if (packet.response_request_id == 0)
    {
        return sendAppDataNow(packet.channel,
                              packet.portnum,
                              payload,
                              payload_len,
                              packet.dest,
                              packet.want_ack,
                              packet.request_id,
                              packet.want_response);
    }

    auto& mesh_packet = protocol_effect_packet_scratch_;
    std::memset(&mesh_packet, 0, sizeof(mesh_packet));
    mesh_packet.id = packet.request_id;
    mesh_packet.to = packet.dest;
    mesh_packet.channel = (packet.channel == ChannelId::SECONDARY) ? 1 : 0;
    mesh_packet.hop_limit = config_.hop_limit;
    mesh_packet.want_ack = packet.want_ack;
    mesh_packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mesh_packet.decoded.portnum = static_cast<meshtastic_PortNum>(packet.portnum);
    mesh_packet.decoded.want_response = packet.want_response;
    mesh_packet.decoded.request_id = packet.response_request_id;
    mesh_packet.decoded.has_bitfield = true;
    mesh_packet.decoded.bitfield = packet.want_response ? kBitfieldWantResponseMask : 0;
    if (payload_len > sizeof(mesh_packet.decoded.payload.bytes))
    {
        return false;
    }
    mesh_packet.decoded.payload.size = static_cast<pb_size_t>(payload_len);
    if (payload_len > 0)
    {
        std::memcpy(mesh_packet.decoded.payload.bytes, payload, payload_len);
    }
    return sendMeshPacket(mesh_packet);
}

bool MtAdapter::executeProtocolEffects(const runtime::ProtocolEffects& effects)
{
    bool ok = true;
    for (const auto& effect : effects.items)
    {
        ok = executeProtocolEffect(effect) && ok;
    }
    return ok;
}

bool MtAdapter::executeProtocolEffect(const runtime::ProtocolEffect& effect)
{
    bool ok = false;
    runtime::visitProtocolEffect(
        effect,
        [this, &ok](const auto& item)
        {
            using Effect = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Effect, runtime::SendNodeInfoEffect>)
            {
                ok = enqueueNodeInfoAction(item.peer, item.want_response, item.channel);
            }
            else if constexpr (std::is_same_v<Effect, runtime::SendRoutingErrorEffect>)
            {
                ok = enqueueRoutingErrorAction(
                    item.peer,
                    item.request_id,
                    item.channel,
                    static_cast<meshtastic_Routing_Error>(item.error_code));
            }
            else if constexpr (std::is_same_v<Effect, runtime::ForgetPeerKeyEffect>)
            {
                forgetNodePublicKey(item.peer);
                ok = true;
            }
            else if constexpr (std::is_same_v<Effect, runtime::SendPacketEffect>)
            {
                ok = enqueueSendPacketAction(item);
            }
            else if constexpr (std::is_same_v<Effect, runtime::PublishIncomingDataEffect>)
            {
                IncomingQueuePushReport report{};
                ok = app_receive_queue_.push(item.data,
                                             IncomingQueuePriority::P1User,
                                             &report);
                if (report.dropped_existing)
                {
                    LORA_LOG("[LORA] protocol data queue evicted prio=%u depth=%u\n",
                             static_cast<unsigned>(report.dropped_priority),
                             static_cast<unsigned>(app_receive_queue_.size()));
                }
                else if (report.dropped_new)
                {
                    LORA_LOG("[LORA] protocol data queue drop port=%u len=%u depth=%u\n",
                             static_cast<unsigned>(item.data.portnum),
                             static_cast<unsigned>(item.data.payload.size()),
                             static_cast<unsigned>(app_receive_queue_.size()));
                }
            }
        });
    return ok;
}

bool MtAdapter::executePkiResync(runtime::MeshtasticPkiResyncCause cause,
                                 NodeId peer,
                                 MessageId request_id,
                                 ChannelId channel)
{
    runtime::MeshtasticPkiResyncInput input{};
    input.cause = cause;
    input.peer = peer;
    input.request_id = request_id;
    input.channel = channel;
    protocol_effect_workspace_.primary.clear();
    protocol_runtime_.handlePkiResync(input, protocol_effect_workspace_.primary);
    return executeProtocolEffects(protocol_effect_workspace_.primary);
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

    IncomingQueuePushReport report{};
    if (!app_receive_queue_.push(incoming,
                                 routing_buf,
                                 rstream.bytes_written,
                                 IncomingQueuePriority::P0Critical,
                                 &report))
    {
        LORA_LOG("[LORA] synthetic routing drop req=%08lX depth=%u\n",
                 (unsigned long)request_id,
                 static_cast<unsigned>(app_receive_queue_.size()));
    }
    else if (report.dropped_existing)
    {
        LORA_LOG("[LORA] synthetic routing evicted prio=%u req=%08lX depth=%u\n",
                 static_cast<unsigned>(report.dropped_priority),
                 (unsigned long)request_id,
                 static_cast<unsigned>(app_receive_queue_.size()));
    }

    const bool own_self_echo = from == node_id_ && to == node_id_;
    const chat::MessageStatus status =
        reason != meshtastic_Routing_Error_NONE
            ? chat::MessageStatus::Failed
            : (own_self_echo ? chat::MessageStatus::Sent
                             : chat::MessageStatus::Delivered);
    sys::EventBus::publish(
        new sys::ChatSendResultEvent(request_id,
                                     status,
                                     chat::MeshProtocol::Meshtastic,
                                     failureKindFromRoutingError(reason)),
        0);
}

} // namespace meshtastic
} // namespace chat
