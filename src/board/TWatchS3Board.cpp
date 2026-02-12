#include "board/TWatchS3Board.h"

#include "display/drivers/ST7789WatchS3.h"
#include <Wire.h>
#include <esp_idf_version.h>
#include <esp_sleep.h>
#include <limits>
#include <ctime>

namespace
{
#ifndef LEDC_BACKLIGHT_CHANNEL
#define LEDC_BACKLIGHT_CHANNEL 4
#endif
#ifndef LEDC_BACKLIGHT_BIT_WIDTH
#define LEDC_BACKLIGHT_BIT_WIDTH 8
#endif
#ifndef LEDC_BACKLIGHT_FREQ
#define LEDC_BACKLIGHT_FREQ 1000
#endif

bool s_backlight_ready = false;

void setup_backlight()
{
#if defined(DISP_BL)
    if (s_backlight_ready || DISP_BL == -1)
    {
        return;
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    s_backlight_ready = ledcAttach(DISP_BL, LEDC_BACKLIGHT_FREQ, LEDC_BACKLIGHT_BIT_WIDTH);
#else
    ledcSetup(LEDC_BACKLIGHT_CHANNEL, LEDC_BACKLIGHT_FREQ, LEDC_BACKLIGHT_BIT_WIDTH);
    ledcAttachPin(DISP_BL, LEDC_BACKLIGHT_CHANNEL);
    s_backlight_ready = true;
#endif
#endif
}

#if defined(ARDUINO_LILYGO_LORA_SX1262)
void apply_tx_power(SX1262Access& radio, int8_t tx_power)
{
    constexpr int8_t kTxPowerMinDbm = -9;
    int8_t clipped = tx_power;
    if (clipped < kTxPowerMinDbm) clipped = kTxPowerMinDbm;
    radio.setOutputPower(clipped);
}
#endif
}

TWatchS3Board::TWatchS3Board()
    : LilyGo_Display(SPI_DRIVER, false),
      LilyGoDispArduinoSPI(SCREEN_WIDTH, SCREEN_HEIGHT,
                           display::drivers::ST7789WatchS3::getInitCommands(),
                           display::drivers::ST7789WatchS3::getInitCommandsCount(),
                           display::drivers::ST7789WatchS3::getRotationConfig(SCREEN_WIDTH, SCREEN_HEIGHT))
{
}

TWatchS3Board* TWatchS3Board::getInstance()
{
    static TWatchS3Board instance;
    return &instance;
}

uint32_t TWatchS3Board::begin(uint32_t disable_hw_init)
{
    devices_probe_ = 0;

    Serial.begin(115200);
    delay(20);
    Serial.println("[TWatchS3Board] begin");

#ifdef DISP_CS
    pinMode(DISP_CS, OUTPUT);
    digitalWrite(DISP_CS, HIGH);
#endif
#ifdef LORA_CS
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
#endif

    Wire.begin(SDA, SCL);
    delay(10);

    pmu_ready_ = initPMU();
    if (pmu_ready_)
    {
        devices_probe_ |= HW_PMU_ONLINE;
    }

    rtc_ready_ = (time(nullptr) > 0);

#if defined(DISP_SCK) && defined(DISP_MISO) && defined(DISP_MOSI) && defined(DISP_CS) && defined(DISP_DC)
    LilyGoDispArduinoSPI::init(DISP_SCK, DISP_MISO, DISP_MOSI, DISP_CS, DISP_RST, DISP_DC, DISP_BL, 40, SPI);
    LilyGoDispArduinoSPI::setRotation(2);
    rotation_ = LilyGoDispArduinoSPI::getRotation();
    display_ready_ = true;
    Serial.printf("[TWatchS3Board] display init OK: %ux%u\n", LilyGoDispArduinoSPI::_width, LilyGoDispArduinoSPI::_height);
#else
    Serial.println("[TWatchS3Board] display init skipped: missing DISP_* pins");
#endif

    lora_spi_.begin(LORA_SCK, LORA_MISO, LORA_MOSI);
    radio_.reset();
    int radio_state = radio_.begin();
    if (radio_state == RADIOLIB_ERR_NONE)
    {
        devices_probe_ |= HW_RADIO_ONLINE;
        Serial.println("[TWatchS3Board] radio init OK");
    }
    else
    {
        Serial.printf("[TWatchS3Board] radio init failed: %d\n", radio_state);
    }

    if ((disable_hw_init & NO_HW_TOUCH) == 0)
    {
        touch_ready_ = initTouch();
        if (touch_ready_)
        {
            devices_probe_ |= HW_TOUCH_ONLINE;
        }
    }

    return devices_probe_;
}

bool TWatchS3Board::initPMU()
{
    bool ok = pmu_.begin(Wire, AXP2101_SLAVE_ADDRESS, SDA, SCL);
    if (!ok)
    {
        Serial.println("[TWatchS3Board] PMU init failed");
        return false;
    }

    pmu_.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);
    pmu_.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_900MA);
    pmu_.setSysPowerDownVoltage(2600);

    pmu_.setALDO2Voltage(3300);
    pmu_.setALDO3Voltage(3300);
    pmu_.setALDO4Voltage(3300);
    pmu_.setBLDO2Voltage(3300);
    pmu_.setButtonBatteryChargeVoltage(3300);

    pmu_.enableALDO2();
    pmu_.enableALDO3();
    pmu_.enableALDO4();
    pmu_.enableBLDO2();
    pmu_.enableButtonBatteryCharge();

    pmu_.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    pmu_.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);

    pmu_.enableBattDetection();
    pmu_.enableVbusVoltageMeasure();
    pmu_.enableBattVoltageMeasure();
    pmu_.enableSystemVoltageMeasure();
    pmu_.enableTemperatureMeasure();

    pmu_.setChargingLedMode(XPOWERS_CHG_LED_OFF);
    pmu_.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
    pmu_.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_125MA);
    pmu_.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
    pmu_.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V35);

    Serial.println("[TWatchS3Board] PMU init OK");
    return true;
}

