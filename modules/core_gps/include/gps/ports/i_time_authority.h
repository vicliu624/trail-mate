#pragma once

#include "gps/domain/time_fact.h"

namespace gps
{

class ITimeAuthority
{
  public:
    virtual ~ITimeAuthority() = default;

    virtual uint32_t nowMonotonicMs() const = 0;
    virtual bool nowEpoch(uint64_t& out_epoch_seconds) const = 0;
    virtual TimeSyncStatus syncStatus() const = 0;
};

class ITimeAuthorityUpdater
{
  public:
    virtual ~ITimeAuthorityUpdater() = default;

    virtual void observeGpsTime(const TimeSyncStatus& fact) = 0;
    virtual void observeHostTime(const TimeSyncStatus& fact) = 0;
};

} // namespace gps
