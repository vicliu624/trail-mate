#include "board/TDeckBoard.h"
#include "board/sd_utils.h"
#include "display/drivers/ST7789TDeck.h"
#include <SD.h>
#include <Wire.h>
#include <ctime>
#include <limits>
#include <sys/time.h>

namespace
{
constexpr time_t kMinValidEpochSeconds = 1577836800; // 2020-01-01 00:00:00 UTC

// Civil date to UNIX epoch days (UTC), based on Howard Hinnant's algorithm.
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

bool gps_datetime_valid(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    if (year < 2020 || year > 2100)
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

time_t gps_datetime_to_epoch_utc(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    const int64_t days = days_from_civil(year, month, day);
    const int64_t sec_of_day = static_cast<int64_t>(hour) * 3600 +
                               static_cast<int64_t>(minute) * 60 +
                               static_cast<int64_t>(second);
    const int64_t epoch64 = days * 86400 + sec_of_day;
    if (epoch64 < 0 || epoch64 > static_cast<int64_t>(std::numeric_limits<time_t>::max()))
    {
        return static_cast<time_t>(-1);
    }
    return static_cast<time_t>(epoch64);
}

int read_battery_mv_adc_fallback()
{
#ifdef BOARD_BAT_ADC
    analogSetPinAttenuation(BOARD_BAT_ADC, ADC_11db);
    // LilyGo T-Deck reference code uses analogReadMilliVolts(BOARD_BAT_ADC) * 2.
    int mv = analogReadMilliVolts(BOARD_BAT_ADC);
    if (mv <= 0)
    {
        return -1;
    }
    return mv * 2;
#else
    return -1;
#endif
}

int battery_percent_from_mv(int mv)
{
    if (mv <= 0)
    {
        return -1;
    }
    int pct = static_cast<int>(((mv - 3300) / 900.0f) * 100.0f);
    if (pct < 0)
    {
        pct = 0;
    }
    if (pct > 100)
    {
        pct = 100;
    }
    return pct;
}

int read_battery_percent_adc_fallback()
{
    return battery_percent_from_mv(read_battery_mv_adc_fallback());
}
} // namespace

TDeckBoard::TDeckBoard()
    : LilyGo_Display(SPI_DRIVER, false),
      LilyGoDispArduinoSPI(SCREEN_WIDTH, SCREEN_HEIGHT,
                           display::drivers::ST7789TDeck::getInitCommands(),
                           display::drivers::ST7789TDeck::getInitCommandsCount(),
                           display::drivers::ST7789TDeck::getRotationConfig(SCREEN_WIDTH, SCREEN_HEIGHT))
{
}

TDeckBoard* TDeckBoard::getInstance()
{
    static TDeckBoard instance;
    return &instance;
}

bool TDeckBoard::initGPS()
{
    // T-Deck examples wire GPS to UART on pins 43/44.
    Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
    delay(50);

    bool ok = gps_.init(&Serial1);
    setGPSOnline(ok);
    Serial.printf("[TDeckBoard] GPS init: %s\n", ok ? "OK" : "FAIL");
    return ok;
}

uint32_t TDeckBoard::begin(uint32_t disable_hw_init)
{
    (void)disable_hw_init;
    boot_ms_ = millis();

    // Early probe: try to bring up serial ASAP for boot diagnostics.
    Serial.begin(115200);
    delay(20);
    Serial.println("[TDeckBoard] begin: early probe start");

    // T-Deck requires the power enable pin to be asserted very early.
#ifdef BOARD_POWERON
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    delay(50);
    Serial.println("[TDeckBoard] poweron=HIGH");
#endif

    // Follow LilyGo T-Deck examples: de-conflict shared SPI bus early.
#ifdef SD_CS
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
#endif
#ifdef LORA_CS
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
#endif
#ifdef DISP_CS
    pinMode(DISP_CS, OUTPUT);
    digitalWrite(DISP_CS, HIGH);
#endif
#ifdef MISO
    pinMode(MISO, INPUT_PULLUP);
#endif

    // Initialize I2C early for PMU/RTC-class peripherals.
    Wire.begin(SDA, SCL);
    delay(10);

    SPI.begin(SCK, MISO, MOSI);
    Serial.println("[TDeckBoard] SPI bus initialized");

    // Initialize trackball pins early; they are active-low in LilyGo examples.
#if defined(TRACKBALL_UP) && defined(TRACKBALL_DOWN) && defined(TRACKBALL_LEFT) && defined(TRACKBALL_RIGHT)
    pinMode(TRACKBALL_UP, INPUT_PULLUP);
    pinMode(TRACKBALL_DOWN, INPUT_PULLUP);
    pinMode(TRACKBALL_LEFT, INPUT_PULLUP);
    pinMode(TRACKBALL_RIGHT, INPUT_PULLUP);
#endif
#if defined(TRACKBALL_CLICK)
    pinMode(TRACKBALL_CLICK, INPUT_PULLUP);
#endif

    // Ensure backlight rail is enabled even before full display init.
#ifdef DISP_BL
    if (DISP_BL >= 0)
    {
        pinMode(DISP_BL, OUTPUT);
        // Blink backlight briefly as a visual heartbeat.
        digitalWrite(DISP_BL, LOW);
        delay(60);
        digitalWrite(DISP_BL, HIGH);
        delay(60);
        digitalWrite(DISP_BL, LOW);
        delay(60);
        digitalWrite(DISP_BL, HIGH);
        Serial.println("[TDeckBoard] backlight blinked");
    }
#endif

    // Initialize radio minimally; only mark online on success.
    devices_probe_ = 0;
    pmu_ready_ = initPMU();
    touch_ready_ = initTouch();
    rtc_ready_ = (time(nullptr) > 0);

    // Initialize display (ST7789) before SD so the SPI lock exists (pager-style ordering).
#if defined(DISP_SCK) && defined(DISP_MISO) && defined(DISP_MOSI) && defined(DISP_CS) && defined(DISP_DC)
    // ST7789 on T-Deck is sensitive to long bursts; use a conservative SPI clock.
    LilyGoDispArduinoSPI::init(DISP_SCK, DISP_MISO, DISP_MOSI, DISP_CS, DISP_RST, DISP_DC, DISP_BL, 10, SPI);
    // T-Deck default orientation should be rotated right by 90 degrees.
    LilyGoDispArduinoSPI::setRotation(1);
    rotation_ = LilyGoDispArduinoSPI::getRotation();
    display_ready_ = true;
    Serial.printf("[TDeckBoard] display init OK: %ux%u\n", LilyGoDispArduinoSPI::_width, LilyGoDispArduinoSPI::_height);
#else
    Serial.println("[TDeckBoard] display init skipped: missing DISP_* pins");
#endif

    // Initialize radio before SD to align with the pager begin() sequence.
    radio_.reset();
    int radio_state = radio_.begin();
    if (radio_state == RADIOLIB_ERR_NONE)
    {
        devices_probe_ |= HW_RADIO_ONLINE;
        Serial.println("[TDeckBoard] radio init OK");
    }
    else
    {
        Serial.printf("[TDeckBoard] radio init failed: %d\n", radio_state);
    }

    // Initialize SD card - optional, with retry (pager-style ordering).
    if ((disable_hw_init & NO_HW_SD) == 0)
    {
        const int max_retries = 2;
        sd_ready_ = false;
        for (int retry = 0; retry < max_retries; ++retry)
        {
            if (installSD())
            {
                sd_ready_ = true;
                devices_probe_ |= HW_SD_ONLINE;
                break;
            }
            if (retry < max_retries - 1)
            {
                Serial.printf("[TDeckBoard] SD init failed, retrying... (%d/%d)\n", retry + 1, max_retries);
                delay(100);
            }
            else
            {
                Serial.printf("[TDeckBoard] SD init failed after %d attempts\n", max_retries);
            }
        }
    }
    else
    {
        Serial.println("[TDeckBoard] SD init skipped by NO_HW_SD");
    }

    if ((disable_hw_init & NO_HW_GPS) == 0)
    {
        (void)initGPS();
    }
    else
    {
        Serial.println("[TDeckBoard] GPS init skipped by NO_HW_GPS");
    }
    Serial.println("[TDeckBoard] begin: early probe done");
    return devices_probe_;
}

bool TDeckBoard::initPMU()
{
    // T-Deck commonly ships with AXP2101 at 0x34, but I2C pins can differ by revision.
    bool ok = pmu_.begin(Wire, AXP2101_SLAVE_ADDRESS, SDA, SCL);
    if (!ok)
    {
#if defined(SENSOR_SDA) && defined(SENSOR_SCL)
        if (SENSOR_SDA != SDA || SENSOR_SCL != SCL)
        {
            Wire.begin(SENSOR_SDA, SENSOR_SCL);
            delay(10);
            ok = pmu_.begin(Wire, AXP2101_SLAVE_ADDRESS, SENSOR_SDA, SENSOR_SCL);
        }
#endif
        // Restore the primary bus for the rest of the system.
        Wire.begin(SDA, SCL);
        delay(5);
    }

    if (ok)
    {
        // Ensure PMU battery metrics are enabled; otherwise SOC can report stale 0.
        pmu_.enableBattDetection();
        pmu_.enableVbusVoltageMeasure();
        pmu_.enableBattVoltageMeasure();
        pmu_.enableSystemVoltageMeasure();
        pmu_.enableTemperatureMeasure();
    }
    Serial.printf("[TDeckBoard] PMU init: %s\n", ok ? "OK" : "FAIL");
    return ok;
}

bool TDeckBoard::initTouch()
{
#if defined(BOARD_TOUCH_INT)
    touch_.setPins(-1, BOARD_TOUCH_INT);
#else
    touch_.setPins(-1, -1);
#endif

    bool ok = touch_.begin(Wire, GT911_SLAVE_ADDRESS_H, SDA, SCL);
    if (!ok)
    {
        ok = touch_.begin(Wire, GT911_SLAVE_ADDRESS_L, SDA, SCL);
    }
    if (!ok)
    {
        Serial.println("[TDeckBoard] touch init failed");
        return false;
    }

    // Align with LilyGo T-Deck reference touch mapping.
    touch_.setMaxCoordinates(SCREEN_WIDTH, SCREEN_HEIGHT);
    touch_.setSwapXY(true);
    touch_.setMirrorXY(false, true);
    Serial.println("[TDeckBoard] touch init OK");
    return true;
}

bool TDeckBoard::installSD()
{
#ifdef SD_CS
    const int* extra_cs = nullptr;
    size_t extra_cs_count = 0;
#if defined(LORA_CS) && defined(DISP_CS)
    static const int extra_cs_pins[] = {LORA_CS, DISP_CS};
    extra_cs = extra_cs_pins;
    extra_cs_count = sizeof(extra_cs_pins) / sizeof(extra_cs_pins[0]);
#elif defined(LORA_CS)
    static const int extra_cs_pins[] = {LORA_CS};
    extra_cs = extra_cs_pins;
    extra_cs_count = 1;
#elif defined(DISP_CS)
    static const int extra_cs_pins[] = {DISP_CS};
    extra_cs = extra_cs_pins;
    extra_cs_count = 1;
#endif

    uint8_t cardType = CARD_NONE;
    uint32_t cardSizeMB = 0;
    // Prefer a practical default speed; fallback ladder inside sd_utils preserves compatibility.
    bool ok = sdutil::installSpiSd(*this, SD_CS, 4000000U, "/sd",
                                   extra_cs, extra_cs_count,
                                   &cardType, &cardSizeMB,
                                   display_ready_);

    Serial.printf("[TDeckBoard] SD init: %s\n", ok ? "OK" : "FAIL");
    if (ok)
    {
        Serial.printf("[TDeckBoard] SD card type=%u size=%luMB\n",
                      (unsigned)cardType, (unsigned long)cardSizeMB);
    }
    return ok;
#else
    Serial.println("[TDeckBoard] SD init skipped: missing SD_CS");
    return false;
#endif
}

void TDeckBoard::uninstallSD()
{
    if (LilyGoDispArduinoSPI::lock(portMAX_DELAY))
    {
        SD.end();
        LilyGoDispArduinoSPI::unlock();
        Serial.println("[TDeckBoard] SD unmounted");
    }
    else
    {
        Serial.println("[TDeckBoard] SD unmount: SPI lock failed");
    }
}

bool TDeckBoard::isRTCReady() const
{
    // T-Deck has no dedicated external RTC chip in this project. We treat
    // "RTC ready" as "system epoch has been set to a sane value".
    const time_t now = time(nullptr);
    return rtc_ready_ || (now >= kMinValidEpochSeconds);
}

bool TDeckBoard::isCharging()
{
    if (!pmu_ready_)
    {
        return false;
    }
    return pmu_.isCharging();
}

int TDeckBoard::getBatteryLevel()
{
    int percent = -1;
    if (pmu_ready_)
    {
        percent = pmu_.getBatteryPercent();
    }

    int adc_percent = -1;
    auto ensure_adc_percent = [&adc_percent]()
    {
        if (adc_percent < 0)
        {
            adc_percent = read_battery_percent_adc_fallback();
        }
    };

    // PMU can transiently return invalid values on noisy buses.
    if (percent < 0 || percent > 100)
    {
        ensure_adc_percent();
        if (adc_percent >= 0)
        {
            percent = adc_percent;
        }
    }

    // Guard against fake PMU 0% (common when PMU state is stale/not actually wired).
    if (percent == 0)
    {
        ensure_adc_percent();
        if (adc_percent >= 10)
        {
            percent = adc_percent;
        }
    }

    // Guard against unrealistic sudden drops; re-check with ADC before accepting.
    if (last_battery_level_ >= 0 && percent >= 0 && percent + 40 < last_battery_level_)
    {
        ensure_adc_percent();
        if (adc_percent >= 0)
        {
            percent = adc_percent;
        }
    }

    if (percent < 0)
    {
        return last_battery_level_;
    }

    if (percent > 100)
    {
        percent = 100;
    }

    // Suppress sudden fake drops to 0% while battery is clearly not empty.
    bool charging = isCharging();
    if (!charging && percent == 0 && last_battery_level_ >= 15)
    {
        if (battery_zero_streak_ < 3)
        {
            battery_zero_streak_++;
            return last_battery_level_;
        }
    }
    else
    {
        battery_zero_streak_ = 0;
    }

    last_battery_level_ = percent;
    return percent;
}

void TDeckBoard::setBrightness(uint8_t level)
{
    brightness_ = level;
    if (!display_ready_)
    {
        return;
    }

    LilyGoDispArduinoSPI::setBrightness(level);
#if defined(DISP_BL)
    if (DISP_BL >= 0)
    {
        pinMode(DISP_BL, OUTPUT);
        digitalWrite(DISP_BL, (level > 0) ? HIGH : LOW);
    }
#endif
}

bool TDeckBoard::isCardReady()
{
    bool ready = false;
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(100)))
    {
        ready = (SD.sectorSize() != 0);
        LilyGoDispArduinoSPI::unlock();
    }
    return ready;
}

