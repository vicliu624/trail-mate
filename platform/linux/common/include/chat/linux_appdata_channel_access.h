#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace trailmate::linux_app
{
// Keep application data behind the receive/relay exchange that follows an RF
// packet. This is worker-thread scheduling: callers enqueue without sleeping.
class AppDataChannelAccess
{
  public:
    static uint32_t guardForLoRa(float bandwidth_khz, uint8_t sf, uint8_t cr, uint16_t preamble)
    {
        if (!std::isfinite(bandwidth_khz) || bandwidth_khz <= 0 || sf < 5 || sf > 12 || cr < 5 || cr > 8)
            return 4000;
        const double symbol_ms = (1U << sf) / static_cast<double>(bandwidth_khz);
        const unsigned de = symbol_ms >= 16 ? 2 : 0;
        const double payload = 8 + std::ceil((8 * 255 - 4 * sf + 28 + 16) / (4.0 * (sf - de))) * cr;
        const double airtime = (preamble + 4.25 + payload) * symbol_ms;
        // The current flooding planner schedules a relay within 1800 ms. Let
        // that relay finish before competing with it with another local frame.
        return static_cast<uint32_t>(std::clamp(std::ceil(1800 + airtime), 2000.0, 15000.0));
    }

    void reset(uint32_t guard_ms = 4000)
    {
        *this = {};
        guard_ms_ = guard_ms;
    }

    void beginAttempt(uint32_t now, uint32_t entropy)
    {
        if (attempt_) return;
        attempt_ = true;
        // Even an idle channel needs a contention interval: peers can enqueue
        // simultaneously before either receiver has detected the other's RF.
        const auto contention_due = now + 250 + entropy % 1250;
        if (!waiting_ || static_cast<int32_t>(contention_due - due_) > 0)
            due_ = contention_due;
        waiting_ = true;
    }

    void completeAttempt() { attempt_ = false; }

    void observe(uint32_t now, uint32_t received, uint32_t transmitted, uint32_t damaged,
                 float rssi, uint32_t entropy)
    {
        const bool changed = observed_ && (received != received_ || transmitted != transmitted_ || damaged != damaged_);
        observed_ = true;
        received_ = received;
        transmitted_ = transmitted;
        damaged_ = damaged;
        // Instant RSSI also sees a preamble/frame that has not completed yet.
        const bool carrier = std::isfinite(rssi) && rssi > -95 && rssi < 0;
        if (changed || carrier)
        {
            waiting_ = true;
            due_ = now + guard_ms_ + entropy % 1500;
        }
    }

    bool ready(uint32_t now) const
    {
        return !waiting_ || static_cast<int32_t>(now - due_) >= 0;
    }

    bool mayTransmit(uint32_t now) const { return attempt_ && ready(now); }

  private:
    uint32_t guard_ms_ = 4000, due_ = 0;
    uint32_t received_ = 0, transmitted_ = 0, damaged_ = 0;
    bool observed_ = false, waiting_ = false, attempt_ = false;
};
} // namespace trailmate::linux_app
