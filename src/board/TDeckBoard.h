#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include <SensorBHI260AP.hpp>

#include "BoardBase.h"
#include "GpsBoard.h"
#include "LoraBoard.h"
#include "MotionBoard.h"
#include "board/TLoRaPagerTypes.h"
#include "display/DisplayInterface.h"
#include "gps/GPS.h"
#include "pins_arduino.h"

#if !defined(SCREEN_WIDTH) || !defined(SCREEN_HEIGHT)
#error "SCREEN_WIDTH and SCREEN_HEIGHT must be provided via build flags (env .ini)."
#endif

#define newModule() new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY)

class TDeckBoard : public BoardBase,
                   public LoraBoard,
                   public GpsBoard,
                   public MotionBoard,
                   public LilyGo_Display
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

    bool isRTCReady() const override { return false; }
    bool isCharging() override { return false; }
    int getBatteryLevel() override { return -1; }

    bool isSDReady() const override { return false; }
    bool isCardReady() override { return false; }
    bool isGPSReady() const override { return (devices_probe_ & HW_GPS_ONLINE) != 0; }

    void vibrator() override {}
    void stopVibrator() override {}

    // LilyGo_Display
    void setRotation(uint8_t rotation) override { rotation_ = rotation; }
    uint8_t getRotation() override { return rotation_; }
    void pushColors(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t*) override {}
    uint16_t width() override { return SCREEN_WIDTH; }
    uint16_t height() override { return SCREEN_HEIGHT; }

    // LoraBoard
    bool isRadioOnline() const override { return (devices_probe_ & HW_RADIO_ONLINE) != 0; }
    int transmitRadio(const uint8_t* data, size_t len) override { return radio_.transmit(data, len); }
    int startRadioReceive() override { return radio_.startReceive(); }
    uint32_t getRadioIrqFlags() override { return radio_.getIrqFlags(); }
    int getRadioPacketLength(bool update) override { return static_cast<int>(radio_.getPacketLength(update)); }
    int readRadioData(uint8_t* buf, size_t len) override { return radio_.readData(buf, len); }
    void clearRadioIrqFlags(uint32_t flags) override { radio_.clearIrqFlags(flags); }
    void configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                            int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                            uint8_t crc_len) override
    {
        radio_.setFrequency(freq_mhz);
        radio_.setBandwidth(bw_khz);
        radio_.setSpreadingFactor(sf);
        radio_.setCodingRate(cr_denom);
        radio_.setOutputPower(tx_power);
        radio_.setPreambleLength(preamble_len);
        radio_.setSyncWord(sync_word);
        radio_.setCRC(crc_len);
    }

    // GpsBoard
    bool initGPS() override { return false; }
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

  private:
    uint32_t devices_probe_ = 0;
    uint8_t brightness_ = 8;
    uint8_t keyboard_brightness_ = 0;
    uint8_t rotation_ = 0;

    GPS gps_;
    SensorBHI260AP sensor_;
#if defined(ARDUINO_LILYGO_LORA_SX1262)
    SX1262 radio_ = newModule();
#elif defined(ARDUINO_LILYGO_LORA_SX1280)
    SX1280 radio_ = newModule();
#else
    SX1262 radio_ = newModule();
#endif
};

extern TDeckBoard& instance;
extern BoardBase& board;
