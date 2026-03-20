#include "apps/esp_idf/meshtastic_radio_adapter.h"

#include "chat/domain/contact_types.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "chat/time_utils.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/mesh.pb.h"
#include "pb_encode.h"
#include "sys/event_bus.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace apps::esp_idf
{
namespace
{

constexpr const char* kTag = "idf-mt";
constexpr uint8_t kDefaultPskIndex = 1;
constexpr uint8_t kLoraSyncWord = 0x2B;
constexpr uint16_t kLoraPreambleLen = 16;
constexpr uint16_t kIrqRxDone = 0x0002;
constexpr uint16_t kIrqHeaderErr = 0x0020;
constexpr uint16_t kIrqCrcErr = 0x0040;
constexpr uint16_t kIrqTimeout = 0x0200;
constexpr uint8_t kBitfieldWantResponseMask = 0x02;
constexpr uint32_t kRadioOk = 0;
constexpr char kSecondaryChannelName[] = "Squad";

uint32_t now_millis()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

const char* primary_channel_name(const chat::MeshConfig& config)
{
    if (!config.use_preset)
    {
        return "Custom";
    }

    const auto preset =
        static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(config.modem_preset);
    const char* name = chat::meshtastic::presetDisplayName(preset);
    return (name && name[0] != '\0') ? name : "Custom";
}

uint8_t to_channel_index(chat::ChannelId channel)
{
    return (channel == chat::ChannelId::SECONDARY) ? 1U : 0U;
}

chat::ChannelId channel_from_hash(uint8_t hash, uint8_t primary_hash, uint8_t secondary_hash)
{
    if (hash == secondary_hash)
    {
        return chat::ChannelId::SECONDARY;
    }
    (void)primary_hash;
    return chat::ChannelId::PRIMARY;
}

void fill_rx_meta(chat::RxMeta& rx_meta,
                  const chat::meshtastic::PacketHeaderWire& header,
                  float rssi,
                  float snr,
                  uint32_t freq_hz,
                  uint32_t bw_hz,
                  uint8_t sf,
                  uint8_t cr)
{
    rx_meta.rx_timestamp_ms = now_millis();
    const uint32_t epoch_s = chat::now_epoch_seconds();
    if (chat::is_valid_epoch(epoch_s))
    {
        rx_meta.rx_timestamp_s = epoch_s;
        rx_meta.time_source = chat::RxTimeSource::DeviceUtc;
    }
    else
    {
        rx_meta.rx_timestamp_s = rx_meta.rx_timestamp_ms / 1000U;
        rx_meta.time_source = chat::RxTimeSource::Uptime;
    }

    rx_meta.origin = chat::RxOrigin::Mesh;
    rx_meta.channel_hash = header.channel;
    rx_meta.wire_flags = header.flags;
    rx_meta.next_hop = header.next_hop;
    rx_meta.relay_node = header.relay_node;
    rx_meta.hop_count = chat::meshtastic::computeHopsAway(header.flags);
    rx_meta.hop_limit = header.flags & chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
    rx_meta.direct = (rx_meta.hop_count == 0);
    rx_meta.from_is = false;
    rx_meta.rssi_dbm_x10 = static_cast<int16_t>(std::lround(rssi * 10.0f));
    rx_meta.snr_db_x10 = static_cast<int16_t>(std::lround(snr * 10.0f));
    rx_meta.freq_hz = freq_hz;
    rx_meta.bw_hz = bw_hz;
    rx_meta.sf = sf;
    rx_meta.cr = cr;
}

} // namespace

MeshtasticRadioAdapter::MeshtasticRadioAdapter(LoraBoard& board)
    : board_(board)
{
    initNodeIdentity();
}

chat::MeshCapabilities MeshtasticRadioAdapter::getCapabilities() const
{
    chat::MeshCapabilities caps{};
    caps.supports_unicast_text = true;
    caps.supports_unicast_appdata = true;
    caps.supports_node_info = true;
    return caps;
}

bool MeshtasticRadioAdapter::sendText(chat::ChannelId channel,
                                      const std::string& text,
                                      chat::MessageId* out_msg_id,
                                      chat::NodeId peer)
{
    if (out_msg_id)
    {
        *out_msg_id = 0;
    }
    if (!ready_ || text.empty())
    {
        return false;
    }

    const chat::NodeId dest = (peer != 0) ? peer : kBroadcastNodeId;
    const chat::MessageId msg_id = next_packet_id_++;
    uint8_t data_buffer[256];
    size_t data_size = sizeof(data_buffer);
    if (!chat::meshtastic::encodeTextMessage(channel, text, node_id_, msg_id, dest,
                                             data_buffer, &data_size))
    {
        return false;
    }

    if (out_msg_id)
    {
        *out_msg_id = msg_id;
    }
    return sendEncodedPayload(channel, data_buffer, data_size, peer, false, msg_id, true);
}

bool MeshtasticRadioAdapter::pollIncomingText(chat::MeshIncomingText* out)
{
    if (!out || text_queue_.empty())
    {
        return false;
    }
    *out = text_queue_.front();
    text_queue_.pop();
    return true;
}

bool MeshtasticRadioAdapter::sendAppData(chat::ChannelId channel,
                                         uint32_t portnum,
                                         const uint8_t* payload,
                                         size_t len,
                                         chat::NodeId dest,
                                         bool want_ack,
                                         chat::MessageId packet_id,
                                         bool want_response)
{
    if (!ready_)
    {
        return false;
    }

    uint8_t data_buffer[256];
    size_t data_size = sizeof(data_buffer);
    if (!chat::meshtastic::encodeAppData(portnum, payload, len, want_response,
                                         data_buffer, &data_size))
    {
        return false;
    }

    return sendEncodedPayload(channel, data_buffer, data_size, dest, want_ack, packet_id, false);
}

bool MeshtasticRadioAdapter::pollIncomingData(chat::MeshIncomingData* out)
{
    if (!out || data_queue_.empty())
    {
        return false;
    }
    *out = data_queue_.front();
    data_queue_.pop();
    return true;
}

bool MeshtasticRadioAdapter::requestNodeInfo(chat::NodeId dest, bool want_response)
{
    chat::ChannelId channel = chat::ChannelId::PRIMARY;
    if (dest != 0)
    {
        auto it = node_last_channel_.find(dest);
        if (it != node_last_channel_.end())
        {
            channel = it->second;
        }
    }
    return sendNodeInfoTo(dest == 0 ? kBroadcastNodeId : dest, want_response, channel);
}

bool MeshtasticRadioAdapter::triggerDiscoveryAction(chat::MeshDiscoveryAction action)
{
    switch (action)
    {
    case chat::MeshDiscoveryAction::SendIdBroadcast:
        return broadcastNodeInfo();
    case chat::MeshDiscoveryAction::SendIdLocal:
    case chat::MeshDiscoveryAction::ScanLocal:
        return requestNodeInfo(0, true);
    default:
        return false;
    }
}

void MeshtasticRadioAdapter::applyConfig(const chat::MeshConfig& config)
{
    config_ = config;
    updateChannelKeys();
    configureRadio();
}

void MeshtasticRadioAdapter::setUserInfo(const char* long_name, const char* short_name)
{
    user_long_name_ = long_name ? long_name : "";
    user_short_name_ = short_name ? short_name : "";
    nodeinfo_broadcast_sent_ = false;
}

bool MeshtasticRadioAdapter::isReady() const
{
    return ready_;
}

bool MeshtasticRadioAdapter::pollIncomingRawPacket(uint8_t* out_data,
                                                   size_t& out_len,
                                                   size_t max_len)
{
    (void)out_data;
    (void)out_len;
    (void)max_len;
    return false;
}

void MeshtasticRadioAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    processReceivedPacket(data, size);
}

