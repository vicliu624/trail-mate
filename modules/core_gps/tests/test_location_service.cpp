#include "gps/usecase/location_service.h"

#include <cassert>
#include <cstdint>
#include <string>

namespace
{
std::string withChecksum(const char* body)
{
    uint8_t checksum = 0;
    for (const char* p = body; p && *p; ++p)
    {
        checksum ^= static_cast<uint8_t>(*p);
    }
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out = "$";
    out += body;
    out += "*";
    out.push_back(kHex[(checksum >> 4) & 0x0F]);
    out.push_back(kHex[checksum & 0x0F]);
    out += "\n";
    return out;
}

class FakeTimeUpdater final : public gps::ITimeAuthorityUpdater
{
  public:
    void observeGpsTime(const gps::TimeSyncStatus& fact) override
    {
        gps_count++;
        last = fact;
    }

    void observeHostTime(const gps::TimeSyncStatus& fact) override
    {
        host_count++;
        last = fact;
    }

    int gps_count = 0;
    int host_count = 0;
    gps::TimeSyncStatus last{};
};

class FakeLocationEventSink final : public gps::ILocationEventSink
{
  public:
    void onLocationUpdated(const gps::LocationFix& fix) override
    {
        event_count++;
        last = fix;
    }

    int event_count = 0;
    gps::LocationFix last{};
};
} // namespace

int main()
{
    gps::nmea::NmeaParser parser;
    gps::GpsJitterFilterConfig cfg{};
    cfg.max_speed_mps = 500.0f;
    gps::GpsJitterFilter jitter(cfg);
    FakeTimeUpdater time;
    FakeLocationEventSink events;
    gps::LocationService service(parser, jitter, time, events);

    const std::string rmc = withChecksum("GPRMC,010203,A,2502.334,N,10243.098,E,001.0,090.0,010124,,,A");
    service.onGnssBytes(reinterpret_cast<const uint8_t*>(rmc.data()), rmc.size(), 1000, 0);

    gps::LocationFix fix{};
    assert(service.latestFix(fix));
    assert(fix.valid);
    assert(events.event_count == 1);
    assert(time.gps_count == 1);
    assert(time.last.epoch_valid);
    assert(events.last.observed_at_ms == 1000);

    const std::string jump = withChecksum("GPRMC,010204,A,2602.334,N,10343.098,E,001.0,090.0,010124,,,A");
    service.onGnssBytes(reinterpret_cast<const uint8_t*>(jump.data()), jump.size(), 2000, 2000);
    assert(events.event_count == 1);
    return 0;
}
