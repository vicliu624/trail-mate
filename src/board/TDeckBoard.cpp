#include "board/TDeckBoard.h"
#include "board/sd_utils.h"
#include "display/drivers/ST7789TDeck.h"
#include <SD.h>
#include <Wire.h>
#include <ctime>

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
    Serial.printf("[TDeckBoard] PMU init: %s\n", ok ? "OK" : "FAIL");
    return ok;
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
    // T-Deck reference example uses a conservative 800 kHz SPI speed.
    bool ok = sdutil::installSpiSd(*this, SD_CS, 800000U, "/sd",
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
    // T-Deck has no guaranteed external RTC; treat system time as RTC readiness.
    return rtc_ready_ || (time(nullptr) > 0);
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
    if (!pmu_ready_)
    {
        // Fallback: approximate via ADC if PMU is not reachable.
#ifdef BOARD_BAT_ADC
        int raw = analogRead(BOARD_BAT_ADC);
        if (raw <= 0)
        {
            return -1;
        }
        // Configure attenuation for full-scale ~3.3V before converting.
        analogSetPinAttenuation(BOARD_BAT_ADC, ADC_11db);
#ifndef BAT_ADC_MULTIPLIER
#define BAT_ADC_MULTIPLIER 2.0f
#endif
        // Account for the board's voltage divider via BAT_ADC_MULTIPLIER.
        float v = (raw / 4095.0f) * 3.3f * BAT_ADC_MULTIPLIER;
        int pct = static_cast<int>(((v - 3.3f) / (4.2f - 3.3f)) * 100.0f);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        static bool logged_once = false;
        if (!logged_once)
        {
            logged_once = true;
            Serial.printf("[TDeckBoard] BAT adc raw=%d v=%.3f pct=%d (mult=%.2f)\n",
                          raw, (double)v, pct, (double)BAT_ADC_MULTIPLIER);
        }
        return pct;
#else
        return -1;
#endif
    }

    int percent = pmu_.getBatteryPercent();
    if (percent < 0)
    {
        return -1;
    }
    if (percent > 100)
    {
        percent = 100;
    }
    return percent;
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
        radio_.setOutputPower(tx_power);
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

    const uint32_t repeat_ms = 70;    // Even more responsive trackball repeat
    const uint32_t click_ms = 110;    // Click debounce
    const uint8_t stable_polls = 2;   // Increase sensitivity on T-Deck trackball

#if defined(TRACKBALL_RIGHT) && defined(TRACKBALL_LEFT)
    const bool right_pressed = digitalRead(TRACKBALL_RIGHT) == LOW;
    const bool left_pressed = digitalRead(TRACKBALL_LEFT) == LOW;

    // Treat simultaneous left+right as noise and require stable edges.
    if (right_pressed && !left_pressed)
    {
        if (right_count_ < stable_polls)
        {
            ++right_count_;
        }
    }
    else
    {
        right_count_ = 0;
        right_latched_ = false;
    }

    if (left_pressed && !right_pressed)
    {
        if (left_count_ < stable_polls)
        {
            ++left_count_;
        }
    }
    else
    {
        left_count_ = 0;
        left_latched_ = false;
    }

#endif

#if defined(TRACKBALL_UP) && defined(TRACKBALL_DOWN)
    const bool up_pressed = digitalRead(TRACKBALL_UP) == LOW;
    const bool down_pressed = digitalRead(TRACKBALL_DOWN) == LOW;

    if (up_pressed && !down_pressed)
    {
        if (up_count_ < stable_polls)
        {
            ++up_count_;
        }
    }
    else
    {
        up_count_ = 0;
        up_latched_ = false;
    }

    if (down_pressed && !up_pressed)
    {
        if (down_count_ < stable_polls)
        {
            ++down_count_;
        }
    }
    else
    {
        down_count_ = 0;
        down_latched_ = false;
    }

    // Prefer vertical motion when present.
    if (up_count_ >= stable_polls && !up_latched_ && (now - last_trackball_ms_) >= repeat_ms)
    {
        msg.dir = ROTARY_DIR_DOWN;
        last_trackball_ms_ = now;
        up_latched_ = true;
        down_latched_ = true;
        left_latched_ = true;
        right_latched_ = true;
    }
    else if (down_count_ >= stable_polls && !down_latched_ && (now - last_trackball_ms_) >= repeat_ms)
    {
        msg.dir = ROTARY_DIR_UP;
        last_trackball_ms_ = now;
        down_latched_ = true;
        up_latched_ = true;
        left_latched_ = true;
        right_latched_ = true;
    }
#endif

#if defined(TRACKBALL_RIGHT) && defined(TRACKBALL_LEFT)
    // Horizontal fallback when no vertical event was generated.
    if (msg.dir == ROTARY_DIR_NONE)
    {
        if (right_count_ >= stable_polls && !right_latched_ && (now - last_trackball_ms_) >= repeat_ms)
        {
            msg.dir = ROTARY_DIR_DOWN;
            last_trackball_ms_ = now;
            right_latched_ = true;
            left_latched_ = true;
            up_latched_ = true;
            down_latched_ = true;
        }
        else if (left_count_ >= stable_polls && !left_latched_ && (now - last_trackball_ms_) >= repeat_ms)
        {
            msg.dir = ROTARY_DIR_UP;
            last_trackball_ms_ = now;
            left_latched_ = true;
            right_latched_ = true;
            up_latched_ = true;
            down_latched_ = true;
        }
    }
#endif

#if defined(TRACKBALL_CLICK)
    const bool click_pressed = digitalRead(TRACKBALL_CLICK) == LOW;
    if (click_pressed)
    {
        if (click_count_ < stable_polls)
        {
            ++click_count_;
        }
        if (click_count_ >= stable_polls && !click_latched_ && (now - last_click_ms_) >= click_ms)
        {
            msg.centerBtnPressed = true;
            last_click_ms_ = now;
            click_latched_ = true;
        }
    }
    else
    {
        click_count_ = 0;
        click_latched_ = false;
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