bool TWatchS3Board::initTouch()
{
    bool ok = false;

    Wire1.begin(TOUCH_SDA, TOUCH_SCL);
    touch_.setPins(TOUCH_RST, TOUCH_INT);
    ok = touch_.begin(Wire1, FT6X36_SLAVE_ADDRESS, TOUCH_SDA, TOUCH_SCL);
    if (!ok)
    {
        Serial.println("[TWatchS3Board] touch init failed");
        return false;
    }

    touch_.setMaxCoordinates(SCREEN_WIDTH, SCREEN_HEIGHT);
    touch_.interruptTrigger();
    Serial.println("[TWatchS3Board] touch init OK");
    return true;
}

void TWatchS3Board::softwareShutdown()
{
    if (pmu_ready_)
    {
        pmu_.shutdown();
        delay(200);
    }
    esp_deep_sleep_start();
}

void TWatchS3Board::setBrightness(uint8_t level)
{
    brightness_ = level;
    if (display_ready_)
    {
        LilyGoDispArduinoSPI::setBrightness(level);
        setup_backlight();
#if defined(DISP_BL)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        if (s_backlight_ready)
        {
            ledcWrite(DISP_BL, brightness_);
        }
#else
        if (s_backlight_ready)
        {
            ledcWrite(LEDC_BACKLIGHT_CHANNEL, brightness_);
        }
#endif
#endif
    }
}

bool TWatchS3Board::isRTCReady() const
{
    return rtc_ready_ || (time(nullptr) > 0);
}

bool TWatchS3Board::isCharging()
{
    if (!pmu_ready_)
    {
        return false;
    }
    return pmu_.isCharging();
}

int TWatchS3Board::getBatteryLevel()
{
    if (!pmu_ready_)
    {
        return -1;
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

void TWatchS3Board::setRotation(uint8_t rotation)
{
    LilyGoDispArduinoSPI::setRotation(rotation);
    rotation_ = LilyGoDispArduinoSPI::getRotation();
}

uint8_t TWatchS3Board::getRotation()
{
    return LilyGoDispArduinoSPI::getRotation();
}

void TWatchS3Board::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color)
{
    LilyGoDispArduinoSPI::pushColors(x1, y1, x2, y2, color);
}

uint16_t TWatchS3Board::width()
{
    return LilyGoDispArduinoSPI::_width;
}

uint16_t TWatchS3Board::height()
{
    return LilyGoDispArduinoSPI::_height;
}

uint8_t TWatchS3Board::getPoint(int16_t* x, int16_t* y, uint8_t get_point)
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
    const uint16_t w = SCREEN_WIDTH;
    const uint16_t h = SCREEN_HEIGHT;
    uint8_t rot = getRotation() & 0x3;

    switch (rot)
    {
    case 1:
    {
        int16_t nx = static_cast<int16_t>(h - 1 - ty);
        int16_t ny = tx;
        tx = nx;
        ty = ny;
        break;
    }
    case 2:
        tx = static_cast<int16_t>(w - 1 - tx);
        ty = static_cast<int16_t>(h - 1 - ty);
        break;
    case 3:
    {
        int16_t nx = ty;
        int16_t ny = static_cast<int16_t>(w - 1 - tx);
        tx = nx;
        ty = ny;
        break;
    }
    default:
        break;
    }

    if (tx < 0)
    {
        tx = 0;
    }
    else if (tx >= static_cast<int16_t>(w))
    {
        tx = static_cast<int16_t>(w - 1);
    }

    if (ty < 0)
    {
        ty = 0;
    }
    else if (ty >= static_cast<int16_t>(h))
    {
        ty = static_cast<int16_t>(h - 1);
    }

    *x = tx;
    *y = ty;
    return touched;
}

int TWatchS3Board::transmitRadio(const uint8_t* data, size_t len)
{
    return radio_.transmit(data, len);
}

int TWatchS3Board::startRadioReceive()
{
    return radio_.startReceive();
}

uint32_t TWatchS3Board::getRadioIrqFlags()
{
    return radio_.getIrqFlags();
}

int TWatchS3Board::getRadioPacketLength(bool update)
{
    return static_cast<int>(radio_.getPacketLength(update));
}

int TWatchS3Board::readRadioData(uint8_t* buf, size_t len)
{
    return radio_.readData(buf, len);
}

void TWatchS3Board::clearRadioIrqFlags(uint32_t flags)
{
    radio_.clearIrqFlags(flags);
}

float TWatchS3Board::getRadioRSSI()
{
    return radio_.getRSSI();
}

float TWatchS3Board::getRadioSNR()
{
    return radio_.getSNR();
}

void TWatchS3Board::configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                                       int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                                       uint8_t crc_len)
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
}

namespace
{
TWatchS3Board& getInstanceRef()
{
    return *TWatchS3Board::getInstance();
}
} // namespace

TWatchS3Board& instance = getInstanceRef();
BoardBase& board = getInstanceRef();
