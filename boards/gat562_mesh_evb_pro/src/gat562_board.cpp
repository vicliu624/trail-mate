#include "boards/gat562_mesh_evb_pro/gat562_board.h"

#include "boards/gat562_mesh_evb_pro/board_profile.h"
#include "boards/gat562_mesh_evb_pro/gps_runtime.h"
#include "boards/gat562_mesh_evb_pro/input_runtime.h"
#include "boards/gat562_mesh_evb_pro/settings_store.h"
#include "boards/gat562_mesh_evb_pro/sx1262_radio_packet_io.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"
#include "ui/mono_128x64/runtime.h"

#include ".pio/libdeps/gat562_mesh_evb_pro/Adafruit SSD1306/Adafruit_SSD1306.h"
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
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

bool readActiveLowPin(int pin, bool use_pullup)
{
    if (pin < 0)
    {
        return false;
    }
    pinMode(pin, use_pullup ? INPUT_PULLUP : INPUT);
    return digitalRead(pin) == LOW;
}

void writeLed(int pin, bool active_high, bool on)
{
    if (pin < 0)
    {
        return;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, (on == active_high) ? HIGH : LOW);
}

struct DebounceState
{
    bool stable = false;
    bool sampled = false;
    uint32_t changed_at_ms = 0;
};

struct InputRuntimeState
{
    uint32_t last_activity_ms = 0;
    BoardInputSnapshot snapshot{};
    DebounceState button_primary{};
    DebounceState button_secondary{};
    DebounceState joystick_up{};
    DebounceState joystick_down{};
    DebounceState joystick_left{};
    DebounceState joystick_right{};
    DebounceState joystick_press{};
} s_input;

struct GpsRuntimeState
{
    TinyGPSPlus parser{};
    ::gps::GpsState data{};
    ::gps::GnssStatus status{};
    struct GsvCollector
    {
        std::array<::gps::GnssSatInfo, ::gps::kMaxGnssSats> sats{};
        std::size_t count = 0;
    };
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
} s_gps;

constexpr uint32_t kMinValidEpochSeconds = 1700000000UL;
constexpr std::size_t kNmeaFieldMax = 24;

enum class CollectorSlot : uint8_t
{
    GPS = 0,
    GLN = 1,
    GAL = 2,
    BD = 3,
    UNKNOWN = 4,
};

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

bool usedSat(uint16_t sat_id)
{
    for (std::size_t i = 0; i < s_gps.used_sat_count; ++i)
    {
        if (s_gps.used_sat_ids[i] == sat_id)
        {
            return true;
        }
    }
    return false;
}

void mergeGnssSatellites()
{
    s_gps.sat_count = 0;
    for (std::size_t collector_index = 0; collector_index < s_gps.gsv.size(); ++collector_index)
    {
        auto& collector = s_gps.gsv[collector_index];
        for (std::size_t sat_index = 0;
             sat_index < collector.count && s_gps.sat_count < s_gps.sats.size();
             ++sat_index)
        {
            auto sat = collector.sats[sat_index];
            sat.used = usedSat(sat.id);
            s_gps.sats[s_gps.sat_count++] = sat;
        }
    }

    s_gps.status.sats_in_view = static_cast<uint8_t>(std::min<std::size_t>(s_gps.sat_count, 255U));
    if (s_gps.used_sat_count > 0)
    {
        s_gps.status.sats_in_use = static_cast<uint8_t>(std::min<std::size_t>(s_gps.used_sat_count, 255U));
    }
}

void clearGpsObservations()
{
    s_gps.parser = TinyGPSPlus{};
    s_gps.data = ::gps::GpsState{};
    s_gps.status = ::gps::GnssStatus{};
    s_gps.sats.fill(::gps::GnssSatInfo{});
    s_gps.used_sat_ids.fill(0);
    s_gps.gsv.fill(GpsRuntimeState::GsvCollector{});
    s_gps.sat_count = 0;
    s_gps.used_sat_count = 0;
    s_gps.last_nmea_ms = 0;
    s_gps.nmea_line_len = 0;
    s_gps.nmea_line[0] = '\0';
    s_gps.nmea_seen = false;
}

