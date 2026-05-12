#pragma once

#include "gps/domain/location_fix.h"
#include "gps/domain/satellite_info.h"
#include "gps/domain/time_fact.h"
#include "gps/protocol/nmea/nmea_sentence.h"

#include <cstddef>
#include <cstdint>

namespace gps::nmea
{

class NmeaParser
{
  public:
    void reset();
    void feed(const uint8_t* bytes, std::size_t len);

    bool latestFix(LocationFix& out) const;
    bool latestTime(TimeSyncStatus& out) const;
    bool latestSatellites(SatelliteSnapshot& out) const;

    uint32_t fixRevision() const { return fix_revision_; }
    uint32_t timeRevision() const { return time_revision_; }
    uint32_t satelliteRevision() const { return satellite_revision_; }

  private:
    void processSentence(const NmeaSentence& sentence);
    void processRmc(char** fields, int field_count);
    void processGga(char** fields, int field_count);
    void processGsa(char** fields, int field_count);

    NmeaSentenceAssembler assembler_{};
    LocationFix latest_fix_{};
    TimeSyncStatus latest_time_{};
    SatelliteSnapshot latest_satellites_{};
    uint32_t fix_revision_ = 0;
    uint32_t time_revision_ = 0;
    uint32_t satellite_revision_ = 0;
    uint16_t last_date_year_ = 0;
    uint8_t last_date_month_ = 0;
    uint8_t last_date_day_ = 0;
};

} // namespace gps::nmea
