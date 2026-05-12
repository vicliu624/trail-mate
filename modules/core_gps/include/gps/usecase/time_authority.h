#pragma once

#include "gps/ports/i_time_authority.h"

namespace gps
{

class IClock
{
  public:
    virtual ~IClock() = default;

    virtual uint32_t nowMs() const = 0;
};

struct TimeAuthorityPolicy
{
    bool gps_overrides_host = true;
    bool host_time_fallback = true;
};

class TimeAuthority final : public ITimeAuthority, public ITimeAuthorityUpdater
{
  public:
    explicit TimeAuthority(IClock& clock, TimeAuthorityPolicy policy = TimeAuthorityPolicy{});

    uint32_t nowMonotonicMs() const override;
    bool nowEpoch(uint64_t& out_epoch_seconds) const override;
    TimeSyncStatus syncStatus() const override;

    void observeGpsTime(const TimeSyncStatus& fact) override;
    void observeHostTime(const TimeSyncStatus& fact) override;

  private:
    bool shouldAcceptGpsTime(const TimeSyncStatus& fact) const;
    bool shouldAcceptHostTime(const TimeSyncStatus& fact) const;

    IClock& clock_;
    TimeAuthorityPolicy policy_{};
    TimeSyncStatus status_{};
};

class SystemClock final : public IClock
{
  public:
    uint32_t nowMs() const override;
};

} // namespace gps
