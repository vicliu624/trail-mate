#pragma once

#include "BoardBase.h"
#include "GpsBoard.h"
#include "LoraBoard.h"
#include "SdBoard.h"
#include "MotionBoard.h"
#include "display/DisplayInterface.h"

// Minimal M5Stack Tab5 board integration.
// This is a skeleton that allows the firmware to build and run basic UI
// on ESP32-P4, with hardware-specific details to be filled in incrementally.

class SensorBHI260AP;
class GPS;

class M5Tab5Board : public BoardBase,
                    public LoraBoard,
                    public GpsBoard,
                    public MotionBoard,
                    public SdBoard,
                    public LilyGo_Display
{
  public:
    static M5Tab5Board* getInstance();

    // BoardBase
    uint32_t begin(uint32_t disable_hw_init = 0) override;
    void wakeUp() override {}
    void handlePowerButton() override {}
    void softwareShutdown() override {}

    void setBrightness(uint8_t level) override;
    uint8_t getBrightness() override { return brightness_; }

    bool hasKeyboard() override { return false; }
    void keyboardSetBrightness(uint8_t /*level*/) override {}
    uint8_t keyboardGetBrightness() override { return 0; }

    bool isRTCReady() const override { return rtc_ready_; }
    bool isCharging() override { return false; }
    int getBatteryLevel() override { return -1; }

    bool isSDReady() const override { return sd_ready_; }
    bool isGPSReady() const override { return gps_ready_; }

    void vibrator() override {}
    void stopVibrator() override {}

    // Display / input (LilyGo_Display interface used by LV_Helper_v9)
    void setRotation(uint8_t rotation) override;
    uint8_t getRotation() override { return rotation_; }
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color) override;
    uint16_t width() override;
    uint16_t height() override;
    bool useDMA() override { return false; }
    bool hasTouch() override { return true; }
    uint8_t getPoint(int16_t* x, int16_t* y, uint8_t get_point) override;
    bool hasEncoder() override { return false; }
    RotaryMsg_t getRotary() override;
    int getKeyChar(char* c) override;
    void feedback(void* args = NULL) override { (void)args; }

    // LoraBoard (stubs for now; real radio wiring to be added later)
    bool isRadioOnline() const override { return false; }
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

    // GpsBoard (stubs; to be connected to Tab5 GNSS module)
    bool initGPS() override;
    void setGPSOnline(bool online) override { gps_ready_ = online; }
    GPS& getGPS() override;
    bool isGPSReady() const override { return gps_ready_; }
    void powerControl(PowerCtrlChannel_t ch, bool enable) override;
    bool syncTimeFromGPS(uint32_t gps_task_interval_ms = 0) override;

    // MotionBoard (Tab5 has IMU; stub until wired)
    SensorBHI260AP& getMotionSensor() override;
    bool isSensorReady() const override { return false; }

    // SdBoard
    bool installSD() override;
    void uninstallSD() override;
    bool isCardReady() override;

  private:
    M5Tab5Board();

  private:
    uint8_t brightness_ = DEVICE_MAX_BRIGHTNESS_LEVEL;
    uint8_t rotation_ = 0;
    bool display_ready_ = false;
    bool touch_ready_ = false;
    bool rtc_ready_ = false;
    bool sd_ready_ = false;
    bool gps_ready_ = false;
};