void parseGsaSentence(const std::array<char*, kNmeaFieldMax>& fields, std::size_t count)
{
    if (count < 4)
    {
        return;
    }

    s_gps.used_sat_count = 0;
    for (std::size_t i = 3; i <= 14 && i < count; ++i)
    {
        uint32_t sat_id = 0;
        if (!parseUint(fields[i], &sat_id) || sat_id == 0 || s_gps.used_sat_count >= s_gps.used_sat_ids.size())
        {
            continue;
        }
        s_gps.used_sat_ids[s_gps.used_sat_count++] = static_cast<uint16_t>(sat_id);
    }
    mergeGnssSatellites();
}

void parseGsvSentence(const char* talker, const std::array<char*, kNmeaFieldMax>& fields, std::size_t count)
{
    if (count < 4)
    {
        return;
    }

    const CollectorSlot slot = collectorSlotForTalker(talker);
    auto& collector = s_gps.gsv[static_cast<std::size_t>(slot)];

    uint32_t msg_num = 0;
    if (!parseUint(fields[2], &msg_num))
    {
        return;
    }

    uint32_t sats_in_view = 0;
    if (parseUint(fields[3], &sats_in_view))
    {
        s_gps.status.sats_in_view = static_cast<uint8_t>(std::min<uint32_t>(sats_in_view, 255U));
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

    if (std::strcmp(type, "GSA") == 0)
    {
        parseGsaSentence(fields, count);
    }
    else if (std::strcmp(type, "GSV") == 0)
    {
        parseGsvSentence(talker, fields, count);
    }
}

void processGpsNmeaChar(char ch)
{
    if (ch == '$')
    {
        s_gps.nmea_line_len = 0;
        s_gps.nmea_line[s_gps.nmea_line_len++] = ch;
        return;
    }

    if (s_gps.nmea_line_len == 0)
    {
        return;
    }

    if (ch == '\r' || ch == '\n')
    {
        if (s_gps.nmea_line_len > 0)
        {
            s_gps.nmea_line[s_gps.nmea_line_len] = '\0';
            parseNmeaSentence(s_gps.nmea_line);
            s_gps.nmea_line_len = 0;
        }
        return;
    }

    if (s_gps.nmea_line_len + 1 < sizeof(s_gps.nmea_line))
    {
        s_gps.nmea_line[s_gps.nmea_line_len++] = ch;
    }
    else
    {
        s_gps.nmea_line_len = 0;
    }
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

bool updateDebounced(bool sampled,
                     DebounceState& state,
                     uint16_t debounce_ms,
                     BoardInputKey key,
                     BoardInputEvent* out_event,
                     uint32_t now_ms)
{
    if (sampled != state.sampled)
    {
        state.sampled = sampled;
        state.changed_at_ms = now_ms;
    }

    if (state.stable == state.sampled)
    {
        return false;
    }

    if ((now_ms - state.changed_at_ms) < debounce_ms)
    {
        return false;
    }

    state.stable = state.sampled;
    if (out_event)
    {
        out_event->key = key;
        out_event->pressed = state.stable;
        out_event->timestamp_ms = now_ms;
    }
    if (state.stable)
    {
        s_input.last_activity_ms = now_ms;
    }
    return true;
}

void applyGpsTimeIfValid()
{
    if (!s_gps.parser.time.isValid() || !s_gps.parser.date.isValid())
    {
        return;
    }

    const uint16_t year = s_gps.parser.date.year();
    const uint8_t month = s_gps.parser.date.month();
    const uint8_t day = s_gps.parser.date.day();
    const uint8_t hour = s_gps.parser.time.hour();
    const uint8_t minute = s_gps.parser.time.minute();
    const uint8_t second = s_gps.parser.time.second();

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
    if (s_gps.epoch_base_s == utc_s)
    {
        return;
    }
    const uint32_t prev_epoch_s = s_gps.epoch_base_s;
    s_gps.epoch_base_s = utc_s;
    s_gps.epoch_base_ms = millis();
    s_gps.time_synced = true;
    if (s_gps.last_time_sync_epoch_logged != utc_s ||
        (s_gps.epoch_base_ms - s_gps.last_time_sync_log_ms) >= 1000U)
    {
        s_gps.last_time_sync_epoch_logged = utc_s;
        s_gps.last_time_sync_log_ms = s_gps.epoch_base_ms;
        Serial.printf(
            "[gat562][gps] time sync source=gnss epoch=%lu prev=%lu sats=%u fix=%u age_ms=%lu date=%04u-%02u-%02u time=%02u:%02u:%02u\n",
            static_cast<unsigned long>(utc_s),
            static_cast<unsigned long>(prev_epoch_s),
            static_cast<unsigned>(s_gps.parser.satellites.isValid() ? s_gps.parser.satellites.value() : 0U),
            static_cast<unsigned>(s_gps.parser.location.isValid() ? 1U : 0U),
            static_cast<unsigned long>(s_gps.parser.location.isValid() ? s_gps.parser.location.age() : 0U),
            static_cast<unsigned>(year),
            static_cast<unsigned>(month),
            static_cast<unsigned>(day),
            static_cast<unsigned>(hour),
            static_cast<unsigned>(minute),
            static_cast<unsigned>(second));
    }
    syncSystemClockFromEpoch(utc_s);
}

void logGpsStatusIfDue()
{
    if (!s_gps.initialized || !s_gps.enabled)
    {
        return;
    }

    const uint32_t now_ms = millis();
    const uint32_t interval_ms = s_gps.collection_interval_ms > 0 ? s_gps.collection_interval_ms : 60000U;
    if ((now_ms - s_gps.last_status_log_ms) < interval_ms)
    {
        return;
    }
    s_gps.last_status_log_ms = now_ms;

    const bool time_valid = s_gps.parser.time.isValid();
    const bool date_valid = s_gps.parser.date.isValid();
    const bool fix_valid = s_gps.parser.location.isValid();
    const uint16_t year = date_valid ? s_gps.parser.date.year() : 0U;
    const uint8_t month = date_valid ? s_gps.parser.date.month() : 0U;
    const uint8_t day = date_valid ? s_gps.parser.date.day() : 0U;
    const uint8_t hour = time_valid ? s_gps.parser.time.hour() : 0U;
    const uint8_t minute = time_valid ? s_gps.parser.time.minute() : 0U;
    const uint8_t second = time_valid ? s_gps.parser.time.second() : 0U;
    const bool datetime_shape_valid = time_valid && date_valid && gpsDateTimeValid(year, month, day, hour, minute, second);
    const time_t utc = datetime_shape_valid ? gpsDateTimeToEpochUtc(year, month, day, hour, minute, second)
                                            : static_cast<time_t>(0);
    const bool epoch_ok = utc >= static_cast<time_t>(kMinValidEpochSeconds);
    const uint32_t sat_count = s_gps.parser.satellites.isValid() ? s_gps.parser.satellites.value() : 0U;
    const uint32_t nmea_age_ms = s_gps.last_nmea_ms > 0 ? (now_ms - s_gps.last_nmea_ms) : 0U;
    const char* state = "idle";
    if (!s_gps.nmea_seen)
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
    else if (sat_count == 0U)
    {
        state = "time_only";
    }
    else if (!fix_valid)
    {
        state = "search_fix";
    }
    else if (!s_gps.time_synced)
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
        static_cast<unsigned>(s_gps.enabled ? 1 : 0),
        static_cast<unsigned>(s_gps.powered ? 1 : 0),
        static_cast<unsigned>(s_gps.nmea_seen ? 1 : 0),
        static_cast<unsigned long>(nmea_age_ms),
        static_cast<unsigned>(time_valid ? 1 : 0),
        static_cast<unsigned>(date_valid ? 1 : 0),
        static_cast<unsigned>(fix_valid ? 1 : 0),
        static_cast<unsigned>(sat_count),
        fix_valid ? s_gps.parser.location.lat() : 0.0,
        fix_valid ? s_gps.parser.location.lng() : 0.0,
        static_cast<unsigned long>(s_gps.epoch_base_s),
        static_cast<unsigned long>(epoch_ok ? static_cast<uint32_t>(utc) : 0U),
        static_cast<unsigned>(year),
        static_cast<unsigned>(month),
        static_cast<unsigned>(day),
        static_cast<unsigned>(hour),
        static_cast<unsigned>(minute),
        static_cast<unsigned>(second));
}

void refreshGpsFix()
{
    s_gps.data.valid = s_gps.parser.location.isValid();
    s_gps.data.lat = s_gps.parser.location.lat();
    s_gps.data.lng = s_gps.parser.location.lng();
    s_gps.data.has_alt = s_gps.parser.altitude.isValid();
    s_gps.data.alt_m = s_gps.data.has_alt ? s_gps.parser.altitude.meters() : 0.0;
    s_gps.data.has_speed = s_gps.parser.speed.isValid();
    s_gps.data.speed_mps = s_gps.data.has_speed ? (s_gps.parser.speed.kmph() / 3.6) : 0.0;
    s_gps.data.has_course = s_gps.parser.course.isValid();
    s_gps.data.course_deg = s_gps.data.has_course ? s_gps.parser.course.deg() : 0.0;
    s_gps.data.satellites = static_cast<uint8_t>(
        std::min<uint32_t>(s_gps.parser.satellites.isValid() ? s_gps.parser.satellites.value() : 0U, 255U));
    s_gps.data.age = s_gps.parser.location.isValid()
                         ? static_cast<uint32_t>(s_gps.parser.location.age())
                         : 0xFFFFFFFFUL;

    s_gps.status.sats_in_use = s_gps.used_sat_count > 0
                                   ? static_cast<uint8_t>(std::min<std::size_t>(s_gps.used_sat_count, 255U))
                                   : s_gps.data.satellites;
    s_gps.status.sats_in_view = s_gps.sat_count > 0
                                    ? static_cast<uint8_t>(std::min<std::size_t>(s_gps.sat_count, 255U))
                                    : s_gps.data.satellites;
    s_gps.status.hdop = s_gps.parser.hdop.isValid()
                            ? static_cast<float>(s_gps.parser.hdop.hdop())
                            : 0.0f;
    s_gps.status.fix = s_gps.data.valid
                           ? (s_gps.data.has_alt ? ::gps::GnssFix::FIX3D : ::gps::GnssFix::FIX2D)
                           : ::gps::GnssFix::NOFIX;

    if (s_gps.data.valid)
    {
        s_gps.last_motion_ms = millis();
    }
}

} // namespace

