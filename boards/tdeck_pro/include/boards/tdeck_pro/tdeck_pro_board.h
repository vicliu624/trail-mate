#pragma once

#define XPOWERS_CHIP_BQ25896

#include <Adafruit_TCA8418.h>
#include <GaugeBQ27220.hpp>
#include <GxEPD2_BW.h>
#include <RadioLib.h>
#include <SPI.h>
#include <SensorBHI260AP.hpp>
#include <TouchDrvCSTXXX.hpp>
#include <XPowersLib.h>
#include <memory>
#include <vector>

#include "board/BoardBase.h"
#include "board/GpsBoard.h"
#include "board/LoraBoard.h"
#include "board/MotionBoard.h"
#include "board/SdBoard.h"
#include "board/TLoRaPagerTypes.h"
#include "boards/tdeck_pro/board_profile.h"
#include "display/DisplayInterface.h"
#include "platform/esp/arduino_common/gps/GPS.h"

namespace boards::tdeck_pro
{

class SX1262Access : public SX1262
{
  public:
    using SX1262::SX1262;
    using SX126x::readRegister;
    using SX126x::setTxParams;
    using SX126x::writeRegister;
};

class TDeckProBoard : public BoardBase,
                      public LoraBoard,
                      public GpsBoard,
                      public MotionBoard,
                      public SdBoard,
                      public LilyGo_Display
{
  public:
    static TDeckProBoard* getInstance();
    static constexpr const BoardProfile& profile()
    {
        return kBoardProfile;
    }

    uint32_t begin(uint32_t disable_hw_init = 0) override;
    void wakeUp() override {}
    void handlePowerButton() override {}
    void softwareShutdown() override {}

    void setBrightness(uint8_t level) override;
    uint8_t getBrightness() override { return brightness_; }

    bool hasKeyboard() override { return keyboard_ready_; }
    void keyboardSetBrightness(uint8_t level) override;
    uint8_t keyboardGetBrightness() override { return keyboard_brightness_; }

    bool isRTCReady() const override;
    bool isCharging() override;
    int getBatteryLevel() override;

    bool isSDReady() const override { return sd_ready_; }
    bool isCardReady() override;
    bool isGPSReady() const override { return gps_ready_; }
    bool hasGPSHardware() const override { return true; }

    void vibrator() override;
    void stopVibrator() override;
    void playMessageTone() override;
    void setMessageToneVolume(uint8_t volume_percent) override;
    uint8_t getMessageToneVolume() const override;

    void setRotation(uint8_t rotation) override;
    uint8_t getRotation() override { return rotation_; }
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color) override;
    uint16_t width() override;
    uint16_t height() override;
    bool hasTouch() override { return touch_ready_; }
    uint8_t getPoint(int16_t* x, int16_t* y, uint8_t get_point) override;
    int getKeyChar(char* c) override;

    bool isRadioOnline() const override { return radio_ready_; }
    int transmitRadio(const uint8_t* data, size_t len) override;
    int startRadioReceive() override;
    uint32_t getRadioIrqFlags() override;
    int getRadioPacketLength(bool update) override;
    int readRadioData(uint8_t* buf, size_t len) override;
    void clearRadioIrqFlags(uint32_t flags) override;
    float getRadioRSSI() override;
    float getRadioSNR() override;
    void configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                            int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                            uint8_t crc_len) override;

    bool initGPS() override;
    void setGPSOnline(bool online) override { gps_ready_ = online; }
    GPS& getGPS() override { return gps_; }
    void powerControl(PowerCtrlChannel_t ch, bool enable) override;
    bool syncTimeFromGPS(uint32_t gps_task_interval_ms = 0) override;

    SensorBHI260AP& getMotionSensor() override { return motion_; }
    bool isSensorReady() const override { return motion_ready_; }

    bool hasCellularModem() const { return profile().optional.has_a7682e; }
    bool hasAudioDac() const { return profile().optional.has_pcm512a; }

  private:
    TDeckProBoard();
    void initSharedPins();
    bool initPower();
    bool initDisplay();
    bool initTouch();
    bool initKeyboard();
    bool initMotion();
    bool initRadio();
    bool initStorage();
    bool installSD() override;
    void uninstallSD() override;
    void renderEpd();
    void setBit(int16_t x, int16_t y, bool black);
    bool keyEventToChar(uint8_t event, char* c, bool* pressed);

    using EpdPanel = GxEPD2_BW<GxEPD2_310_GDEQ031T10, GxEPD2_310_GDEQ031T10::HEIGHT>;

    uint8_t brightness_ = DEVICE_MAX_BRIGHTNESS_LEVEL;
    uint8_t keyboard_brightness_ = static_cast<uint8_t>(kBoardProfile.keyboard_default_brightness);
    uint8_t message_tone_volume_ = 45;
    uint8_t rotation_ = 0;
    bool power_ready_ = false;
    bool display_ready_ = false;
    bool touch_ready_ = false;
    bool keyboard_ready_ = false;
    bool radio_ready_ = false;
    bool sd_ready_ = false;
    bool gps_ready_ = false;
    bool motion_ready_ = false;
    bool rtc_ready_ = false;
    bool battery_gauge_ready_ = false;
    int last_battery_level_ = -1;

    SPIClass& shared_spi_ = SPI;
    Module lora_module_{static_cast<uint32_t>(profile().lora.cs),
                        static_cast<uint32_t>(profile().lora.irq),
                        static_cast<uint32_t>(profile().lora.rst),
                        static_cast<uint32_t>(profile().lora.busy),
                        shared_spi_};
    SX1262Access radio_{&lora_module_};
    EpdPanel epd_{GxEPD2_310_GDEQ031T10(profile().epd.cs, profile().epd.dc, profile().epd.rst, profile().epd.busy)};
    TouchDrvCSTXXX touch_;
    Adafruit_TCA8418 keyboard_;
    GPS gps_;
    SensorBHI260AP motion_;
    PowersBQ25896 pmu_;
    GaugeBQ27220 gauge_;
    std::vector<uint8_t> mono_buffer_;
};

extern TDeckProBoard& instance;
extern BoardBase& board;

} // namespace boards::tdeck_pro
