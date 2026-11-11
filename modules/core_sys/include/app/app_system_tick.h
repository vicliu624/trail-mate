#pragma once

#include "sys/event_bus.h"

#include <cstdint>

namespace app
{

// Owned by the UI lifecycle. All targets use the same bounded event cadence;
// merely registering a SystemTick handler does not produce periodic work.
class AppSystemTickPublisher
{
  public:
    bool publish(uint32_t now_ms)
    {
        if (started_ && static_cast<uint32_t>(now_ms - last_attempt_ms_) < 1000U)
        {
            return false;
        }
        started_ = true;
        last_attempt_ms_ = now_ms;
        // EventBus owns the event, including when its bounded queue is full.
        // A failed attempt is also rate limited; never spin on backpressure.
        return sys::EventBus::publish(new sys::Event(sys::EventType::SystemTick), 0);
    }

  private:
    uint32_t last_attempt_ms_ = 0;
    bool started_ = false;
};

inline void publishSystemTick(uint32_t now_ms)
{
    static AppSystemTickPublisher publisher;
    (void)publisher.publish(now_ms);
}

} // namespace app