Gat562Board& Gat562Board::instance()
{
    static Gat562Board board_instance;
    return board_instance;
}

Gat562Board::Gat562Board()
    : gps_runtime_(new GpsRuntime()),
      input_runtime_(new InputRuntime())
{
}

Gat562Board::~Gat562Board() = default;

uint32_t Gat562Board::begin(uint32_t disable_hw_init)
{
    (void)disable_hw_init;
    if (initialized_)
    {
        return 1U;
    }

    initializeBoardHardware();
    ensureI2cReady();
    message_tone_volume_ = ::boards::gat562_mesh_evb_pro::settings_store::loadMessageToneVolume();
    initialized_ = true;
    return 1U;
}

void Gat562Board::initializeBoardHardware()
{
    const auto& profile = kBoardProfile;
    enablePeripheralRail();

    writeLed(profile.leds.status, profile.leds.active_high, false);
    if (!profile.leds.notification_shares_status)
    {
        writeLed(profile.leds.notification, profile.leds.active_high, false);
    }

    const auto setup_input = [](int pin, bool pullup)
    {
        if (pin >= 0)
        {
            pinMode(pin, pullup ? INPUT_PULLUP : INPUT);
        }
    };

    setup_input(profile.inputs.button_primary, profile.inputs.buttons_need_pullup);
    setup_input(profile.inputs.button_secondary, profile.inputs.buttons_need_pullup);
    setup_input(profile.inputs.joystick_up, profile.inputs.joystick_need_pullup);
    setup_input(profile.inputs.joystick_down, profile.inputs.joystick_need_pullup);
    setup_input(profile.inputs.joystick_left, profile.inputs.joystick_need_pullup);
    setup_input(profile.inputs.joystick_right, profile.inputs.joystick_need_pullup);
    setup_input(profile.inputs.joystick_press, profile.inputs.joystick_need_pullup);
}

