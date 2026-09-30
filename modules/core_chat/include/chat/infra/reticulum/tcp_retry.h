#pragma once

#include <cstdint>

namespace chat::reticulum
{
// Endpoint-local retry state. No allocations, wall clock, or background task.
class TcpRetry
{
  public:
    static constexpr uint32_t kBaseDelayMs = 10000;
    static constexpr uint32_t kMaxDelayMs = 300000;
    static constexpr uint32_t kStableConnectionMs = 60000;

    bool ready(uint32_t now) const
    {
        return !waiting_ || static_cast<uint32_t>(now - since_) >= delay_;
    }

    void defer(uint32_t now)
    {
        since_ = now;
        waiting_ = true;
        online_ = false;
    }

    void failed(uint32_t now)
    {
        defer(now);
        if (failures_ < 6) ++failures_;
        delay_ = kBaseDelayMs << (failures_ - 1);
        if (delay_ > kMaxDelayMs) delay_ = kMaxDelayMs;
    }

    void connected(uint32_t now)
    {
        if (!online_)
        {
            since_ = now;
            online_ = true;
        }
        if (static_cast<uint32_t>(now - since_) >= kStableConnectionMs)
        {
            failures_ = 0;
            delay_ = kBaseDelayMs;
        }
        waiting_ = false;
    }

    void reset() { *this = TcpRetry{}; }
    uint8_t failures() const { return failures_; }
    bool online() const { return online_; }
    void disconnected() { online_ = false; }
    bool stable(uint32_t now) const
    {
        return online_ && static_cast<uint32_t>(now - since_) >= kStableConnectionMs;
    }

  private:
    uint32_t since_ = 0;
    uint32_t delay_ = kBaseDelayMs;
    uint8_t failures_ = 0;
    bool waiting_ = false;
    bool online_ = false;
};
} // namespace chat::reticulum
