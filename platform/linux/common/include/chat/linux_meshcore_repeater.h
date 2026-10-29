#pragma once

#include "chat/linux_meshcore_group_wire.h"
#include "platform/linux/packet_radio.h"

namespace trailmate::linux_app::meshcore_wire
{
// Worker-owned wire planning only: no hardware, UI, identity changes or sleeps.
class Repeater
{
  public:
    enum class Disposition
    {
        Local,
        Transit
    };
    Disposition receive(const chat::MeshConfig& config, const std::array<uint8_t, 32>& own_key,
                        const platform::linux_runtime::RadioPacket& packet, uint32_t now, uint32_t random);
    void sent(const uint8_t* bytes, size_t size, uint32_t now);
    bool take(uint32_t now, Frame& frame);
    void reset();
    size_t queued() const;

  private:
    struct Pending
    {
        Frame frame;
        uint32_t signature = 0, started = 0, delay = 0, lifetime = 0;
        bool valid = false;
    };
    struct Seen
    {
        uint32_t signature = 0, started = 0;
        bool valid = false;
    };
    bool seen(uint32_t signature, uint32_t now) const;
    void remember(uint32_t signature, uint32_t now);
    bool enqueue(const Frame& frame, uint32_t signature, uint32_t now, uint32_t delay, uint32_t lifetime);
    std::array<Pending, 8> pending_{};
    std::array<Seen, 64> seen_{};
    size_t cursor_ = 0;
};
} // namespace trailmate::linux_app::meshcore_wire