void Gat562Board::enablePeripheralRail()
{
    if (peripheral_rail_enabled_)
    {
        return;
    }

    const auto& profile = kBoardProfile;
    if (profile.peripheral_3v3_enable >= 0)
    {
        pinMode(profile.peripheral_3v3_enable, OUTPUT);
        digitalWrite(profile.peripheral_3v3_enable, HIGH);
    }
    peripheral_rail_enabled_ = true;
}

void Gat562Board::wakeUp()
{
    enablePeripheralRail();
    setStatusLed(false);
}

void Gat562Board::handlePowerButton()
{
    pulseNotificationLed(40);
}

void Gat562Board::softwareShutdown()
{
    NVIC_SystemReset();
}

int Gat562Board::getPowerTier() const
{
    const int level = const_cast<Gat562Board*>(this)->readBatteryPercent();
    if (level < 0)
    {
        return 0;
    }
    if (level <= 10)
    {
        return 2;
    }
    if (level <= 20)
    {
        return 1;
    }
    return 0;
}

void Gat562Board::setBrightness(uint8_t level)
{
    brightness_ = static_cast<uint8_t>(
        std::clamp<int>(level, DEVICE_MIN_BRIGHTNESS_LEVEL, DEVICE_MAX_BRIGHTNESS_LEVEL));
}

