/**
 * @file rnode_adapter.cpp
 * @brief Minimal RNode raw-payload mesh adapter
 */

#include "platform/esp/arduino_common/chat/infra/rnode/rnode_adapter.h"

#include "chat/time_utils.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace chat
{
namespace rnode
{

namespace
{
constexpr float kDefaultFrequencyMHz = 869.525f;
constexpr float kDefaultBandwidthKHz = 125.0f;
constexpr uint8_t kDefaultSpreadingFactor = 9;
constexpr uint8_t kDefaultCodingRate = 5;
constexpr int8_t kDefaultTxPowerDbm = 17;

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

} // namespace

RNodeAdapter::RNodeAdapter(LoraBoard& board)
    : board_(board)
{
}

MeshCapabilities RNodeAdapter::getCapabilities() const
{
    return MeshCapabilities{};
}

bool RNodeAdapter::sendText(ChannelId channel, const std::string& text,
                            MessageId* out_msg_id, NodeId peer)
{
    (void)channel;
    (void)text;
    (void)peer;
    if (out_msg_id)
    {
        *out_msg_id = 0;
    }
    return false;
}

bool RNodeAdapter::pollIncomingText(MeshIncomingText* out)
{
    (void)out;
    return false;
}

bool RNodeAdapter::sendAppData(ChannelId channel, uint32_t portnum,
                               const uint8_t* payload, size_t len,
                               NodeId dest, bool want_ack,
                               MessageId packet_id,
                               bool want_response)
{
    (void)channel;
    (void)dest;
    (void)want_ack;
    (void)want_response;

    // RNode air payloads are raw Reticulum/TNC bytes. We reserve port 0
    // for pass-through raw payload transmission and reject higher-level
    // app-data semantics until a Reticulum-compatible upper layer exists.
    if (!payload || len == 0 || portnum != 0 || !ready_ || !board_.isRadioOnline())
    {
        return false;
    }

    EncodedAirPacketSet air_packets{};
    const uint8_t sequence = static_cast<uint8_t>(((packet_id != 0 ? packet_id : next_sequence_) & 0x0FU));
    next_sequence_ = static_cast<uint8_t>((sequence + 1U) & 0x0FU);
    if (!encodeAirPacketSet(payload, len, sequence, &air_packets))
    {
        return false;
    }

    const int first_state = board_.transmitRadio(air_packets.first, air_packets.first_len);
    if (first_state != RADIOLIB_ERR_NONE)
    {
        startRadioReceive();
        return false;
    }

    if (air_packets.count > 1U)
    {
        const int second_state = board_.transmitRadio(air_packets.second, air_packets.second_len);
        if (second_state != RADIOLIB_ERR_NONE)
        {
            startRadioReceive();
            return false;
        }
    }

    startRadioReceive();
    return true;
}

bool RNodeAdapter::pollIncomingData(MeshIncomingData* out)
{
    if (!out || app_receive_queue_.empty())
    {
        return false;
    }

    *out = std::move(app_receive_queue_.front());
    app_receive_queue_.pop();
    return true;
}

void RNodeAdapter::applyConfig(const MeshConfig& config)
{
    config_ = config;

    const float freq_mhz =
        (config_.override_frequency_mhz > 0.0f) ? config_.override_frequency_mhz : kDefaultFrequencyMHz;
    const float bw_khz =
        (config_.bandwidth_khz > 0.0f) ? config_.bandwidth_khz : kDefaultBandwidthKHz;
    const uint8_t sf =
        clampValue<uint8_t>(config_.spread_factor != 0 ? config_.spread_factor : kDefaultSpreadingFactor, 5U, 12U);
    const uint8_t cr =
        clampValue<uint8_t>(config_.coding_rate != 0 ? config_.coding_rate : kDefaultCodingRate, 5U, 8U);
    const int8_t tx_power =
        clampValue<int8_t>(config_.tx_power != 0 ? config_.tx_power : kDefaultTxPowerDbm, -9, 22);

    radio_freq_hz_ = static_cast<uint32_t>(std::lround(freq_mhz * 1000000.0f));
    radio_bw_hz_ = static_cast<uint32_t>(std::lround(bw_khz * 1000.0f));
    radio_sf_ = sf;
    radio_cr_ = cr;

    const uint16_t preamble =
        chat::rnode::recommendPreambleSymbols(radio_bw_hz_, radio_sf_, radio_cr_);

    board_.configureLoraRadio(freq_mhz, bw_khz, sf, cr, tx_power, preamble, kSyncWord, kCrcLen);
    ready_ = true;
    startRadioReceive();
}

void RNodeAdapter::setLastRxStats(float rssi, float snr)
{
    last_rx_rssi_ = rssi;
    last_rx_snr_ = snr;
}

bool RNodeAdapter::isReady() const
{
    return ready_ && board_.isRadioOnline();
}

bool RNodeAdapter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    if (!has_pending_raw_packet_ || !out_data || max_len == 0)
    {
        return false;
    }

    const size_t copy_len = std::min(last_raw_packet_.len, max_len);
    memcpy(out_data, last_raw_packet_.data, copy_len);
    out_len = copy_len;
    has_pending_raw_packet_ = false;
    return true;
}

void RNodeAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    if (!data || size == 0)
    {
        return;
    }

    uint8_t payload[chat::rnode::kRNodeMaxPayloadSize] = {};
    size_t payload_len = sizeof(payload);
    bool complete = false;
    if (!feedAirPacket(&reassembly_, data, size, payload, &payload_len, &complete) || !complete)
    {
        return;
    }

    memcpy(last_raw_packet_.data, payload, payload_len);
    last_raw_packet_.len = payload_len;
    has_pending_raw_packet_ = true;
    enqueueIncomingData(payload, payload_len);
}

