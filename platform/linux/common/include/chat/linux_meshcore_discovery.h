#pragma once
#include "chat/linux_meshcore_group_wire.h"
#include "platform/linux/packet_radio.h"

namespace trailmate::linux_app::meshcore_wire
{
struct DiscoveredPeer
{
    chat::NodeId node = 0;
    uint8_t role = 0;
    int8_t snr_qdb = 0;
};
// Pure bounded scheduling/correlation state. Caller owns the radio and clock.
class Discovery
{
  public:
    static constexpr uint32_t window_ms = 5000;
    bool makeRequest(const chat::MeshConfig& config, uint32_t tag, Frame& frame) const;
    void arm(uint32_t tag, uint32_t now);
    bool listening(uint32_t now) const;
    void reset();
    bool receive(const chat::MeshConfig& config, const std::array<uint8_t, 32>& own_key,
                 const platform::linux_runtime::RadioPacket& packet, uint32_t now,
                 uint32_t epoch, uint32_t random, DiscoveredPeer* peer);
    bool takeResponse(uint32_t now, Frame& frame);

  private:
    struct Pending
    {
        Frame frame;
        uint32_t started = 0;
        uint32_t delay = 0;
        bool valid = false;
    };
    struct Seen
    {
        uint32_t value = 0, started = 0;
        bool valid = false;
    };
    bool remember(std::array<Seen, 32>& items, uint32_t value, uint32_t now);
    std::array<Pending, 8> pending_{};
    std::array<Seen, 32> requests_{}, replies_{};
    uint32_t tag_ = 0, started_ = 0;
    bool armed_ = false;
};
} // namespace trailmate::linux_app::meshcore_wire