uint8_t Gat562Board::getBrightness()
{
    return brightness_;
}

bool Gat562Board::hasKeyboard()
{
    return false;
}

void Gat562Board::keyboardSetBrightness(uint8_t level)
{
    keyboard_brightness_ = level;
}

uint8_t Gat562Board::keyboardGetBrightness()
{
    return keyboard_brightness_;
}

bool Gat562Board::isRTCReady() const
{
    return gps_runtime_ ? gps_runtime_->isRtcReady() : false;
}

bool Gat562Board::isCharging()
{
    return false;
}

int Gat562Board::readBatteryPercent() const
{
    const auto& battery = kBoardProfile.battery;
    if (battery.adc_pin < 0)
    {
        return -1;
    }

    analogReference(AR_INTERNAL_3_0);
    analogReadResolution(battery.adc_resolution_bits);
    const int raw = analogRead(battery.adc_pin);
    if (raw <= 0)
    {
        return -1;
    }

    const float max_raw = static_cast<float>((1UL << battery.adc_resolution_bits) - 1UL);
    const float voltage = (static_cast<float>(raw) / max_raw) * battery.aref_voltage * battery.adc_multiplier;
    const float ratio = (voltage - 3.30f) / (4.20f - 3.30f);
    const float clamped = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
    return static_cast<int>(clamped * 100.0f + 0.5f);
}

int Gat562Board::getBatteryLevel()
{
    return readBatteryPercent();
}

bool Gat562Board::isSDReady() const
{
    return false;
}

bool Gat562Board::isCardReady()
{
    return false;
}

bool Gat562Board::isGPSReady() const
{
    return isGpsRuntimeReady();
}

void Gat562Board::vibrator()
{
    pulseNotificationLed(20);
}

void Gat562Board::stopVibrator()
{
    const auto& leds = kBoardProfile.leds;
    const int pin = leds.notification_shares_status ? leds.status : leds.notification;
    writeLed(pin, leds.active_high, false);
}

