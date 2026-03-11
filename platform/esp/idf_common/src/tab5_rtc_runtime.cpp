#include "platform/esp/idf_common/tab5_rtc_runtime.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sys/time.h>

#include "esp_log.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "driver/i2c_master.h"

extern "C"
{
    i2c_master_bus_handle_t bsp_i2c_get_handle(void);
    bool bsp_i2c_lock(uint32_t timeout_ms);
    void bsp_i2c_unlock(void);
}
#endif

namespace platform::esp::idf_common::tab5_rtc_runtime
{
namespace
{

constexpr const char* kTag = "tab5-rtc-runtime";

int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool is_leap_year(int year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

uint8_t days_in_month(int year, uint8_t month)
{
    static constexpr uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
    {
        return 0;
    }
    if (month == 2 && is_leap_year(year))
    {
        return 29;
    }
    return kDays[month - 1];
}

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
constexpr uint8_t kRtcAddress = 0x32;
constexpr uint32_t kI2cTimeoutMs = 1000;

constexpr uint8_t kRegSec = 0x10;
constexpr uint8_t kRegMin = 0x11;
constexpr uint8_t kRegHour = 0x12;
constexpr uint8_t kRegWeekday = 0x13;
constexpr uint8_t kRegDay = 0x14;
constexpr uint8_t kRegMonth = 0x15;
constexpr uint8_t kRegYear = 0x16;
constexpr uint8_t kRegFlag = 0x1D;
constexpr uint8_t kRegCtrl0 = 0x1E;
constexpr uint8_t kRegCtrl1 = 0x1F;

constexpr uint8_t kFlagVlf = 1u << 1;
constexpr uint8_t kCtrl0Stop = 1u << 6;
constexpr uint8_t kCtrl1BackupMask = (1u << 4) | (1u << 5);

uint8_t bcd_to_dec(uint8_t value)
{
    return static_cast<uint8_t>(((value >> 4) * 10) + (value & 0x0F));
}

uint8_t dec_to_bcd(uint8_t value)
{
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

bool epoch_to_tm_utc(std::time_t epoch_seconds, std::tm* out_tm)
{
    if (out_tm == nullptr)
    {
        return false;
    }

    return ::gmtime_r(&epoch_seconds, out_tm) != nullptr;
}

uint8_t weekday_mask_from_date(int year, uint8_t month, uint8_t day)
{
    const int64_t days = days_from_civil(year, month, day);
    int weekday = static_cast<int>((days + 4) % 7);
    if (weekday < 0)
    {
        weekday += 7;
    }
    return static_cast<uint8_t>(1u << weekday);
}

class ScopedRtcDevice
{
  public:
    ScopedRtcDevice()
    {
        if (!bsp_i2c_lock(kI2cTimeoutMs))
        {
            ESP_LOGW(kTag, "Failed to lock SYS I2C for RTC access");
            return;
        }

        locked_ = true;
        i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
        if (bus == nullptr)
        {
            ESP_LOGW(kTag, "SYS I2C handle is not available");
            return;
        }

        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = kRtcAddress;
        dev_cfg.scl_speed_hz = 400000;
        const esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &handle_);
        if (err != ESP_OK)
        {
            handle_ = nullptr;
            ESP_LOGW(kTag, "i2c_master_bus_add_device(RTC) failed: %s", esp_err_to_name(err));
            return;
        }

        uint8_t ctrl1 = 0;
        if (read8(kRegCtrl1, &ctrl1) == ESP_OK)
        {
            const uint8_t desired = static_cast<uint8_t>(ctrl1 | kCtrl1BackupMask);
            if (desired != ctrl1)
            {
                (void)write8(kRegCtrl1, desired);
            }
        }
    }

    ~ScopedRtcDevice()
    {
        if (handle_ != nullptr)
        {
            (void)i2c_master_bus_rm_device(handle_);
            handle_ = nullptr;
        }
        if (locked_)
        {
            bsp_i2c_unlock();
            locked_ = false;
        }
    }

    bool ok() const
    {
        return handle_ != nullptr;
    }

    esp_err_t read(uint8_t reg, uint8_t* out, size_t len)
    {
        if (handle_ == nullptr || out == nullptr || len == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }
        const uint8_t reg_addr = reg;
        return i2c_master_transmit_receive(handle_, &reg_addr, 1, out, len, kI2cTimeoutMs);
    }

    esp_err_t write(uint8_t reg, const uint8_t* data, size_t len)
    {
        if (handle_ == nullptr || data == nullptr || len == 0 || len > 16)
        {
            return ESP_ERR_INVALID_ARG;
        }
        uint8_t buffer[17] = {};
        buffer[0] = reg;
        std::memcpy(buffer + 1, data, len);
        return i2c_master_transmit(handle_, buffer, len + 1, kI2cTimeoutMs);
    }

    esp_err_t read8(uint8_t reg, uint8_t* out)
    {
        return read(reg, out, 1);
    }

    esp_err_t write8(uint8_t reg, uint8_t value)
    {
        return write(reg, &value, 1);
    }

  private:
    bool locked_ = false;
    i2c_master_dev_handle_t handle_ = nullptr;
};

bool read_rtc_epoch(std::time_t* out_epoch)
{
    if (out_epoch == nullptr)
    {
        return false;
    }

    ScopedRtcDevice rtc;
    if (!rtc.ok())
    {
        return false;
    }

    uint8_t flags = 0;
    if (rtc.read8(kRegFlag, &flags) != ESP_OK)
    {
        ESP_LOGW(kTag, "Failed to read RTC flag register");
        return false;
    }
    if ((flags & kFlagVlf) != 0)
    {
        ESP_LOGW(kTag, "RTC reports low-voltage flag; stored time is not trusted");
        return false;
    }

    uint8_t date[7] = {};
    if (rtc.read(kRegSec, date, sizeof(date)) != ESP_OK)
    {
        ESP_LOGW(kTag, "Failed to read RTC datetime registers");
        return false;
    }

    const uint8_t second = bcd_to_dec(static_cast<uint8_t>(date[0] & 0x7F));
    const uint8_t minute = bcd_to_dec(static_cast<uint8_t>(date[1] & 0x7F));
    const uint8_t hour = bcd_to_dec(static_cast<uint8_t>(date[2] & 0x3F));
    const uint8_t day = bcd_to_dec(static_cast<uint8_t>(date[4] & 0x3F));
    const uint8_t month = bcd_to_dec(static_cast<uint8_t>(date[5] & 0x1F));
    const int year = 2000 + static_cast<int>(bcd_to_dec(date[6]));

    if (!validate_datetime_utc(year, month, day, hour, minute, second))
    {
        ESP_LOGW(kTag,
                 "RTC datetime is invalid: %04d-%02u-%02u %02u:%02u:%02u",
                 year,
                 static_cast<unsigned>(month),
                 static_cast<unsigned>(day),
                 static_cast<unsigned>(hour),
                 static_cast<unsigned>(minute),
                 static_cast<unsigned>(second));
        return false;
    }

    const std::time_t epoch = datetime_to_epoch_utc(year, month, day, hour, minute, second);
    if (epoch < 0)
    {
        ESP_LOGW(kTag, "Failed to convert RTC datetime to epoch");
        return false;
    }

    *out_epoch = epoch;
    return true;
}

bool write_rtc_epoch(std::time_t epoch_seconds, const char* source)
{
    std::tm utc_tm = {};
    if (!epoch_to_tm_utc(epoch_seconds, &utc_tm))
    {
        ESP_LOGW(kTag, "Failed to expand epoch %lld for RTC write", static_cast<long long>(epoch_seconds));
        return false;
    }

    const int year = utc_tm.tm_year + 1900;
    const uint8_t month = static_cast<uint8_t>(utc_tm.tm_mon + 1);
    const uint8_t day = static_cast<uint8_t>(utc_tm.tm_mday);
    const uint8_t hour = static_cast<uint8_t>(utc_tm.tm_hour);
    const uint8_t minute = static_cast<uint8_t>(utc_tm.tm_min);
    const uint8_t second = static_cast<uint8_t>(utc_tm.tm_sec);

    if (!validate_datetime_utc(year, month, day, hour, minute, second))
    {
        ESP_LOGW(kTag,
                 "Refusing to write invalid RTC datetime from %s: %04d-%02u-%02u %02u:%02u:%02u",
                 source ? source : "unknown",
                 year,
                 static_cast<unsigned>(month),
                 static_cast<unsigned>(day),
                 static_cast<unsigned>(hour),
                 static_cast<unsigned>(minute),
                 static_cast<unsigned>(second));
        return false;
    }

    ScopedRtcDevice rtc;
    if (!rtc.ok())
    {
        return false;
    }

    uint8_t ctrl0 = 0;
    if (rtc.read8(kRegCtrl0, &ctrl0) != ESP_OK)
    {
        ESP_LOGW(kTag, "Failed to read RTC control register");
        return false;
    }

    if (rtc.write8(kRegCtrl0, static_cast<uint8_t>(ctrl0 | kCtrl0Stop)) != ESP_OK)
    {
        ESP_LOGW(kTag, "Failed to stop RTC before programming time");
        return false;
    }

    const uint8_t payload[7] = {
        dec_to_bcd(second),
        dec_to_bcd(minute),
        dec_to_bcd(hour),
        weekday_mask_from_date(year, month, day),
        dec_to_bcd(day),
        dec_to_bcd(month),
        dec_to_bcd(static_cast<uint8_t>(year % 100)),
    };

    if (rtc.write(kRegSec, payload, sizeof(payload)) != ESP_OK)
    {
        (void)rtc.write8(kRegCtrl0, ctrl0);
        ESP_LOGW(kTag, "Failed to write RTC datetime payload");
        return false;
    }

    (void)rtc.write8(kRegFlag, 0);
    if (rtc.write8(kRegCtrl0, static_cast<uint8_t>(ctrl0 & ~kCtrl0Stop)) != ESP_OK)
    {
        ESP_LOGW(kTag, "Failed to restart RTC after programming time");
        return false;
    }

    ESP_LOGI(kTag,
             "RTC updated from %s: %04d-%02u-%02u %02u:%02u:%02u UTC",
             source ? source : "unknown",
             year,
             static_cast<unsigned>(month),
             static_cast<unsigned>(day),
             static_cast<unsigned>(hour),
             static_cast<unsigned>(minute),
             static_cast<unsigned>(second));
    return true;
}
#endif

} // namespace

bool is_valid_epoch(std::time_t epoch_seconds)
{
    return epoch_seconds >= kMinValidEpochSeconds;
}

bool validate_datetime_utc(int year,
                           uint8_t month,
                           uint8_t day,
                           uint8_t hour,
                           uint8_t minute,
                           uint8_t second)
{
    if (year < 2000 || year > 2099)
    {
        return false;
    }
    if (month < 1 || month > 12)
    {
        return false;
    }
    const uint8_t max_day = days_in_month(year, month);
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

std::time_t datetime_to_epoch_utc(int year,
                                  uint8_t month,
                                  uint8_t day,
                                  uint8_t hour,
                                  uint8_t minute,
                                  uint8_t second)
{
    if (!validate_datetime_utc(year, month, day, hour, minute, second))
    {
        return static_cast<std::time_t>(-1);
    }

    const int64_t days = days_from_civil(year, month, day);
    const int64_t sec_of_day = static_cast<int64_t>(hour) * 3600 +
                               static_cast<int64_t>(minute) * 60 +
                               static_cast<int64_t>(second);
    const int64_t epoch64 = days * 86400 + sec_of_day;
    if (epoch64 < 0 || epoch64 > static_cast<int64_t>(std::numeric_limits<std::time_t>::max()))
    {
        return static_cast<std::time_t>(-1);
    }
    return static_cast<std::time_t>(epoch64);
}

bool apply_system_time(std::time_t epoch_seconds, const char* source)
{
    if (epoch_seconds < 0)
    {
        ESP_LOGW(kTag, "Refusing to apply negative epoch from %s", source ? source : "unknown");
        return false;
    }

    timeval tv = {};
    tv.tv_sec = epoch_seconds;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0)
    {
        ESP_LOGW(kTag,
                 "settimeofday failed for %s epoch=%lld",
                 source ? source : "unknown",
                 static_cast<long long>(epoch_seconds));
        return false;
    }

    ESP_LOGI(kTag,
             "System time updated from %s: epoch=%lld",
             source ? source : "unknown",
             static_cast<long long>(epoch_seconds));
    return true;
}

bool apply_system_time_and_sync_rtc(std::time_t epoch_seconds, const char* source)
{
    if (!apply_system_time(epoch_seconds, source))
    {
        return false;
    }

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    if (!write_rtc_epoch(epoch_seconds, source))
    {
        ESP_LOGW(kTag,
                 "RTC persistence failed after system time update from %s",
                 source ? source : "unknown");
        return false;
    }
#endif

    return true;
}

bool sync_system_time_from_hardware_rtc()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    std::time_t epoch_seconds = 0;
    if (!read_rtc_epoch(&epoch_seconds))
    {
        return false;
    }
    if (!is_valid_epoch(epoch_seconds))
    {
        ESP_LOGW(kTag,
                 "RTC epoch is below validity threshold: epoch=%lld",
                 static_cast<long long>(epoch_seconds));
        return false;
    }
    return apply_system_time(epoch_seconds, "tab5_rtc_boot");
#else
    return false;
#endif
}

bool sync_hardware_rtc_from_system_time()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return write_rtc_epoch(std::time(nullptr), "system_time");
#else
    return false;
#endif
}

} // namespace platform::esp::idf_common::tab5_rtc_runtime
