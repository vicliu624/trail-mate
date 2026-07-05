#include "boards/t_impulse_plus/gps_runtime.h"

#include "boards/t_impulse_plus/board_profile.h"

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <time.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace boards::t_impulse_plus
{
namespace
{

constexpr uint32_t kMinValidEpochSeconds = 1700000000UL;
constexpr uint32_t kTimeSyncLogIntervalMs = 60000UL;

uint32_t readSystemEpochSeconds()
{
    const time_t now = ::time(nullptr);
    if (now < static_cast<time_t>(kMinValidEpochSeconds))
    {
        return 0;
    }
    return static_cast<uint32_t>(now);
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
    if (day < 1 || day > daysInMonth(year, month))
    {
        return false;
    }
    return hour < 24 && minute < 60 && second < 60;
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

void beginGpsSerial()
{
    const auto& gps = kBoardProfile.gps;
    if (gps.uart.enable >= 0)
    {
        pinMode(gps.uart.enable, OUTPUT);
        digitalWrite(gps.uart.enable, HIGH);
        delay(10);
    }
    Serial1.setPins(gps.uart.rx, gps.uart.tx);
    Serial1.begin(gps.baud_rate);
}

void endGpsSerial()
{
    Serial1.end();
    const auto& gps = kBoardProfile.gps;
    if (gps.uart.enable >= 0)
    {
        digitalWrite(gps.uart.enable, LOW);
    }
}

} // namespace

struct GpsRuntime::Impl
{
    TinyGPSPlus parser{};
    ::gps::GpsState data{};
    uint32_t collection_interval_ms = 60000;
    uint32_t last_motion_ms = 0;
    uint32_t epoch_base_s = 0;
    uint32_t epoch_base_ms = 0;
    uint32_t last_nmea_ms = 0;
    uint32_t last_status_log_ms = 0;
    uint32_t last_time_sync_log_ms = 0;
    bool user_enabled = true;
    bool enabled = true;
    bool powered = false;
    bool initialized = false;
    bool time_synced = false;
    bool nmea_seen = false;

    void clearObservations()
    {
        parser = TinyGPSPlus{};
        data = ::gps::GpsState{};
        last_nmea_ms = 0;
        nmea_seen = false;
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

        const uint32_t prev = epoch_base_s;
        epoch_base_s = utc_s;
        epoch_base_ms = millis();
        time_synced = true;

        const uint32_t now_ms = millis();
        if (last_time_sync_log_ms == 0 || (now_ms - last_time_sync_log_ms) >= kTimeSyncLogIntervalMs)
        {
            last_time_sync_log_ms = now_ms;
            Serial.printf("[t-impulse-plus][gps] time sync epoch=%lu prev=%lu sats=%u fix=%u\n",
                          static_cast<unsigned long>(epoch_base_s),
                          static_cast<unsigned long>(prev),
                          static_cast<unsigned>(parser.satellites.isValid() ? parser.satellites.value() : 0U),
                          static_cast<unsigned>(parser.location.isValid() ? 1U : 0U));
        }
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
        if (data.valid)
        {
            last_motion_ms = millis();
        }
    }

    void logStatusIfDue()
    {
        const uint32_t now_ms = millis();
        if ((now_ms - last_status_log_ms) < collection_interval_ms)
        {
            return;
        }
        last_status_log_ms = now_ms;
        Serial.printf("[t-impulse-plus][gps] status enabled=%u powered=%u nmea=%u fix=%u sats=%u epoch=%lu\n",
                      enabled ? 1U : 0U,
                      powered ? 1U : 0U,
                      nmea_seen ? 1U : 0U,
                      data.valid ? 1U : 0U,
                      static_cast<unsigned>(data.satellites),
                      static_cast<unsigned long>(currentEpoch()));
    }

    uint32_t currentEpoch() const
    {
        const uint32_t system_epoch_s = readSystemEpochSeconds();
        if (system_epoch_s >= kMinValidEpochSeconds)
        {
            return system_epoch_s;
        }
        if (!time_synced || epoch_base_s == 0)
        {
            return 0;
        }
        return epoch_base_s + ((millis() - epoch_base_ms) / 1000U);
    }
};

GpsRuntime::GpsRuntime()
    : impl_(new Impl())
{
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
    auto& s = *impl();
    s.user_enabled = config.gps_enabled;
    s.enabled = s.user_enabled;
    if (!s.initialized)
    {
        s.initialized = true;
        if (s.enabled)
        {
            beginGpsSerial();
            s.powered = true;
        }
    }
    return true;
}

void GpsRuntime::applyConfig(const app::AppConfig& config)
{
    auto& s = *impl();
    s.collection_interval_ms = config.gps_interval_ms ? config.gps_interval_ms : 60000UL;
    s.user_enabled = config.gps_enabled;
    s.enabled = s.user_enabled;
    if (!s.enabled)
    {
        if (s.powered)
        {
            endGpsSerial();
            s.powered = false;
        }
        s.clearObservations();
    }
    else if (s.initialized && !s.powered)
    {
        beginGpsSerial();
        s.powered = true;
    }
    Serial.printf("[t-impulse-plus][gps] config enabled=%u interval_ms=%lu\n",
                  s.enabled ? 1U : 0U,
                  static_cast<unsigned long>(s.collection_interval_ms));
}

void GpsRuntime::tick()
{
    auto& s = *impl();
    if (!s.initialized || !s.enabled || !s.powered)
    {
        return;
    }

    while (Serial1.available() > 0)
    {
        s.nmea_seen = true;
        s.last_nmea_ms = millis();
        s.parser.encode(static_cast<char>(Serial1.read()));
    }
    s.applyTimeIfValid();
    s.refreshFix();
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
    (void)out;
    (void)max;
    if (out_count)
    {
        *out_count = 0;
    }
    if (status)
    {
        *status = ::gps::GnssStatus{};
    }
    return impl()->data.valid || impl()->data.satellites > 0;
}

bool GpsRuntime::debugCheckMemoryGuard(const char* reason)
{
    (void)reason;
    return true;
}

void GpsRuntime::setCollectionInterval(uint32_t interval_ms) { impl()->collection_interval_ms = interval_ms; }
void GpsRuntime::setPowerStrategy(uint8_t strategy) { (void)strategy; }
void GpsRuntime::setConfig(uint8_t mode, uint8_t sat_mask)
{
    (void)mode;
    (void)sat_mask;
}
void GpsRuntime::setExternalNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    (void)output_hz;
    (void)sentence_mask;
}
void GpsRuntime::setMotionIdleTimeout(uint32_t timeout_ms) { (void)timeout_ms; }
void GpsRuntime::setMotionSensorId(uint8_t sensor_id) { (void)sensor_id; }

void GpsRuntime::setEnabled(bool enabled)
{
    auto& s = *impl();
    s.user_enabled = enabled;
    s.enabled = enabled;
    if (!s.enabled)
    {
        if (s.powered)
        {
            endGpsSerial();
            s.powered = false;
        }
        s.clearObservations();
        return;
    }
    if (s.initialized && !s.powered)
    {
        beginGpsSerial();
        s.powered = true;
    }
}

void GpsRuntime::suspend()
{
    auto& s = *impl();
    if (s.powered)
    {
        endGpsSerial();
        s.powered = false;
    }
    s.enabled = false;
    s.clearObservations();
}

void GpsRuntime::resume()
{
    auto& s = *impl();
    s.enabled = s.user_enabled;
    if (s.enabled && s.initialized && !s.powered)
    {
        beginGpsSerial();
        s.powered = true;
    }
}

void GpsRuntime::setCurrentEpochSeconds(uint32_t epoch_s)
{
    if (epoch_s < kMinValidEpochSeconds)
    {
        return;
    }
    auto& s = *impl();
    s.epoch_base_s = epoch_s;
    s.epoch_base_ms = millis();
    s.time_synced = true;
    Serial.printf("[t-impulse-plus][gps] time sync source=external epoch=%lu\n",
                  static_cast<unsigned long>(epoch_s));
}

uint32_t GpsRuntime::currentEpochSeconds() const
{
    return impl()->currentEpoch();
}

bool GpsRuntime::isRtcReady() const
{
    return currentEpochSeconds() >= kMinValidEpochSeconds;
}

} // namespace boards::t_impulse_plus