void TDeckBoard::setRotation(uint8_t rotation)
{
    LilyGoDispArduinoSPI::setRotation(rotation);
    rotation_ = LilyGoDispArduinoSPI::getRotation();
}

uint8_t TDeckBoard::getRotation()
{
    return LilyGoDispArduinoSPI::getRotation();
}

void TDeckBoard::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color)
{
    LilyGoDispArduinoSPI::pushColors(x1, y1, x2, y2, color);
}

uint16_t TDeckBoard::width()
{
    return LilyGoDispArduinoSPI::_width;
}

uint16_t TDeckBoard::height()
{
    return LilyGoDispArduinoSPI::_height;
}

uint8_t TDeckBoard::getPoint(int16_t* x, int16_t* y, uint8_t get_point)
{
    if (!touch_ready_)
    {
        return 0;
    }

    uint8_t touched = touch_.getPoint(x, y, get_point);
    if (!touched || !x || !y)
    {
        return touched;
    }

    int16_t tx = *x;
    int16_t ty = *y;
    const int16_t w = static_cast<int16_t>(SCREEN_WIDTH);
    const int16_t h = static_cast<int16_t>(SCREEN_HEIGHT);
    if (tx < 0)
    {
        tx = 0;
    }
    else if (tx >= w)
    {
        tx = static_cast<int16_t>(w - 1);
    }
    if (ty < 0)
    {
        ty = 0;
    }
    else if (ty >= h)
    {
        ty = static_cast<int16_t>(h - 1);
    }

    *x = tx;
    *y = ty;
    return touched;
}

