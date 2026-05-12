#include "platform/esp/arduino_common/team/runtime/team_track_source_gps.h"

#include "platform/esp/arduino_common/gps/gps_service_api.h"

namespace team::infra
{

TeamTrackSourceGps::TeamTrackSourceGps()
    : location_source_(&gps::gps_location_source())
{
}

TeamTrackSourceGps::TeamTrackSourceGps(const gps::ILocationSource& location_source)
    : location_source_(&location_source)
{
}

bool TeamTrackSourceGps::readTrackPoint(proto::TeamTrackPoint* out_point)
{
    if (!out_point)
    {
        return false;
    }

    gps::LocationFix fix{};
    if (!location_source_ || !location_source_->latestFix(fix))
    {
        out_point->lat_e7 = 0;
        out_point->lon_e7 = 0;
        return false;
    }

    out_point->lat_e7 = static_cast<int32_t>(fix.latitude * 1e7);
    out_point->lon_e7 = static_cast<int32_t>(fix.longitude * 1e7);
    return true;
}

} // namespace team::infra
