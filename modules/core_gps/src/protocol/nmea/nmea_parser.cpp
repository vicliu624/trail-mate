#include "gps/protocol/nmea/nmea_parser.h"

#include <cstdlib>
#include <cstring>

namespace gps::nmea
{
namespace
{
constexpr double kKnotsToMps = 0.514444444;

bool isSentenceType(const char* field0, const char* type)
{
    return field0 && std::strlen(field0) >= 5 && std::strncmp(field0 + 2, type, 3) == 0;
}

bool parseUintField(const char* text, uint32_t& out)
{
    if (!text || text[0] == '\0')
    {
        return false;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text)
    {
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

double parseDoubleField(const char* text, double fallback = 0.0)
{
    if (!text || text[0] == '\0')
    {
        return fallback;
    }
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    return end == text ? fallback : value;
}

bool parseNmeaCoordinate(const char* value, const char* hemisphere, double& out)
{
    if (!value || !hemisphere || value[0] == '\0' || hemisphere[0] == '\0')
    {
        return false;
    }

    const double raw = parseDoubleField(value);
    const int degrees = static_cast<int>(raw / 100.0);
    const double minutes = raw - static_cast<double>(degrees * 100);
    double decimal = static_cast<double>(degrees) + minutes / 60.0;
    if (hemisphere[0] == 'S' || hemisphere[0] == 'W')
    {
        decimal = -decimal;
    }
    out = decimal;
    return true;
}

bool parseTimeParts(const char* value, uint8_t& hour, uint8_t& minute, uint8_t& second)
{
    if (!value || std::strlen(value) < 6)
    {
        return false;
    }
    char buf[3] = {};
    buf[0] = value[0];
    buf[1] = value[1];
    hour = static_cast<uint8_t>(std::atoi(buf));
    buf[0] = value[2];
    buf[1] = value[3];
    minute = static_cast<uint8_t>(std::atoi(buf));
    buf[0] = value[4];
    buf[1] = value[5];
    second = static_cast<uint8_t>(std::atoi(buf));
    return hour < 24 && minute < 60 && second < 60;
}

bool parseDateParts(const char* value, uint16_t& year, uint8_t& month, uint8_t& day)
{
    if (!value || std::strlen(value) < 6)
    {
        return false;
    }
    char buf[3] = {};
    buf[0] = value[0];
    buf[1] = value[1];
    day = static_cast<uint8_t>(std::atoi(buf));
    buf[0] = value[2];
    buf[1] = value[3];
    month = static_cast<uint8_t>(std::atoi(buf));
    buf[0] = value[4];
    buf[1] = value[5];
    const uint8_t yy = static_cast<uint8_t>(std::atoi(buf));
    year = static_cast<uint16_t>((yy >= 80U) ? (1900U + yy) : (2000U + yy));
    return year >= 1980 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

bool makeEpoch(uint16_t year, uint8_t month, uint8_t day,
               uint8_t hour, uint8_t minute, uint8_t second,
               uint64_t& out)
{
    if (year < 1970 || month == 0 || day == 0 || month > 12 || day > 31 ||
        hour > 23 || minute > 59 || second > 59)
    {
        return false;
    }
    const int64_t days = daysFromCivil(static_cast<int>(year), month, day);
    if (days < 0)
    {
        return false;
    }
    out = static_cast<uint64_t>(days) * 86400ULL +
          static_cast<uint64_t>(hour) * 3600ULL +
          static_cast<uint64_t>(minute) * 60ULL +
          static_cast<uint64_t>(second);
    return true;
}
} // namespace

void NmeaParser::reset()
{
    assembler_.reset();
    latest_fix_ = {};
    latest_time_ = {};
    latest_satellites_ = {};
    fix_revision_ = 0;
    time_revision_ = 0;
    satellite_revision_ = 0;
    last_date_year_ = 0;
    last_date_month_ = 0;
    last_date_day_ = 0;
}

void NmeaParser::feed(const uint8_t* bytes, std::size_t len)
{
    if (!bytes || len == 0)
    {
        return;
    }

    for (std::size_t i = 0; i < len; ++i)
    {
        NmeaSentence sentence{};
        if (assembler_.feed(bytes[i], sentence))
        {
            processSentence(sentence);
        }
    }
}

bool NmeaParser::latestFix(LocationFix& out) const
{
    if (!latest_fix_.valid)
    {
        out = {};
        return false;
    }
    out = latest_fix_;
    return true;
}

bool NmeaParser::latestTime(TimeSyncStatus& out) const
{
    if (!latest_time_.epoch_valid)
    {
        out = {};
        return false;
    }
    out = latest_time_;
    return true;
}

bool NmeaParser::latestSatellites(SatelliteSnapshot& out) const
{
    out = latest_satellites_;
    return latest_satellites_.count > 0 ||
           latest_satellites_.status.sats_in_use > 0 ||
           latest_satellites_.status.fix != GnssFix::NOFIX;
}

void NmeaParser::processSentence(const NmeaSentence& sentence)
{
    if (!sentence.checksum_valid || sentence.len == 0)
    {
        return;
    }

    char scratch[kMaxNmeaSentenceLen] = {};
    std::strncpy(scratch, sentence.text, sizeof(scratch) - 1U);
    char* star = std::strchr(scratch, '*');
    if (star)
    {
        *star = '\0';
    }

    char* start = scratch[0] == '$' ? scratch + 1 : scratch;
    char* fields[32] = {};
    int field_count = 0;
    fields[field_count++] = start;
    for (char* p = start; *p && field_count < 32; ++p)
    {
        if (*p == ',')
        {
            *p = '\0';
            fields[field_count++] = p + 1;
        }
    }

    if (field_count == 0)
    {
        return;
    }

    if (isSentenceType(fields[0], "RMC"))
    {
        processRmc(fields, field_count);
    }
    else if (isSentenceType(fields[0], "GGA"))
    {
        processGga(fields, field_count);
    }
    else if (isSentenceType(fields[0], "GSA"))
    {
        processGsa(fields, field_count);
    }
}

void NmeaParser::processRmc(char** fields, int field_count)
{
    if (field_count < 10 || !fields[2] || fields[2][0] != 'A')
    {
        return;
    }

    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint64_t epoch = 0;
    const bool has_time = parseTimeParts(fields[1], hour, minute, second);
    const bool has_date = parseDateParts(fields[9], year, month, day);
    if (has_date)
    {
        last_date_year_ = year;
        last_date_month_ = month;
        last_date_day_ = day;
    }
    if (has_time && has_date && makeEpoch(year, month, day, hour, minute, second, epoch))
    {
        latest_time_.epoch_valid = true;
        latest_time_.synced = true;
        latest_time_.source = TimeSourceKind::Gps;
        latest_time_.epoch_seconds = epoch;
        ++time_revision_;
    }

    double lat = 0.0;
    double lon = 0.0;
    if (!parseNmeaCoordinate(fields[3], fields[4], lat) ||
        !parseNmeaCoordinate(fields[5], fields[6], lon))
    {
        return;
    }

    latest_fix_.valid = true;
    latest_fix_.latitude = lat;
    latest_fix_.longitude = lon;
    latest_fix_.speed_mps = static_cast<float>(parseDoubleField(fields[7]) * kKnotsToMps);
    latest_fix_.course_deg = static_cast<float>(parseDoubleField(fields[8]));
    latest_fix_.has_speed = fields[7] && fields[7][0] != '\0';
    latest_fix_.has_course = fields[8] && fields[8][0] != '\0';
    if (epoch != 0)
    {
        latest_fix_.epoch_seconds = epoch;
    }
    ++fix_revision_;
}

void NmeaParser::processGga(char** fields, int field_count)
{
    if (field_count < 10)
    {
        return;
    }

    uint32_t fix_quality = 0;
    if (!parseUintField(fields[6], fix_quality) || fix_quality == 0)
    {
        latest_satellites_.status.fix = GnssFix::NOFIX;
        ++satellite_revision_;
        return;
    }

    double lat = 0.0;
    double lon = 0.0;
    if (!parseNmeaCoordinate(fields[2], fields[3], lat) ||
        !parseNmeaCoordinate(fields[4], fields[5], lon))
    {
        return;
    }

    uint32_t satellites = 0;
    (void)parseUintField(fields[7], satellites);
    const float hdop = static_cast<float>(parseDoubleField(fields[8]));

    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint64_t epoch = 0;
    if (parseTimeParts(fields[1], hour, minute, second) &&
        last_date_year_ != 0 &&
        makeEpoch(last_date_year_, last_date_month_, last_date_day_, hour, minute, second, epoch))
    {
        latest_time_.epoch_valid = true;
        latest_time_.synced = true;
        latest_time_.source = TimeSourceKind::Gps;
        latest_time_.epoch_seconds = epoch;
        ++time_revision_;
    }

    latest_fix_.valid = true;
    latest_fix_.latitude = lat;
    latest_fix_.longitude = lon;
    latest_fix_.satellites = static_cast<uint8_t>(satellites > 255U ? 255U : satellites);
    latest_fix_.hdop = hdop;
    latest_fix_.altitude_m = static_cast<float>(parseDoubleField(fields[9]));
    latest_fix_.has_altitude = fields[9] && fields[9][0] != '\0';
    if (epoch != 0)
    {
        latest_fix_.epoch_seconds = epoch;
    }
    latest_satellites_.status.sats_in_use = latest_fix_.satellites;
    latest_satellites_.status.hdop = hdop;
    latest_satellites_.status.fix = GnssFix::FIX3D;
    ++fix_revision_;
    ++satellite_revision_;
}

void NmeaParser::processGsa(char** fields, int field_count)
{
    if (field_count < 3)
    {
        return;
    }

    const int fix = std::atoi(fields[2]);
    if (fix <= 1)
    {
        latest_satellites_.status.fix = GnssFix::NOFIX;
    }
    else if (fix == 2)
    {
        latest_satellites_.status.fix = GnssFix::FIX2D;
    }
    else
    {
        latest_satellites_.status.fix = GnssFix::FIX3D;
    }
    if (field_count >= 17 && fields[16] && fields[16][0] != '\0')
    {
        latest_satellites_.status.hdop = static_cast<float>(parseDoubleField(fields[16]));
    }
    ++satellite_revision_;
}

} // namespace gps::nmea