bool TDeckBoard::syncTimeFromGPS(uint32_t gps_task_interval_ms)
{
    if (!gps_.date.isValid() || !gps_.time.isValid())
    {
        return false;
    }

    uint16_t year = gps_.date.year();
    uint8_t month = gps_.date.month();
    uint8_t day = gps_.date.day();
    uint8_t hour = gps_.time.hour();
    uint8_t minute = gps_.time.minute();
    uint8_t second = gps_.time.second();

    if (!gps_datetime_valid(year, month, day, hour, minute, second))
    {
        Serial.printf("[TDeckBoard] GPS time rejected: %04u-%02u-%02u %02u:%02u:%02u\n",
                      year, month, day, hour, minute, second);
        return false;
    }

    // NMEA timestamps are typically not "now"; apply a conservative compensation.
    const uint32_t read_start_ms = millis();
    uint32_t task_interval_comp_ms = 0;
    if (gps_task_interval_ms > 5000)
    {
        task_interval_comp_ms = gps_task_interval_ms / 2;
        if (task_interval_comp_ms > 5000)
        {
            task_interval_comp_ms = 5000;
        }
    }
    const uint32_t processing_delay_ms = millis() - read_start_ms;
    const uint32_t total_delay_ms = 2000 + task_interval_comp_ms + processing_delay_ms;

    time_t epoch = gps_datetime_to_epoch_utc(year, month, day, hour, minute, second);
    if (epoch < kMinValidEpochSeconds)
    {
        return false;
    }
    epoch += static_cast<time_t>((total_delay_ms + 500) / 1000);

    timeval tv{};
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0)
    {
        Serial.println("[TDeckBoard] settimeofday() failed");
        return false;
    }

    rtc_ready_ = true;
    Serial.printf("[TDeckBoard] Time synced from GPS: %04u-%02u-%02u %02u:%02u:%02u (sat=%u fix=%d)\n",
                  year, month, day, hour, minute, second,
                  static_cast<unsigned>(gps_.satellites.value()),
                  gps_.location.isValid() ? 1 : 0);
    return true;
}