void MeshtasticRadioAdapter::setLastRxStats(float rssi, float snr)
{
    last_rx_rssi_ = rssi;
    last_rx_snr_ = snr;
}

void MeshtasticRadioAdapter::processSendQueue()
{
    pollRadio();
    if (!nodeinfo_broadcast_sent_ && ready_)
    {
        nodeinfo_broadcast_sent_ = broadcastNodeInfo();
    }
}

chat::NodeId MeshtasticRadioAdapter::getNodeId() const
{
    return node_id_;
}

bool MeshtasticRadioAdapter::broadcastNodeInfo()
{
    return sendNodeInfoTo(kBroadcastNodeId, false, chat::ChannelId::PRIMARY);
}

bool MeshtasticRadioAdapter::sendEncodedPayload(chat::ChannelId channel,
                                                const uint8_t* payload,
                                                size_t len,
                                                chat::NodeId dest,
                                                bool want_ack,
                                                chat::MessageId packet_id,
                                                bool publish_send_result)
{
    if (!ready_ || !payload || len == 0 || !board_.isRadioOnline())
    {
        return false;
    }

    size_t psk_len = 0;
    const uint8_t* psk = channelKeyFor(channel, &psk_len);
    const uint8_t channel_hash = channelHashFor(channel);
    const chat::NodeId dest_node = (dest != 0) ? dest : kBroadcastNodeId;
    const chat::MessageId msg_id = (packet_id != 0) ? packet_id : next_packet_id_++;

    uint8_t wire_buffer[512];
    size_t wire_size = sizeof(wire_buffer);
    if (!chat::meshtastic::buildWirePacket(payload, len, node_id_, msg_id,
                                           dest_node, channel_hash, config_.hop_limit,
                                           want_ack, psk, psk_len,
                                           wire_buffer, &wire_size))
    {
        return false;
    }

    const int state = board_.transmitRadio(wire_buffer, wire_size);
    const bool ok = (state == static_cast<int>(kRadioOk));
    if (ok)
    {
        rx_started_ = false;
        ensureReceiveStarted();
        if (publish_send_result)
        {
            sys::EventBus::publish(new sys::ChatSendResultEvent(msg_id, true), 0);
        }
    }
    else if (publish_send_result)
    {
        sys::EventBus::publish(new sys::ChatSendResultEvent(msg_id, false), 0);
    }

    ESP_LOGI(kTag,
             "tx from=%08lX to=%08lX id=%08lX ch=%u len=%u ok=%d",
             static_cast<unsigned long>(node_id_),
             static_cast<unsigned long>(dest_node),
             static_cast<unsigned long>(msg_id),
             static_cast<unsigned>(channel_hash),
             static_cast<unsigned>(wire_size),
             ok ? 1 : 0);
    return ok;
}

