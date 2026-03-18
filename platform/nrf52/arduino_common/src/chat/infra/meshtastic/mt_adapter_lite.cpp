#include "platform/nrf52/arduino_common/chat/infra/meshtastic/mt_adapter_lite.h"

#include "chat/infra/meshtastic/mt_codec_pb.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/runtime/meshtastic_self_announcement_core.h"
#include "chat/runtime/self_identity_policy.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"
#include "platform/nrf52/arduino_common/device_identity.h"

#include <Arduino.h>

#include <array>
#include <cstring>

namespace platform::nrf52::arduino_common::chat::meshtastic
{
namespace
{

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
        if (config.secondary_key[0] != 0)
        {
            if (out_len)
            {
                *out_len = sizeof(config.secondary_key);
            }
            return config.secondary_key;
        }
        return nullptr;
    }

    if (config.primary_key[0] != 0)
    {
        if (out_len)
        {
            *out_len = sizeof(config.primary_key);
        }
        return config.primary_key;
    }
    return nullptr;
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
    if (::chat::meshtastic::computeChannelHash("Primary", key, key_len) == channel_hash)
    {
        if (out_len)
        {
            *out_len = key_len;
        }
        return key;
    }

    key = selectKey(config, ::chat::ChannelId::SECONDARY, &key_len);
    if (::chat::meshtastic::computeChannelHash("Secondary", key, key_len) == channel_hash)
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

    if (::chat::meshtastic::computeChannelHash("Primary", nullptr, 0) == channel_hash)
    {
        return nullptr;
    }
    return nullptr;
}

} // namespace

MtAdapterLite::MtAdapterLite(const ::chat::runtime::SelfIdentityProvider* identity_provider)
    : node_id_(device_identity::getSelfNodeId()),
      identity_provider_(identity_provider)
{
}

::chat::MeshCapabilities MtAdapterLite::getCapabilities() const
{
    ::chat::MeshCapabilities caps{};
    caps.supports_unicast_text = true;
    caps.supports_unicast_appdata = true;
    caps.supports_node_info = true;
    return caps;
}

bool MtAdapterLite::sendText(::chat::ChannelId channel, const std::string& text,
                             ::chat::MessageId* out_msg_id, ::chat::NodeId peer)
{
    if (text.empty() || !config_.tx_enabled)
    {
        return false;
    }

    uint8_t payload[256] = {};
    size_t payload_size = sizeof(payload);
    const ::chat::MessageId packet_id = next_packet_id_++;
    if (!::chat::meshtastic::encodeTextMessage(channel,
                                               text,
                                               node_id_,
                                               packet_id,
                                               peer == 0 ? 0xFFFFFFFFUL : peer,
                                               payload,
                                               &payload_size))
    {
        return false;
    }

    size_t key_len = 0;
    const uint8_t* key = selectKey(config_, channel, &key_len);
    const uint8_t channel_hash = ::chat::meshtastic::computeChannelHash(
        channel == ::chat::ChannelId::SECONDARY ? "Secondary" : "Primary",
        key,
        key_len);

    uint8_t wire[384] = {};
    size_t wire_size = sizeof(wire);
    if (!::chat::meshtastic::buildWirePacket(payload,
                                             payload_size,
                                             node_id_,
                                             packet_id,
                                             peer == 0 ? 0xFFFFFFFFUL : peer,
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

    if (!transmitWire(wire, wire_size))
    {
        return false;
    }

    if (out_msg_id)
    {
        *out_msg_id = packet_id;
    }
    return true;
}

bool MtAdapterLite::pollIncomingText(::chat::MeshIncomingText* out)
{
    if (!out || text_queue_.empty())
    {
        return false;
    }
    *out = text_queue_.front();
    text_queue_.pop();
    return true;
}

bool MtAdapterLite::sendAppData(::chat::ChannelId channel, uint32_t portnum,
                                const uint8_t* payload, size_t len,
                                ::chat::NodeId dest, bool want_ack,
                                ::chat::MessageId packet_id,
                                bool want_response)
{
    (void)want_ack;
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

    size_t key_len = 0;
    const uint8_t* key = selectKey(config_, channel, &key_len);
    const uint8_t channel_hash = ::chat::meshtastic::computeChannelHash(
        channel == ::chat::ChannelId::SECONDARY ? "Secondary" : "Primary",
        key,
        key_len);

    uint8_t wire[384] = {};
    size_t wire_size = sizeof(wire);
    return ::chat::meshtastic::buildWirePacket(data_pb,
                                               data_pb_size,
                                               node_id_,
                                               packet_id,
                                               dest == 0 ? 0xFFFFFFFFUL : dest,
                                               channel_hash,
                                               config_.hop_limit,
                                               false,
                                               key,
                                               key_len,
                                               wire,
                                               &wire_size) &&
           transmitWire(wire, wire_size);
}

bool MtAdapterLite::pollIncomingData(::chat::MeshIncomingData* out)
{
    if (!out || data_queue_.empty())
    {
        return false;
    }
    *out = data_queue_.front();
    data_queue_.pop();
    return true;
}

bool MtAdapterLite::requestNodeInfo(::chat::NodeId dest, bool want_response)
{
    return buildAndQueueNodeInfo(dest == 0 ? 0xFFFFFFFFUL : dest, want_response);
}

void MtAdapterLite::applyConfig(const ::chat::MeshConfig& config)
{
    config_ = config;
}

void MtAdapterLite::setUserInfo(const char* long_name, const char* short_name)
{
    long_name_ = long_name ? long_name : "";
    short_name_ = short_name ? short_name : "";
}

void MtAdapterLite::setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent)
{
    (void)duty_cycle_enabled;
    (void)util_percent;
}

void MtAdapterLite::setPrivacyConfig(uint8_t encrypt_mode, bool pki_enabled)
{
    (void)encrypt_mode;
    (void)pki_enabled;
}

bool MtAdapterLite::isReady() const
{
    return ::platform::nrf52::arduino_common::chat::infra::radioPacketIo() != nullptr;
}

::chat::NodeId MtAdapterLite::getNodeId() const
{
    return node_id_;
}

bool MtAdapterLite::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    (void)out_data;
    (void)max_len;
    out_len = 0;
    return false;
}