void RNodeAdapter::startRadioReceive()
{
    if (!board_.isRadioOnline())
    {
        return;
    }
    (void)board_.startRadioReceive();
}

void RNodeAdapter::enqueueIncomingData(const uint8_t* payload, size_t len)
{
    if (!payload || len == 0)
    {
        return;
    }

    MeshIncomingData incoming;
    incoming.portnum = 0;
    incoming.from = 0;
    incoming.to = 0;
    incoming.packet_id = now_message_timestamp();
    incoming.request_id = 0;
    incoming.channel = ChannelId::PRIMARY;
    incoming.channel_hash = 0xFF;
    incoming.hop_limit = 0xFF;
    incoming.want_response = false;
    incoming.payload.assign(payload, payload + len);

    incoming.rx_meta.rx_timestamp_ms = millis();
    const uint32_t epoch_s = now_epoch_seconds();
    if (is_valid_epoch(epoch_s))
    {
        incoming.rx_meta.rx_timestamp_s = epoch_s;
        incoming.rx_meta.time_source = RxTimeSource::DeviceUtc;
    }
    else
    {
        incoming.rx_meta.rx_timestamp_s = incoming.rx_meta.rx_timestamp_ms / 1000U;
        incoming.rx_meta.time_source = RxTimeSource::Uptime;
    }
    incoming.rx_meta.origin = RxOrigin::Mesh;
    incoming.rx_meta.direct = true;
    incoming.rx_meta.from_is = false;
    incoming.rx_meta.rssi_dbm_x10 = static_cast<int16_t>(std::lround(last_rx_rssi_ * 10.0f));
    incoming.rx_meta.snr_db_x10 = static_cast<int16_t>(std::lround(last_rx_snr_ * 10.0f));
    incoming.rx_meta.freq_hz = radio_freq_hz_;
    incoming.rx_meta.bw_hz = radio_bw_hz_;
    incoming.rx_meta.sf = radio_sf_;
    incoming.rx_meta.cr = radio_cr_;

    app_receive_queue_.push(std::move(incoming));
}

} // namespace rnode
} // namespace chat