int TDeckBoard::transmitRadio(const uint8_t* data, size_t len)
{
    // Share the SPI bus with display to avoid tearing due to contention.
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(50)))
    {
        int rc = radio_.transmit(data, len);
        LilyGoDispArduinoSPI::unlock();
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

int TDeckBoard::startRadioReceive()
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(50)))
    {
        int rc = radio_.startReceive();
        LilyGoDispArduinoSPI::unlock();
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

uint32_t TDeckBoard::getRadioIrqFlags()
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(20)))
    {
        uint32_t flags = radio_.getIrqFlags();
        LilyGoDispArduinoSPI::unlock();
        return flags;
    }
    return 0;
}

int TDeckBoard::getRadioPacketLength(bool update)
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(20)))
    {
        int len = static_cast<int>(radio_.getPacketLength(update));
        LilyGoDispArduinoSPI::unlock();
        return len;
    }
    return 0;
}

int TDeckBoard::readRadioData(uint8_t* buf, size_t len)
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(50)))
    {
        int rc = radio_.readData(buf, len);
        LilyGoDispArduinoSPI::unlock();
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

void TDeckBoard::clearRadioIrqFlags(uint32_t flags)
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(20)))
    {
        radio_.clearIrqFlags(flags);
        LilyGoDispArduinoSPI::unlock();
    }
}

