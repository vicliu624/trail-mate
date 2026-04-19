#if defined(ARDUINO_T_DECK)
#include "boards/tdeck/tdeck_board.h"
#include "board/sd_utils.h"
#include "display/drivers/ST7789TDeck.h"
#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorRTTTL.h>
#include <AudioOutputI2S.h>
#include <SD.h>
#include <Wire.h>
#include <ctime>
#include <limits>
#include <sys/time.h>

namespace boards::tdeck
{

namespace
{
constexpr time_t kMinValidEpochSeconds = 1577836800; // 2020-01-01 00:00:00 UTC
constexpr uint8_t kTDeckKeyboardAddress = 0x55;
constexpr uint8_t kTDeckKeyboardBrightnessCmd = 0x01;
constexpr uint8_t kTDeckKeyboardDefaultBrightnessCmd = 0x02;
constexpr uint8_t kTDeckKeyboardModeRawCmd = 0x03;
constexpr uint8_t kTDeckKeyboardModeKeyCmd = 0x04;
constexpr uint32_t kTDeckKeyboardBootDelayMs = 500;
constexpr float kTDeckRadioTcxoVoltage = 1.8f;
constexpr float kTDeckRadioCurrentLimitMa = 140.0f;

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
                           display::drivers::ST7789TDeck::getRotationConfig(SCREEN_WIDTH, SCREEN_HEIGHT),
                           display::drivers::ST7789TDeck::getTransferConfig())
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
    keyboard_ready_ = initKeyboard();
    if (keyboard_ready_)
    {
        devices_probe_ |= HW_KEYBOARD_ONLINE;
        Serial.println("[TDeckBoard] keyboard init OK");
    }
    else
    {
        Serial.println("[TDeckBoard] keyboard not detected");
    }
    rtc_ready_ = (time(nullptr) > 0);