void MtAdapterLite::handleRawPacket(const uint8_t* data, size_t size)
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
        return;
    }

    uint8_t plain[256] = {};
    size_t plain_len = sizeof(plain);
    ::chat::ChannelId channel = ::chat::ChannelId::PRIMARY;
    size_t key_len = 0;
    const uint8_t* key = selectKeyByHash(config_, header.channel, &key_len, &channel);
    if (!::chat::meshtastic::decryptPayload(header, payload, payload_size,
                                            key, key_len, plain, &plain_len))
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
        incoming.rx_meta.rssi_dbm_x10 = static_cast<int16_t>(last_rx_rssi_ * 10.0f);
        incoming.rx_meta.snr_db_x10 = static_cast<int16_t>(last_rx_snr_ * 10.0f);
        incoming.rx_meta.channel_hash = header.channel;
        text_queue_.push(std::move(incoming));
        return;
    }

    ::chat::MeshIncomingData app_data{};
    if (::chat::meshtastic::decodeAppData(plain, plain_len, &app_data))
    {
        app_data.from = header.from;
        app_data.to = header.to;
        app_data.packet_id = header.id;
        app_data.channel = channel;
        app_data.channel_hash = header.channel;
        app_data.rx_meta.rssi_dbm_x10 = static_cast<int16_t>(last_rx_rssi_ * 10.0f);
        app_data.rx_meta.snr_db_x10 = static_cast<int16_t>(last_rx_snr_ * 10.0f);
        app_data.rx_meta.channel_hash = header.channel;
        data_queue_.push(std::move(app_data));
    }
}

void MtAdapterLite::setLastRxStats(float rssi, float snr)
{
    last_rx_rssi_ = rssi;
    last_rx_snr_ = snr;
}

void MtAdapterLite::processSendQueue()
{
}

::chat::runtime::EffectiveSelfIdentity MtAdapterLite::buildEffectiveIdentity() const
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

bool MtAdapterLite::transmitWire(const uint8_t* data, size_t size)
{
    auto* io = ::platform::nrf52::arduino_common::chat::infra::radioPacketIo();
    return io && io->transmit(data, size);
}

bool MtAdapterLite::buildAndQueueNodeInfo(::chat::NodeId dest, bool want_response)
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

} // namespace platform::nrf52::arduino_common::chat::meshtastic
