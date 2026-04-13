#if defined(ARDUINO_T_DECK_PRO)

#include "boards/tdeck_pro/tdeck_pro_board.h"

#include <Arduino.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorRTTTL.h>
#include <AudioOutputI2S.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <ctime>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <limits>
#include <sys/time.h>

namespace boards::tdeck_pro
{

namespace
{
constexpr const char* kTag = "TDeckProBoard";
constexpr time_t kMinValidEpochSeconds = 1577836800; // 2020-01-01 UTC
constexpr uint8_t kKeyboardRows = 4;
constexpr uint8_t kKeyboardCols = 10;
constexpr uint32_t kEpdSpiHz = 2000000;
constexpr uint8_t kFlushLogLimit = 8;
SemaphoreHandle_t g_shared_spi_mutex = nullptr;

void sharedSpiReleaseAllCs()
{
    digitalWrite(TDeckProBoard::profile().lora.cs, HIGH);
    digitalWrite(TDeckProBoard::profile().sd.cs, HIGH);
    digitalWrite(TDeckProBoard::profile().epd.cs, HIGH);
}

void sharedSpiBusInit()
{
    if (g_shared_spi_mutex == nullptr)
    {
        g_shared_spi_mutex = xSemaphoreCreateRecursiveMutex();
        if (g_shared_spi_mutex == nullptr)
        {
            Serial.printf("[%s] shared SPI mutex create failed\n", kTag);
            return;
        }
    }
    sharedSpiReleaseAllCs();
}

void sharedSpiLock()
{
    if (g_shared_spi_mutex == nullptr)
    {
        sharedSpiBusInit();
    }
    if (g_shared_spi_mutex != nullptr)
    {
        xSemaphoreTakeRecursive(g_shared_spi_mutex, portMAX_DELAY);
    }
}

void sharedSpiUnlock()
{
    if (g_shared_spi_mutex != nullptr)
    {
        sharedSpiReleaseAllCs();
        xSemaphoreGiveRecursive(g_shared_spi_mutex);
    }
}

void sharedSpiPrepareDevice(int cs_pin)
{
    sharedSpiReleaseAllCs();
    if (cs_pin >= 0)
    {
        digitalWrite(cs_pin, HIGH);
    }
}

int batteryPercentFromMv(int mv)
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

bool isLeapYear(int year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

uint8_t daysInMonth(int year, uint8_t month)
{
    static constexpr uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
    {
        return 0;
    }
    if (month == 2 && isLeapYear(year))
    {
        return 29;
    }
    return kDays[month - 1];
}

bool gpsDatetimeValid(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
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
    return hour < 24 && minute < 60 && second < 60;
}

int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

time_t gpsDatetimeToEpochUtc(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    const int64_t days = daysFromCivil(year, month, day);
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

char translateKey(uint8_t idx)
{
    static constexpr char kMap[kKeyboardRows][kKeyboardCols] = {
        {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
        {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
        {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '\n'},
        {'z', 'x', 'c', 'v', 'b', 'n', 'm', ' ', '\b', '.'},
    };
    const uint8_t row = idx / 10;
    const uint8_t col = idx % 10;
    if (row >= kKeyboardRows || col >= kKeyboardCols)
    {
        return '\0';
    }
    return kMap[row][col];
}

void applyTxPower(SX1262Access& radio, int8_t tx_power)
{
    constexpr int8_t kTxPowerMinDbm = -9;
    int8_t clipped = tx_power;
    if (clipped < kTxPowerMinDbm)
    {
        clipped = kTxPowerMinDbm;
    }
    radio.setOutputPower(clipped);
}
} // namespace

TDeckProBoard::TDeckProBoard()
    : LilyGo_Display(SPI_DRIVER, true)
{
    mono_buffer_.resize(static_cast<size_t>(profile().screen_width * profile().screen_height) / 8U, 0xFF);
}

TDeckProBoard* TDeckProBoard::getInstance()
{
    static TDeckProBoard singleton;
    return &singleton;
}

void TDeckProBoard::initSharedPins()
{
    pinMode(profile().lora.enable, OUTPUT);
    digitalWrite(profile().lora.enable, HIGH);

    pinMode(profile().gps.enable, OUTPUT);
    digitalWrite(profile().gps.enable, HIGH);

    pinMode(profile().motion.enable_1v8, OUTPUT);
    digitalWrite(profile().motion.enable_1v8, HIGH);

    if (profile().optional.has_a7682e && profile().optional.a7682e_enable >= 0)
    {
        pinMode(profile().optional.a7682e_enable, OUTPUT);
        digitalWrite(profile().optional.a7682e_enable, HIGH);
    }
    if (profile().optional.has_a7682e && profile().optional.a7682e_pwrkey >= 0)
    {
        pinMode(profile().optional.a7682e_pwrkey, OUTPUT);
        digitalWrite(profile().optional.a7682e_pwrkey, HIGH);
    }
    if (profile().motor_pin >= 0)
    {
        pinMode(profile().motor_pin, OUTPUT);
        digitalWrite(profile().motor_pin, LOW);
    }

    pinMode(profile().sd.cs, OUTPUT);
    digitalWrite(profile().sd.cs, HIGH);
    pinMode(profile().epd.cs, OUTPUT);
    digitalWrite(profile().epd.cs, HIGH);
    pinMode(profile().lora.cs, OUTPUT);
    digitalWrite(profile().lora.cs, HIGH);
    sharedSpiBusInit();
}

bool TDeckProBoard::initPower()
{
    Wire.begin(profile().i2c.sda, profile().i2c.scl);
    delay(10);

    power_ready_ = pmu_.begin(Wire, 0x6B, profile().i2c.sda, profile().i2c.scl);
    battery_gauge_ready_ = gauge_.begin(Wire, profile().i2c.sda, profile().i2c.scl);
    if (power_ready_)
    {
        pmu_.enableMeasure();
        pmu_.disableOTG();
    }
    Serial.printf("[%s] power init charger=%d gauge=%d\n", kTag, power_ready_ ? 1 : 0, battery_gauge_ready_ ? 1 : 0);
    return power_ready_ || battery_gauge_ready_;
}

bool TDeckProBoard::initDisplay()
{
    SPI.begin(profile().spi.sck, profile().spi.miso, profile().spi.mosi, profile().epd.cs);
    sharedSpiLock();
    sharedSpiPrepareDevice(profile().epd.cs);
    epd_.epd2.selectSPI(SPI, SPISettings(kEpdSpiHz, MSBFIRST, SPI_MODE0));
    epd_.init(0, true, 2, false);
    epd_.setRotation(rotation_);
    epd_.setFullWindow();
    epd_.firstPage();
    do
    {
        epd_.fillScreen(GxEPD_WHITE);
    } while (epd_.nextPage());
    epd_.powerOff();
    sharedSpiUnlock();
    display_ready_ = true;
    Serial.printf("[%s] epd init ok %ux%u spi=%luHz\n", kTag, width(), height(), (unsigned long)kEpdSpiHz);
    return true;
}

bool TDeckProBoard::initTouch()
{
    touch_.setPins(profile().touch.rst, profile().touch.int_pin);
    touch_ready_ = touch_.begin(Wire, profile().touch.i2c_addr, profile().i2c.sda, profile().i2c.scl);
    if (touch_ready_)
    {
        touch_.setMaxCoordinates(profile().screen_width, profile().screen_height);
    }
    Serial.printf("[%s] touch init %s\n", kTag, touch_ready_ ? "ok" : "fail");
    return touch_ready_;
}

bool TDeckProBoard::initKeyboard()
{
    keyboard_ready_ = keyboard_.begin(profile().keyboard.i2c_addr, &Wire);
    if (!keyboard_ready_)
    {
        Serial.printf("[%s] keyboard init fail\n", kTag);
        return false;
    }
    keyboard_.matrix(profile().keyboard.rows, profile().keyboard.cols);
    keyboard_.flush();
    keyboardSetBrightness(keyboard_brightness_);
    Serial.printf("[%s] keyboard init ok\n", kTag);
    return true;
}

bool TDeckProBoard::initMotion()
{
    motion_ready_ = motion_.begin(Wire, profile().motion.i2c_addr, profile().i2c.sda, profile().i2c.scl);
    Serial.printf("[%s] motion init %s\n", kTag, motion_ready_ ? "ok" : "fail");
    return motion_ready_;
}

bool TDeckProBoard::initRadio()
{
    SPI.begin(profile().spi.sck, profile().spi.miso, profile().spi.mosi, profile().lora.cs);
    sharedSpiLock();
    sharedSpiPrepareDevice(profile().lora.cs);
    radio_.reset();
    radio_ready_ = (radio_.begin() == RADIOLIB_ERR_NONE);
    sharedSpiUnlock();
    Serial.printf("[%s] radio init %s\n", kTag, radio_ready_ ? "ok" : "fail");
    return radio_ready_;
}

bool TDeckProBoard::initStorage()
{
    sd_ready_ = installSD();
    Serial.printf("[%s] sd init %s\n", kTag, sd_ready_ ? "ok" : "fail");
    return sd_ready_;
}

bool TDeckProBoard::installSD()
{
    pinMode(profile().sd.cs, OUTPUT);
    digitalWrite(profile().sd.cs, HIGH);
    sharedSpiLock();
    sharedSpiPrepareDevice(profile().sd.cs);
    const bool ok = SD.begin(profile().sd.cs, SPI);
    sharedSpiUnlock();
    return ok;
}

void TDeckProBoard::uninstallSD()
{
    SD.end();
}

uint32_t TDeckProBoard::begin(uint32_t disable_hw_init)
{
    Serial.begin(115200);
    delay(30);
    Serial.printf("[%s] begin variant=%s\n", kTag,
#if defined(TRAIL_MATE_TDECK_PRO_A7682E)
                  "a7682e"
#else
                  "pcm512a"
#endif
    );

    initSharedPins();
    (void)initPower();
    (void)initDisplay();

    if ((disable_hw_init & NO_HW_TOUCH) == 0)
    {
        (void)initTouch();
    }
    (void)initKeyboard();
    (void)initMotion();
    if ((disable_hw_init & NO_HW_SD) == 0)
    {
        (void)initStorage();
    }
    if ((disable_hw_init & NO_HW_GPS) == 0)
    {
        (void)initGPS();
    }
    (void)initRadio();

    rtc_ready_ = time(nullptr) >= kMinValidEpochSeconds;

    uint32_t probe = 0;
    if (power_ready_) probe |= HW_PMU_ONLINE;
    if (battery_gauge_ready_) probe |= HW_GAUGE_ONLINE;
    if (touch_ready_) probe |= HW_TOUCH_ONLINE;
    if (keyboard_ready_) probe |= HW_KEYBOARD_ONLINE;
    if (motion_ready_) probe |= HW_BHI260AP_ONLINE;
    if (sd_ready_) probe |= HW_SD_ONLINE;
    if (gps_ready_) probe |= HW_GPS_ONLINE;
    if (radio_ready_) probe |= HW_RADIO_ONLINE;
    return probe;
}

void TDeckProBoard::setBrightness(uint8_t level)
{
    brightness_ = level;
}

void TDeckProBoard::keyboardSetBrightness(uint8_t level)
{
    keyboard_brightness_ = level;
    if (profile().keyboard.led >= 0)
    {
        analogWrite(profile().keyboard.led, level);
    }
}

bool TDeckProBoard::isRTCReady() const
{
    return rtc_ready_ || (time(nullptr) >= kMinValidEpochSeconds);
}

bool TDeckProBoard::isCharging()
{
    return power_ready_ ? pmu_.isCharging() : false;
}

int TDeckProBoard::getBatteryLevel()
{
    if (battery_gauge_ready_)
    {
        int level = static_cast<int>(gauge_.getStateOfCharge());
        if (level >= 0 && level <= 100)
        {
            last_battery_level_ = level;
            return level;
        }
    }
    if (power_ready_)
    {
        int level = batteryPercentFromMv(static_cast<int>(pmu_.getBattVoltage()));
        if (level >= 0 && level <= 100)
        {
            last_battery_level_ = level;
            return level;
        }
    }
    return last_battery_level_;
}

bool TDeckProBoard::isCardReady()
{
    return sd_ready_ && SD.cardType() != CARD_NONE;
}

void TDeckProBoard::vibrator()
{
    if (profile().motor_pin >= 0)
    {
        digitalWrite(profile().motor_pin, HIGH);
    }
}

void TDeckProBoard::stopVibrator()
{
    if (profile().motor_pin >= 0)
    {
        digitalWrite(profile().motor_pin, LOW);
    }
}

void TDeckProBoard::playMessageTone()
{
    if (!profile().optional.has_pcm512a || message_tone_volume_ == 0)
    {
        return;
    }

    static const char kMessageToneRtttl[] = "Msg:d=4,o=6,b=200:32e,32g,32b,16c7";
    AudioOutputI2S audio_out(1, AudioOutputI2S::EXTERNAL_I2S);
    audio_out.SetPinout(profile().optional.i2s_bclk,
                        profile().optional.i2s_lrc,
                        profile().optional.i2s_dout);
    float gain = static_cast<float>(message_tone_volume_) / 250.0f;
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
}

void TDeckProBoard::setMessageToneVolume(uint8_t volume_percent)
{
    if (volume_percent > 100)
    {
        volume_percent = 100;
    }
    message_tone_volume_ = volume_percent;
}

uint8_t TDeckProBoard::getMessageToneVolume() const
{
    return message_tone_volume_;
}

void TDeckProBoard::setRotation(uint8_t rotation)
{
    rotation_ = rotation & 0x3;
    epd_.setRotation(rotation_);
}

uint16_t TDeckProBoard::width()
{
    return rotation_ % 2 == 0 ? static_cast<uint16_t>(profile().screen_width)
                              : static_cast<uint16_t>(profile().screen_height);
}

uint16_t TDeckProBoard::height()
{
    return rotation_ % 2 == 0 ? static_cast<uint16_t>(profile().screen_height)
                              : static_cast<uint16_t>(profile().screen_width);
}

void TDeckProBoard::setBit(int16_t x, int16_t y, bool black)
{
    if (x < 0 || y < 0 || x >= profile().screen_width || y >= profile().screen_height)
    {
        return;
    }
    const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(profile().screen_width) + static_cast<size_t>(x);
    const size_t byte_idx = idx / 8U;
    const uint8_t bit_mask = static_cast<uint8_t>(0x80U >> (idx % 8U));
    if (black)
    {
        mono_buffer_[byte_idx] &= static_cast<uint8_t>(~bit_mask);
    }
    else
    {
        mono_buffer_[byte_idx] |= bit_mask;
    }
}

void TDeckProBoard::renderEpd()
{
    if (!display_ready_)
    {
        return;
    }

    sharedSpiLock();
    sharedSpiPrepareDevice(profile().epd.cs);
    epd_.setRotation(rotation_);
    epd_.setFullWindow();
    epd_.firstPage();
    do
    {
        epd_.drawInvertedBitmap(0, 0, mono_buffer_.data(), profile().screen_width, profile().screen_height, GxEPD_BLACK);
    } while (epd_.nextPage());
    epd_.powerOff();
    sharedSpiUnlock();
}

void TDeckProBoard::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color)
{
    if (!color)
    {
        return;
    }

    static uint8_t s_flush_log_count = 0;
    uint32_t dark_pixels = 0;

    for (uint16_t row = 0; row < y2; ++row)
    {
        for (uint16_t col = 0; col < x2; ++col)
        {
            const uint16_t pixel = color[static_cast<size_t>(row) * x2 + col];
            const uint8_t r = static_cast<uint8_t>((pixel >> 11) & 0x1F);
            const uint8_t g = static_cast<uint8_t>((pixel >> 5) & 0x3F);
            const uint8_t b = static_cast<uint8_t>(pixel & 0x1F);
            const uint16_t luminance = static_cast<uint16_t>(r * 299U + g * 587U + b * 114U);
            const bool black = luminance < 16384U;
            if (black)
            {
                dark_pixels++;
            }
            setBit(static_cast<int16_t>(x1 + col), static_cast<int16_t>(y1 + row), black);
        }
    }
    if (s_flush_log_count < kFlushLogLimit)
    {
        Serial.printf("[%s] flush #%u area=(%u,%u %ux%u) dark=%lu/%lu\n",
                      kTag,
                      static_cast<unsigned>(s_flush_log_count + 1),
                      static_cast<unsigned>(x1),
                      static_cast<unsigned>(y1),
                      static_cast<unsigned>(x2),
                      static_cast<unsigned>(y2),
                      static_cast<unsigned long>(dark_pixels),
                      static_cast<unsigned long>(static_cast<uint32_t>(x2) * static_cast<uint32_t>(y2)));
        s_flush_log_count++;
    }
    renderEpd();
}

uint8_t TDeckProBoard::getPoint(int16_t* x, int16_t* y, uint8_t get_point)
{
    if (!touch_ready_)
    {
        return 0;
    }
    int16_t tx = 0;
    int16_t ty = 0;
    const uint8_t touched = touch_.getPoint(&tx, &ty, get_point);
    if (!touched || !x || !y)
    {
        return touched;
    }

    switch (rotation_ & 0x3)
    {
    case 1:
        *x = ty;
        *y = static_cast<int16_t>(profile().screen_width - 1 - tx);
        break;
    case 2:
        *x = static_cast<int16_t>(profile().screen_width - 1 - tx);
        *y = static_cast<int16_t>(profile().screen_height - 1 - ty);
        break;
    case 3:
        *x = static_cast<int16_t>(profile().screen_height - 1 - ty);
        *y = tx;
        break;
    default:
        *x = tx;
        *y = ty;
        break;
    }
    return touched;
}

bool TDeckProBoard::keyEventToChar(uint8_t event, char* c, bool* pressed)
{
    if (!c || !pressed)
    {
        return false;
    }
    *pressed = (event & 0x80U) != 0;
    uint8_t key = static_cast<uint8_t>(event & 0x7FU);
    if (key == 0)
    {
        return false;
    }
    key -= 1;
    *c = translateKey(key);
    return *c != '\0';
}

int TDeckProBoard::getKeyChar(char* c)
{
    if (!keyboard_ready_ || !c || keyboard_.available() == 0)
    {
        return -1;
    }
    bool pressed = false;
    if (!keyEventToChar(keyboard_.getEvent(), c, &pressed))
    {
        return -1;
    }
    return pressed ? KEYBOARD_PRESSED : KEYBOARD_RELEASED;
}

int TDeckProBoard::transmitRadio(const uint8_t* data, size_t len)
{
    sharedSpiLock();
    sharedSpiPrepareDevice(profile().lora.cs);
    const int rc = radio_.transmit(data, len);
    sharedSpiUnlock();
    return rc;
}

int TDeckProBoard::startRadioReceive()
{
    sharedSpiLock();
    sharedSpiPrepareDevice(profile().lora.cs);
    const int rc = radio_.startReceive();
    sharedSpiUnlock();
    return rc;
}

uint32_t TDeckProBoard::getRadioIrqFlags()
{
    sharedSpiLock();
    sharedSpiPrepareDevice(profile().lora.cs);
    const uint32_t flags = radio_.getIrqFlags();
    sharedSpiUnlock();
    return flags;
}

int TDeckProBoard::getRadioPacketLength(bool update)
{
    sharedSpiLock();
    sharedSpiPrepareDevice(profile().lora.cs);
    const int len = static_cast<int>(radio_.getPacketLength(update));
    sharedSpiUnlock();
    return len;
}

int TDeckProBoard::readRadioData(uint8_t* buf, size_t len)
{
    sharedSpiLock();
    sharedSpiPrepareDevice(profile().lora.cs);
    const int rc = radio_.readData(buf, len);
    sharedSpiUnlock();
    return rc;
}

void TDeckProBoard::clearRadioIrqFlags(uint32_t flags)
{
    sharedSpiLock();
    sharedSpiPrepareDevice(profile().lora.cs);
    radio_.clearIrqFlags(flags);
    sharedSpiUnlock();
}

float TDeckProBoard::getRadioRSSI()
{
    sharedSpiLock();
    sharedSpiPrepareDevice(profile().lora.cs);
    const float rssi = radio_.getRSSI();
    sharedSpiUnlock();
    return rssi;
}

float TDeckProBoard::getRadioSNR()
{
    sharedSpiLock();
    sharedSpiPrepareDevice(profile().lora.cs);
    const float snr = radio_.getSNR();
    sharedSpiUnlock();
    return snr;
}

void TDeckProBoard::configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                                       int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                                       uint8_t crc_len)
{
    sharedSpiLock();
    sharedSpiPrepareDevice(profile().lora.cs);
    radio_.setFrequency(freq_mhz);
    radio_.setBandwidth(bw_khz);
    radio_.setSpreadingFactor(sf);
    radio_.setCodingRate(cr_denom);
    applyTxPower(radio_, tx_power);
    radio_.setPreambleLength(preamble_len);
    radio_.setSyncWord(sync_word);
    radio_.setCRC(crc_len);
    sharedSpiUnlock();
}

bool TDeckProBoard::initGPS()
{
    Serial2.begin(profile().gps.uart.baud, SERIAL_8N1, profile().gps.uart.rx, profile().gps.uart.tx);
    delay(50);
    gps_ready_ = gps_.init(&Serial2);
    Serial.printf("[%s] gps init %s\n", kTag, gps_ready_ ? "ok" : "fail");
    return gps_ready_;
}

void TDeckProBoard::powerControl(PowerCtrlChannel_t ch, bool enable)
{
    switch (ch)
    {
    case POWER_GPS:
        if (profile().gps.enable >= 0)
        {
            digitalWrite(profile().gps.enable, enable ? HIGH : LOW);
        }
        break;
    case POWER_SENSOR:
        if (profile().motion.enable_1v8 >= 0)
        {
            digitalWrite(profile().motion.enable_1v8, enable ? HIGH : LOW);
        }
        break;
    case POWER_RADIO:
        if (profile().lora.enable >= 0)
        {
            digitalWrite(profile().lora.enable, enable ? HIGH : LOW);
        }
        break;
    default:
        break;
    }
}

bool TDeckProBoard::syncTimeFromGPS(uint32_t gps_task_interval_ms)
{
    (void)gps_task_interval_ms;
    if (!gps_.date.isValid() || !gps_.time.isValid())
    {
        return false;
    }

    const int year = gps_.date.year();
    const uint8_t month = gps_.date.month();
    const uint8_t day = gps_.date.day();
    const uint8_t hour = gps_.time.hour();
    const uint8_t minute = gps_.time.minute();
    const uint8_t second = gps_.time.second();
    if (!gpsDatetimeValid(year, month, day, hour, minute, second))
    {
        return false;
    }

    const time_t epoch = gpsDatetimeToEpochUtc(year, month, day, hour, minute, second);
    if (epoch < kMinValidEpochSeconds)
    {
        return false;
    }

    timeval tv{};
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0)
    {
        return false;
    }
    rtc_ready_ = true;
    return true;
}

namespace
{
TDeckProBoard& getInstanceRef()
{
    return *TDeckProBoard::getInstance();
}
} // namespace

TDeckProBoard& instance = getInstanceRef();
BoardBase& board = getInstanceRef();

} // namespace boards::tdeck_pro

BoardBase& board = ::boards::tdeck_pro::instance;

#endif // defined(ARDUINO_T_DECK_PRO)