float TDeckBoard::getRadioRSSI()
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(20)))
    {
        float rssi = radio_.getRSSI();
        LilyGoDispArduinoSPI::unlock();
        return rssi;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

float TDeckBoard::getRadioSNR()
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(20)))
    {
        float snr = radio_.getSNR();
        LilyGoDispArduinoSPI::unlock();
        return snr;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

#if defined(ARDUINO_LILYGO_LORA_SX1262)
static void apply_tx_power(SX1262Access& radio, int8_t tx_power)
{
    constexpr int8_t kTxPowerMinDbm = -9;
    int8_t clipped = tx_power;
    if (clipped < kTxPowerMinDbm) clipped = kTxPowerMinDbm;
    radio.setOutputPower(clipped);
}
#endif

void TDeckBoard::configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                                    int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                                    uint8_t crc_len)
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(100)))
    {
        radio_.setFrequency(freq_mhz);
        radio_.setBandwidth(bw_khz);
        radio_.setSpreadingFactor(sf);
        radio_.setCodingRate(cr_denom);
#if defined(ARDUINO_LILYGO_LORA_SX1262)
        apply_tx_power(radio_, tx_power);
#else
        radio_.setOutputPower(tx_power);
#endif
        radio_.setPreambleLength(preamble_len);
        radio_.setSyncWord(sync_word);
        radio_.setCRC(crc_len);
        LilyGoDispArduinoSPI::unlock();
    }
}