bool MeshtasticRadioAdapter::sendNodeInfoTo(chat::NodeId dest,
                                            bool want_response,
                                            chat::ChannelId channel)
{
    uint8_t payload[256];
    size_t payload_len = sizeof(payload);
    if (!chat::meshtastic::encodeNodeInfoMessage(std::to_string(node_id_),
                                                 user_long_name_,
                                                 user_short_name_,
                                                 meshtastic_HardwareModel_PRIVATE_HW,
                                                 mac_addr_,
                                                 nullptr,
                                                 0,
                                                 want_response,
                                                 payload,
                                                 &payload_len))
    {
        return false;
    }

    return sendEncodedPayload(channel,
                              payload,
                              payload_len,
                              dest == kBroadcastNodeId ? 0 : dest,
                              false,
                              0,
                              false);
}

bool MeshtasticRadioAdapter::sendRoutingAck(chat::NodeId dest,
                                            chat::MessageId request_id,
                                            chat::ChannelId channel)
{
    meshtastic_Routing routing = meshtastic_Routing_init_default;
    routing.which_variant = meshtastic_Routing_error_reason_tag;
    routing.error_reason = meshtastic_Routing_Error_NONE;

    uint8_t routing_buf[64];
    pb_ostream_t routing_stream = pb_ostream_from_buffer(routing_buf, sizeof(routing_buf));
    if (!pb_encode(&routing_stream, meshtastic_Routing_fields, &routing))
    {
        return false;
    }

    meshtastic_Data data = meshtastic_Data_init_default;
    data.portnum = meshtastic_PortNum_ROUTING_APP;
    data.dest = dest;
    data.source = node_id_;
    data.request_id = request_id;
    data.has_bitfield = true;
    data.bitfield = 0;
    data.payload.size = routing_stream.bytes_written;
    std::memcpy(data.payload.bytes, routing_buf, data.payload.size);

    uint8_t data_buf[128];
    pb_ostream_t data_stream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
    if (!pb_encode(&data_stream, meshtastic_Data_fields, &data))
    {
        return false;
    }

    return sendEncodedPayload(channel, data_buf, data_stream.bytes_written, dest, false, 0, false);
}

