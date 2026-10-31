#pragma once
#include <cstdint>

namespace platform::shared::radio
{
// A board owns exactly one long-lived radio reference. Serialize calls with
// the board's lock; foreground tools own separate references in the driver.
class BoardRadioLease
{
  public:
    template <typename Radio, typename Clock>
    bool ensure(Radio& radio, Clock milliseconds)
    {
        if (acquired_ && radio.isOnline()) return true;
        if (retry_pending_ && static_cast<int32_t>(milliseconds() - retry_after_) < 0) return false;
        if (acquired_)
        {
            radio.release();
            acquired_ = false;
        }
        acquired_ = radio.acquire();
        retry_pending_ = !acquired_;
        if (retry_pending_) retry_after_ = milliseconds() + 1000u;
        return acquired_;
    }

  private:
    bool acquired_ = false;
    bool retry_pending_ = false;
    uint32_t retry_after_ = 0;
};
} // namespace platform::shared::radio
