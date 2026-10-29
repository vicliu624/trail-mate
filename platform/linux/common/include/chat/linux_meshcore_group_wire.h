#pragma once

#include "chat/domain/chat_types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace trailmate::linux_app::meshcore_wire
{
// Wire processing owns no hardware, persistence, UI or radio leases.
struct Frame
{
    std::array<uint8_t, 255> bytes{};
    std::size_t size = 0;
};
struct Incoming
{
    bool text = false;
    chat::ChannelId channel = chat::ChannelId::PRIMARY;
    chat::NodeId sender = 0;
    uint32_t timestamp = 0;
    uint32_t port = 0;
    uint32_t signature = 0;
    uint8_t channel_hash = 0;
    uint8_t hops = 0;
    std::array<uint8_t, 184> payload{};
    std::size_t size = 0;
};
bool encodeText(const chat::MeshConfig& config, chat::ChannelId channel,
                const std::string& text, uint32_t timestamp, Frame& frame);
bool encodeData(const chat::MeshConfig& config, chat::ChannelId channel,
                chat::NodeId sender, uint32_t port, const uint8_t* payload,
                std::size_t size, Frame& frame);
bool decode(const chat::MeshConfig& config, const uint8_t* bytes,
            std::size_t size, Incoming& incoming);
} // namespace trailmate::linux_app::meshcore_wire
