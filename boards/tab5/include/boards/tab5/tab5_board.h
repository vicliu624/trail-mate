#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "board/BoardBase.h"
#include "board/LoraBoard.h"
#include "boards/tab5/board_profile.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"

namespace boards::tab5
{

class Tab5Board final : public BoardBase, public LoraBoard
{
  public:
    struct SystemI2cDeviceConfig
    {
        const char* owner = nullptr;
        uint16_t address = 0;
        uint32_t speed_hz = 400000;
    };

    class SysI2cGuard
    {
      public:
        explicit SysI2cGuard(Tab5Board& board, uint32_t timeout_ms = 1000);
        ~SysI2cGuard();

        SysI2cGuard(const SysI2cGuard&) = delete;
        SysI2cGuard& operator=(const SysI2cGuard&) = delete;

        bool locked() const;
        explicit operator bool() const;

      private:
        Tab5Board* board_ = nullptr;
        bool locked_ = false;
    };

    class ManagedSystemI2cGuard
    {
      public:
        ManagedSystemI2cGuard(Tab5Board& board,
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
        Tab5Board* board_ = nullptr;
        i2c_master_dev_handle_t handle_ = nullptr;
        bool locked_ = false;
    };

    static Tab5Board& instance();
    static constexpr const BoardProfile& profile()
    {
        return kBoardProfile;
    }
    static constexpr bool hasDisplay() { return profile().has_display; }
    static constexpr bool hasTouch() { return profile().has_touch; }
    static constexpr bool hasAudio() { return profile().has_audio; }
    static constexpr bool hasSdCard() { return profile().has_sdcard; }
    static constexpr bool hasGpsUart() { return profile().has_gps_uart; }
    static constexpr bool hasRs485Uart() { return profile().has_rs485_uart; }
    static constexpr bool hasLora() { return profile().has_lora; }
    static constexpr bool hasM5BusLoraRouting() { return profile().has_m5bus_lora_module_routing; }
    static constexpr const BoardProfile::I2cBus& systemI2c() { return profile().sys_i2c; }
    static constexpr const BoardProfile::I2cBus& externalI2c() { return profile().ext_i2c; }
    static constexpr const BoardProfile::UartPort& gpsUart() { return profile().gps_uart; }
    static constexpr const BoardProfile::UartPort& rs485Uart() { return profile().rs485_uart; }
    static constexpr const BoardProfile::SdmmcPins& sdmmcPins() { return profile().sdmmc; }
    static constexpr const BoardProfile::AudioI2sPins& audioI2sPins() { return profile().audio_i2s; }
    static constexpr const BoardProfile::LoRaModulePins& loraModulePins() { return profile().lora_module; }
    static constexpr int backlightPin() { return profile().lcd_backlight; }
    static constexpr int touchInterruptPin() { return profile().touch_int; }
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

    bool setExt5vEnabled(bool enabled);
    bool acquireExt5vRail(const char* owner);
    void releaseExt5vRail(const char* owner);
    bool isExt5vEnabled() const;
    bool touchInterruptActive() const;

    bool configureGpsUart();
    void teardownGpsUart();
    uart_port_t gpsUartPort() const;

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

    struct ManagedLeaseSlot
    {
        bool active = false;
        uint32_t refs = 0;
        char owner[24] = {};
    };

    Tab5Board() = default;
    static bool ensureRadioReady();
    static void copyOwnerTag(const char* src, char* dst, size_t dst_len);
    ManagedI2cSlot* findManagedI2cSlot(uint16_t address, uint32_t speed_hz);
    const ManagedI2cSlot* findManagedI2cSlot(uint16_t address, uint32_t speed_hz) const;
    ManagedI2cSlot* findFreeManagedI2cSlot();
    ManagedLeaseSlot* findLeaseSlot(std::array<ManagedLeaseSlot, 6>& slots, const char* owner);
    const ManagedLeaseSlot* findLeaseSlot(const std::array<ManagedLeaseSlot, 6>& slots, const char* owner) const;
    ManagedLeaseSlot* findFreeLeaseSlot(std::array<ManagedLeaseSlot, 6>& slots);
    void updateExt5vStateLocked(bool enabled);
    void logManagedResourcesLocked(const char* reason) const;

    uint8_t brightness_level_ = DEVICE_MAX_BRIGHTNESS_LEVEL;
    uint8_t message_tone_volume_ = 45;
    bool ext_5v_enabled_ = false;
    bool gps_uart_configured_ = false;
    mutable std::mutex resource_mutex_;
    std::array<ManagedI2cSlot, 8> managed_system_i2c_{};
    std::array<ManagedLeaseSlot, 6> ext5v_leases_{};
};

} // namespace boards::tab5