RotaryMsg_t TDeckBoard::getRotary()
{
    RotaryMsg_t msg{};

    const uint32_t now = millis();
    if ((now - boot_ms_) < kRotaryBootGuardMs)
    {
        // Ignore early boot noise from the trackball/boot pins.
        return msg;
    }

    // T-Deck trackball tuning:
    // - Use press-edge pulse detection to avoid mixed/sticky level states.
    // - Direction mapping follows physical intuition:
    //   up/left => ROTARY_DIR_UP, down/right => ROTARY_DIR_DOWN.
    const uint32_t repeat_ms = 110;  // Minimum spacing between direction events
    const uint32_t click_ms = 150;   // Click debounce
    const uint32_t debounce_ms = 22; // Require stable press/release

    static bool up_state = false;
    static bool down_state = false;
    static bool left_state = false;
    static bool right_state = false;
    static bool click_state = false;
    static bool click_consumed = false;
    static uint32_t up_change_ms = 0;
    static uint32_t down_change_ms = 0;
    static uint32_t left_change_ms = 0;
    static uint32_t right_change_ms = 0;
    static uint32_t click_change_ms = 0;
    bool up_pressed = false;
    bool down_pressed = false;
    bool left_pressed = false;
    bool right_pressed = false;
#if defined(TRACKBALL_RIGHT) && defined(TRACKBALL_LEFT)
    right_pressed = (digitalRead(TRACKBALL_RIGHT) == LOW);
    left_pressed = (digitalRead(TRACKBALL_LEFT) == LOW);
#endif
#if defined(TRACKBALL_UP) && defined(TRACKBALL_DOWN)
    up_pressed = (digitalRead(TRACKBALL_UP) == LOW);
    down_pressed = (digitalRead(TRACKBALL_DOWN) == LOW);
#endif

    auto detect_press_edge = [&](bool pressed, bool& state, uint32_t& change_ms) -> bool
    {
        if (pressed != state)
        {
            state = pressed;
            if (pressed && (now - change_ms) >= debounce_ms)
            {
                change_ms = now;
                return true;
            }
            change_ms = now;
        }
        return false;
    };

    const bool up_edge = detect_press_edge(up_pressed, up_state, up_change_ms);
    const bool down_edge = detect_press_edge(down_pressed, down_state, down_change_ms);
    const bool left_edge = detect_press_edge(left_pressed, left_state, left_change_ms);
    const bool right_edge = detect_press_edge(right_pressed, right_state, right_change_ms);

    const uint8_t up_score = (up_edge ? 1U : 0U) + (left_edge ? 1U : 0U);
    const uint8_t down_score = (down_edge ? 1U : 0U) + (right_edge ? 1U : 0U);

    if ((up_score > 0 || down_score > 0) &&
        (now - last_trackball_ms_) >= repeat_ms)
    {
        if (up_score > down_score)
        {
            msg.dir = ROTARY_DIR_UP;
            last_trackball_ms_ = now;
        }
        else if (down_score > up_score)
        {
            msg.dir = ROTARY_DIR_DOWN;
            last_trackball_ms_ = now;
        }
    }

#if defined(TRACKBALL_CLICK)
    const bool click_pressed = digitalRead(TRACKBALL_CLICK) == LOW;
    if (click_pressed != click_state)
    {
        click_state = click_pressed;
        click_change_ms = now;
    }

    // Re-arm click detection after a stable release, so every press can emit one event.
    if (!click_state && (now - click_change_ms) >= debounce_ms)
    {
        click_consumed = false;
    }

    if (click_state && (now - click_change_ms) >= debounce_ms &&
        !click_consumed && (now - last_click_ms_) >= click_ms)
    {
        msg.centerBtnPressed = true;
        last_click_ms_ = now;
        click_consumed = true;
    }
#endif

    return msg;
}

namespace
{
TDeckBoard& getInstanceRef()
{
    return *TDeckBoard::getInstance();
}
} // namespace

TDeckBoard& instance = getInstanceRef();
BoardBase& board = getInstanceRef();