void Gat562Board::playMessageTone()
{
    pulseNotificationLed(25);

    if (message_tone_volume_ == 0)
    {
        return;
    }

    const int buzzer_pin = kBoardProfile.buzzer.pin;
    if (buzzer_pin < 0)
    {
        return;
    }

    pinMode(buzzer_pin, OUTPUT);
    digitalWrite(buzzer_pin, kBoardProfile.buzzer.active_high ? LOW : HIGH);

    struct ToneStep
    {
        unsigned frequency_hz;
        uint16_t duration_ms;
        uint16_t gap_ms;
    };

    static constexpr ToneStep kMessageTone[] = {
        {1760U, 70U, 25U},
        {2093U, 110U, 0U},
    };

    for (const ToneStep& step : kMessageTone)
    {
        tone(static_cast<uint8_t>(buzzer_pin), step.frequency_hz, step.duration_ms);
        delay(step.duration_ms);
        noTone(static_cast<uint8_t>(buzzer_pin));
        if (step.gap_ms > 0)
        {
            delay(step.gap_ms);
        }
    }

    digitalWrite(buzzer_pin, kBoardProfile.buzzer.active_high ? LOW : HIGH);
}

void Gat562Board::setMessageToneVolume(uint8_t volume_percent)
{
    message_tone_volume_ = volume_percent;
    ::boards::gat562_mesh_evb_pro::settings_store::queueSaveMessageToneVolume(volume_percent);
}

uint8_t Gat562Board::getMessageToneVolume() const
{
    return message_tone_volume_;
}

void Gat562Board::setStatusLed(bool on)
{
    const auto& leds = kBoardProfile.leds;
    writeLed(leds.status, leds.active_high, on);
}

void Gat562Board::pulseNotificationLed(uint32_t pulse_ms)
{
    const auto& leds = kBoardProfile.leds;
    const int pin = leds.notification_shares_status ? leds.status : leds.notification;
    if (pin < 0)
    {
        return;
    }

    writeLed(pin, leds.active_high, true);
    delay(pulse_ms);
    writeLed(pin, leds.active_high, false);
}

bool Gat562Board::pollInputSnapshot(BoardInputSnapshot* out_snapshot) const
{
    return input_runtime_ ? input_runtime_->pollSnapshot(out_snapshot) : false;
}

bool Gat562Board::formatLoraFrequencyMHz(uint32_t freq_hz, char* out, std::size_t out_len) const
{
    if (!out || out_len == 0 || freq_hz == 0)
    {
        return false;
    }

    const uint32_t mhz = freq_hz / 1000000UL;
    const uint32_t khz = (freq_hz % 1000000UL) / 1000UL;
    std::snprintf(out, out_len, "%lu.%03luMHz",
                  static_cast<unsigned long>(mhz),
                  static_cast<unsigned long>(khz));
    return true;
}

uint16_t Gat562Board::inputDebounceMs() const
{
    return input_runtime_ ? input_runtime_->debounceMs() : kBoardProfile.inputs.debounce_ms;
}

bool Gat562Board::ensureI2cReady()
{
    if (i2c_initialized_)
    {
        return true;
    }

    const auto& profile = kBoardProfile;
    Wire.setPins(profile.oled_i2c.sda, profile.oled_i2c.scl);
    Wire.begin();
    Wire.setClock(400000);
    i2c_initialized_ = true;
    return true;
}

bool Gat562Board::lockI2c(uint32_t timeout_ms)
{
    const uint32_t start_ms = millis();
    while (true)
    {
        noInterrupts();
        if (!i2c_locked_)
        {
            i2c_locked_ = true;
            interrupts();
            return true;
        }
        interrupts();

        if ((millis() - start_ms) >= timeout_ms)
        {
            return false;
        }
        delay(1);
    }
}

void Gat562Board::unlockI2c()
{
    noInterrupts();
    i2c_locked_ = false;
    interrupts();
}

TwoWire& Gat562Board::i2cWire()
{
    (void)ensureI2cReady();
    return Wire;
}

bool Gat562Board::pollInputEvent(BoardInputEvent* out_event)
{
    return input_runtime_ ? input_runtime_->pollEvent(out_event) : false;
}

namespace
{
constexpr int kMonoScreenWidth = 128;
constexpr int kMonoScreenHeight = 64;

class Ssd1306MonoDisplay final : public ::ui::mono_128x64::MonoDisplay
{
  public:
    Ssd1306MonoDisplay()
        : display_(kMonoScreenWidth,
                   kMonoScreenHeight,
                   &::boards::gat562_mesh_evb_pro::Gat562Board::instance().i2cWire(),
                   -1)
    {
    }