    // Initialize display (ST7789) before SD so the SPI lock exists (pager-style ordering).
#if defined(DISP_SCK) && defined(DISP_MISO) && defined(DISP_MOSI) && defined(DISP_CS) && defined(DISP_DC)
    // Match LilyGo's T-Deck reference setup: a 40 MHz display clock shortens LVGL flush time
    // and reduces the visible scan/jelly effect during scrolling and animations.
    LilyGoDispArduinoSPI::init(DISP_SCK, DISP_MISO, DISP_MOSI, DISP_CS, DISP_RST, DISP_DC, DISP_BL, 40, SPI);
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
    int radio_state = radio_.begin(434.0f, 125.0f, 9, 7, RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                                   10, 8, kTDeckRadioTcxoVoltage, false);
    if (radio_state == RADIOLIB_ERR_NONE)
    {
        const int rf_switch_state = radio_.setDio2AsRfSwitch(true);
        const int current_limit_state = radio_.setCurrentLimit(kTDeckRadioCurrentLimitMa);
        if (rf_switch_state == RADIOLIB_ERR_NONE && current_limit_state == RADIOLIB_ERR_NONE)
        {
            devices_probe_ |= HW_RADIO_ONLINE;
            Serial.println("[TDeckBoard] radio init OK");
        }
        else
        {
            Serial.printf("[TDeckBoard] radio post-init failed: rf_switch=%d current_limit=%d\n",
                          rf_switch_state,
                          current_limit_state);
        }
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

bool TDeckBoard::initKeyboard()
{
#ifdef USING_INPUT_DEV_KEYBOARD
    const uint32_t elapsed = millis() - boot_ms_;
    if (elapsed < kTDeckKeyboardBootDelayMs)
    {
        delay(kTDeckKeyboardBootDelayMs - elapsed);
    }

    Wire.beginTransmission(kTDeckKeyboardAddress);
    if (Wire.endTransmission() != 0)
    {
        Serial.println("[TDeckBoard] keyboard probe failed");
        return false;
    }

    if (!sendKeyboardCommand(kTDeckKeyboardModeKeyCmd))
    {
        Serial.println("[TDeckBoard] keyboard key-mode command failed");
        return false;
    }

    const int value = readKeyboardByte();
    if (value < 0)
    {
        return false;
    }

    keyboard_pending_release_ = false;
    keyboard_last_char_ = '\0';
    const uint8_t default_brightness = keyboard_brightness_ > 0 ? keyboard_brightness_ : 127;
    sendKeyboardCommand(kTDeckKeyboardDefaultBrightnessCmd, default_brightness);
    sendKeyboardCommand(kTDeckKeyboardBrightnessCmd, keyboard_brightness_);
    return true;
#else
    return false;
#endif
}

bool TDeckBoard::sendKeyboardCommand(uint8_t cmd)
{
#ifdef USING_INPUT_DEV_KEYBOARD
    Wire.beginTransmission(kTDeckKeyboardAddress);
    Wire.write(cmd);
    return Wire.endTransmission() == 0;
#else
    (void)cmd;
    return false;
#endif
}

bool TDeckBoard::sendKeyboardCommand(uint8_t cmd, uint8_t value)
{
#ifdef USING_INPUT_DEV_KEYBOARD
    Wire.beginTransmission(kTDeckKeyboardAddress);
    Wire.write(cmd);
    Wire.write(value);
    return Wire.endTransmission() == 0;
#else
    (void)cmd;
    (void)value;
    return false;
#endif
}

int TDeckBoard::readKeyboardByte()
{
#ifdef USING_INPUT_DEV_KEYBOARD
    const uint8_t bytes = Wire.requestFrom(kTDeckKeyboardAddress, static_cast<uint8_t>(1));
    if (bytes < 1 || Wire.available() < 1)
    {
        return -1;
    }
    const int value = Wire.read();
    while (Wire.available() > 0)
    {
        Wire.read();
    }
    return value;
#else
    return -1;
#endif
}

char TDeckBoard::translateKeyboardByte(uint8_t value)
{
    if (value == 0x00)
    {
        return '\0';
    }
    if (value == 0x08)
    {
        return '\b';
    }
    if (value == 0x0D || value == 0x0A)
    {
        return '\n';
    }
    return static_cast<char>(value);
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

bool TDeckBoard::hasKeyboard()
{
#ifdef USING_INPUT_DEV_KEYBOARD
    return (devices_probe_ & HW_KEYBOARD_ONLINE) != 0;
#else
    return false;
#endif
}

void TDeckBoard::keyboardSetBrightness(uint8_t level)
{
    keyboard_brightness_ = level;
#ifdef USING_INPUT_DEV_KEYBOARD
    if (keyboard_ready_)
    {
        sendKeyboardCommand(kTDeckKeyboardBrightnessCmd, level);
    }
#endif
}

uint8_t TDeckBoard::keyboardGetBrightness()
{
    return keyboard_brightness_;
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

    // Map raw touch coordinates into the current display rotation.
    // This keeps the logical LVGL coordinates aligned with what the user sees.
    int16_t lx = tx;
    int16_t ly = ty;
    switch (rotation_)
    {
    case 0:
        // No rotation: direct mapping.
        break;
    case 1:
        // Rotated 90 degrees (T-Deck default: landscape with USB on the right).
        // Raw touch uses panel's native axes; map to LVGL's rotated axes.
        lx = ty;
        ly = static_cast<int16_t>(w - 1 - tx);
        break;
    case 2:
        // Rotated 180 degrees.
        lx = static_cast<int16_t>(w - 1 - tx);
        ly = static_cast<int16_t>(h - 1 - ty);
        break;
    case 3:
        // Rotated 270 degrees.
        lx = static_cast<int16_t>(h - 1 - ty);
        ly = tx;
        break;
    default:
        break;
    }

    *x = lx;
    *y = ly;
    return touched;
}

int TDeckBoard::getKeyChar(char* c)
{
#ifdef USING_INPUT_DEV_KEYBOARD
    if (!keyboard_ready_ || !c)
    {
        return -1;
    }

    if (keyboard_pending_release_)
    {
        *c = keyboard_last_char_;
        keyboard_pending_release_ = false;
        return KEYBOARD_RELEASED;
    }

    const int value = readKeyboardByte();
    if (value < 0)
    {
        return -1;
    }

    const char translated = translateKeyboardByte(static_cast<uint8_t>(value));
    if (translated == '\0')
    {
        return -1;
    }

    *c = translated;
    keyboard_last_char_ = translated;
    keyboard_pending_release_ = true;
    return KEYBOARD_PRESSED;
#else
    (void)c;
    return -1;
#endif
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
    if (LilyGoDispArduinoSPI::lock(portMAX_DELAY))
    {
        int rc = radio_.transmit(data, len);
        LilyGoDispArduinoSPI::unlock();
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

int TDeckBoard::startRadioReceive()
{
    if (LilyGoDispArduinoSPI::lock(portMAX_DELAY))
    {
        int rc = radio_.startReceive();
        LilyGoDispArduinoSPI::unlock();
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

uint32_t TDeckBoard::getRadioIrqFlags()
{
    if (LilyGoDispArduinoSPI::lock(portMAX_DELAY))
    {
        uint32_t flags = radio_.getIrqFlags();
        LilyGoDispArduinoSPI::unlock();
        return flags;
    }
    return 0;
}

int TDeckBoard::getRadioPacketLength(bool update)
{
    if (LilyGoDispArduinoSPI::lock(portMAX_DELAY))
    {
        int len = static_cast<int>(radio_.getPacketLength(update));
        LilyGoDispArduinoSPI::unlock();
        return len;
    }
    return 0;
}

int TDeckBoard::readRadioData(uint8_t* buf, size_t len)
{
    if (LilyGoDispArduinoSPI::lock(portMAX_DELAY))
    {
        int rc = radio_.readData(buf, len);
        LilyGoDispArduinoSPI::unlock();
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

void TDeckBoard::clearRadioIrqFlags(uint32_t flags)
{
    if (LilyGoDispArduinoSPI::lock(portMAX_DELAY))
    {
        radio_.clearIrqFlags(flags);
        LilyGoDispArduinoSPI::unlock();
    }
}

float TDeckBoard::getRadioRSSI()
{
    if (LilyGoDispArduinoSPI::lock(portMAX_DELAY))
    {
        float rssi = radio_.getRSSI();
        LilyGoDispArduinoSPI::unlock();
        return rssi;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

float TDeckBoard::getRadioSNR()
{
    if (LilyGoDispArduinoSPI::lock(portMAX_DELAY))
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
    if (LilyGoDispArduinoSPI::lock(portMAX_DELAY))
    {
#if defined(ARDUINO_LILYGO_LORA_SX1262)
        radio_.setDio2AsRfSwitch(true);
        radio_.setCurrentLimit(kTDeckRadioCurrentLimitMa);
#endif
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
    // T-Deck trackball is modeled as directional keypad input so the menu can
    // move in two dimensions. Leave encoder output disabled on this board.
    RotaryMsg_t msg{};
    return msg;
}

int TDeckBoard::getNavKey(uint32_t* key)
{
    if (!key)
    {
        return -1;
    }

    static bool up_state = false;
    static bool down_state = false;
    static bool left_state = false;
    static bool right_state = false;
    static bool click_state = false;
    static bool click_consumed = false;
    static bool pending_release = false;
    static uint32_t pending_key = INPUT_NAV_KEY_NONE;
    static uint32_t up_change_ms = 0;
    static uint32_t down_change_ms = 0;
    static uint32_t left_change_ms = 0;
    static uint32_t right_change_ms = 0;
    static uint32_t click_change_ms = 0;

    if (pending_release)
    {
        *key = pending_key;
        pending_release = false;
        return KEYBOARD_RELEASED;
    }

    const uint32_t now = millis();
    if ((now - boot_ms_) < kRotaryBootGuardMs)
    {
        // Ignore early boot noise from the trackball/boot pins.
        return -1;
    }

    // T-Deck trackball tuning:
    // - Use press-edge pulse detection to avoid mixed/sticky level states.
    // - Map physical directions directly to logical UI directions.
    const uint32_t repeat_ms = 110;  // Minimum spacing between direction events
    const uint32_t click_ms = 150;   // Click debounce
    const uint32_t debounce_ms = 22; // Require stable press/release

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

    uint32_t nav_key = INPUT_NAV_KEY_NONE;
    if ((now - last_trackball_ms_) >= repeat_ms)
    {
        if (left_edge)
        {
            nav_key = INPUT_NAV_KEY_LEFT;
            last_trackball_ms_ = now;
        }
        else if (right_edge)
        {
            nav_key = INPUT_NAV_KEY_RIGHT;
            last_trackball_ms_ = now;
        }
        else if (up_edge)
        {
            nav_key = INPUT_NAV_KEY_UP;
            last_trackball_ms_ = now;
        }
        else if (down_edge)
        {
            nav_key = INPUT_NAV_KEY_DOWN;
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
        nav_key = INPUT_NAV_KEY_ENTER;
        last_click_ms_ = now;
        click_consumed = true;
    }
#endif

    if (nav_key == INPUT_NAV_KEY_NONE)
    {
        return -1;
    }

    pending_key = nav_key;
    pending_release = true;
    *key = nav_key;
    return KEYBOARD_PRESSED;
}

void TDeckBoard::playMessageTone()
{
#if defined(DAC_I2S_BCK) && defined(DAC_I2S_WS) && defined(DAC_I2S_DOUT)
    if (message_tone_volume_ == 0)
    {
        return;
    }

    static bool s_playing = false;
    static uint32_t s_last_play_ms = 0;

    if (s_playing)
    {
        return;
    }

    const uint32_t now = millis();
    if ((now - s_last_play_ms) < 240)
    {
        return;
    }

    s_playing = true;
    s_last_play_ms = now;

    static const char kMessageToneRtttl[] = "MsgRcv:d=4,o=6,b=200:32e,32g,32b,16c7";

    AudioOutputI2S audio_out(1, AudioOutputI2S::EXTERNAL_I2S);
#if defined(DAC_I2S_MCLK)
    audio_out.SetPinout(DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT, DAC_I2S_MCLK);
#else
    audio_out.SetPinout(DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT);
#endif
    float gain = static_cast<float>(message_tone_volume_) / 250.0f;
    if (gain < 0.0f)
    {
        gain = 0.0f;
    }
    if (gain > 0.40f)
    {
        gain = 0.40f;
    }
    audio_out.SetGain(gain);

    AudioFileSourcePROGMEM song(kMessageToneRtttl, sizeof(kMessageToneRtttl) - 1);
    AudioGeneratorRTTTL generator;

    if (generator.begin(&song, &audio_out))
    {
        const uint32_t deadline = millis() + 1600;
        while (generator.isRunning() && millis() < deadline)
        {
            if (!generator.loop())
            {
                break;
            }
            delay(1);
        }
        generator.stop();
    }
    audio_out.stop();
    s_playing = false;
#endif
}

void TDeckBoard::setMessageToneVolume(uint8_t volume_percent)
{
    if (volume_percent > 100)
    {
        volume_percent = 100;
    }
    message_tone_volume_ = volume_percent;
}

uint8_t TDeckBoard::getMessageToneVolume() const
{
    return message_tone_volume_;
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

} // namespace boards::tdeck

BoardBase& board = ::boards::tdeck::instance;
#endif // defined(ARDUINO_T_DECK)
