#include "gps/usecase/time_authority.h"

#include "sys/clock.h"

namespace gps
{

TimeAuthority::TimeAuthority(IClock& clock, TimeAuthorityPolicy policy)
    : clock_(clock),
      policy_(policy)
{
}

uint32_t TimeAuthority::nowMonotonicMs() const
{
    return clock_.nowMs();
}

bool TimeAuthority::nowEpoch(uint64_t& out_epoch_seconds) const
{
    if (!status_.epoch_valid)
    {
        out_epoch_seconds = 0;
        return false;
    }

    const uint32_t delta_ms = clock_.nowMs() - status_.observed_at_ms;
    out_epoch_seconds = status_.epoch_seconds + delta_ms / 1000U;
    return true;
}

TimeSyncStatus TimeAuthority::syncStatus() const
{
    return status_;
}

void TimeAuthority::observeGpsTime(const TimeSyncStatus& fact)
{
    if (!shouldAcceptGpsTime(fact))
    {
        return;
    }

    status_ = fact;
    status_.source = TimeSourceKind::Gps;
    status_.synced = true;
    status_.epoch_valid = true;
}

void TimeAuthority::observeHostTime(const TimeSyncStatus& fact)
{
    if (!shouldAcceptHostTime(fact))
    {
        return;
    }

    status_ = fact;
    status_.source = TimeSourceKind::Host;
    status_.synced = true;
    status_.epoch_valid = true;
}

bool TimeAuthority::shouldAcceptGpsTime(const TimeSyncStatus& fact) const
{
    if (!fact.epoch_valid)
    {
        return false;
    }
    if (policy_.gps_overrides_host)
    {
        return true;
    }
    return !status_.synced || status_.source != TimeSourceKind::Host;
}

bool TimeAuthority::shouldAcceptHostTime(const TimeSyncStatus& fact) const
{
    if (!fact.epoch_valid || !policy_.host_time_fallback)
    {
        return false;
    }
    return !status_.synced || status_.source != TimeSourceKind::Gps;
}

uint32_t SystemClock::nowMs() const
{
    return sys::millis_now();
}

} // namespace gps