    bool begin() override;
    int width() const override { return kMonoScreenWidth; }
    int height() const override { return kMonoScreenHeight; }
    void clear() override
    {
        if (online_)
        {
            display_.clearDisplay();
        }
    }
    void drawPixel(int x, int y, bool on) override
    {
        if (online_)
        {
            display_.drawPixel(x, y, on ? SSD1306_WHITE : SSD1306_BLACK);
        }
    }
    void drawHLine(int x, int y, int w) override
    {
        if (online_)
        {
            display_.drawFastHLine(x, y, w, SSD1306_WHITE);
        }
    }
    void fillRect(int x, int y, int w, int h, bool on) override
    {
        if (online_)
        {
            display_.fillRect(x, y, w, h, on ? SSD1306_WHITE : SSD1306_BLACK);
        }
    }
    void present() override
    {
        if (!online_)
        {
            return;
        }
        auto& board = ::boards::gat562_mesh_evb_pro::Gat562Board::instance();
        Gat562Board::I2cGuard guard(board, 100);
        if (guard)
        {
            display_.display();
        }
    }

  private:
    Adafruit_SSD1306 display_;
    bool initialized_ = false;
    bool online_ = false;
};

bool Ssd1306MonoDisplay::begin()
{
    if (initialized_)
    {
        return online_;
    }
    initialized_ = true;

    const auto& profile = ::boards::gat562_mesh_evb_pro::kBoardProfile;
    auto& board = ::boards::gat562_mesh_evb_pro::Gat562Board::instance();
    Gat562Board::I2cGuard guard(board, 200);
    if (!guard)
    {
        return false;
    }
    online_ = display_.begin(SSD1306_SWITCHCAPVCC, profile.oled_i2c.address, true, false);
    if (online_)
    {
        display_.clearDisplay();
        display_.setTextWrap(false);
        // Mono UI text is rendered by ui::mono_128x64::TextRenderer. Keep the
        // panel implementation at raw-pixel level so NRF never falls back to
        // Adafruit_GFX's built-in ASCII font path.
        display_.display();
    }
    return online_;
}

} // namespace

::ui::mono_128x64::MonoDisplay& Gat562Board::monoDisplay()
{
    static Ssd1306MonoDisplay display;
    return display;
}

Gat562Board::I2cGuard::I2cGuard(Gat562Board& board, uint32_t timeout_ms)
    : board_(&board),
      locked_(board.ensureI2cReady() && board.lockI2c(timeout_ms))
{
}

Gat562Board::I2cGuard::~I2cGuard()
{
    if (board_ && locked_)
    {
        board_->unlockI2c();
    }
}

bool Gat562Board::I2cGuard::locked() const
{
    return locked_;
}

Gat562Board::I2cGuard::operator bool() const
{
    return locked_;
}

const char* Gat562Board::defaultLongName() const
{
    return kBoardProfile.identity.long_name;
}

const char* Gat562Board::defaultShortName() const
{
    return kBoardProfile.identity.short_name;
}

const char* Gat562Board::defaultBleName() const
{
    return kBoardProfile.identity.ble_name;
}

bool Gat562Board::prepareRadioHardware()
{
    if (radio_hw_ready_)
    {
        return true;
    }

    (void)begin();
    enablePeripheralRail();

    const auto& profile = kBoardProfile;
    if (profile.lora.power_en >= 0)
    {
        pinMode(profile.lora.power_en, OUTPUT);
        digitalWrite(profile.lora.power_en, HIGH);
        delay(5);
    }

    SPI.begin();
    radio_hw_ready_ = true;
    return true;
}

bool Gat562Board::beginRadioIo()
{
    return ::boards::gat562_mesh_evb_pro::sx1262RadioPacketIo().begin();
}

platform::nrf52::arduino_common::chat::infra::IRadioPacketIo* Gat562Board::bindRadioIo()
{
    auto& io = ::boards::gat562_mesh_evb_pro::sx1262RadioPacketIo();
    ::platform::nrf52::arduino_common::chat::infra::bindRadioPacketIo(&io);
    return &io;
}

