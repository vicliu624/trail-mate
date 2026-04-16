#pragma once

#include "app/app_config.h"
#include "board/BoardBase.h"
#include "chat/domain/chat_types.h"
#include "gps/domain/gnss_satellite.h"
#include "gps/domain/gps_state.h"

#include <cstddef>
#include <cstdint>
#include <memory>

class TwoWire;

namespace ui::mono_128x64
{
class MonoDisplay;
}

namespace platform::nrf52::arduino_common::chat::infra
{
class IRadioPacketIo;
}

namespace boards::gat562_mesh_evb_pro
{

class GpsRuntime;
class InputRuntime;

enum class BoardInputKey : uint8_t
{
    None = 0,
    PrimaryButton,
    SecondaryButton,
    JoystickUp,
    JoystickDown,
    JoystickLeft,
    JoystickRight,
    JoystickPress,
};

struct BoardInputEvent
{
    BoardInputKey key = BoardInputKey::None;
    bool pressed = false;
    uint32_t timestamp_ms = 0;
};

struct BoardInputSnapshot
{
    bool button_primary = false;
    bool button_secondary = false;
    bool joystick_up = false;
    bool joystick_down = false;
    bool joystick_left = false;
    bool joystick_right = false;
    bool joystick_press = false;
    bool any_activity = false;
};

class Gat562Board final : public BoardBase
{
  public:
    class I2cGuard
    {
      public:
        explicit I2cGuard(Gat562Board& board, uint32_t timeout_ms = 100);
        ~I2cGuard();

        I2cGuard(const I2cGuard&) = delete;
        I2cGuard& operator=(const I2cGuard&) = delete;

        bool locked() const;
        explicit operator bool() const;

      private:
        Gat562Board* board_ = nullptr;
        bool locked_ = false;
    };

    static Gat562Board& instance();
    ~Gat562Board() override;

    uint32_t begin(uint32_t disable_hw_init = 0) override;
    void wakeUp() override;
    void handlePowerButton() override;
    void softwareShutdown() override;
    int getPowerTier() const override;

    void setBrightness(uint8_t level) override;
    uint8_t getBrightness() override;

    bool hasKeyboard() override;
    void keyboardSetBrightness(uint8_t level) override;
    uint8_t keyboardGetBrightness() override;

    bool isRTCReady() const override;
    bool isCharging() override;
    int getBatteryLevel() override;

    bool isSDReady() const override;
    bool isCardReady() override;
    bool isGPSReady() const override;
    bool hasGPSHardware() const override { return true; }

    void vibrator() override;
    void stopVibrator() override;
    void playMessageTone() override;

    void setMessageToneVolume(uint8_t volume_percent) override;
    uint8_t getMessageToneVolume() const override;

    void setStatusLed(bool on);
    void pulseNotificationLed(uint32_t pulse_ms = 25);
    bool pollInputSnapshot(BoardInputSnapshot* out_snapshot) const;
    bool pollInputEvent(BoardInputEvent* out_event);
    bool formatLoraFrequencyMHz(uint32_t freq_hz, char* out, std::size_t out_len) const;
    uint16_t inputDebounceMs() const;
    ::ui::mono_128x64::MonoDisplay& monoDisplay();
    bool ensureI2cReady();
    bool lockI2c(uint32_t timeout_ms = 100);
    void unlockI2c();
    TwoWire& i2cWire();
    const char* defaultLongName() const;
    const char* defaultShortName() const;
    const char* defaultBleName() const;
    bool prepareRadioHardware();
    bool beginRadioIo();
    platform::nrf52::arduino_common::chat::infra::IRadioPacketIo* bindRadioIo();
    void applyRadioConfig(chat::MeshProtocol protocol, const chat::MeshConfig& config);
    uint32_t activeLoraFrequencyHz() const;

    bool startGpsRuntime(const app::AppConfig& config);
    bool beginGps(const app::AppConfig& config);
    void applyGpsConfig(const app::AppConfig& config);
    void tickGps();
    bool isGpsRuntimeReady() const;
    ::gps::GpsState gpsData() const;
    bool gpsEnabled() const;
    bool gpsPowered() const;
    uint32_t gpsLastMotionMs() const;
    bool gpsGnssSnapshot(::gps::GnssSatInfo* out,
                         std::size_t max,
                         std::size_t* out_count,
                         ::gps::GnssStatus* status) const;
    void setGpsCollectionInterval(uint32_t interval_ms);
    void setGpsPowerStrategy(uint8_t strategy);
    void setGpsConfig(uint8_t mode, uint8_t sat_mask);
    void setGpsNmeaConfig(uint8_t output_hz, uint8_t sentence_mask);
    void setGpsMotionIdleTimeout(uint32_t timeout_ms);
    void setGpsMotionSensorId(uint8_t sensor_id);
    void suspendGps();
    void resumeGps();
    void setCurrentEpochSeconds(uint32_t epoch_s);
    uint32_t currentEpochSeconds() const;

  private:
    Gat562Board();

    void initializeBoardHardware();
    void enablePeripheralRail();
    int readBatteryPercent() const;

    bool initialized_ = false;
    bool i2c_initialized_ = false;
    bool i2c_locked_ = false;
    bool peripheral_rail_enabled_ = false;
    bool radio_hw_ready_ = false;
    uint8_t brightness_ = DEVICE_MAX_BRIGHTNESS_LEVEL;
    uint8_t keyboard_brightness_ = 0;
    uint8_t message_tone_volume_ = 45;
    std::unique_ptr<GpsRuntime> gps_runtime_;
    std::unique_ptr<InputRuntime> input_runtime_;
};

} // namespace boards::gat562_mesh_evb_pro
