#pragma once

#include "gps/ports/i_location_event_sink.h"
#include "gps/ports/i_location_source.h"
#include "gps/ports/i_time_authority.h"
#include "gps/protocol/nmea/nmea_parser.h"
#include "gps/usecase/gps_jitter_filter.h"

#include <cstddef>
#include <cstdint>

namespace gps
{

class LocationService final : public ILocationSource
{
  public:
    LocationService(nmea::NmeaParser& parser,
                    GpsJitterFilter& jitter_filter,
                    ITimeAuthorityUpdater& time_updater,
                    ILocationEventSink& events);

    void reset();
    void onGnssBytes(const uint8_t* bytes, std::size_t len);
    void onGnssBytes(const uint8_t* bytes, std::size_t len, uint32_t now_ms, uint32_t last_motion_ms);

    bool latestFix(LocationFix& out) const override;

  private:
    nmea::NmeaParser& parser_;
    GpsJitterFilter& jitter_filter_;
    ITimeAuthorityUpdater& time_updater_;
    ILocationEventSink& events_;
    LocationFix latest_fix_{};
    uint32_t last_fix_revision_ = 0;
    uint32_t last_time_revision_ = 0;
};

} // namespace gps