void Gat562Board::applyRadioConfig(chat::MeshProtocol protocol, const chat::MeshConfig& config)
{
    ::boards::gat562_mesh_evb_pro::sx1262RadioPacketIo().applyConfig(protocol, config);
}

uint32_t Gat562Board::activeLoraFrequencyHz() const
{
    return ::boards::gat562_mesh_evb_pro::sx1262RadioPacketIo().appliedFrequencyHz();
}

bool Gat562Board::startGpsRuntime(const app::AppConfig& config)
{
    return gps_runtime_ ? gps_runtime_->start(config) : false;
}

bool Gat562Board::beginGps(const app::AppConfig& config)
{
    return gps_runtime_ ? gps_runtime_->begin(config) : false;
}

void Gat562Board::applyGpsConfig(const app::AppConfig& config)
{
    if (gps_runtime_)
    {
        gps_runtime_->applyConfig(config);
    }
}

void Gat562Board::tickGps()
{
    if (gps_runtime_)
    {
        gps_runtime_->tick();
    }
}

bool Gat562Board::isGpsRuntimeReady() const
{
    return gps_runtime_ ? gps_runtime_->isReady() : false;
}

::gps::GpsState Gat562Board::gpsData() const
{
    return gps_runtime_ ? gps_runtime_->data() : ::gps::GpsState{};
}

bool Gat562Board::gpsEnabled() const
{
    return gps_runtime_ ? gps_runtime_->enabled() : false;
}

bool Gat562Board::gpsPowered() const
{
    return gps_runtime_ ? gps_runtime_->powered() : false;
}

uint32_t Gat562Board::gpsLastMotionMs() const
{
    return gps_runtime_ ? gps_runtime_->lastMotionMs() : 0;
}

bool Gat562Board::gpsGnssSnapshot(::gps::GnssSatInfo* out,
                                  std::size_t max,
                                  std::size_t* out_count,
                                  ::gps::GnssStatus* status) const
{
    return gps_runtime_ ? gps_runtime_->gnssSnapshot(out, max, out_count, status) : false;
}

void Gat562Board::setGpsCollectionInterval(uint32_t interval_ms)
{
    if (gps_runtime_)
    {
        gps_runtime_->setCollectionInterval(interval_ms);
    }
}
void Gat562Board::setGpsPowerStrategy(uint8_t strategy)
{
    if (gps_runtime_)
    {
        gps_runtime_->setPowerStrategy(strategy);
    }
}
void Gat562Board::setGpsConfig(uint8_t mode, uint8_t sat_mask)
{
    if (gps_runtime_)
    {
        gps_runtime_->setConfig(mode, sat_mask);
    }
}
void Gat562Board::setGpsNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    if (gps_runtime_)
    {
        gps_runtime_->setNmeaConfig(output_hz, sentence_mask);
    }
}
void Gat562Board::setGpsMotionIdleTimeout(uint32_t timeout_ms)
{
    if (gps_runtime_)
    {
        gps_runtime_->setMotionIdleTimeout(timeout_ms);
    }
}
void Gat562Board::setGpsMotionSensorId(uint8_t sensor_id)
{
    if (gps_runtime_)
    {
        gps_runtime_->setMotionSensorId(sensor_id);
    }
}
void Gat562Board::suspendGps()
{
    if (gps_runtime_)
    {
        gps_runtime_->suspend();
    }
}

void Gat562Board::resumeGps()
{
    if (gps_runtime_)
    {
        gps_runtime_->resume();
    }
}
void Gat562Board::setCurrentEpochSeconds(uint32_t epoch_s)
{
    if (gps_runtime_)
    {
        gps_runtime_->setCurrentEpochSeconds(epoch_s);
    }
}

uint32_t Gat562Board::currentEpochSeconds() const
{
    return gps_runtime_ ? gps_runtime_->currentEpochSeconds() : 0;
}

} // namespace boards::gat562_mesh_evb_pro

BoardBase& board = ::boards::gat562_mesh_evb_pro::Gat562Board::instance();
