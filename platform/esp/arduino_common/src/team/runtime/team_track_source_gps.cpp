#include "platform/esp/arduino_common/team/runtime/team_track_source_gps.h"

#include "platform/esp/arduino_common/gps/gps_service_api.h"

namespace team::infra
{

bool TeamTrackSourceGps::readTrackPoint(proto::TeamTrackPoint* out_point)
{
    if (!out_point)
    {
        return false;
    }

    gps::GpsState gps_state = gps::gps_get_data();
    if (!gps_state.valid)
    {
        out_point->lat_e7 = 0;
        out_point->lon_e7 = 0;
        return false;
    }

    out_point->lat_e7 = static_cast<int32_t>(gps_state.lat * 1e7);
    out_point->lon_e7 = static_cast<int32_t>(gps_state.lng * 1e7);
    return true;
}

} // namespace team::infra
