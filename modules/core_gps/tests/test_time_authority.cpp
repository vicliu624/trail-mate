#include "gps/usecase/time_authority.h"

#include <cassert>

namespace
{
class FakeClock final : public gps::IClock
{
  public:
    uint32_t nowMs() const override
    {
        return now_ms;
    }

    uint32_t now_ms = 0;
};
} // namespace

int main()
{
    FakeClock clock;
    gps::TimeAuthority authority(clock);

    uint64_t epoch = 0;
    assert(!authority.nowEpoch(epoch));

    gps::TimeSyncStatus invalid{};
    authority.observeGpsTime(invalid);
    assert(!authority.nowEpoch(epoch));

    gps::TimeSyncStatus host{};
    host.epoch_valid = true;
    host.synced = true;
    host.source = gps::TimeSourceKind::Host;
    host.epoch_seconds = 1700000000ULL;
    host.observed_at_ms = 1000;
    clock.now_ms = 1000;
    authority.observeHostTime(host);
    assert(authority.nowEpoch(epoch));
    assert(epoch == 1700000000ULL);

    clock.now_ms = 3500;
    assert(authority.nowEpoch(epoch));
    assert(epoch == 1700000002ULL);

    gps::TimeSyncStatus gps_time{};
    gps_time.epoch_valid = true;
    gps_time.synced = true;
    gps_time.source = gps::TimeSourceKind::Gps;
    gps_time.epoch_seconds = 1800000000ULL;
    gps_time.observed_at_ms = 4000;
    clock.now_ms = 4000;
    authority.observeGpsTime(gps_time);
    assert(authority.syncStatus().source == gps::TimeSourceKind::Gps);
    assert(authority.nowEpoch(epoch));
    assert(epoch == 1800000000ULL);
    return 0;
}
