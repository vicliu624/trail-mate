/**
 * @file meshcore_adapter.cpp
 * @brief MeshCore protocol adapter implementation
 */

#include "meshcore_adapter.h"
#include "../../time_utils.h"
#include <Arduino.h>
#include <cstring>

namespace chat
{
namespace meshcore
{

namespace
{
constexpr uint8_t kRouteTypeFlood = 0x01;
constexpr uint8_t kPayloadTypeRawCustom = 0x0F;
constexpr uint8_t kPayloadTypeTxtMsg = 0x02;
constexpr uint8_t kPayloadVer1 = 0x00;

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

bool parsePacket(const uint8_t* data, size_t len, uint8_t& payload_type, const uint8_t*& payload, size_t& payload_len)
{
    if (!data || len < 2)
    {
        return false;
    }

    uint8_t header = data[0];
    uint8_t route_type = header & 0x03;
    payload_type = (header >> 2) & 0x0F;

    size_t index = 1;
    if (route_type == 0 || route_type == 3)
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

    uint8_t path_len = data[index++];
    if (index + path_len > len)
    {
        return false;
    }
    index += path_len;

    if (index > len)
    {
        return false;
    }

    payload = &data[index];
    payload_len = len - index;
    return true;
}
} // namespace

MeshCoreAdapter::MeshCoreAdapter(TLoRaPagerBoard& board)
    : board_(board),
      initialized_(false),
      last_raw_packet_len_(0),
      has_pending_raw_packet_(false),
      next_msg_id_(1)
{
    // TODO: Initialize MeshCore library
}

bool MeshCoreAdapter::sendText(ChannelId channel, const std::string& text,
                               MessageId* out_msg_id, NodeId peer)
{
    // TODO: Implement MeshCore message sending
    // This is a placeholder implementation

    if (!initialized_)
    {
        return false;
    }

    // Minimal implementation: send RAW_CUSTOM payload with plain text
    // This does not provide MeshCore encryption/authentication.
    (void)channel;
    (void)peer;

    if (text.empty())
    {
        return false;
    }

    uint8_t buffer[256];
    size_t index = 0;
    buffer[index++] = buildHeader(kRouteTypeFlood, kPayloadTypeRawCustom, kPayloadVer1);
    buffer[index++] = 0; // path_len = 0

    size_t max_payload = sizeof(buffer) - index;
    size_t payload_len = text.size();
    if (payload_len > max_payload)
    {
        payload_len = max_payload;
    }
    memcpy(&buffer[index], text.data(), payload_len);
    index += payload_len;

    int state = RADIOLIB_ERR_UNSUPPORTED;
#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    if (board_.isHardwareOnline(HW_RADIO_ONLINE))
    {
        state = board_.radio.transmit(buffer, index);
    }
#endif

    MESHCORE_LOG("[MESHCORE] TX raw len=%u hex=%s\n",
                 static_cast<unsigned>(index),
                 toHex(buffer, index).c_str());

    if (out_msg_id)
    {
        *out_msg_id = next_msg_id_++;
    }

    return (state == RADIOLIB_ERR_NONE);
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
    (void)channel;
    (void)portnum;
    (void)payload;
    (void)len;
    (void)dest;
    (void)want_ack;
    return false;
}

bool MeshCoreAdapter::pollIncomingData(MeshIncomingData* out)
{
    (void)out;
    return false;
}

void MeshCoreAdapter::applyConfig(const MeshConfig& config)
{
    config_ = config;

    // TODO: Apply MeshCore-specific configuration
    // - Radio frequency and modulation parameters
    // - Network keys and authentication
    // - Node identity and routing preferences

    // Mark as initialized once configuration is applied
    initialized_ = true;
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
    if (!data || size == 0)
    {
        return;
    }

    if (size <= sizeof(last_raw_packet_))
    {
        memcpy(last_raw_packet_, data, size);
        last_raw_packet_len_ = size;
        has_pending_raw_packet_ = true;
    }

    MESHCORE_LOG("[MESHCORE] RX raw len=%u hex=%s\n",
                 static_cast<unsigned>(size),
                 toHex(data, size).c_str());

    uint8_t payload_type = 0;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    if (!parsePacket(data, size, payload_type, payload, payload_len))
    {
        return;
    }

    // Minimal parsing for RAW_CUSTOM (treat payload as plain text)
    if (payload_type == kPayloadTypeRawCustom && payload && payload_len > 0)
    {
        MeshIncomingText incoming;
        incoming.channel = ChannelId::PRIMARY;
        incoming.from = 0;
        incoming.to = 0xFFFFFFFF;
        incoming.msg_id = next_msg_id_++;
        incoming.timestamp = now_message_timestamp();
        incoming.text.assign(reinterpret_cast<const char*>(payload), payload_len);
        incoming.hop_limit = 0;
        incoming.encrypted = false;
        receive_queue_.push(incoming);
    }

    // TODO: Implement full MeshCore TXT_MSG parsing and decryption
}

void MeshCoreAdapter::processSendQueue()
{
    // No queued sending for MeshCore in this placeholder
}

} // namespace meshcore
} // namespace chat