void MeshtasticRadioAdapter::processReceivedPacket(const uint8_t* data, size_t size)
{
    if (!data || size < sizeof(chat::meshtastic::PacketHeaderWire))
    {
        return;
    }

    chat::meshtastic::PacketHeaderWire header{};
    uint8_t payload[256];
    size_t payload_size = sizeof(payload);
    if (!chat::meshtastic::parseWirePacket(data, size, &header, payload, &payload_size))
    {
        return;
    }

    if (header.from == node_id_ || dedup_.isDuplicate(header.from, header.id))
    {
        return;
    }
    dedup_.markSeen(header.from, header.id);

    const chat::ChannelId channel =
        channel_from_hash(header.channel, primary_channel_hash_, secondary_channel_hash_);
    if (header.channel != channelHashFor(channel))
    {
        ESP_LOGI(kTag,
                 "drop unknown channel from=%08lX id=%08lX hash=0x%02X",
                 static_cast<unsigned long>(header.from),
                 static_cast<unsigned long>(header.id),
                 static_cast<unsigned>(header.channel));
        return;
    }

    size_t psk_len = 0;
    const uint8_t* psk = channelKeyFor(channel, &psk_len);
    uint8_t plaintext[256];
    size_t plaintext_len = sizeof(plaintext);
    if (psk_len > 0)
    {
        if (!chat::meshtastic::decryptPayload(header, payload, payload_size, psk, psk_len,
                                              plaintext, &plaintext_len))
        {
            return;
        }
    }
    else
    {
        std::memcpy(plaintext, payload, payload_size);
        plaintext_len = payload_size;
    }

    chat::RxMeta rx_meta{};
    fill_rx_meta(rx_meta, header, last_rx_rssi_, last_rx_snr_,
                 radio_freq_hz_, radio_bw_hz_, radio_sf_, radio_cr_);

    meshtastic_Data decoded = meshtastic_Data_init_default;
    pb_istream_t stream = pb_istream_from_buffer(plaintext, plaintext_len);
    if (!pb_decode(&stream, meshtastic_Data_fields, &decoded))
    {
        return;
    }

    node_last_channel_[header.from] = channel;

    const bool to_us = (header.to == node_id_);
    const bool is_broadcast = (header.to == kBroadcastNodeId);
    const bool want_ack = (header.flags & chat::meshtastic::PACKET_FLAGS_WANT_ACK_MASK) != 0;
    const bool want_response =
        decoded.want_response ||
        (decoded.has_bitfield && ((decoded.bitfield & kBitfieldWantResponseMask) != 0));

    if (want_ack && to_us)
    {
        (void)sendRoutingAck(header.from, header.id, channel);
    }

    if (decoded.portnum == meshtastic_PortNum_NODEINFO_APP)
    {
        if (decoded.payload.size > 0)
        {
            (void)decodeUserPayload(decoded.payload.bytes,
                                    decoded.payload.size,
                                    rx_meta,
                                    header.from,
                                    to_channel_index(channel));
        }
        if (want_response && (to_us || is_broadcast))
        {
            (void)sendNodeInfoTo(header.from, false, channel);
        }
        return;
    }

    if (decoded.portnum == meshtastic_PortNum_POSITION_APP && decoded.payload.size > 0)
    {
        meshtastic_Position pos = meshtastic_Position_init_zero;
        pb_istream_t pos_stream = pb_istream_from_buffer(decoded.payload.bytes, decoded.payload.size);
        if (pb_decode(&pos_stream, meshtastic_Position_fields, &pos))
        {
            publishPositionEvent(header.from, pos);
        }
    }

    if (decoded.portnum == meshtastic_PortNum_ROUTING_APP)
    {
        return;
    }

    chat::MeshIncomingText incoming_text{};
    if (chat::meshtastic::decodeTextMessage(plaintext, plaintext_len, &incoming_text))
    {
        incoming_text.from = header.from;
        incoming_text.to = header.to;
        incoming_text.msg_id = header.id;
        incoming_text.channel = channel;
        incoming_text.hop_limit = header.flags & chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
        incoming_text.encrypted = (psk_len > 0);
        incoming_text.rx_meta = rx_meta;
        text_queue_.push(incoming_text);
        return;
    }

    if (decoded.payload.size > 0)
    {
        chat::MeshIncomingData incoming_data{};
        incoming_data.portnum = decoded.portnum;
        incoming_data.from = header.from;
        incoming_data.to = header.to;
        incoming_data.packet_id = header.id;
        incoming_data.request_id = decoded.request_id;
        incoming_data.channel = channel;
        incoming_data.channel_hash = header.channel;
        incoming_data.hop_limit = header.flags & chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
        incoming_data.want_response = want_response;
        incoming_data.payload.assign(decoded.payload.bytes,
                                     decoded.payload.bytes + decoded.payload.size);
        incoming_data.rx_meta = rx_meta;
        data_queue_.push(incoming_data);
    }
}

