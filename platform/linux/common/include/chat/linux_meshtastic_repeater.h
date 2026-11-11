#pragma once

#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "platform/linux/packet_radio.h"

namespace trailmate::linux_app::meshtastic_wire
{
// Physical-radio flooding planner. It preserves the encrypted payload and
// origin identity; transport/protocol owners decide when forwarding is enabled.
class Repeater
{
  public:
    struct Frame
    {
        std::array<uint8_t, 255> bytes{};
        size_t size = 0;
    };
    bool receive(uint32_t self, const platform::linux_runtime::RadioPacket& packet,
                 uint8_t primary_hash, uint8_t secondary_hash, uint32_t now, uint32_t random);
    bool take(uint32_t now, Frame& frame);
    void complete(const Frame& frame, uint32_t now, bool transmitted);
    void reset();
    size_t queued() const;

  private:
    struct Key
    {
        uint32_t from = 0, id = 0;
        uint8_t channel = 0;
        bool operator==(const Key& other) const;
    };
    struct Pending
    {
        Frame frame;
        Key key;
        uint32_t started = 0, due = 0;
        uint8_t failures = 0;
        bool valid = false;
    };
    struct Seen
    {
        Key key;
        uint32_t started = 0, forwarded = 0;
        uint8_t transmissions = 0;
        bool valid = false;
    };
    std::array<Pending, 8> pending_{};
    std::array<Seen, 64> seen_{};
    size_t cursor_ = 0;
};
} // namespace trailmate::linux_app::meshtastic_wire
