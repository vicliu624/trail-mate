#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include <TouchDrvFT6X36.hpp>
#include <XPowersAXP2101.tpp>

#include "BoardBase.h"
#include "LoraBoard.h"
#include "board/TLoRaPagerTypes.h"
#include "display/DisplayInterface.h"
#include "pins_arduino.h"

#if !defined(SCREEN_WIDTH) || !defined(SCREEN_HEIGHT)
#error "SCREEN_WIDTH and SCREEN_HEIGHT must be provided via build flags (env .ini)."
#endif

class SX1262Access : public SX1262
{
  public:
    using SX1262::SX1262;
    using SX126x::readRegister;
    using SX126x::setTxParams;
    using SX126x::writeRegister;
};

class TWatchS3Board : public BoardBase,
                      public LoraBoard,
                      public LilyGo_Display,
                      public LilyGoDispArduinoSPI
{
  public:
    static TWatchS3Board* getInstance();

    // BoardBase
    uint32_t begin(uint32_t disable_hw_init = 0) override;
    void wakeUp() override {}
    void handlePowerButton() override {}
    void softwareShutdown() override;

    void setBrightness(uint8_t level) override;
    uint8_t getBrightness() override { return brightness_; }

    bool hasKeyboard() override { return false; }
    void keyboardSetBrightness(uint8_t) override {}
    uint8_t keyboardGetBrightness() override { return 0; }

    bool isRTCReady() const override;
    bool isCharging() override;
    int getBatteryLevel() override;

    bool isSDReady() const override { return false; }
    bool isCardReady() override { return false; }
    bool isGPSReady() const override { return false; }

    void vibrator() override {}
    void stopVibrator() override {}

    // LilyGo_Display
    void setRotation(uint8_t rotation) override;
    uint8_t getRotation() override;
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color) override;
    uint16_t width() override;
    uint16_t height() override;
    bool useDMA() override { return true; }
    bool hasTouch() override { return touch_ready_; }
    uint8_t getPoint(int16_t* x, int16_t* y, uint8_t get_point) override;

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

  private:
    TWatchS3Board();
    bool initPMU();
    bool initTouch();

  private:
    uint32_t devices_probe_ = 0;
    uint8_t brightness_ = 8;
    uint8_t rotation_ = 0;
    bool pmu_ready_ = false;
    bool rtc_ready_ = false;
    bool display_ready_ = false;
    bool touch_ready_ = false;

    XPowersAXP2101 pmu_;
    TouchDrvFT6X36 touch_;
    SPIClass lora_spi_{HSPI};
    Module lora_module_{LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY, lora_spi_};
#if defined(ARDUINO_LILYGO_LORA_SX1262)
    SX1262Access radio_{&lora_module_};
#elif defined(ARDUINO_LILYGO_LORA_SX1280)
    SX1280 radio_{&lora_module_};
#else
    SX1262Access radio_{&lora_module_};
#endif
};

extern TWatchS3Board& instance;
extern BoardBase& board;