void MeshtasticRadioAdapter::pollRadio()
{
    if (!ready_ || !board_.isRadioOnline())
    {
        return;
    }

    ensureReceiveStarted();

    const uint32_t irq = board_.getRadioIrqFlags();
    if (irq == 0)
    {
        return;
    }

    board_.clearRadioIrqFlags(irq);
    if ((irq & kIrqRxDone) == 0)
    {
        rx_started_ = false;
        ensureReceiveStarted();
        return;
    }

    const int packet_length = board_.getRadioPacketLength(true);
    if (packet_length <= 0 || packet_length > 255)
    {
        rx_started_ = false;
        ensureReceiveStarted();
        return;
    }

    uint8_t buffer[255];
    if (board_.readRadioData(buffer, static_cast<size_t>(packet_length)) == static_cast<int>(kRadioOk))
    {
        setLastRxStats(board_.getRadioRSSI(), board_.getRadioSNR());
        processReceivedPacket(buffer, static_cast<size_t>(packet_length));
    }

    if ((irq & (kIrqHeaderErr | kIrqCrcErr | kIrqTimeout)) != 0)
    {
        ESP_LOGI(kTag, "radio irq=0x%04lX", static_cast<unsigned long>(irq));
    }

    rx_started_ = false;
    ensureReceiveStarted();
}

