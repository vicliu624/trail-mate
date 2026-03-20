#include "platform/nrf52/arduino_common/chat/infra/meshtastic/meshtastic_radio_adapter.h"

#include "chat/domain/contact_types.h"
#include "chat/infra/meshtastic/mt_codec_pb.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "chat/runtime/meshtastic_self_announcement_core.h"
#include "chat/runtime/self_identity_policy.h"
#include "meshtastic/mqtt.pb.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"
#include "platform/nrf52/arduino_common/device_identity.h"

#include <Arduino.h>

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
constexpr ::chat::NodeId kBroadcastNode = 0xFFFFFFFFUL;
constexpr uint8_t kDefaultPskIndex = 1;

using ::chat::meshtastic::expandShortPsk;
using ::chat::meshtastic::fillDecodedPacketCommon;
using ::chat::meshtastic::isZeroKey;
using ::chat::meshtastic::makeEncryptedPacketFromWire;
using ::chat::meshtastic::readPbString;

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

} // namespace

MeshtasticRadioAdapter::MeshtasticRadioAdapter(const ::chat::runtime::SelfIdentityProvider* identity_provider,
                                               NodeStore* node_store)
    : node_id_(device_identity::getSelfNodeId()),
      identity_provider_(identity_provider),
      node_store_(node_store)
{
    randomSeed(static_cast<unsigned long>(NRF_FICR->DEVICEADDR[0] ^ NRF_FICR->DEVICEADDR[1] ^ micros()));
    next_packet_id_ = static_cast<::chat::MessageId>(random(1, 0x7FFFFFFF));
}

::chat::MeshCapabilities MeshtasticRadioAdapter::getCapabilities() const
{
    ::chat::MeshCapabilities caps{};
    caps.supports_unicast_text = true;
    caps.supports_unicast_appdata = true;
    caps.supports_appdata_ack = true;
    caps.provides_appdata_sender = true;
    caps.supports_node_info = true;
    return caps;
}

