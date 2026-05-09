#pragma once

/**
 * I2C lock ordering on T-Display P4 (MUST be respected by all callers):
 *
 * The system I2C bus is shared by the XL9535 expander (touch reset, GPIO,
 * power control), the touch controller (Hi8561 / GT9895), RTC, and
 * potentially GPS/LoRa prepare paths.
 *
 * Lock order:
 *   1.  resource_mutex_     (board device registry)
 *   2.  system_i2c_mutex_   (bus-level mutual exclusion)
 *
 * Rules:
 *   - Touch read callbacks may only take system_i2c_mutex_; they must NOT
 *     touch resource_mutex_ (no device creation / reset in the hot path).
 *   - Device creation (getManagedSystemI2cDevice) may take both, in order.
 *   - Never hold system_i2c_mutex_ while waiting on resource_mutex_.
 *   - High-frequency touch reads use a 50 ms lock timeout (raised from the
 *     original 5 ms to avoid spurious failures under bus contention).
 *
 * See also: platform/esp/idf_components/t_display_p4/trail_mate_t_display_p4_runtime.cpp
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "board/BoardBase.h"
#include "board/LoraBoard.h"
#include "boards/t_display_p4/board_profile.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"

namespace boards::t_display_p4
{

class TDisplayP4Board final : public BoardBase, public LoraBoard
{
  public:
    struct SystemI2cDeviceConfig
    {
        const char* owner = nullptr;
        uint16_t address = 0;
        uint32_t speed_hz = 400000;
    };

    class ManagedSystemI2cGuard
    {
      public:
        ManagedSystemI2cGuard(TDisplayP4Board& board,
                              const SystemI2cDeviceConfig& config,
                              uint32_t timeout_ms = 1000);
        ~ManagedSystemI2cGuard();

        ManagedSystemI2cGuard(const ManagedSystemI2cGuard&) = delete;
        ManagedSystemI2cGuard& operator=(const ManagedSystemI2cGuard&) = delete;

        bool ok() const;
        explicit operator bool() const;
        i2c_master_dev_handle_t handle() const;
        esp_err_t transmit(const uint8_t* data, size_t len, uint32_t timeout_ms) const;
        esp_err_t transmitReceive(const uint8_t* tx_data, size_t tx_len,
                                  uint8_t* rx_data, size_t rx_len,
                                  uint32_t timeout_ms) const;

      private:
        TDisplayP4Board* board_ = nullptr;
        i2c_master_dev_handle_t handle_ = nullptr;
        bool locked_ = false;
    };

    static TDisplayP4Board& instance();

    static constexpr const BoardProfile& profile()
    {
        return kBoardProfile;
    }

    static constexpr DisplayPanelType configuredPanelType()
    {
#if defined(CONFIG_TRAIL_MATE_T_DISPLAY_P4_PANEL_RM69A10)
        return DisplayPanelType::Rm69a10;
#else
        return DisplayPanelType::Hi8561;
#endif
    }

    static constexpr bool hasDisplay() { return profile().has_display; }
    static constexpr bool hasTouch() { return profile().has_touch; }
    static constexpr bool hasAudio() { return profile().has_audio; }
    static constexpr bool hasSdCard() { return profile().has_sdcard; }
    static constexpr bool hasGpsUart() { return profile().has_gps_uart; }
    static constexpr bool hasLora() { return profile().has_lora; }
    static constexpr const BoardProfile::I2cBus& systemI2c() { return profile().sys_i2c; }
    static constexpr const BoardProfile::I2cBus& externalI2c() { return profile().ext_i2c; }
    static constexpr const BoardProfile::UartPort& gpsUart() { return profile().gps_uart; }
    static constexpr const BoardProfile::SdmmcPins& sdmmcPins() { return profile().sdmmc; }
    static constexpr const BoardProfile::LoRaModulePins& loraModulePins() { return profile().lora; }
    static constexpr const BoardProfile::IoExpanderPins& ioExpanderPins() { return profile().io_expander; }
    static constexpr const BoardProfile::PanelGeometry& activePanel()
    {
        return configuredPanelType() == DisplayPanelType::Rm69a10 ? profile().rm69a10_panel
                                                                  : profile().hi8561_panel;
    }
    static constexpr int touchI2cAddress()
    {
        return activePanel().touch_address;
    }
    static constexpr const char* defaultLongName() { return profile().identity.long_name; }
    static constexpr const char* defaultShortName() { return profile().identity.short_name; }
    static constexpr const char* defaultBleName() { return profile().identity.ble_name; }

    uint32_t begin(uint32_t disable_hw_init = 0) override;
    void wakeUp() override;
    void handlePowerButton() override;
    void softwareShutdown() override;
    void enterScreenSleep() override;
    void exitScreenSleep() override;

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
    bool hasGPSHardware() const override { return hasGpsUart(); }

    void vibrator() override;
    void stopVibrator() override;
    void setMessageToneVolume(uint8_t volume_percent) override;
    uint8_t getMessageToneVolume() const override;

    bool lockSystemI2c(uint32_t timeout_ms = 1000);
    void unlockSystemI2c();
    i2c_master_bus_handle_t systemI2cHandle() const;
    i2c_master_bus_handle_t externalI2cHandle() const;
    i2c_master_dev_handle_t addSystemI2cDevice(uint16_t address, uint32_t speed_hz = 400000) const;
    void removeSystemI2cDevice(i2c_master_dev_handle_t handle) const;
    i2c_master_dev_handle_t getManagedSystemI2cDevice(const SystemI2cDeviceConfig& config,
                                                      uint32_t timeout_ms = 1000);

    bool expanderReady() const;
    bool expanderPinMode(int pin, bool output);
    bool expanderWrite(int pin, bool high);
    bool expanderRead(int pin, bool* out_high) const;
    bool expanderWriteActive(int pin, bool active, bool active_high = true);
    bool expanderReadActive(int pin, bool* out_active, bool active_high = true) const;
    bool resetTouchController(uint32_t pre_delay_ms = 10,
                              uint32_t reset_pulse_ms = 10,
                              uint32_t post_delay_ms = 10);
    bool isTouchInterruptActive() const;

    bool prepareGpsRuntime();
    void teardownGpsRuntime();

    bool mountSdCard(const char* mount_point, size_t max_files);
    bool sdCardMounted() const;
    sdmmc_card_t* sdCard() const;

    bool ensureRtcAccessible();
    bool prepareLoraRuntime();
    bool setLoraResetAsserted(bool asserted);
    bool readLoraDio1(bool* out_high) const;
    bool setLoraRfSwitchTransmit(bool transmit);

    bool isRadioOnline() const override;
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
    struct ManagedI2cSlot
    {
        bool active = false;
        uint16_t address = 0;
        uint32_t speed_hz = 0;
        i2c_master_dev_handle_t handle = nullptr;
        char owner[24] = {};
    };

    TDisplayP4Board();
    static void copyOwnerTag(const char* src, char* dst, size_t dst_len);
    bool initializeI2cBuses();
    bool initializeExpander();
    bool runColdBootPowerSequence();
    bool initializeBatteryGauge();
    bool configureGpsUart(uint32_t baud_rate);
    void teardownGpsUart();
    bool readBatteryGaugeWord(uint8_t reg, uint16_t* out_word) const;
    bool readBatteryGaugeWordSigned(uint8_t reg, int16_t* out_word) const;
    bool loraReady() const;
    bool ensureRadioReady() const;

    ManagedI2cSlot* findManagedI2cSlot(uint16_t address, uint32_t speed_hz);
    const ManagedI2cSlot* findManagedI2cSlot(uint16_t address, uint32_t speed_hz) const;
    ManagedI2cSlot* findFreeManagedI2cSlot();

    mutable std::mutex resource_mutex_;
    SemaphoreHandle_t system_i2c_mutex_ = nullptr;
    i2c_master_bus_handle_t system_i2c_handle_ = nullptr;
    i2c_master_bus_handle_t external_i2c_handle_ = nullptr;
    std::array<ManagedI2cSlot, 8> managed_system_i2c_{};

    bool started_ = false;
    bool expander_ready_ = false;
    bool rtc_accessible_ = false;
    bool battery_gauge_ready_ = false;
    bool gps_uart_configured_ = false;
    bool gps_runtime_prepared_ = false;
    bool sd_ready_ = false;
    bool sd_enabled_ = false;
    bool battery_charging_ = false;
    int last_battery_level_ = -1;
    uint8_t brightness_level_ = DEVICE_MAX_BRIGHTNESS_LEVEL;
    uint8_t message_tone_volume_ = 45;
    sdmmc_card_t* sd_card_ = nullptr;
    char sd_mount_point_[32] = {};
    uint8_t expander_output_port0_ = 0x00;
    uint8_t expander_output_port1_ = 0x00;
    uint8_t expander_config_port0_ = 0xFF;
    uint8_t expander_config_port1_ = 0xFF;
};

} // namespace boards::t_display_p4
