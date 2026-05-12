#include "gps/usecase/location_service.h"

namespace gps
{

LocationService::LocationService(nmea::NmeaParser& parser,
                                 GpsJitterFilter& jitter_filter,
                                 ITimeAuthorityUpdater& time_updater,
                                 ILocationEventSink& events)
    : parser_(parser),
      jitter_filter_(jitter_filter),
      time_updater_(time_updater),
      events_(events)
{
}

void LocationService::reset()
{
    parser_.reset();
    jitter_filter_.reset();
    latest_fix_ = {};
    last_fix_revision_ = 0;
    last_time_revision_ = 0;
}

void LocationService::onGnssBytes(const uint8_t* bytes, std::size_t len)
{
    onGnssBytes(bytes, len, 0, 0);
}

void LocationService::onGnssBytes(const uint8_t* bytes, std::size_t len,
                                  uint32_t now_ms, uint32_t last_motion_ms)
{
    parser_.feed(bytes, len);

    TimeSyncStatus time{};
    const uint32_t time_revision = parser_.timeRevision();
    if (time_revision != last_time_revision_ && parser_.latestTime(time))
    {
        if (time.observed_at_ms == 0)
        {
            time.observed_at_ms = now_ms;
        }
        time_updater_.observeGpsTime(time);
        last_time_revision_ = time_revision;
    }

    LocationFix fix{};
    const uint32_t fix_revision = parser_.fixRevision();
    if (fix_revision == last_fix_revision_ || !parser_.latestFix(fix))
    {
        return;
    }
    last_fix_revision_ = fix_revision;

    if (fix.observed_at_ms == 0)
    {
        fix.observed_at_ms = now_ms;
    }

    const auto decision = jitter_filter_.update(fix.latitude, fix.longitude, fix.observed_at_ms, last_motion_ms);
    if (!decision.accepted)
    {
        return;
    }

    latest_fix_ = fix;
    events_.onLocationUpdated(latest_fix_);
}

bool LocationService::latestFix(LocationFix& out) const
{
    if (!latest_fix_.valid)
    {
        out = {};
        return false;
    }
    out = latest_fix_;
    return true;
}

} // namespace gps
