#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include <SensorBHI260AP.hpp>
#include <XPowersAXP2101.tpp>

#include "BoardBase.h"
#include "GpsBoard.h"
#include "LoraBoard.h"
#include "MotionBoard.h"
#include "SdBoard.h"
#include "board/TLoRaPagerTypes.h"
#include "display/DisplayInterface.h"
#include "gps/GPS.h"
#include "pins_arduino.h"

#if !defined(SCREEN_WIDTH) || !defined(SCREEN_HEIGHT)
#error "SCREEN_WIDTH and SCREEN_HEIGHT must be provided via build flags (env .ini)."
#endif

#define newModule() new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY)

class SX1262Access : public SX1262
{
  public:
    using SX1262::SX1262;
    using SX126x::readRegister;
    using SX126x::setTxParams;
    using SX126x::writeRegister;
};

class TDeckBoard : public BoardBase,
                   public LoraBoard,
                   public GpsBoard,
                   public MotionBoard,
                   public SdBoard,
                   public LilyGo_Display,
                   public LilyGoDispArduinoSPI
{
  public:
    static TDeckBoard* getInstance();

    // BoardBase
    uint32_t begin(uint32_t disable_hw_init = 0) override;
    void wakeUp() override {}
    void handlePowerButton() override {}
    void softwareShutdown() override {}

    void setBrightness(uint8_t level) override { brightness_ = level; }
    uint8_t getBrightness() override { return brightness_; }

    bool hasKeyboard() override { return false; }
    void keyboardSetBrightness(uint8_t level) override { keyboard_brightness_ = level; }
    uint8_t keyboardGetBrightness() override { return keyboard_brightness_; }

    bool isRTCReady() const override;
    bool isCharging() override;
    int getBatteryLevel() override;

    bool isSDReady() const override { return sd_ready_; }
    bool isCardReady() override;
    bool isGPSReady() const override { return (devices_probe_ & HW_GPS_ONLINE) != 0; }

    void vibrator() override {}
    void stopVibrator() override {}

    // LilyGo_Display
    void setRotation(uint8_t rotation) override;
    uint8_t getRotation() override;
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color) override;
    uint16_t width() override;
    uint16_t height() override;
    bool useDMA() override { return true; }
    bool hasEncoder() override { return true; }
    RotaryMsg_t getRotary() override;

    // LoraBoard
    bool isRadioOnline() const override { return (devices_probe_ & HW_RADIO_ONLINE) != 0; }
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

    // GpsBoard
    bool initGPS() override;
    void setGPSOnline(bool online) override
    {
        if (online)
        {
            devices_probe_ |= HW_GPS_ONLINE;
        }
        else
        {
            devices_probe_ &= ~HW_GPS_ONLINE;
        }
    }
    GPS& getGPS() override { return gps_; }
    void powerControl(PowerCtrlChannel_t, bool) override {}
    bool syncTimeFromGPS(uint32_t = 0) override { return false; }

    // MotionBoard
    SensorBHI260AP& getMotionSensor() override { return sensor_; }
    bool isSensorReady() const override { return (devices_probe_ & HW_BHI260AP_ONLINE) != 0; }

  private:
    TDeckBoard();
    bool initPMU();
    bool installSD();
    void uninstallSD();

  private:
    uint32_t devices_probe_ = 0;
    uint8_t brightness_ = 8;
    uint8_t keyboard_brightness_ = 0;
    uint8_t rotation_ = 0;
    bool pmu_ready_ = false;
    bool rtc_ready_ = false;
    bool sd_ready_ = false;
    bool display_ready_ = false;
    uint32_t boot_ms_ = 0;
    uint32_t last_trackball_ms_ = 0;
    uint32_t last_click_ms_ = 0;
    uint8_t left_count_ = 0;
    uint8_t right_count_ = 0;
    uint8_t up_count_ = 0;
    uint8_t down_count_ = 0;
    uint8_t click_count_ = 0;
    bool left_latched_ = false;
    bool right_latched_ = false;
    bool up_latched_ = false;
    bool down_latched_ = false;
    bool click_latched_ = false;
    static constexpr uint32_t kRotaryBootGuardMs = 1200;

    GPS gps_;
    SensorBHI260AP sensor_;
    XPowersAXP2101 pmu_;
#if defined(ARDUINO_LILYGO_LORA_SX1262)
    SX1262Access radio_ = newModule();
#elif defined(ARDUINO_LILYGO_LORA_SX1280)
    SX1280 radio_ = newModule();
#else
    SX1262Access radio_ = newModule();
#endif
};

extern TDeckBoard& instance;
extern BoardBase& board;