bool MeshtasticRadioAdapter::sendText(::chat::ChannelId channel, const std::string& text,
                                      ::chat::MessageId* out_msg_id, ::chat::NodeId peer)
{
    if (text.empty() || !config_.tx_enabled)
    {
        return false;
    }

    uint8_t payload[256] = {};
    size_t payload_size = sizeof(payload);
    const ::chat::MessageId packet_id = next_packet_id_++;
    const ::chat::NodeId dest = (peer == 0) ? kBroadcastNode : peer;
    if (!::chat::meshtastic::encodeTextMessage(channel,
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
    const uint8_t* key = selectKey(config_, channel, &key_len);
    const uint8_t channel_hash = ::chat::meshtastic::computeChannelHash(channelNameFor(config_, channel),
                                                                        key,
                                                                        key_len);

    uint8_t wire[384] = {};
    size_t wire_size = sizeof(wire);
    if (!::chat::meshtastic::buildWirePacket(payload,
                                             payload_size,
                                             node_id_,
                                             packet_id,
                                             dest,
                                             channel_hash,
                                             config_.hop_limit,
                                             false,
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

    if (!transmitPreparedWire(wire, wire_size, channel, &mqtt_data, true, true))
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
    if (!payload || len == 0 || !config_.tx_enabled)
    {
        return false;
    }

    uint8_t data_pb[256] = {};
    size_t data_pb_size = sizeof(data_pb);
    if (!::chat::meshtastic::encodeAppData(portnum, payload, len, want_response, data_pb, &data_pb_size))
    {
        return false;
    }

    if (packet_id == 0)
    {
        packet_id = next_packet_id_++;
    }

    const ::chat::NodeId wire_dest = (dest == 0) ? kBroadcastNode : dest;
    size_t key_len = 0;
    const uint8_t* key = selectKey(config_, channel, &key_len);
    const uint8_t channel_hash = ::chat::meshtastic::computeChannelHash(channelNameFor(config_, channel),
                                                                        key,
                                                                        key_len);

    uint8_t wire[384] = {};
    size_t wire_size = sizeof(wire);
    if (!::chat::meshtastic::buildWirePacket(data_pb,
                                             data_pb_size,
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
    mqtt_data.want_response = want_response;
    mqtt_data.dest = wire_dest;
    mqtt_data.source = node_id_;
    mqtt_data.has_bitfield = true;
    mqtt_data.payload.size = std::min(len, sizeof(mqtt_data.payload.bytes));
    if (mqtt_data.payload.size > 0)
    {
        std::memcpy(mqtt_data.payload.bytes, payload, mqtt_data.payload.size);
    }

    return transmitPreparedWire(wire, wire_size, channel, &mqtt_data, true, true);
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
    return buildAndQueueNodeInfo(dest == 0 ? kBroadcastNode : dest, want_response);
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

void MeshtasticRadioAdapter::setPrivacyConfig(uint8_t encrypt_mode, bool pki_enabled)
{
    (void)encrypt_mode;
    (void)pki_enabled;
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

    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    char channel_id[32] = {0};
    char gateway_id[16] = {0};
    if (!decodeMqttServiceEnvelope(data_field->bytes, data_field->size,
                                   &packet,
                                   channel_id, sizeof(channel_id),
                                   gateway_id, sizeof(gateway_id)))
    {
        return false;
    }

    return injectMqttEnvelope(packet, channel_id, gateway_id);
}

bool MeshtasticRadioAdapter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    (void)out_data;
    (void)max_len;
    out_len = 0;
    return false;
}

void MeshtasticRadioAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    if (!data || size == 0)
    {
        return;
    }

    ::chat::meshtastic::PacketHeaderWire header{};
    uint8_t payload[256] = {};
    size_t payload_size = sizeof(payload);
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

    uint8_t plain[256] = {};
    size_t plain_len = sizeof(plain);
    ::chat::ChannelId channel = ::chat::ChannelId::PRIMARY;
    size_t key_len = 0;
    const uint8_t* key = selectKeyByHash(config_, header.channel, &key_len, &channel);
    const bool want_ack_flag = (header.flags & ::chat::meshtastic::PACKET_FLAGS_WANT_ACK_MASK) != 0;
    if (header.channel != 0 && !key)
    {
        logMeshtasticRx("[gat562][mt] unknown channel from=%08lX to=%08lX id=%lu ch=%u\n",
                        static_cast<unsigned long>(header.from),
                        static_cast<unsigned long>(header.to),
                        static_cast<unsigned long>(header.id),
                        static_cast<unsigned>(header.channel));
        return;
    }
    if (!::chat::meshtastic::decryptPayload(header, payload, payload_size,
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
        const uint8_t primary_hash = ::chat::meshtastic::computeChannelHash(
            channelNameFor(config_, ::chat::ChannelId::PRIMARY),
            primary_key,
            primary_key_len);
        size_t secondary_key_len = 0;
        const uint8_t* secondary_key = selectKey(config_, ::chat::ChannelId::SECONDARY, &secondary_key_len);
        const uint8_t secondary_hash = ::chat::meshtastic::computeChannelHash(
            channelNameFor(config_, ::chat::ChannelId::SECONDARY),
            secondary_key,
            secondary_key_len);

        if (header.to == node_id_ && want_ack_flag)
        {
            if (header.channel == 0)
            {
                (void)buildAndQueueNodeInfo(header.from, true);
                (void)sendRoutingError(header.from, header.id, primary_hash,
                                       ::chat::ChannelId::PRIMARY,
                                       primary_key, primary_key_len,
                                       meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY, 0);
            }
            else if (header.channel != primary_hash && header.channel != secondary_hash)
            {
                (void)buildAndQueueNodeInfo(header.from, true);
                (void)sendRoutingError(header.from, header.id, primary_hash,
                                       ::chat::ChannelId::PRIMARY,
                                       primary_key, primary_key_len,
                                       meshtastic_Routing_Error_NO_CHANNEL, 0);
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
            ::chat::RxMeta implicit_rx{};
            implicit_rx.rx_timestamp_ms = millis();
            implicit_rx.rx_timestamp_s = static_cast<uint32_t>(std::time(nullptr));
            implicit_rx.time_source = ::chat::RxTimeSource::DeviceUtc;
            implicit_rx.origin = ::chat::RxOrigin::Mesh;
            implicit_rx.channel_hash = header.channel;
            implicit_rx.next_hop = header.next_hop;
            implicit_rx.relay_node = header.relay_node;
            const ::chat::NodeId ack_from =
                (header.relay_node != 0) ? static_cast<::chat::NodeId>(header.relay_node) : node_id_;
            pending_retransmits_.erase(pending_it);
            emitRoutingResult(header.id,
                              meshtastic_Routing_Error_NONE,
                              ack_from,
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
    rx_meta.rx_timestamp_s = static_cast<uint32_t>(std::time(nullptr));
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

    meshtastic_Data decoded = meshtastic_Data_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(plain, plain_len);
    const bool decoded_ok = pb_decode(&stream, meshtastic_Data_fields, &decoded);
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

    if (decoded_ok && decoded.portnum == meshtastic_PortNum_NODEINFO_APP && decoded.payload.size > 0 && node_store_)
    {
        meshtastic_NodeInfo node = meshtastic_NodeInfo_init_default;
        pb_istream_t nstream = pb_istream_from_buffer(decoded.payload.bytes, decoded.payload.size);
        if (pb_decode(&nstream, meshtastic_NodeInfo_fields, &node))
        {
            const uint32_t node_id = node.num ? node.num : header.from;
            const char* short_name = node.has_user ? node.user.short_name : "";
            const char* long_name = node.has_user ? node.user.long_name : "";
            uint8_t role = ::chat::contacts::kNodeRoleUnknown;
            if (node.has_user && node.user.role <= meshtastic_Config_DeviceConfig_Role_CLIENT_BASE)
            {
                role = static_cast<uint8_t>(node.user.role);
            }
            const float snr = std::isnan(last_rx_snr_) ? node.snr : last_rx_snr_;
            const uint8_t hops_away =
                node.has_hops_away ? node.hops_away : ::chat::meshtastic::computeHopsAway(header.flags);
            node_store_->upsert(node_id, short_name, long_name,
                                static_cast<uint32_t>(std::time(nullptr)),
                                snr, last_rx_rssi_,
                                static_cast<uint8_t>(::chat::contacts::NodeProtocolType::Meshtastic),
                                role, hops_away,
                                static_cast<uint8_t>(node.has_user ? node.user.hw_model : 0),
                                static_cast<uint8_t>(channel));
        }
        else
        {
            meshtastic_User user = meshtastic_User_init_default;
            pb_istream_t ustream = pb_istream_from_buffer(decoded.payload.bytes, decoded.payload.size);
            if (pb_decode(&ustream, meshtastic_User_fields, &user))
            {
                uint8_t role = ::chat::contacts::kNodeRoleUnknown;
                if (user.role <= meshtastic_Config_DeviceConfig_Role_CLIENT_BASE)
                {
                    role = static_cast<uint8_t>(user.role);
                }
                node_store_->upsert(header.from, user.short_name, user.long_name,
                                    static_cast<uint32_t>(std::time(nullptr)),
                                    last_rx_snr_, last_rx_rssi_,
                                    static_cast<uint8_t>(::chat::contacts::NodeProtocolType::Meshtastic),
                                    role, ::chat::meshtastic::computeHopsAway(header.flags),
                                    static_cast<uint8_t>(user.hw_model),
                                    static_cast<uint8_t>(channel));
            }
        }
    }

    if (want_ack_flag && to_us && decoded_ok)
    {
        (void)sendRoutingAck(header.from, header.id, header.channel, channel, key, key_len, 0);
    }

    const bool want_response = decoded_ok &&
                               (decoded.want_response ||
                                (decoded.has_bitfield && ((decoded.bitfield & 0x02U) != 0)));
    if (decoded_ok && decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP)
    {
        (void)handleTraceRoutePacket(header, &decoded, &rx_meta, channel, want_ack_flag, want_response);
    }

    if (!duplicate || history.was_fallback)
    {
        (void)maybeRebroadcast(header, payload, payload_size, channel, decoded_ok ? &decoded : nullptr);
    }

    if (!to_us_or_broadcast || duplicate)
    {
        return;
    }

    ::chat::MeshIncomingText incoming{};
    if (::chat::meshtastic::decodeTextMessage(plain, plain_len, &incoming))
    {
        incoming.from = header.from;
        incoming.to = header.to;
        incoming.msg_id = header.id;
        incoming.channel = channel;
        incoming.encrypted = key_len > 0;
        incoming.hop_limit = hop_limit;
        incoming.rx_meta = rx_meta;
        logMeshtasticRx("[gat562][mt] text queued from=%08lX to=%08lX id=%lu len=%u text=\"%s\"\n",
                        static_cast<unsigned long>(incoming.from),
                        static_cast<unsigned long>(incoming.to),
                        static_cast<unsigned long>(incoming.msg_id),
                        static_cast<unsigned>(incoming.text.size()),
                        incoming.text.c_str());
        text_queue_.push(std::move(incoming));
        return;
    }

    if (decoded_ok &&
        (decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
         decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP))
    {
        logMeshtasticRx("[gat562][mt] text decode fail from=%08lX id=%lu payload=%u plain=%u\n",
                        static_cast<unsigned long>(header.from),
                        static_cast<unsigned long>(header.id),
                        static_cast<unsigned>(decoded.payload.size),
                        static_cast<unsigned>(plain_len));
    }

    ::chat::MeshIncomingData app_data{};
    if (::chat::meshtastic::decodeAppData(plain, plain_len, &app_data))
    {
        app_data.from = header.from;
        app_data.to = header.to;
        app_data.packet_id = header.id;
        app_data.request_id = decoded.request_id;
        app_data.channel = channel;
        app_data.channel_hash = header.channel;
        app_data.hop_limit = hop_limit;
        app_data.want_response = decoded.want_response;
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
                node_store_->setNextHop(pending.dest, 0, static_cast<uint32_t>(std::time(nullptr)));
            }
            it = pending_retransmits_.erase(it);
            continue;
        }

        if (pending.retries_left == 1 && header->next_hop != 0)
        {
            header->next_hop = 0;
            pending.fallback_sent = true;
            if (node_store_ && pending.dest != 0 && pending.dest != kBroadcastNode)
            {
                node_store_->setNextHop(pending.dest, 0, static_cast<uint32_t>(std::time(nullptr)));
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
                                                  bool local_origin, uint8_t retries_override)
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
    if (track_retransmit && is_unicast && (header->next_hop != 0 || wants_ack))
    {
        queuePendingRetransmit(*header, data, size, channel, local_origin, retries_override);
    }
    return true;
}

bool MeshtasticRadioAdapter::buildAndQueueNodeInfo(::chat::NodeId dest, bool want_response)
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
    request.channel = ::chat::ChannelId::PRIMARY;
    request.packet_id = next_packet_id_++;
    request.dest_node = dest;
    request.hop_limit = config_.hop_limit;
    request.want_response = want_response;
    request.mac_addr = mac.data();

    ::chat::runtime::MeshtasticAnnouncementPacket packet{};
    return ::chat::runtime::MeshtasticSelfAnnouncementCore::buildNodeInfoPacket(request, &packet) &&
           transmitWire(packet.wire, packet.wire_size);
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

    uint8_t wire[256] = {};
    size_t wire_size = sizeof(wire);
    if (!::chat::meshtastic::buildWirePacket(data_buf, dstream.bytes_written,
                                             node_id_, next_packet_id_++,
                                             dest, channel_hash, hop_limit, false,
                                             key, key_len, wire, &wire_size))
    {
        return false;
    }

    return transmitPreparedWire(wire, wire_size, channel, &data, false, true);
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
        incoming.rx_meta.rx_timestamp_s = static_cast<uint32_t>(std::time(nullptr));
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
    (void)node_store_->setNextHop(dest, next_hop, static_cast<uint32_t>(std::time(nullptr)));
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
    node_store_->upsert(node_id, "", "", static_cast<uint32_t>(std::time(nullptr)),
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
                node_store_->setNextHop(header.from, 0, static_cast<uint32_t>(std::time(nullptr)));
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
                                                    bool local_origin, uint8_t retries_override)
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
    pending.retries_left = retries_override != 0
                               ? retries_override
                               : (pending.want_ack ? kDefaultAckRetries : kDefaultNextHopRetries);
    pending.next_tx_ms = millis() + kRetransmitIntervalMs;
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

    uint8_t wire_buffer[sizeof(::chat::meshtastic::PacketHeaderWire) + 256] = {};
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
        if (wire_size + enc_size > sizeof(wire_buffer))
        {
            return false;
        }
        std::memcpy(wire_buffer + wire_size, packet.encrypted.bytes, enc_size);
        wire_size += enc_size;
    }
    else
    {
        meshtastic_Data decoded = packet.decoded;
        decoded.dest = packet.to;
        decoded.source = packet.from;
        decoded.has_bitfield = true;

        uint8_t data_buffer[256] = {};
        pb_ostream_t dstream = pb_ostream_from_buffer(data_buffer, sizeof(data_buffer));
        if (!pb_encode(&dstream, meshtastic_Data_fields, &decoded))
        {
            return false;
        }

        size_t psk_len = 0;
        const uint8_t* psk = selectKey(config_, channel_index, &psk_len);
        size_t rebuilt_size = sizeof(wire_buffer);
        if (!::chat::meshtastic::buildWirePacket(data_buffer, dstream.bytes_written,
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

    uint8_t env_buf[435] = {};
    meshtastic_MeshPacket packet_copy = packet;
    std::string node_id = mqttNodeIdString();
    meshtastic_ServiceEnvelope env = meshtastic_ServiceEnvelope_init_zero;
    env.packet = &packet_copy;
    env.channel_id = const_cast<char*>(channel_id);
    env.gateway_id = const_cast<char*>(node_id.c_str());

    pb_ostream_t estream = pb_ostream_from_buffer(env_buf, sizeof(env_buf));
    if (!pb_encode(&estream, meshtastic_ServiceEnvelope_fields, &env))
    {
        return false;
    }

    meshtastic_MqttClientProxyMessage proxy = meshtastic_MqttClientProxyMessage_init_zero;
    proxy.which_payload_variant = meshtastic_MqttClientProxyMessage_data_tag;
    const std::string root = mqtt_proxy_settings_.root.empty() ? std::string("msh") : mqtt_proxy_settings_.root;
    const std::string topic = root + "/2/e/" + channel_id + "/" + node_id;
    std::strncpy(proxy.topic, topic.c_str(), sizeof(proxy.topic) - 1);
    proxy.topic[sizeof(proxy.topic) - 1] = '\0';
    proxy.payload_variant.data.size = static_cast<pb_size_t>(estream.bytes_written);
    std::memcpy(proxy.payload_variant.data.bytes, env_buf, estream.bytes_written);
    proxy.retained = false;

    while (mqtt_proxy_queue_.size() >= kMaxMqttProxyQueue)
    {
        mqtt_proxy_queue_.pop();
    }
    mqtt_proxy_queue_.push(proxy);
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
    uint8_t payload[256] = {};
    size_t payload_size = sizeof(payload);
    if (!::chat::meshtastic::parseWirePacket(wire_data, wire_size, &header, payload, &payload_size))
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

} // namespace platform::nrf52::arduino_common::chat::meshtastic
