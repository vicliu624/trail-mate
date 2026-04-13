#include "boards/gat562_mesh_evb_pro/gps_runtime.h"

#include "boards/gat562_mesh_evb_pro/board_profile.h"

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <time.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace boards::gat562_mesh_evb_pro
{
namespace
{

constexpr uint32_t kMinValidEpochSeconds = 1700000000UL;
constexpr std::size_t kNmeaFieldMax = 24;
constexpr std::size_t kGpsTailCanarySize = 32;
constexpr uint8_t kGpsTailCanaryByte = 0xA5;

uint32_t readSystemEpochSeconds()
{
    const time_t now = ::time(nullptr);
    if (now < static_cast<time_t>(kMinValidEpochSeconds))
    {
        return 0;
    }
    return static_cast<uint32_t>(now);
}

void syncSystemClockFromEpoch(uint32_t epoch_s)
{
    (void)epoch_s;
}

void fillCanary(uint8_t* bytes, std::size_t len)
{
    if (!bytes || len == 0)
    {
        return;
    }
    std::memset(bytes, static_cast<int>(kGpsTailCanaryByte), len);
}

bool canaryIntact(const uint8_t* bytes, std::size_t len)
{
    if (!bytes)
    {
        return false;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (bytes[i] != kGpsTailCanaryByte)
        {
            return false;
        }
    }
    return true;
}

int firstBadCanaryOffset(const uint8_t* bytes, std::size_t len)
{
    if (!bytes)
    {
        return -1;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (bytes[i] != kGpsTailCanaryByte)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void dumpCanaryHex(const uint8_t* bytes, std::size_t len, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!bytes || len == 0)
    {
        return;
    }

    static constexpr char kHex[] = "0123456789ABCDEF";
    std::size_t pos = 0;
    for (std::size_t i = 0; i < len && (pos + 3) < out_len; ++i)
    {
        const uint8_t b = bytes[i];
        out[pos++] = kHex[(b >> 4) & 0x0F];
        out[pos++] = kHex[b & 0x0F];
        if (i + 1 < len)
        {
            out[pos++] = ' ';
        }
    }
    out[pos] = '\0';
}

enum class CollectorSlot : uint8_t
{
    GPS = 0,
    GLN = 1,
    GAL = 2,
    BD = 3,
    UNKNOWN = 4,
};

CollectorSlot collectorSlotForTalker(const char* talker)
{
    if (!talker || talker[0] == '\0' || talker[1] == '\0')
    {
        return CollectorSlot::UNKNOWN;
    }
    if (talker[0] == 'G' && talker[1] == 'P')
    {
        return CollectorSlot::GPS;
    }
    if (talker[0] == 'G' && talker[1] == 'L')
    {
        return CollectorSlot::GLN;
    }
    if (talker[0] == 'G' && talker[1] == 'A')
    {
        return CollectorSlot::GAL;
    }
    if ((talker[0] == 'G' && talker[1] == 'B') || (talker[0] == 'B' && talker[1] == 'D'))
    {
        return CollectorSlot::BD;
    }
    return CollectorSlot::UNKNOWN;
}

::gps::GnssSystem systemForSlot(CollectorSlot slot)
{
    switch (slot)
    {
    case CollectorSlot::GPS:
        return ::gps::GnssSystem::GPS;
    case CollectorSlot::GLN:
        return ::gps::GnssSystem::GLN;
    case CollectorSlot::GAL:
        return ::gps::GnssSystem::GAL;
    case CollectorSlot::BD:
        return ::gps::GnssSystem::BD;
    default:
        return ::gps::GnssSystem::UNKNOWN;
    }
}

bool parseUint(const char* text, uint32_t* out)
{
    if (!text || !*text || !out)
    {
        return false;
    }
    char* end = nullptr;
    unsigned long value = std::strtoul(text, &end, 10);
    if (end == text)
    {
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

bool parseInt(const char* text, int* out)
{
    if (!text || !*text || !out)
    {
        return false;
    }
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (end == text)
    {
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool verifyChecksum(const char* line)
{
    if (!line || line[0] != '$')
    {
        return false;
    }

    const char* star = std::strchr(line, '*');
    if (!star || !star[1] || !star[2])
    {
        return true;
    }

    uint8_t checksum = 0;
    for (const char* cursor = line + 1; cursor < star; ++cursor)
    {
        checksum ^= static_cast<uint8_t>(*cursor);
    }

    char checksum_text[3] = {star[1], star[2], '\0'};
    char* end = nullptr;
    long expected = std::strtol(checksum_text, &end, 16);
    return end != checksum_text && checksum == static_cast<uint8_t>(expected & 0xFF);
}

std::size_t splitFields(char* sentence, std::array<char*, kNmeaFieldMax>& fields)
{
    fields.fill(nullptr);
    std::size_t count = 0;
    char* cursor = sentence;
    while (cursor && *cursor && count < fields.size())
    {
        fields[count++] = cursor;
        char* comma = std::strchr(cursor, ',');
        if (!comma)
        {
            break;
        }
        *comma = '\0';
        cursor = comma + 1;
    }
    return count;
}

uint8_t daysInMonth(int year, uint8_t month)
{
    static constexpr uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 0 || month > 12)
    {
        return 31;
    }
    if (month != 2)
    {
        return kDays[month - 1];
    }
    const bool leap = ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
    return leap ? 29 : 28;
}

bool gpsDateTimeValid(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    if (year < 2020 || year > 2100)
    {
        return false;
    }
    if (month < 1 || month > 12)
    {
        return false;
    }
    const uint8_t max_day = daysInMonth(year, month);
    if (day < 1 || day > max_day)
    {
        return false;
    }
    if (hour >= 24 || minute >= 60 || second >= 60)
    {
        return false;
    }
    return true;
}

int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

time_t gpsDateTimeToEpochUtc(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    const int64_t days = daysFromCivil(year, month, day);
    const int64_t sec_of_day =
        static_cast<int64_t>(hour) * 3600 + static_cast<int64_t>(minute) * 60 + static_cast<int64_t>(second);
    const int64_t epoch64 = days * 86400 + sec_of_day;
    if (epoch64 < 0 || epoch64 > static_cast<int64_t>(std::numeric_limits<time_t>::max()))
    {
        return static_cast<time_t>(-1);
    }
    return static_cast<time_t>(epoch64);
}

} // namespace

struct GpsRuntime::Impl
{
    static constexpr uint32_t kGuardValue = 0x47505352UL;

    struct GsvCollector
    {
        std::array<::gps::GnssSatInfo, ::gps::kMaxGnssSats> sats{};
        std::size_t count = 0;
    };

    uint32_t guard_begin = kGuardValue;
    TinyGPSPlus parser{};
    ::gps::GpsState data{};
    ::gps::GnssStatus status{};
    uint32_t last_motion_ms = 0;
    uint32_t collection_interval_ms = 60000;
    uint32_t motion_idle_timeout_ms = 0;
    uint8_t power_strategy = 0;
    uint8_t gnss_mode = 0;
    uint8_t sat_mask = 0;
    uint8_t nmea_output_hz = 0;
    uint8_t nmea_sentence_mask = 0;
    uint8_t motion_sensor_id = 0;
    uint32_t epoch_base_s = 0;
    uint32_t epoch_base_ms = 0;
    uint32_t last_nmea_ms = 0;
    uint32_t last_time_sync_log_ms = 0;
    uint32_t last_time_sync_epoch_logged = 0;
    uint32_t last_status_log_ms = 0;
    std::array<GsvCollector, 5> gsv{};
    std::array<::gps::GnssSatInfo, ::gps::kMaxGnssSats> sats{};
    std::array<uint16_t, ::gps::kMaxGnssSats> used_sat_ids{};
    char nmea_line[96] = {};
    std::size_t sat_count = 0;
    std::size_t used_sat_count = 0;
    std::size_t nmea_line_len = 0;
    bool enabled = true;
    bool powered = false;
    bool initialized = false;
    bool time_synced = false;
    bool nmea_seen = false;
    uint32_t last_sat_diag_log_ms = 0;
    uint8_t last_logged_parser_sats = 0xFF;
    uint8_t last_logged_status_in_view = 0xFF;
    uint8_t last_logged_status_in_use = 0xFF;
    std::size_t last_logged_snapshot_count = static_cast<std::size_t>(-1);
    bool last_logged_snapshot_available = false;
    bool first_nmea_sentence_logged = false;
    std::array<uint8_t, kGpsTailCanarySize> tail_canary{};
    uint32_t guard_end = kGuardValue;

    void initializeDebugGuards()
    {
        guard_begin = kGuardValue;
        guard_end = kGuardValue;
        fillCanary(tail_canary.data(), tail_canary.size());
    }

    void logMemoryLayout(const char* reason) const
    {
        Serial.printf(
            "[gat562][gps][mem] reason=%s impl=%p size=%u guard_begin@%p guard_end@%p canary@%p canary_len=%u parser@%p data@%p status@%p sats@%p used_ids@%p nmea@%p\n",
            reason ? reason : "unknown",
            static_cast<const void*>(this),
            static_cast<unsigned>(sizeof(*this)),
            static_cast<const void*>(&guard_begin),
            static_cast<const void*>(&guard_end),
            static_cast<const void*>(tail_canary.data()),
            static_cast<unsigned>(tail_canary.size()),
            static_cast<const void*>(&parser),
            static_cast<const void*>(&data),
            static_cast<const void*>(&status),
            static_cast<const void*>(sats.data()),
            static_cast<const void*>(used_sat_ids.data()),
            static_cast<const void*>(nmea_line));
    }

    bool usedSat(uint16_t sat_id) const
    {
        for (std::size_t i = 0; i < used_sat_count; ++i)
        {
            if (used_sat_ids[i] == sat_id)
            {
                return true;
            }
        }
        return false;
    }

    void mergeGnssSatellites()
    {
        sat_count = 0;
        for (std::size_t collector_index = 0; collector_index < gsv.size(); ++collector_index)
        {
            auto& collector = gsv[collector_index];
            for (std::size_t sat_index = 0; sat_index < collector.count && sat_count < sats.size(); ++sat_index)
            {
                auto sat = collector.sats[sat_index];
                sat.used = usedSat(sat.id);
                sats[sat_count++] = sat;
            }
        }

        status.sats_in_view = static_cast<uint8_t>(std::min<std::size_t>(sat_count, 255U));
        if (used_sat_count > 0)
        {
            status.sats_in_use = static_cast<uint8_t>(std::min<std::size_t>(used_sat_count, 255U));
        }
    }

    void clearObservations()
    {
        parser = TinyGPSPlus{};
        data = ::gps::GpsState{};
        status = ::gps::GnssStatus{};
        sats.fill(::gps::GnssSatInfo{});
        used_sat_ids.fill(0);
        gsv.fill(GsvCollector{});
        sat_count = 0;
        used_sat_count = 0;
        last_nmea_ms = 0;
        nmea_line_len = 0;
        nmea_line[0] = '\0';
        nmea_seen = false;
    }

    bool satelliteStateLooksCorrupt() const
    {
        if (guard_begin != kGuardValue || guard_end != kGuardValue)
        {
            return true;
        }
        if (!canaryIntact(tail_canary.data(), tail_canary.size()))
        {
            return true;
        }
        if (sat_count > sats.size())
        {
            return true;
        }
        if (used_sat_count > used_sat_ids.size())
        {
            return true;
        }
        if (status.sats_in_view > ::gps::kMaxGnssSats)
        {
            return true;
        }
        if (status.sats_in_use > ::gps::kMaxGnssSats)
        {
            return true;
        }
        if (!parser.satellites.isValid() && data.satellites == 0 && sat_count == ::gps::kMaxGnssSats)
        {
            return true;
        }
        return false;
    }

    void repairCorruptSatelliteState(const char* reason)
    {
        const int bad_offset = firstBadCanaryOffset(tail_canary.data(), tail_canary.size());
        const uint8_t bad_value =
            (bad_offset >= 0) ? tail_canary[static_cast<std::size_t>(bad_offset)] : 0;

        char canary_hex[3 * kGpsTailCanarySize + 1] = {};
        dumpCanaryHex(tail_canary.data(), tail_canary.size(), canary_hex, sizeof(canary_hex));

        Serial.printf(
            "[gat562][gps][corrupt] reason=%s impl=%p parser_sats=%u snapshot_count=%u used_count=%u status_view=%u status_use=%u guards=%08lX/%08lX canary_bad=%d bad_val=%02X canary=[%s]\n",
            reason ? reason : "unknown",
            static_cast<void*>(this),
            static_cast<unsigned>(parser.satellites.isValid() ? parser.satellites.value() : 0U),
            static_cast<unsigned>(sat_count),
            static_cast<unsigned>(used_sat_count),
            static_cast<unsigned>(status.sats_in_view),
            static_cast<unsigned>(status.sats_in_use),
            static_cast<unsigned long>(guard_begin),
            static_cast<unsigned long>(guard_end),
            bad_offset,
            static_cast<unsigned>(bad_value),
            canary_hex);

        logMemoryLayout("corrupt");

        guard_begin = kGuardValue;
        guard_end = kGuardValue;
        fillCanary(tail_canary.data(), tail_canary.size());

        sats.fill(::gps::GnssSatInfo{});
        used_sat_ids.fill(0);
        gsv.fill(GsvCollector{});
        sat_count = 0;
        used_sat_count = 0;
        status.sats_in_view = 0;
        status.sats_in_use = 0;
        data.satellites = parser.satellites.isValid()
                              ? static_cast<uint8_t>(std::min<uint32_t>(parser.satellites.value(), 255U))
                              : 0U;
    }

    void parseGsaSentence(const std::array<char*, kNmeaFieldMax>& fields, std::size_t count)
    {
        if (count < 4)
        {
            return;
        }

        used_sat_count = 0;
        for (std::size_t i = 3; i <= 14 && i < count; ++i)
        {
            uint32_t sat_id = 0;
            if (!parseUint(fields[i], &sat_id) || sat_id == 0 || used_sat_count >= used_sat_ids.size())
            {
                continue;
            }
            used_sat_ids[used_sat_count++] = static_cast<uint16_t>(sat_id);
        }
        mergeGnssSatellites();
        if (satelliteStateLooksCorrupt())
        {
            repairCorruptSatelliteState("parse_gsa");
        }
    }

    void parseGsvSentence(const char* talker, const std::array<char*, kNmeaFieldMax>& fields, std::size_t count)
    {
        if (count < 4)
        {
            return;
        }

        const CollectorSlot slot = collectorSlotForTalker(talker);
        auto& collector = gsv[static_cast<std::size_t>(slot)];

        uint32_t msg_num = 0;
        if (!parseUint(fields[2], &msg_num))
        {
            return;
        }

        uint32_t sats_in_view = 0;
        if (parseUint(fields[3], &sats_in_view))
        {
            status.sats_in_view = static_cast<uint8_t>(std::min<uint32_t>(sats_in_view, 255U));
        }

        if (msg_num == 1)
        {
            collector.count = 0;
        }

        for (std::size_t base = 4; base + 3 < count && collector.count < collector.sats.size(); base += 4)
        {
            uint32_t sat_id = 0;
            if (!parseUint(fields[base], &sat_id) || sat_id == 0)
            {
                continue;
            }

            ::gps::GnssSatInfo sat{};
            sat.id = static_cast<uint16_t>(sat_id);
            sat.sys = systemForSlot(slot);

            uint32_t elevation = 0;
            if (parseUint(fields[base + 1], &elevation))
            {
                sat.elevation = static_cast<uint8_t>(std::min<uint32_t>(elevation, 90U));
            }

            uint32_t azimuth = 0;
            if (parseUint(fields[base + 2], &azimuth))
            {
                sat.azimuth = static_cast<uint16_t>(std::min<uint32_t>(azimuth, 359U));
            }

            int snr = -1;
            if (parseInt(fields[base + 3], &snr))
            {
                sat.snr = static_cast<int8_t>(std::clamp(snr, -1, 99));
            }

            collector.sats[collector.count++] = sat;
        }

        mergeGnssSatellites();
        if (satelliteStateLooksCorrupt())
        {
            repairCorruptSatelliteState("parse_gsv");
        }
    }

    void parseNmeaSentence(char* sentence)
    {
        if (!sentence || sentence[0] != '$' || !verifyChecksum(sentence))
        {
            return;
        }

        char* payload = sentence + 1;
        char* star = std::strchr(payload, '*');
        if (star)
        {
            *star = '\0';
        }

        std::array<char*, kNmeaFieldMax> fields{};
        const std::size_t count = splitFields(payload, fields);
        if (count == 0 || !fields[0] || std::strlen(fields[0]) < 5)
        {
            return;
        }

        char talker[3] = {fields[0][0], fields[0][1], '\0'};
        const char* type = fields[0] + std::strlen(fields[0]) - 3;

        if (!first_nmea_sentence_logged)
        {
            first_nmea_sentence_logged = true;
            Serial.printf("[gat562][gps][flow] first_nmea talker=%s type=%s raw_sats=%u\n",
                          talker,
                          type,
                          static_cast<unsigned>(parser.satellites.isValid() ? parser.satellites.value() : 0U));
        }

        if (std::strcmp(type, "GSA") == 0)
        {
            parseGsaSentence(fields, count);
        }
        else if (std::strcmp(type, "GSV") == 0)
        {
            parseGsvSentence(talker, fields, count);
        }
    }

    void processNmeaChar(char ch)
    {
        if (ch == '$')
        {
            nmea_line_len = 0;
            nmea_line[nmea_line_len++] = ch;
            return;
        }

        if (nmea_line_len == 0)
        {
            return;
        }

        if (ch == '\r' || ch == '\n')
        {
            if (nmea_line_len > 0)
            {
                nmea_line[nmea_line_len] = '\0';
                parseNmeaSentence(nmea_line);
                nmea_line_len = 0;
            }
            return;
        }

        if (nmea_line_len + 1 < sizeof(nmea_line))
        {
            nmea_line[nmea_line_len++] = ch;
        }
        else
        {
            nmea_line_len = 0;
        }
    }

    void applyTimeIfValid()
    {
        if (!parser.time.isValid() || !parser.date.isValid())
        {
            return;
        }

        const uint16_t year = parser.date.year();
        const uint8_t month = parser.date.month();
        const uint8_t day = parser.date.day();
        const uint8_t hour = parser.time.hour();
        const uint8_t minute = parser.time.minute();
        const uint8_t second = parser.time.second();

        if (!gpsDateTimeValid(year, month, day, hour, minute, second))
        {
            return;
        }

        const time_t utc = gpsDateTimeToEpochUtc(year, month, day, hour, minute, second);
        if (utc < static_cast<time_t>(kMinValidEpochSeconds))
        {
            return;
        }

        const uint32_t utc_s = static_cast<uint32_t>(utc);
        if (epoch_base_s == utc_s)
        {
            return;
        }
        const uint32_t prev_epoch_s = epoch_base_s;
        epoch_base_s = utc_s;
        epoch_base_ms = millis();
        time_synced = true;
        if (last_time_sync_epoch_logged != utc_s || (epoch_base_ms - last_time_sync_log_ms) >= 1000U)
        {
            last_time_sync_epoch_logged = utc_s;
            last_time_sync_log_ms = epoch_base_ms;
            Serial.printf(
                "[gat562][gps] time sync source=gnss epoch=%lu prev=%lu sats=%u fix=%u age_ms=%lu date=%04u-%02u-%02u time=%02u:%02u:%02u\n",
                static_cast<unsigned long>(utc_s),
                static_cast<unsigned long>(prev_epoch_s),
                static_cast<unsigned>(parser.satellites.isValid() ? parser.satellites.value() : 0U),
                static_cast<unsigned>(parser.location.isValid() ? 1U : 0U),
                static_cast<unsigned long>(parser.location.isValid() ? parser.location.age() : 0U),
                static_cast<unsigned>(year),
                static_cast<unsigned>(month),
                static_cast<unsigned>(day),
                static_cast<unsigned>(hour),
                static_cast<unsigned>(minute),
                static_cast<unsigned>(second));
        }
        syncSystemClockFromEpoch(utc_s);
    }

    void logStatusIfDue()
    {
        if (!initialized || !enabled)
        {
            return;
        }

        const uint32_t now_ms = millis();
        const uint32_t interval_ms = collection_interval_ms > 0 ? collection_interval_ms : 60000U;
        if ((now_ms - last_status_log_ms) < interval_ms)
        {
            return;
        }
        last_status_log_ms = now_ms;

        const bool time_valid = parser.time.isValid();
        const bool date_valid = parser.date.isValid();
        const bool fix_valid = parser.location.isValid();
        const uint16_t year = date_valid ? parser.date.year() : 0U;
        const uint8_t month = date_valid ? parser.date.month() : 0U;
        const uint8_t day = date_valid ? parser.date.day() : 0U;
        const uint8_t hour = time_valid ? parser.time.hour() : 0U;
        const uint8_t minute = time_valid ? parser.time.minute() : 0U;
        const uint8_t second = time_valid ? parser.time.second() : 0U;
        const bool datetime_shape_valid =
            time_valid && date_valid && gpsDateTimeValid(year, month, day, hour, minute, second);
        const time_t utc = datetime_shape_valid ? gpsDateTimeToEpochUtc(year, month, day, hour, minute, second)
                                                : static_cast<time_t>(0);
        const bool epoch_ok = utc >= static_cast<time_t>(kMinValidEpochSeconds);
        const uint32_t sat_count_parser = parser.satellites.isValid() ? parser.satellites.value() : 0U;
        const uint32_t nmea_age_ms = last_nmea_ms > 0 ? (now_ms - last_nmea_ms) : 0U;
        const char* state = "idle";
        if (!nmea_seen)
        {
            state = "no_nmea";
        }
        else if (!time_valid || !date_valid)
        {
            state = "time_invalid";
        }
        else if (!datetime_shape_valid)
        {
            state = "datetime_reject";
        }
        else if (!epoch_ok)
        {
            state = "epoch_reject";
        }
        else if (sat_count_parser == 0U)
        {
            state = "time_only";
        }
        else if (!fix_valid)
        {
            state = "search_fix";
        }
        else if (!time_synced)
        {
            state = "ready_unsynced";
        }
        else
        {
            state = "synced";
        }

        Serial.printf(
            "[gat562][gps] status state=%s enabled=%u powered=%u nmea=%u nmea_age_ms=%lu time=%u date=%u fix=%u sats=%u lat=%.6f lng=%.6f epoch=%lu utc=%lu dt=%04u-%02u-%02uT%02u:%02u:%02u\n",
            state,
            static_cast<unsigned>(enabled ? 1 : 0),
            static_cast<unsigned>(powered ? 1 : 0),
            static_cast<unsigned>(nmea_seen ? 1 : 0),
            static_cast<unsigned long>(nmea_age_ms),
            static_cast<unsigned>(time_valid ? 1 : 0),
            static_cast<unsigned>(date_valid ? 1 : 0),
            static_cast<unsigned>(fix_valid ? 1 : 0),
            static_cast<unsigned>(sat_count_parser),
            fix_valid ? parser.location.lat() : 0.0,
            fix_valid ? parser.location.lng() : 0.0,
            static_cast<unsigned long>(epoch_base_s),
            static_cast<unsigned long>(epoch_ok ? static_cast<uint32_t>(utc) : 0U),
            static_cast<unsigned>(year),
            static_cast<unsigned>(month),
            static_cast<unsigned>(day),
            static_cast<unsigned>(hour),
            static_cast<unsigned>(minute),
            static_cast<unsigned>(second));
    }

    void refreshFix()
    {
        data.valid = parser.location.isValid();
        data.lat = parser.location.lat();
        data.lng = parser.location.lng();
        data.has_alt = parser.altitude.isValid();
        data.alt_m = data.has_alt ? parser.altitude.meters() : 0.0;
        data.has_speed = parser.speed.isValid();
        data.speed_mps = data.has_speed ? (parser.speed.kmph() / 3.6) : 0.0;
        data.has_course = parser.course.isValid();
        data.course_deg = data.has_course ? parser.course.deg() : 0.0;
        data.satellites = static_cast<uint8_t>(
            std::min<uint32_t>(parser.satellites.isValid() ? parser.satellites.value() : 0U, 255U));
        data.age = parser.location.isValid() ? static_cast<uint32_t>(parser.location.age()) : 0xFFFFFFFFUL;

        status.sats_in_use = used_sat_count > 0
                                 ? static_cast<uint8_t>(std::min<std::size_t>(used_sat_count, 255U))
                                 : data.satellites;
        status.sats_in_view = sat_count > 0
                                  ? static_cast<uint8_t>(std::min<std::size_t>(sat_count, 255U))
                                  : data.satellites;

        if (sat_count == 0 && data.satellites == 0)
        {
            status.sats_in_use = 0;
            status.sats_in_view = 0;
        }

        status.hdop = parser.hdop.isValid() ? static_cast<float>(parser.hdop.hdop()) : 0.0f;
        status.fix = data.valid ? (data.has_alt ? ::gps::GnssFix::FIX3D : ::gps::GnssFix::FIX2D)
                                : ::gps::GnssFix::NOFIX;

        if (data.valid)
        {
            last_motion_ms = millis();
        }

        if (satelliteStateLooksCorrupt())
        {
            repairCorruptSatelliteState("refresh_fix");
        }
    }

    void logSatelliteFlowIfChanged()
    {
        const uint32_t now_ms = millis();
        const uint8_t parser_sats =
            static_cast<uint8_t>(std::min<uint32_t>(parser.satellites.isValid() ? parser.satellites.value() : 0U, 255U));
        const uint8_t status_in_view = status.sats_in_view;
        const uint8_t status_in_use = status.sats_in_use;
        const std::size_t snapshot_count = sat_count;
        const bool snapshot_available = data.valid || data.satellites > 0 || sat_count > 0;

        const bool changed = (parser_sats != last_logged_parser_sats) || (status_in_view != last_logged_status_in_view) ||
                             (status_in_use != last_logged_status_in_use) ||
                             (snapshot_count != last_logged_snapshot_count) ||
                             (snapshot_available != last_logged_snapshot_available);
        if (!changed && (now_ms - last_sat_diag_log_ms) < 3000U)
        {
            return;
        }

        last_sat_diag_log_ms = now_ms;
        last_logged_parser_sats = parser_sats;
        last_logged_status_in_view = status_in_view;
        last_logged_status_in_use = status_in_use;
        last_logged_snapshot_count = snapshot_count;
        last_logged_snapshot_available = snapshot_available;

        Serial.printf(
            "[gat562][gps][flow] parser_sats=%u snapshot_count=%u status_view=%u status_use=%u data_valid=%u nmea=%u fix=%u\n",
            static_cast<unsigned>(parser_sats),
            static_cast<unsigned>(snapshot_count),
            static_cast<unsigned>(status_in_view),
            static_cast<unsigned>(status_in_use),
            static_cast<unsigned>(data.valid ? 1 : 0),
            static_cast<unsigned>(nmea_seen ? 1 : 0),
            static_cast<unsigned>(status.fix != ::gps::GnssFix::NOFIX ? 1 : 0));
    }
};

GpsRuntime::GpsRuntime()
    : impl_(new Impl())
{
    if (impl_)
    {
        impl_->initializeDebugGuards();
        impl_->logMemoryLayout("ctor");
    }
}

GpsRuntime::~GpsRuntime()
{
    delete impl_;
    impl_ = nullptr;
}

GpsRuntime::Impl* GpsRuntime::impl()
{
    return impl_;
}

const GpsRuntime::Impl* GpsRuntime::impl() const
{
    return impl_;
}

bool GpsRuntime::start(const app::AppConfig& config)
{
    if (!begin(config))
    {
        return false;
    }
    applyConfig(config);
    return true;
}

bool GpsRuntime::begin(const app::AppConfig& config)
{
    (void)config;
    auto& s = *impl();
    if (!s.initialized)
    {
        const auto& profile = kBoardProfile;
        if (profile.gps.uart.aux >= 0)
        {
            pinMode(profile.gps.uart.aux, OUTPUT);
            digitalWrite(profile.gps.uart.aux, HIGH);
        }
        Serial1.setPins(profile.gps.uart.rx, profile.gps.uart.tx);
        Serial1.begin(profile.gps.baud_rate);
        s.initialized = true;
        s.powered = true;
        s.logMemoryLayout("begin");
    }
    return true;
}

void GpsRuntime::applyConfig(const app::AppConfig& config)
{
    auto& s = *impl();
    s.collection_interval_ms = config.gps_interval_ms;
    s.power_strategy = config.gps_strategy;
    s.gnss_mode = config.gps_mode;
    s.sat_mask = config.gps_sat_mask;
    s.nmea_output_hz = config.privacy_nmea_output;
    s.nmea_sentence_mask = config.privacy_nmea_sentence;
    s.motion_idle_timeout_ms = config.motion_config.idle_timeout_ms;
    s.motion_sensor_id = config.motion_config.sensor_id;
    s.enabled = (config.gps_mode != 0);
    if (!s.enabled)
    {
        s.clearObservations();
    }
    Serial.printf(
        "[gat562][gps] config enabled=%u interval_ms=%lu strategy=%u mode=%u sat_mask=0x%02X nmea_hz=%u nmea_mask=0x%02X motion_idle_ms=%lu motion_sensor=%u\n",
        static_cast<unsigned>(s.enabled ? 1 : 0),
        static_cast<unsigned long>(s.collection_interval_ms),
        static_cast<unsigned>(s.power_strategy),
        static_cast<unsigned>(s.gnss_mode),
        static_cast<unsigned>(s.sat_mask),
        static_cast<unsigned>(s.nmea_output_hz),
        static_cast<unsigned>(s.nmea_sentence_mask),
        static_cast<unsigned long>(s.motion_idle_timeout_ms),
        static_cast<unsigned>(s.motion_sensor_id));
}

void GpsRuntime::tick()
{
    auto& s = *impl();
    if (!s.initialized || !s.enabled)
    {
        return;
    }

    if (s.satelliteStateLooksCorrupt())
    {
        s.repairCorruptSatelliteState("tick_pre");
    }

    while (Serial1.available() > 0)
    {
        s.nmea_seen = true;
        s.last_nmea_ms = millis();
        const char ch = static_cast<char>(Serial1.read());
        s.parser.encode(ch);
        s.processNmeaChar(ch);
    }

    s.applyTimeIfValid();
    s.refreshFix();

    if (s.satelliteStateLooksCorrupt())
    {
        s.repairCorruptSatelliteState("tick_post_refresh");
    }

    s.logSatelliteFlowIfChanged();
    s.logStatusIfDue();
}

bool GpsRuntime::isReady() const
{
    const auto& s = *impl();
    return s.initialized && s.powered;
}

::gps::GpsState GpsRuntime::data() const
{
    return impl()->data;
}

bool GpsRuntime::enabled() const
{
    return impl()->enabled;
}

bool GpsRuntime::powered() const
{
    return impl()->powered;
}

uint32_t GpsRuntime::lastMotionMs() const
{
    return impl()->last_motion_ms;
}

bool GpsRuntime::gnssSnapshot(::gps::GnssSatInfo* out,
                              std::size_t max,
                              std::size_t* out_count,
                              ::gps::GnssStatus* status) const
{
    auto& s = *const_cast<Impl*>(impl());

    if (s.satelliteStateLooksCorrupt())
    {
        s.logMemoryLayout("snapshot_pre_corrupt");
        s.repairCorruptSatelliteState("snapshot");
    }

    const bool snapshot_available = s.data.valid || s.data.satellites > 0 || s.sat_count > 0;
    if (out_count)
    {
        *out_count = 0;
    }
    if (status)
    {
        *status = s.status;
    }
    if (out && max > 0 && s.sat_count > 0)
    {
        const std::size_t copy_count = std::min<std::size_t>(max, s.sat_count);
        for (std::size_t index = 0; index < copy_count; ++index)
        {
            out[index] = s.sats[index];
        }
        if (out_count)
        {
            *out_count = copy_count;
        }
        return true;
    }
    if (snapshot_available)
    {
        Serial.printf(
            "[gat562][gps][snapshot] available_without_copy max=%u raw_count=%u status_view=%u status_use=%u parser_sats=%u data_valid=%u\n",
            static_cast<unsigned>(max),
            static_cast<unsigned>(s.sat_count),
            static_cast<unsigned>(s.status.sats_in_view),
            static_cast<unsigned>(s.status.sats_in_use),
            static_cast<unsigned>(s.data.satellites),
            static_cast<unsigned>(s.data.valid ? 1 : 0));
    }
    return snapshot_available;
}

void GpsRuntime::setCollectionInterval(uint32_t interval_ms) { impl()->collection_interval_ms = interval_ms; }
void GpsRuntime::setPowerStrategy(uint8_t strategy) { impl()->power_strategy = strategy; }

void GpsRuntime::setConfig(uint8_t mode, uint8_t sat_mask)
{
    auto& s = *impl();
    s.gnss_mode = mode;
    s.sat_mask = sat_mask;
    s.enabled = (mode != 0);
    if (!s.enabled)
    {
        s.clearObservations();
    }
}

void GpsRuntime::setNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    impl()->nmea_output_hz = output_hz;
    impl()->nmea_sentence_mask = sentence_mask;
}

void GpsRuntime::setMotionIdleTimeout(uint32_t timeout_ms) { impl()->motion_idle_timeout_ms = timeout_ms; }
void GpsRuntime::setMotionSensorId(uint8_t sensor_id) { impl()->motion_sensor_id = sensor_id; }

void GpsRuntime::suspend()
{
    auto& s = *impl();
    s.enabled = false;
    s.clearObservations();
}

void GpsRuntime::resume()
{
    auto& s = *impl();
    s.enabled = (s.gnss_mode != 0);
    if (!s.enabled)
    {
        s.clearObservations();
    }
}

void GpsRuntime::setCurrentEpochSeconds(uint32_t epoch_s)
{
    if (epoch_s < kMinValidEpochSeconds)
    {
        return;
    }

    auto& s = *impl();
    const uint32_t prev_epoch_s = s.epoch_base_s;
    s.epoch_base_s = epoch_s;
    s.epoch_base_ms = millis();
    s.time_synced = true;
    s.last_time_sync_epoch_logged = epoch_s;
    s.last_time_sync_log_ms = s.epoch_base_ms;
    Serial.printf("[gat562][gps] time sync source=external epoch=%lu prev=%lu\n",
                  static_cast<unsigned long>(epoch_s),
                  static_cast<unsigned long>(prev_epoch_s));
    syncSystemClockFromEpoch(epoch_s);
}

uint32_t GpsRuntime::currentEpochSeconds() const
{
    const uint32_t system_epoch_s = readSystemEpochSeconds();
    if (system_epoch_s >= kMinValidEpochSeconds)
    {
        return system_epoch_s;
    }

    const auto& s = *impl();
    if (!s.time_synced || s.epoch_base_s == 0)
    {
        return 0;
    }
    const uint32_t elapsed_s = (millis() - s.epoch_base_ms) / 1000U;
    return s.epoch_base_s + elapsed_s;
}

bool GpsRuntime::isRtcReady() const
{
    const auto& s = *impl();
    return s.time_synced && currentEpochSeconds() >= kMinValidEpochSeconds;
}

} // namespace boards::gat562_mesh_evb_pro