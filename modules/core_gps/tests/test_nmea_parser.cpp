#include "gps/protocol/nmea/nmea_parser.h"

#include <cassert>
#include <cstdint>
#include <cstring>
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
    out += "\r\n";
    return out;
}
} // namespace

int main()
{
    gps::nmea::NmeaParser parser;

    const std::string rmc = withChecksum("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,231123,,,A");
    parser.feed(reinterpret_cast<const uint8_t*>(rmc.data()), rmc.size());

    gps::TimeSyncStatus time{};
    assert(parser.latestTime(time));
    assert(time.epoch_valid);
    assert(time.source == gps::TimeSourceKind::Gps);

    gps::LocationFix fix{};
    assert(parser.latestFix(fix));
    assert(fix.valid);
    assert(fix.latitude > 48.117 && fix.latitude < 48.118);
    assert(fix.longitude > 11.516 && fix.longitude < 11.517);
    assert(fix.has_speed);
    assert(fix.has_course);

    const std::string gga = withChecksum("GPGGA,123520,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    parser.feed(reinterpret_cast<const uint8_t*>(gga.data()), gga.size());
    assert(parser.latestFix(fix));
    assert(fix.satellites == 8);
    assert(fix.has_altitude);
    assert(fix.altitude_m > 545.3f && fix.altitude_m < 545.5f);
    assert(fix.hdop > 0.8f && fix.hdop < 1.0f);

    const uint32_t before_bad = parser.fixRevision();
    const char* invalid = "$GPGGA,123520,0000.000,N,00000.000,E,1,01,9.9,0.0,M,0.0,M,,*00\r\n";
    parser.feed(reinterpret_cast<const uint8_t*>(invalid), std::strlen(invalid));
    assert(parser.fixRevision() == before_bad);

    gps::nmea::NmeaParser partial_parser;
    const std::string partial = withChecksum("GPRMC,010203,A,2502.334,N,10243.098,E,000.0,000.0,010124,,,A");
    partial_parser.feed(reinterpret_cast<const uint8_t*>(partial.data()), 10);
    assert(!partial_parser.latestFix(fix));
    partial_parser.feed(reinterpret_cast<const uint8_t*>(partial.data() + 10), partial.size() - 10);
    assert(partial_parser.latestFix(fix));
    assert(fix.latitude > 25.0 && fix.longitude > 102.0);
    return 0;
}