void MeshtasticRadioAdapter::configureRadio()
{
    if (!board_.isRadioOnline())
    {
        ready_ = false;
        return;
    }

    auto region_code = static_cast<meshtastic_Config_LoRaConfig_RegionCode>(config_.region);
    if (region_code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
    {
        region_code = meshtastic_Config_LoRaConfig_RegionCode_CN;
    }
    const chat::meshtastic::RegionInfo* region = chat::meshtastic::findRegion(region_code);

    float bw_khz = 250.0f;
    uint8_t sf = 11;
    uint8_t cr_denom = 5;
    if (config_.use_preset)
    {
        const auto preset =
            static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(config_.modem_preset);
        chat::meshtastic::modemPresetToParams(preset, region->wide_lora, bw_khz, sf, cr_denom);
    }
    else
    {
        bw_khz = config_.bandwidth_khz;
        sf = config_.spread_factor;
        cr_denom = config_.coding_rate;
    }

    float freq_mhz = chat::meshtastic::computeFrequencyMhz(region, bw_khz, primary_channel_name(config_));
    if (config_.override_frequency_mhz > 0.0f)
    {
        freq_mhz = config_.override_frequency_mhz;
    }
    freq_mhz += config_.frequency_offset_mhz;

    int8_t tx_power = config_.tx_power;
    if (region->power_limit_dbm > 0)
    {
        tx_power = std::min<int8_t>(tx_power == 0 ? static_cast<int8_t>(region->power_limit_dbm) : tx_power,
                                    static_cast<int8_t>(region->power_limit_dbm));
    }
    if (tx_power == 0)
    {
        tx_power = 17;
    }

    board_.configureLoraRadio(freq_mhz, bw_khz, sf, cr_denom, tx_power,
                              kLoraPreambleLen, kLoraSyncWord, 2);
    radio_freq_hz_ = static_cast<uint32_t>(std::lround(freq_mhz * 1000000.0f));
    radio_bw_hz_ = static_cast<uint32_t>(std::lround(bw_khz * 1000.0f));
    radio_sf_ = sf;
    radio_cr_ = cr_denom;
    ready_ = true;
    rx_started_ = false;
    ensureReceiveStarted();

    ESP_LOGI(kTag,
             "radio ready node=%08lX region=%u freq=%.3f bw=%.1f sf=%u cr=4/%u tx=%d hash=(%02X,%02X)",
             static_cast<unsigned long>(node_id_),
             static_cast<unsigned>(region_code),
             freq_mhz,
             bw_khz,
             static_cast<unsigned>(sf),
             static_cast<unsigned>(cr_denom),
             static_cast<int>(tx_power),
             static_cast<unsigned>(primary_channel_hash_),
             static_cast<unsigned>(secondary_channel_hash_));
}

void MeshtasticRadioAdapter::updateChannelKeys()
{
    if (chat::meshtastic::isZeroKey(config_.primary_key, sizeof(config_.primary_key)))
    {
        chat::meshtastic::expandShortPsk(kDefaultPskIndex, primary_psk_, &primary_psk_len_);
    }
    else
    {
        std::memcpy(primary_psk_, config_.primary_key, sizeof(primary_psk_));
        primary_psk_len_ = sizeof(primary_psk_);
    }

    if (chat::meshtastic::isZeroKey(config_.secondary_key, sizeof(config_.secondary_key)))
    {
        std::memset(secondary_psk_, 0, sizeof(secondary_psk_));
        secondary_psk_len_ = 0;
    }
    else
    {
        std::memcpy(secondary_psk_, config_.secondary_key, sizeof(secondary_psk_));
        secondary_psk_len_ = sizeof(secondary_psk_);
    }

    primary_channel_hash_ = chat::meshtastic::computeChannelHash(primary_channel_name(config_),
                                                                 primary_psk_,
                                                                 primary_psk_len_);
    secondary_channel_hash_ = chat::meshtastic::computeChannelHash(kSecondaryChannelName,
                                                                   secondary_psk_len_ > 0 ? secondary_psk_ : nullptr,
                                                                   secondary_psk_len_);
}

void MeshtasticRadioAdapter::initNodeIdentity()
{
    std::memset(mac_addr_, 0, sizeof(mac_addr_));
    (void)esp_efuse_mac_get_default(mac_addr_);
    node_id_ = (static_cast<uint32_t>(mac_addr_[2]) << 24) |
               (static_cast<uint32_t>(mac_addr_[3]) << 16) |
               (static_cast<uint32_t>(mac_addr_[4]) << 8) |
               static_cast<uint32_t>(mac_addr_[5]);
}

void MeshtasticRadioAdapter::ensureReceiveStarted()
{
    if (!rx_started_)
    {
        rx_started_ = (board_.startRadioReceive() == static_cast<int>(kRadioOk));
    }
}

bool MeshtasticRadioAdapter::decodeUserPayload(const uint8_t* payload,
                                               size_t len,
                                               const chat::RxMeta& rx_meta,
                                               chat::NodeId from_node,
                                               uint8_t channel_index)
{
    if (!payload || len == 0)
    {
        return false;
    }

    meshtastic_User user = meshtastic_User_init_default;
    pb_istream_t user_stream = pb_istream_from_buffer(payload, len);
    if (!pb_decode(&user_stream, meshtastic_User_fields, &user))
    {
        return false;
    }

    char short_name[10] = {};
    char long_name[32] = {};
    const size_t short_len = strnlen(user.short_name, sizeof(user.short_name));
    const size_t long_len = strnlen(user.long_name, sizeof(user.long_name));
    std::memcpy(short_name, user.short_name, std::min(short_len, sizeof(short_name) - 1));
    std::memcpy(long_name, user.long_name, std::min(long_len, sizeof(long_name) - 1));

    auto* event = new sys::NodeInfoUpdateEvent(from_node,
                                               "",
                                               "",
                                               static_cast<float>(rx_meta.snr_db_x10) / 10.0f,
                                               static_cast<float>(rx_meta.rssi_dbm_x10) / 10.0f,
                                               chat::now_message_timestamp(),
                                               static_cast<uint8_t>(chat::contacts::NodeProtocolType::Meshtastic),
                                               static_cast<uint8_t>(user.role),
                                               rx_meta.hop_count,
                                               static_cast<uint8_t>(user.hw_model),
                                               channel_index);
    std::memcpy(event->short_name, short_name, sizeof(short_name));
    std::memcpy(event->long_name, long_name, sizeof(long_name));
    sys::EventBus::publish(event, 0);
    return true;
}

void MeshtasticRadioAdapter::publishPositionEvent(chat::NodeId node_id,
                                                  const meshtastic_Position& pos)
{
    if (node_id == 0 || !chat::meshtastic::hasValidPosition(pos))
    {
        return;
    }

    const bool has_altitude = pos.has_altitude || pos.has_altitude_hae;
    const int32_t altitude = pos.has_altitude ? pos.altitude : pos.altitude_hae;
    const uint32_t ts = pos.timestamp ? pos.timestamp : pos.time;
    sys::EventBus::publish(
        new sys::NodePositionUpdateEvent(node_id,
                                         pos.latitude_i,
                                         pos.longitude_i,
                                         has_altitude,
                                         altitude,
                                         ts,
                                         pos.precision_bits,
                                         pos.PDOP,
                                         pos.HDOP,
                                         pos.VDOP,
                                         pos.gps_accuracy),
        0);
}

uint8_t MeshtasticRadioAdapter::channelHashFor(chat::ChannelId channel) const
{
    return (channel == chat::ChannelId::SECONDARY) ? secondary_channel_hash_ : primary_channel_hash_;
}

const uint8_t* MeshtasticRadioAdapter::channelKeyFor(chat::ChannelId channel, size_t* out_len) const
{
    if (channel == chat::ChannelId::SECONDARY)
    {
        if (out_len)
        {
            *out_len = secondary_psk_len_;
        }
        return secondary_psk_;
    }

    if (out_len)
    {
        *out_len = primary_psk_len_;
    }
    return primary_psk_;
}

} // namespace apps::esp_idf
