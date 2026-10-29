#pragma once
#include <algorithm>
#include <cstdint>

namespace platform::linux_runtime
{
// Nonblocking retry gate. Call failed() after the operation finishes so a slow
// failure cannot consume its own cooldown. Clock is supplied by the owner.
class RetryBackoff
{
  public:
    explicit RetryBackoff(uint32_t initial_ms = 250, uint32_t maximum_ms = 5000)
        : initial_(std::max(1U, initial_ms)), maximum_(std::max(initial_, maximum_ms)) {}
    bool ready(uint32_t now) const { return !delay_ || uint32_t(now - failed_at_) >= delay_; }
    void failed(uint32_t now)
    {
        failed_at_ = now;
        delay_ = delay_ ? (delay_ > maximum_ / 2 ? maximum_ : std::min(maximum_, delay_ * 2)) : initial_;
    }
    void reset() { delay_ = 0; }
    uint32_t delay() const { return delay_; }

  private:
    uint32_t initial_, maximum_, failed_at_ = 0, delay_ = 0;
};
} // namespace platform::linux_runtime
