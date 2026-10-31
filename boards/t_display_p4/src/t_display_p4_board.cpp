#include "boards/t_display_p4/t_display_p4_board.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "boards/t_display_p4/haptic_runtime.h"
#include "boards/t_display_p4/rtc_runtime.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/sdmmc_default_configs.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "platform/shared/radio/board_radio_lease.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/esp/idf_common/gps_runtime.h"
#include "platform/esp/idf_common/sd_card_runtime_sdfat_adapter.h"
#include "platform/esp/idf_common/sdmmc_host_runtime.h"
#include "platform/esp/idf_common/selected_radio.h"
#include "platform/esp/idf_common/wireless_companion/c6_companion.h"
#include "platform/ui/device_runtime.h"

namespace
{

constexpr const char* kTag = "TDisplayP4Board";
constexpr uint32_t kI2cTimeoutMs = 1000;
constexpr uint32_t kI2cOwnerDiagnosticIntervalMs = 2000;
constexpr uint32_t kPowerButtonDebounceMs = 30;
constexpr uint32_t kPowerButtonLongPressMs = 1800;
constexpr int kExternal3v3LdoChannel = 4;
constexpr int kExternal3v3Mv = 3300;
constexpr uint8_t kBatteryRegVoltage = 0x08;
constexpr uint8_t kBatteryRegCurrent = 0x0C;
constexpr uint8_t kBatteryRegStateOfCharge = 0x2C;
constexpr uint8_t kBatteryRegOperationStatus = 0x3A;
constexpr uint8_t kBatteryRegDataClass = 0x3E;
constexpr uint8_t kBatteryRegDataBlock = 0x40;
constexpr uint8_t kBatteryRegDataChecksum = 0x60;
constexpr uint8_t kBatteryRegDataLength = 0x61;
constexpr uint16_t kBatterySubcommandEnterConfigUpdate = 0x0090;
constexpr uint16_t kBatterySubcommandExitConfigUpdateReinit = 0x0091;
constexpr uint16_t kBatterySubcommandSeal = 0x0030;
constexpr uint16_t kBatteryDesignCapacityAddress = 0x929F;
constexpr uint16_t kBatteryFullChargeCapacityAddress = 0x929D;
constexpr uint16_t kBatteryConfigUpdateMask = 0x0400;
constexpr uint8_t kBatterySecurityAccessMask = 0x03;
constexpr uint8_t kBatterySecurityAccessShift = 1;
constexpr uint8_t kBatteryFullAccess = 1;
constexpr uint8_t kBatterySealedAccess = 3;

constexpr uint8_t kExpanderRegInput0 = 0x00;
constexpr uint8_t kExpanderRegInput1 = 0x01;
constexpr uint8_t kExpanderRegOutput0 = 0x02;
constexpr uint8_t kExpanderRegOutput1 = 0x03;
constexpr uint8_t kExpanderRegPolarity0 = 0x04;
constexpr uint8_t kExpanderRegPolarity1 = 0x05;
constexpr uint8_t kExpanderRegConfig0 = 0x06;
constexpr uint8_t kExpanderRegConfig1 = 0x07;
constexpr uint32_t kRadioTxBaseTimeoutMs = 500;
constexpr uint32_t kRadioTxPerByteTimeoutMs = 100;
constexpr uint32_t kRadioTxMaxTimeoutMs = 30000;
constexpr uint32_t kRadioTxPollIntervalMs = 10;
constexpr uint32_t kCompanionPollIntervalMs = 50;
constexpr uint32_t kRadioIrqTxDone = 0x0001;
constexpr uint32_t kRadioIrqTimeout = 0x0200;
esp_ldo_channel_handle_t s_external_3v3_ldo_handle = nullptr;
bool s_keyboard_backlight_ready = false;
constexpr ledc_mode_t kKeyboardBacklightSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kKeyboardBacklightTimer = LEDC_TIMER_1;
constexpr ledc_channel_t kKeyboardBacklightChannel = LEDC_CHANNEL_1;
constexpr ledc_timer_bit_t kKeyboardBacklightResolution = LEDC_TIMER_10_BIT;
constexpr uint32_t kKeyboardBacklightFrequencyHz = 20000;
constexpr uint32_t kKeyboardBacklightMaxDuty = (1U << 10U) - 1U;
constexpr uint8_t kKeyboardStartupBrightness = (DEVICE_MAX_BRIGHTNESS_LEVEL * 30U + 50U) / 100U;

struct ExpanderPinLocation
{
    bool valid = false;
    uint8_t port = 0;
    uint8_t bit = 0;
};

ExpanderPinLocation locate_expander_pin(int pin)
{
    if (pin >= 0 && pin <= 7)
    {
        return {true, 0, static_cast<uint8_t>(pin)};
    }
    if (pin >= 10 && pin <= 17)
    {
        return {true, 1, static_cast<uint8_t>(pin - 10)};
    }
    return {};
}

platform::esp::idf_common::SelectedRadio& radio()
{
    return platform::esp::idf_common::SelectedRadio::instance();
}

TickType_t radio_tx_timeout_ticks(size_t len)
{
    uint64_t timeout_ms = kRadioTxBaseTimeoutMs + static_cast<uint64_t>(len) * kRadioTxPerByteTimeoutMs;
    if (timeout_ms > kRadioTxMaxTimeoutMs)
    {
        timeout_ms = kRadioTxMaxTimeoutMs;
    }
    return pdMS_TO_TICKS(static_cast<uint32_t>(timeout_ms));
}

bool ensure_external_3v3_power_control()
{
    // SD and keyboard startup can run alongside audio/motion preparation.
    // Serialize both initial acquisition and subsequent voltage confirmation.
    static std::mutex mutex;
    const std::lock_guard<std::mutex> lock(mutex);
    if (s_external_3v3_ldo_handle != nullptr)
    {
        const esp_err_t err =
            esp_ldo_channel_adjust_voltage(s_external_3v3_ldo_handle, kExternal3v3Mv);
        if (err != ESP_OK)
        {
            ESP_LOGE(kTag,
                     "Failed to adjust P4 LDO4 to %dmV: %s",
                     kExternal3v3Mv,
                     esp_err_to_name(err));
            return false;
        }
        return true;
    }

    // LDO4 supplies VDDPST_5 (GPIO39-48), including SD and keyboard signals.
    // The keyboard connector itself is supplied by ESP_3V3. Keep software
    // ownership of this I/O domain so its requested 3.3 V level is explicit.
    // The board holds the channel for its lifetime; consumers may re-confirm
    // its voltage without acquiring a competing adjustable channel.
    esp_ldo_channel_config_t ldo_config{};
    ldo_config.chan_id = kExternal3v3LdoChannel;
    ldo_config.voltage_mv = kExternal3v3Mv;
    ldo_config.flags.adjustable = 1;
    ldo_config.flags.owned_by_hw = 0;
    const esp_err_t err = esp_ldo_acquire_channel(&ldo_config, &s_external_3v3_ldo_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(kTag,
                 "Failed to acquire P4 LDO4 at %dmV: %s",
                 kExternal3v3Mv,
                 esp_err_to_name(err));
        s_external_3v3_ldo_handle = nullptr;
        return false;
    }

    ESP_LOGI(kTag,
             "P4 LDO4 software-owned at %dmV for GPIO39-48 I/O domain",
             kExternal3v3Mv);
    return true;
}

bool configure_l76k_update_rate(uart_port_t port, uint32_t baud_rate)
{
    // Full multi-constellation NMEA at 5 Hz exceeds a 9600 baud UART. Keep
    // the factory-rate fallback readable; use the vendor 5 Hz rate at 115200.
    const char* body = baud_rate <= 9600 ? "PCAS02,1000" : "PCAS02,200";
    uint8_t checksum = 0;
    for (const char* value = body; *value != '\0'; ++value)
    {
        checksum ^= static_cast<uint8_t>(*value);
    }

    char command[24] = {};
    const int length = std::snprintf(command, sizeof(command), "$%s*%02X\r\n", body, checksum);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(command))
    {
        return false;
    }
    if (uart_write_bytes(port, command, length) != length)
    {
        ESP_LOGW(kTag, "L76K PCAS02 write failed");
        return false;
    }
    if (uart_wait_tx_done(port, pdMS_TO_TICKS(100)) != ESP_OK)
    {
        ESP_LOGW(kTag, "L76K PCAS02 transmit did not complete");
        return false;
    }
    return uart_flush_input(port) == ESP_OK;
}

} // namespace

namespace boards::t_display_p4
{

TDisplayP4Board::ManagedSystemI2cGuard::ManagedSystemI2cGuard(TDisplayP4Board& board,
                                                              const SystemI2cDeviceConfig& config,
                                                              uint32_t timeout_ms)
    : board_(&board)
{
    handle_ = board.getManagedSystemI2cDevice(config, timeout_ms);
    if (handle_ == nullptr)
    {
        return;
    }

    locked_ = board.lockSystemI2c(timeout_ms, config.owner, __FILE__, __LINE__);
    if (!locked_)
    {
        ESP_LOGW(kTag,
                 "Failed to lock SYS I2C for owner=%s addr=0x%02X",
                 config.owner ? config.owner : "unknown",
                 static_cast<unsigned>(config.address));
        handle_ = nullptr;
    }
}

TDisplayP4Board::ManagedSystemI2cGuard::~ManagedSystemI2cGuard()
{
    if (locked_ && board_ != nullptr)
    {
        board_->unlockSystemI2c();
    }
}

bool TDisplayP4Board::ManagedSystemI2cGuard::ok() const
{
    return locked_ && handle_ != nullptr;
}

TDisplayP4Board::ManagedSystemI2cGuard::operator bool() const
{
    return ok();
}

i2c_master_dev_handle_t TDisplayP4Board::ManagedSystemI2cGuard::handle() const
{
    return handle_;
}

esp_err_t TDisplayP4Board::ManagedSystemI2cGuard::transmit(const uint8_t* data,
                                                           size_t len,
                                                           uint32_t timeout_ms) const
{
    if (!ok() || data == nullptr || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit(handle_, data, len, timeout_ms);
}

esp_err_t TDisplayP4Board::ManagedSystemI2cGuard::transmitReceive(const uint8_t* tx_data,
                                                                  size_t tx_len,
                                                                  uint8_t* rx_data,
                                                                  size_t rx_len,
                                                                  uint32_t timeout_ms) const
{
    if (!ok() || tx_data == nullptr || tx_len == 0 || rx_data == nullptr || rx_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(handle_, tx_data, tx_len, rx_data, rx_len, timeout_ms);
}

TDisplayP4Board& TDisplayP4Board::instance()
{
    static TDisplayP4Board board_instance;
    return board_instance;
}

TDisplayP4Board::TDisplayP4Board()
{
    system_i2c_mutex_ = xSemaphoreCreateMutex();
    if (system_i2c_mutex_ == nullptr)
    {
        ESP_LOGE(kTag, "Failed to create SYS I2C mutex");
    }
}

uint32_t TDisplayP4Board::begin(uint32_t disable_hw_init)
{
    (void)disable_hw_init;
    if (started_)
    {
        return 0;
    }

    const auto& panel = activePanel();
    ESP_LOGI(kTag,
             "begin panel=%s size=%dx%d sys_i2c=(%d,%d,%d) ext_i2c=(%d,%d,%d) gps_uart=(%d,%d,%d,%lu) "
             "sdmmc=(%d,%d,%d,%d,%d,%d) lora_spi=(host=%d sck=%d miso=%d mosi=%d) lora_ctrl=(nss=%d busy=%d) "
             "expander=0x%02X backlight=%d",
             configuredPanelType() == DisplayPanelType::Rm69a10 ? "rm69a10" : "hi8561",
             panel.width,
             panel.height,
             systemI2c().port,
             systemI2c().sda,
             systemI2c().scl,
             externalI2c().port,
             externalI2c().sda,
             externalI2c().scl,
             gpsUart().port,
             gpsUart().tx,
             gpsUart().rx,
             static_cast<unsigned long>(gpsUart().baud_rate),
             sdmmcPins().d0,
             sdmmcPins().d1,
             sdmmcPins().d2,
             sdmmcPins().d3,
             sdmmcPins().cmd,
             sdmmcPins().clk,
             loraModulePins().spi.host,
             loraModulePins().spi.sck,
             loraModulePins().spi.miso,
             loraModulePins().spi.mosi,
             loraModulePins().nss,
             loraModulePins().busy,
             profile().i2c.io_expander,
             profile().lcd_backlight);

    const bool buses_ok = initializeI2cBuses();
    const bool expander_ok = buses_ok && initializeExpander();
    const bool power_ok = expander_ok && runColdBootPowerSequence();
    (void)initializeBatteryGauge();
    (void)ensureRtcAccessible();

    if (profile().boot >= 0)
    {
        gpio_config_t power_button_config{};
        power_button_config.pin_bit_mask = 1ULL << profile().boot;
        power_button_config.mode = GPIO_MODE_INPUT;
        power_button_config.pull_up_en = GPIO_PULLUP_ENABLE;
        power_button_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        power_button_config.intr_type = GPIO_INTR_DISABLE;
        power_button_initialized_ = gpio_config(&power_button_config) == ESP_OK;
        power_button_pressed_ =
            power_button_initialized_ &&
            gpio_get_level(static_cast<gpio_num_t>(profile().boot)) == 0;
        power_button_last_change_ticks_ = xTaskGetTickCount();
        power_button_press_start_ticks_ = power_button_last_change_ticks_;
        ESP_LOGI(kTag,
                 "power button gpio=%d ready=%u pressed=%u long_press_ms=%lu",
                 profile().boot,
                 power_button_initialized_ ? 1U : 0U,
                 power_button_pressed_ ? 1U : 0U,
                 static_cast<unsigned long>(kPowerButtonLongPressMs));
    }

    started_ = buses_ok && expander_ok && power_ok;
    if (started_)
    {
        // GPIO47 PWM is independent of the XL9555/TCA8418 input path. A failed
        // keypad probe must not disable the accessory's backlight controls.
        keyboardSetBrightness(kKeyboardStartupBrightness);
    }
    return started_ ? 0 : 1;
}

void TDisplayP4Board::wakeUp()
{
    (void)platform::esp::idf_common::bsp_runtime::wake_display();
}

void TDisplayP4Board::handlePowerButton()
{
    if (!power_button_initialized_ || profile().boot < 0)
    {
        return;
    }

    const TickType_t now = xTaskGetTickCount();
    const bool pressed =
        gpio_get_level(static_cast<gpio_num_t>(profile().boot)) == 0;
    if (pressed != power_button_pressed_ &&
        (now - power_button_last_change_ticks_) >=
            pdMS_TO_TICKS(kPowerButtonDebounceMs))
    {
        power_button_pressed_ = pressed;
        power_button_last_change_ticks_ = now;
        if (pressed)
        {
            power_button_press_start_ticks_ = now;
            power_button_long_press_handled_ = false;
            wakeUp();
            ESP_LOGI(kTag, "power button pressed");
        }
        else
        {
            ESP_LOGI(kTag, "power button released");
        }
    }

    if (power_button_pressed_ && !power_button_long_press_handled_ &&
        (now - power_button_press_start_ticks_) >=
            pdMS_TO_TICKS(kPowerButtonLongPressMs))
    {
        power_button_long_press_handled_ = true;
        ESP_LOGI(kTag, "power button long press -> software shutdown");
        softwareShutdown();
    }
}

void TDisplayP4Board::softwareShutdown()
{
    ESP_LOGI(kTag, "Software shutdown requested");
    stopVibrator();
    keyboardSetBrightness(0);
    (void)platform::esp::idf_common::bsp_runtime::sleep_display();

    if (expander_ready_)
    {
        const auto& io = ioExpanderPins();
        const auto& p = profile();
        (void)expanderWriteActive(io.gps_wake, false, p.gps_wake_active_high);
        (void)expanderWriteActive(io.c6_enable, false, p.c6_enable_active_high);
        (void)expanderWriteActive(io.screen_rst, true, !p.screen_reset_active_low);
        (void)expanderWriteActive(io.touch_rst, true, !p.touch_reset_active_low);
        // LilyGO's P4 V1.0 power sequence requires this rail to stay enabled,
        // including while the CPU sleeps, to avoid reset/download-mode locks.
        (void)expanderWriteActive(io.power_3v3, true, p.power_3v3_active_high);
    }

    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(kTag, "Entering deep sleep; hardware power key/reset resumes the device");
    esp_deep_sleep_start();
}

void TDisplayP4Board::enterScreenSleep()
{
    (void)platform::esp::idf_common::bsp_runtime::sleep_display();
}

void TDisplayP4Board::exitScreenSleep()
{
    (void)platform::esp::idf_common::bsp_runtime::wake_display();
}

void TDisplayP4Board::setBrightness(uint8_t level)
{
    brightness_level_ = level;
    const int percent = (DEVICE_MAX_BRIGHTNESS_LEVEL <= 0)
                            ? 100
                            : static_cast<int>((static_cast<uint32_t>(level) * 100U) /
                                               static_cast<uint32_t>(DEVICE_MAX_BRIGHTNESS_LEVEL));
    (void)platform::esp::idf_common::bsp_runtime::set_display_brightness(percent);
}

uint8_t TDisplayP4Board::getBrightness()
{
    return brightness_level_;
}

bool TDisplayP4Board::hasKeyboard()
{
    return keyboard_ready_;
}

void TDisplayP4Board::keyboardSetBrightness(uint8_t level)
{
    keyboard_brightness_ = std::min<uint8_t>(level, DEVICE_MAX_BRIGHTNESS_LEVEL);
    if (!supportsKeyboardBacklight())
    {
        return;
    }

    if (!s_keyboard_backlight_ready)
    {
        if (!ensureKeyboardLdo4Power())
        {
            ESP_LOGE(kTag, "Failed to prepare keyboard backlight I/O power");
            return;
        }
        ledc_timer_config_t timer_config{};
        timer_config.speed_mode = kKeyboardBacklightSpeedMode;
        timer_config.duty_resolution = kKeyboardBacklightResolution;
        timer_config.timer_num = kKeyboardBacklightTimer;
        timer_config.freq_hz = kKeyboardBacklightFrequencyHz;
        timer_config.clk_cfg = LEDC_AUTO_CLK;
        if (ledc_timer_config(&timer_config) != ESP_OK)
        {
            ESP_LOGE(kTag, "Failed to configure keyboard backlight LEDC timer");
            return;
        }

        ledc_channel_config_t channel_config{};
        channel_config.gpio_num = keyboardModule().backlight;
        channel_config.speed_mode = kKeyboardBacklightSpeedMode;
        channel_config.channel = kKeyboardBacklightChannel;
        channel_config.timer_sel = kKeyboardBacklightTimer;
        channel_config.duty = 0;
        channel_config.hpoint = 0;
        if (ledc_channel_config(&channel_config) != ESP_OK)
        {
            ESP_LOGE(kTag, "Failed to configure keyboard backlight LEDC channel");
            return;
        }
        s_keyboard_backlight_ready = true;
    }

    const uint32_t duty = DEVICE_MAX_BRIGHTNESS_LEVEL > 0
                              ? (static_cast<uint32_t>(keyboard_brightness_) *
                                 kKeyboardBacklightMaxDuty) /
                                    DEVICE_MAX_BRIGHTNESS_LEVEL
                              : kKeyboardBacklightMaxDuty;
    (void)ledc_set_duty(kKeyboardBacklightSpeedMode, kKeyboardBacklightChannel, duty);
    (void)ledc_update_duty(kKeyboardBacklightSpeedMode, kKeyboardBacklightChannel);
}

uint8_t TDisplayP4Board::keyboardGetBrightness()
{
    return keyboard_brightness_;
}

bool TDisplayP4Board::ensureKeyboardLdo4Power()
{
    // Prepare the keyboard signals' I/O voltage domain, not the connector's
    // separate ESP_3V3 supply or the XL9535-switched peripheral rail.
    return ensure_external_3v3_power_control();
}

bool TDisplayP4Board::ensureExternal3v3Power()
{
    if (!ensure_external_3v3_power_control())
    {
        return false;
    }

    if (!expander_ready_)
    {
        if ((!started_ && begin() != 0) || (!expander_ready_ && !initializeExpander()))
        {
            ESP_LOGE(kTag, "Failed to prepare XL9535 for external 3.3V power");
            return false;
        }
    }

    const auto& io = ioExpanderPins();
    if (!expanderPinMode(io.power_3v3, true) ||
        !expanderWriteActive(io.power_3v3, true, profile().power_3v3_active_high))
    {
        ESP_LOGE(kTag, "Failed to assert external 3.3V rail through XL9535");
        return false;
    }
    return true;
}

bool TDisplayP4Board::configureBatteryGaugeCapacity(uint16_t design_capacity_mah,
                                                    uint16_t full_charge_capacity_mah)
{
    if (design_capacity_mah == 0 || full_charge_capacity_mah == 0)
    {
        return false;
    }
    if (!initializeBatteryGauge())
    {
        return false;
    }

    const SystemI2cDeviceConfig config{
        "battery-config", profile().i2c.battery_gauge, 400000};
    ManagedSystemI2cGuard guard(*this, config, kI2cTimeoutMs);
    if (!guard)
    {
        return false;
    }

    const auto write_bytes = [&](const uint8_t* data, size_t len)
    {
        return guard.transmit(data, len, kI2cTimeoutMs) == ESP_OK;
    };
    const auto send_subcommand = [&](uint16_t command)
    {
        const uint8_t payload[] = {
            0x00,
            static_cast<uint8_t>(command & 0xFFU),
            static_cast<uint8_t>((command >> 8U) & 0xFFU),
        };
        const bool ok = write_bytes(payload, sizeof(payload));
        vTaskDelay(pdMS_TO_TICKS(10));
        return ok;
    };
    const auto read_operation_status = [&](uint16_t* out_status)
    {
        if (!out_status)
        {
            return false;
        }
        const uint8_t reg = kBatteryRegOperationStatus;
        uint8_t data[2] = {};
        if (guard.transmitReceive(&reg, 1, data, sizeof(data), kI2cTimeoutMs) != ESP_OK)
        {
            return false;
        }
        *out_status = static_cast<uint16_t>(data[0] |
                                            (static_cast<uint16_t>(data[1]) << 8U));
        return true;
    };
    const auto wait_config_update = [&](bool expected, uint32_t timeout_ms)
    {
        const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
        while (static_cast<int32_t>(deadline - xTaskGetTickCount()) > 0)
        {
            uint16_t status = 0;
            if (read_operation_status(&status) &&
                ((status & kBatteryConfigUpdateMask) != 0) == expected)
            {
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        return false;
    };
    const auto write_capacity = [&](uint16_t address, uint16_t capacity_mah)
    {
        const uint8_t address_low = static_cast<uint8_t>(address & 0xFFU);
        const uint8_t address_high = static_cast<uint8_t>((address >> 8U) & 0xFFU);
        const uint8_t capacity_high = static_cast<uint8_t>((capacity_mah >> 8U) & 0xFFU);
        const uint8_t capacity_low = static_cast<uint8_t>(capacity_mah & 0xFFU);
        const uint8_t select[] = {kBatteryRegDataClass, address_low, address_high};
        const uint8_t data[] = {kBatteryRegDataBlock, capacity_high, capacity_low};
        const uint8_t checksum = static_cast<uint8_t>(
            0xFFU - ((address_low + address_high + capacity_high + capacity_low) & 0xFFU));
        const uint8_t checksum_payload[] = {kBatteryRegDataChecksum, checksum};
        const uint8_t length_payload[] = {kBatteryRegDataLength, 0x06};
        if (!write_bytes(select, sizeof(select)))
        {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        return write_bytes(data, sizeof(data)) &&
               write_bytes(checksum_payload, sizeof(checksum_payload)) &&
               write_bytes(length_payload, sizeof(length_payload));
    };

    uint16_t operation_status = 0;
    if (!read_operation_status(&operation_status))
    {
        return false;
    }
    const uint8_t access = static_cast<uint8_t>(
        (operation_status >> kBatterySecurityAccessShift) & kBatterySecurityAccessMask);
    const bool was_sealed = access == kBatterySealedAccess;
    if (was_sealed)
    {
        const uint8_t unseal_first[] = {0x00, 0x14, 0x04};
        const uint8_t unseal_second[] = {0x00, 0x72, 0x36};
        if (!write_bytes(unseal_first, sizeof(unseal_first)))
        {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        if (!write_bytes(unseal_second, sizeof(unseal_second)))
        {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (access != kBatteryFullAccess)
    {
        const uint8_t full_access[] = {0x00, 0xFF, 0xFF};
        if (!write_bytes(full_access, sizeof(full_access)))
        {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        if (!write_bytes(full_access, sizeof(full_access)))
        {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    bool ok = send_subcommand(kBatterySubcommandEnterConfigUpdate) &&
              wait_config_update(true, 1500) &&
              write_capacity(kBatteryDesignCapacityAddress, design_capacity_mah);
    if (ok)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        ok = write_capacity(kBatteryFullChargeCapacityAddress, full_charge_capacity_mah);
    }
    if (!send_subcommand(kBatterySubcommandExitConfigUpdateReinit) ||
        !wait_config_update(false, 3000))
    {
        ok = false;
    }
    if (was_sealed && !send_subcommand(kBatterySubcommandSeal))
    {
        ok = false;
    }

    ESP_LOGI(kTag,
             "Battery gauge capacity config ok=%u design=%umAh full=%umAh",
             ok ? 1U : 0U,
             static_cast<unsigned>(design_capacity_mah),
             static_cast<unsigned>(full_charge_capacity_mah));
    return ok;
}

void TDisplayP4Board::setKeyboardReady(bool ready)
{
    keyboard_ready_ = ready && profile().supports_keyboard_module;
}

bool TDisplayP4Board::isRTCReady() const
{
    return rtc_runtime::is_valid_epoch(std::time(nullptr));
}

bool TDisplayP4Board::isCharging()
{
    if (!battery_gauge_ready_)
    {
        (void)initializeBatteryGauge();
    }

    int16_t current_ma = 0;
    if (!readBatteryGaugeWordSigned(kBatteryRegCurrent, &current_ma))
    {
        return battery_charging_;
    }

    battery_charging_ = current_ma > 0;
    return battery_charging_;
}

int TDisplayP4Board::getBatteryLevel()
{
    if (!battery_gauge_ready_)
    {
        (void)initializeBatteryGauge();
    }

    uint16_t level = 0;
    if (!readBatteryGaugeWord(kBatteryRegStateOfCharge, &level))
    {
        return last_battery_level_;
    }

    last_battery_level_ = std::clamp<int>(static_cast<int>(level), 0, 100);
    return last_battery_level_;
}

bool TDisplayP4Board::isSDReady() const
{
    return sd_ready_;
}

bool TDisplayP4Board::isCardReady()
{
    return sd_ready_;
}

bool TDisplayP4Board::isGPSReady() const
{
    return gps_runtime_prepared_ || platform::esp::idf_common::gps_runtime::is_enabled() ||
           platform::esp::idf_common::gps_runtime::is_powered();
}

void TDisplayP4Board::vibrator()
{
    if (!haptic_runtime::trigger())
    {
        ESP_LOGW(kTag, "Haptic trigger failed");
    }
}

void TDisplayP4Board::stopVibrator()
{
    haptic_runtime::stop();
}

void TDisplayP4Board::setMessageToneVolume(uint8_t volume_percent)
{
    message_tone_volume_ = volume_percent;
}

uint8_t TDisplayP4Board::getMessageToneVolume() const
{
    return message_tone_volume_;
}

bool TDisplayP4Board::lockSystemI2c(uint32_t timeout_ms,
                                    const char* owner,
                                    const char* source_file,
                                    int source_line)
{
    if (system_i2c_mutex_ == nullptr)
    {
        return false;
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms == UINT32_MAX)
    {
        timeout_ticks = portMAX_DELAY;
    }

    const TaskHandle_t requester = xTaskGetCurrentTaskHandle();
    const TickType_t wait_started = xTaskGetTickCount();
    system_i2c_waiter_since_ticks_.store(wait_started, std::memory_order_relaxed);
    system_i2c_waiter_timeout_ms_.store(timeout_ms, std::memory_order_relaxed);
    system_i2c_waiter_label_.store(owner, std::memory_order_release);
    system_i2c_waiter_file_.store(source_file, std::memory_order_release);
    system_i2c_waiter_line_.store(source_line, std::memory_order_relaxed);
    system_i2c_waiter_task_.store(requester, std::memory_order_release);

    if (xSemaphoreTake(system_i2c_mutex_, timeout_ticks) == pdTRUE)
    {
        TaskHandle_t expected_waiter = requester;
        if (system_i2c_waiter_task_.compare_exchange_strong(
                expected_waiter, nullptr, std::memory_order_acq_rel))
        {
            system_i2c_waiter_since_ticks_.store(0, std::memory_order_relaxed);
            system_i2c_waiter_timeout_ms_.store(0, std::memory_order_relaxed);
            system_i2c_waiter_label_.store(nullptr, std::memory_order_release);
            system_i2c_waiter_file_.store(nullptr, std::memory_order_release);
            system_i2c_waiter_line_.store(0, std::memory_order_relaxed);
        }
        system_i2c_owner_since_ticks_.store(xTaskGetTickCount(), std::memory_order_relaxed);
        system_i2c_owner_label_.store(owner, std::memory_order_release);
        system_i2c_owner_task_.store(requester, std::memory_order_release);
        return true;
    }

    const TickType_t now = xTaskGetTickCount();
    TickType_t last_log = system_i2c_last_timeout_log_ticks_.load(std::memory_order_relaxed);
    if ((now - last_log) >= pdMS_TO_TICKS(kI2cOwnerDiagnosticIntervalMs) &&
        system_i2c_last_timeout_log_ticks_.compare_exchange_strong(
            last_log, now, std::memory_order_relaxed))
    {
        const TaskHandle_t holder = system_i2c_owner_task_.load(std::memory_order_acquire);
        const TickType_t held_since =
            system_i2c_owner_since_ticks_.load(std::memory_order_relaxed);
        const TaskHandle_t waiter = system_i2c_waiter_task_.load(std::memory_order_acquire);
        const TickType_t waiter_since =
            system_i2c_waiter_since_ticks_.load(std::memory_order_relaxed);
        const uint32_t waiter_timeout_ms =
            system_i2c_waiter_timeout_ms_.load(std::memory_order_relaxed);
        const char* holder_label =
            system_i2c_owner_label_.load(std::memory_order_acquire);
        const char* waiter_label =
            system_i2c_waiter_label_.load(std::memory_order_acquire);
        const char* waiter_file =
            system_i2c_waiter_file_.load(std::memory_order_acquire);
        const int waiter_line =
            system_i2c_waiter_line_.load(std::memory_order_relaxed);
        const uint32_t held_ms =
            (holder != nullptr && held_since != 0)
                ? static_cast<uint32_t>((now - held_since) * portTICK_PERIOD_MS)
                : 0;
        const uint32_t waiting_ms =
            (waiter != nullptr && waiter_since != 0)
                ? static_cast<uint32_t>((now - waiter_since) * portTICK_PERIOD_MS)
                : 0;
        ESP_LOGW(kTag,
                 "SYS I2C lock timeout requester=%s request_owner=%s at=%s:%d "
                 "owner=%s owner_task=%s held_ms=%lu waiter=%s waiter_task=%s "
                 "waiting_ms=%lu waiter_timeout_ms=%lu request_timeout_ms=%lu",
                 pcTaskGetName(requester),
                 owner ? owner : "unknown",
                 source_file ? source_file : "unknown",
                 source_line,
                 holder_label ? holder_label : "unknown",
                 holder != nullptr ? pcTaskGetName(holder) : "unknown",
                 static_cast<unsigned long>(held_ms),
                 waiter_label ? waiter_label : "unknown",
                 waiter != nullptr ? pcTaskGetName(waiter) : "none",
                 static_cast<unsigned long>(waiting_ms),
                 static_cast<unsigned long>(waiter_timeout_ms),
                 static_cast<unsigned long>(timeout_ms));
        if (waiter_file != nullptr && waiter_line != 0)
        {
            ESP_LOGW(kTag,
                     "SYS I2C waiter location owner=%s at=%s:%d",
                     waiter_label ? waiter_label : "unknown",
                     waiter_file,
                     waiter_line);
        }
    }

    TaskHandle_t expected_waiter = requester;
    if (system_i2c_waiter_task_.compare_exchange_strong(
            expected_waiter, nullptr, std::memory_order_acq_rel))
    {
        system_i2c_waiter_since_ticks_.store(0, std::memory_order_relaxed);
        system_i2c_waiter_timeout_ms_.store(0, std::memory_order_relaxed);
        system_i2c_waiter_label_.store(nullptr, std::memory_order_release);
        system_i2c_waiter_file_.store(nullptr, std::memory_order_release);
        system_i2c_waiter_line_.store(0, std::memory_order_relaxed);
    }
    return false;
}

void TDisplayP4Board::unlockSystemI2c()
{
    if (system_i2c_mutex_ != nullptr)
    {
        system_i2c_owner_task_.store(nullptr, std::memory_order_release);
        system_i2c_owner_since_ticks_.store(0, std::memory_order_relaxed);
        system_i2c_owner_label_.store(nullptr, std::memory_order_release);
        xSemaphoreGive(system_i2c_mutex_);
    }
}

i2c_master_bus_handle_t TDisplayP4Board::systemI2cHandle() const
{
    return system_i2c_handle_;
}

i2c_master_bus_handle_t TDisplayP4Board::externalI2cHandle() const
{
    return external_i2c_handle_;
}

i2c_master_dev_handle_t TDisplayP4Board::addSystemI2cDevice(uint16_t address, uint32_t speed_hz) const
{
    if (system_i2c_handle_ == nullptr)
    {
        ESP_LOGW(kTag, "SYS I2C handle unavailable when adding device 0x%02X", address);
        return nullptr;
    }

    i2c_master_dev_handle_t handle = nullptr;
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = address;
    dev_cfg.scl_speed_hz = speed_hz;
    const esp_err_t err = i2c_master_bus_add_device(system_i2c_handle_, &dev_cfg, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(kTag,
                 "Failed to add SYS I2C device addr=0x%02X err=%s",
                 static_cast<unsigned>(address),
                 esp_err_to_name(err));
        return nullptr;
    }
    return handle;
}

void TDisplayP4Board::removeSystemI2cDevice(i2c_master_dev_handle_t handle) const
{
    if (handle == nullptr)
    {
        return;
    }
    const esp_err_t err = i2c_master_bus_rm_device(handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(kTag, "Failed to remove SYS I2C device: %s", esp_err_to_name(err));
    }
}

i2c_master_dev_handle_t TDisplayP4Board::getManagedSystemI2cDevice(const SystemI2cDeviceConfig& config,
                                                                   uint32_t timeout_ms)
{
    if (config.address == 0)
    {
        return nullptr;
    }

    ManagedI2cSlot* reserved_slot = nullptr;
    const TickType_t wait_started = xTaskGetTickCount();
    const bool wait_forever = timeout_ms == UINT32_MAX;
    const TickType_t timeout_ticks = timeout_ms == 0 ? 0 : pdMS_TO_TICKS(timeout_ms);
    while (reserved_slot == nullptr)
    {
        {
            std::lock_guard<std::mutex> resource_lock(resource_mutex_);
            ManagedI2cSlot* existing = findManagedI2cSlot(config.address, config.speed_hz);
            if (existing != nullptr)
            {
                return existing->handle;
            }

            bool creation_in_progress = false;
            for (const auto& slot : managed_system_i2c_)
            {
                if (slot.active && slot.creating && slot.address == config.address &&
                    slot.speed_hz == config.speed_hz)
                {
                    creation_in_progress = true;
                    break;
                }
            }

            if (!creation_in_progress)
            {
                reserved_slot = findFreeManagedI2cSlot();
                if (reserved_slot == nullptr)
                {
                    ESP_LOGW(kTag,
                             "No free managed SYS I2C slot for owner=%s addr=0x%02X",
                             config.owner ? config.owner : "unknown",
                             static_cast<unsigned>(config.address));
                    return nullptr;
                }

                reserved_slot->active = true;
                reserved_slot->creating = true;
                reserved_slot->address = config.address;
                reserved_slot->speed_hz = config.speed_hz;
                reserved_slot->handle = nullptr;
                copyOwnerTag(config.owner, reserved_slot->owner, sizeof(reserved_slot->owner));
                break;
            }
        }

        if (!wait_forever && (xTaskGetTickCount() - wait_started) >= timeout_ticks)
        {
            ESP_LOGW(kTag,
                     "Timed out waiting for managed SYS I2C device creation owner=%s addr=0x%02X timeout_ms=%lu",
                     config.owner ? config.owner : "unknown",
                     static_cast<unsigned>(config.address),
                     static_cast<unsigned long>(timeout_ms));
            return nullptr;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (!lockSystemI2c(timeout_ms, config.owner, __FILE__, __LINE__))
    {
        {
            std::lock_guard<std::mutex> resource_lock(resource_mutex_);
            *reserved_slot = ManagedI2cSlot{};
        }
        ESP_LOGW(kTag,
                 "Failed to lock SYS I2C while creating managed device owner=%s addr=0x%02X",
                 config.owner ? config.owner : "unknown",
                 static_cast<unsigned>(config.address));
        return nullptr;
    }

    i2c_master_dev_handle_t handle = addSystemI2cDevice(config.address, config.speed_hz);
    unlockSystemI2c();

    std::lock_guard<std::mutex> resource_lock(resource_mutex_);
    if (handle != nullptr)
    {
        reserved_slot->creating = false;
        reserved_slot->handle = handle;
        return handle;
    }

    *reserved_slot = ManagedI2cSlot{};
    return nullptr;
}

bool TDisplayP4Board::expanderReady() const
{
    return expander_ready_;
}

bool TDisplayP4Board::expanderPinMode(int pin, bool output)
{
    if (!expander_ready_ && !initializeExpander())
    {
        return false;
    }

    const ExpanderPinLocation location = locate_expander_pin(pin);
    if (!location.valid)
    {
        return false;
    }

    const SystemI2cDeviceConfig config{"xl9535", profile().i2c.io_expander, 400000};
    ManagedSystemI2cGuard guard(*this, config, kI2cTimeoutMs);
    if (!guard)
    {
        return false;
    }

    uint8_t* cfg_cache = (location.port == 0) ? &expander_config_port0_ : &expander_config_port1_;
    uint8_t next_config = *cfg_cache;
    if (output)
    {
        next_config = static_cast<uint8_t>(next_config & ~static_cast<uint8_t>(1u << location.bit));
    }
    else
    {
        next_config = static_cast<uint8_t>(next_config | static_cast<uint8_t>(1u << location.bit));
    }

    const uint8_t payload[2] = {
        static_cast<uint8_t>((location.port == 0) ? kExpanderRegConfig0 : kExpanderRegConfig1),
        next_config,
    };
    if (guard.transmit(payload, sizeof(payload), kI2cTimeoutMs) != ESP_OK)
    {
        expander_ready_ = false; // Re-read the physical latch after an uncertain transfer.
        return false;
    }
    *cfg_cache = next_config;
    return true;
}

bool TDisplayP4Board::expanderWrite(int pin, bool high)
{
    if (!expander_ready_ && !initializeExpander())
    {
        return false;
    }

    const ExpanderPinLocation location = locate_expander_pin(pin);
    if (!location.valid)
    {
        return false;
    }

    const SystemI2cDeviceConfig config{"xl9535", profile().i2c.io_expander, 400000};
    ManagedSystemI2cGuard guard(*this, config, kI2cTimeoutMs);
    if (!guard)
    {
        return false;
    }

    uint8_t* out_cache = (location.port == 0) ? &expander_output_port0_ : &expander_output_port1_;
    uint8_t next_output = *out_cache;
    if (high)
    {
        next_output = static_cast<uint8_t>(next_output | static_cast<uint8_t>(1u << location.bit));
    }
    else
    {
        next_output = static_cast<uint8_t>(next_output & ~static_cast<uint8_t>(1u << location.bit));
    }

    const uint8_t payload[2] = {
        static_cast<uint8_t>((location.port == 0) ? kExpanderRegOutput0 : kExpanderRegOutput1),
        next_output,
    };
    if (guard.transmit(payload, sizeof(payload), kI2cTimeoutMs) != ESP_OK)
    {
        expander_ready_ = false;
        return false;
    }
    *out_cache = next_output;
    return true;
}

bool TDisplayP4Board::expanderRead(int pin, bool* out_high) const
{
    if (out_high == nullptr || (!expander_ready_ && !const_cast<TDisplayP4Board*>(this)->initializeExpander()))
    {
        return false;
    }

    const ExpanderPinLocation location = locate_expander_pin(pin);
    if (!location.valid)
    {
        return false;
    }

    const SystemI2cDeviceConfig config{"xl9535", profile().i2c.io_expander, 400000};
    ManagedSystemI2cGuard guard(const_cast<TDisplayP4Board&>(*this), config, kI2cTimeoutMs);
    if (!guard)
    {
        return false;
    }

    const uint8_t reg = (location.port == 0) ? kExpanderRegInput0 : kExpanderRegInput1;
    uint8_t value = 0;
    if (guard.transmitReceive(&reg, 1, &value, 1, kI2cTimeoutMs) != ESP_OK)
    {
        return false;
    }

    *out_high = (value & static_cast<uint8_t>(1u << location.bit)) != 0;
    return true;
}

bool TDisplayP4Board::expanderWriteActive(int pin, bool active, bool active_high)
{
    return expanderWrite(pin, active_high ? active : !active);
}

bool TDisplayP4Board::expanderReadActive(int pin, bool* out_active, bool active_high) const
{
    bool high = false;
    if (!expanderRead(pin, &high) || out_active == nullptr)
    {
        return false;
    }
    *out_active = active_high ? high : !high;
    return true;
}

bool TDisplayP4Board::resetTouchController(uint32_t pre_delay_ms,
                                           uint32_t reset_pulse_ms,
                                           uint32_t post_delay_ms)
{
    if (!expander_ready_ && !initializeExpander())
    {
        return false;
    }

    const auto& io = ioExpanderPins();
    if (!expanderPinMode(io.touch_rst, true))
    {
        return false;
    }

    const bool reset_active_high = !profile().touch_reset_active_low;
    if (!expanderWriteActive(io.touch_rst, false, reset_active_high))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(pre_delay_ms));

    if (!expanderWriteActive(io.touch_rst, true, reset_active_high))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(reset_pulse_ms));

    if (!expanderWriteActive(io.touch_rst, false, reset_active_high))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(post_delay_ms));

    ESP_LOGI(kTag,
             "Touch reset complete pre=%lums pulse=%lums post=%lums",
             static_cast<unsigned long>(pre_delay_ms),
             static_cast<unsigned long>(reset_pulse_ms),
             static_cast<unsigned long>(post_delay_ms));
    return true;
}

bool TDisplayP4Board::isTouchInterruptActive() const
{
    bool active = false;
    if (!expanderReadActive(ioExpanderPins().touch_int, &active, false))
    {
        return false;
    }
    return active;
}

bool TDisplayP4Board::prepareGpsRuntime(uint32_t baud_rate)
{
    const uint32_t effective_baud_rate = baud_rate != 0 ? baud_rate : gpsUart().baud_rate;
    if (effective_baud_rate == 0)
    {
        ESP_LOGE(kTag, "GNSS runtime requested without a valid UART baud rate");
        return false;
    }

    if (gps_runtime_prepared_ && gps_uart_configured_ &&
        gps_uart_baud_rate_ == effective_baud_rate)
    {
        return true;
    }

    if (!ensureExternal3v3Power())
    {
        return false;
    }

    const auto& io = ioExpanderPins();
    if (!expanderPinMode(io.gps_wake, true))
    {
        return false;
    }
    if (!expanderWriteActive(io.gps_wake, true, profile().gps_wake_active_high))
    {
        return false;
    }
    if (!configureGpsUart(effective_baud_rate))
    {
        return false;
    }
    if (!configure_l76k_update_rate(static_cast<uart_port_t>(gpsUart().port), effective_baud_rate))
    {
        ESP_LOGW(kTag, "L76K update-rate initialization failed; continuing with receiver defaults");
    }

    gps_runtime_prepared_ = true;
    ESP_LOGI(kTag,
             "GNSS runtime prepared baud=%lu tx=%d rx=%d",
             static_cast<unsigned long>(effective_baud_rate),
             gpsUart().tx,
             gpsUart().rx);
    return true;
}

void TDisplayP4Board::teardownGpsRuntime()
{
    if (!gps_runtime_prepared_)
    {
        return;
    }

    teardownGpsUart();
    if (expander_ready_)
    {
        (void)expanderWriteActive(ioExpanderPins().gps_wake, false, profile().gps_wake_active_high);
    }
    gps_runtime_prepared_ = false;
    ESP_LOGI(kTag, "GNSS runtime released");
}

bool TDisplayP4Board::mountSdCard(const char* mount_point, size_t max_files)
{
    if (mount_point == nullptr || mount_point[0] == '\0')
    {
        return false;
    }

    if (sd_ready_)
    {
        return true;
    }

    if (!started_ && begin() != 0)
    {
        return false;
    }

    if (!expanderPinMode(ioExpanderPins().sd_enable, true) ||
        !expanderWriteActive(ioExpanderPins().sd_enable, true, !profile().sd_enable_active_low))
    {
        return false;
    }
    sd_enabled_ = true;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    if (!ensure_external_3v3_power_control())
    {
        return false;
    }

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = static_cast<gpio_num_t>(sdmmcPins().clk);
    slot_config.cmd = static_cast<gpio_num_t>(sdmmcPins().cmd);
    slot_config.d0 = static_cast<gpio_num_t>(sdmmcPins().d0);
    slot_config.d1 = static_cast<gpio_num_t>(sdmmcPins().d1);
    slot_config.d2 = static_cast<gpio_num_t>(sdmmcPins().d2);
    slot_config.d3 = static_cast<gpio_num_t>(sdmmcPins().d3);
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const bool mounted = platform::esp::idf_common::sd_card_runtime::mount_sdmmc(
        platform::esp::idf_common::sdmmc_host_runtime::SlotOwner::SdCard,
        host,
        slot_config,
        mount_point,
        static_cast<uint8_t>(std::min<size_t>(max_files, 255)));
    if (!mounted)
    {
        ESP_LOGW(kTag, "SD mount failed via SdFat SDMMC backend");
        return false;
    }

    sd_card_ = platform::esp::idf_common::sd_card_runtime::mounted_card();
    sd_ready_ = true;
    std::snprintf(sd_mount_point_, sizeof(sd_mount_point_), "%s", mount_point);
    if (sd_card_ != nullptr)
    {
        sdmmc_card_print_info(stdout, sd_card_);
    }
    ESP_LOGI(kTag, "SD mounted at %s", sd_mount_point_);
    return true;
}

bool TDisplayP4Board::sdCardMounted() const
{
    return sd_ready_;
}

bool TDisplayP4Board::unmountSdCard()
{
    if (!sd_ready_)
    {
        return true;
    }

    platform::esp::idf_common::sd_card_runtime::unmount_sdmmc(
        platform::esp::idf_common::sdmmc_host_runtime::SlotOwner::SdCard);

    sd_card_ = nullptr;
    sd_ready_ = false;
    ESP_LOGI(kTag, "SD unmounted from %s", sd_mount_point_);
    return true;
}

sdmmc_card_t* TDisplayP4Board::sdCard() const
{
    return sd_card_;
}

bool TDisplayP4Board::ensureRtcAccessible()
{
    if (rtc_accessible_)
    {
        return true;
    }

    const SystemI2cDeviceConfig config{"rtc-probe", profile().i2c.rtc, 400000};
    ManagedSystemI2cGuard guard(*this, config, kI2cTimeoutMs);
    if (!guard)
    {
        return false;
    }

    const uint8_t reg = 0x00;
    uint8_t value = 0;
    if (guard.transmitReceive(&reg, 1, &value, 1, kI2cTimeoutMs) != ESP_OK)
    {
        return false;
    }

    rtc_accessible_ = true;
    return true;
}

bool TDisplayP4Board::prepareLoraRuntime()
{
    if (!ensureExternal3v3Power())
    {
        ESP_LOGE(kTag, "LoRa runtime could not assert external 3.3V power");
        return false;
    }

    const auto& io = ioExpanderPins();
    if (!expanderPinMode(io.lora_dio1, false) ||
        !expanderPinMode(io.lora_rst, true) ||
        !expanderPinMode(io.lora_rf_switch, true))
    {
        return false;
    }

    // The vendor reference routes the shared RF path by driving SKY13453 high.
    return expanderWrite(io.lora_rf_switch, true);
}

bool TDisplayP4Board::setLoraResetAsserted(bool asserted)
{
    if (!prepareLoraRuntime())
    {
        return false;
    }
    return expanderWriteActive(ioExpanderPins().lora_rst,
                               asserted,
                               !profile().lora_reset_active_low);
}

bool TDisplayP4Board::readLoraDio1(bool* out_high) const
{
    return expanderRead(ioExpanderPins().lora_dio1, out_high);
}

bool TDisplayP4Board::setLoraRfSwitchTransmit(bool transmit)
{
    (void)transmit;
    if (!prepareLoraRuntime())
    {
        return false;
    }

    // Keep the SX1262 routed to the verified default RF path until we have a
    // stronger board-level contract for alternative switch states.
    return expanderWrite(ioExpanderPins().lora_rf_switch, true);
}

bool TDisplayP4Board::isRadioOnline() const
{
    return ensureRadioReady() && radio().isOnline();
}

bool TDisplayP4Board::quiesceForExternalStorage()
{
    return !radio().isOnline() || radio().standby();
}

int TDisplayP4Board::transmitRadio(const uint8_t* data, size_t len)
{
    if (!ensureRadioReady())
    {
        return -1;
    }
    const int state = radio().startTransmit(data, len);
    if (state != 0)
    {
        return state;
    }

    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = radio_tx_timeout_ticks(len);
    TickType_t last_companion_poll = started;
    while (true)
    {
        const TickType_t now = xTaskGetTickCount();
        if (static_cast<TickType_t>(now - last_companion_poll) >=
            pdMS_TO_TICKS(kCompanionPollIntervalMs))
        {
            platform::esp::idf_common::wireless_companion::c6_companion().poll();
            last_companion_poll = now;
        }

        const uint32_t irq_flags = radio().getIrqFlags();
        if ((irq_flags & kRadioIrqTxDone) != 0)
        {
            radio().clearIrqFlags(0xFFFF);
            radio().standby();
            return 0;
        }
        const TickType_t elapsed = static_cast<TickType_t>(xTaskGetTickCount() - started);
        if ((irq_flags & kRadioIrqTimeout) != 0 || elapsed > timeout)
        {
            ESP_LOGW(kTag,
                     "%s transmit timeout len=%u elapsed_ms=%lu irq=0x%04lX chip_timeout=%d",
                     platform::esp::idf_common::kSelectedRadioName,
                     static_cast<unsigned>(len),
                     static_cast<unsigned long>(elapsed * portTICK_PERIOD_MS),
                     static_cast<unsigned long>(irq_flags),
                     (irq_flags & kRadioIrqTimeout) != 0 ? 1 : 0);
            radio().clearIrqFlags(0xFFFF);
            radio().standby();
            return -5;
        }
        vTaskDelay(pdMS_TO_TICKS(kRadioTxPollIntervalMs));
    }
}

int TDisplayP4Board::startRadioReceive()
{
    if (!ensureRadioReady())
    {
        return -1;
    }
    return radio().startReceive() ? 0 : -1;
}

uint32_t TDisplayP4Board::getRadioIrqFlags()
{
    if (!ensureRadioReady())
    {
        return 0;
    }
    return radio().getIrqFlags();
}

int TDisplayP4Board::getRadioPacketLength(bool update)
{
    if (!ensureRadioReady())
    {
        return -1;
    }
    return radio().getPacketLength(update);
}

int TDisplayP4Board::readRadioData(uint8_t* buf, size_t len)
{
    if (!ensureRadioReady())
    {
        return -1;
    }
    return radio().readPacket(buf, len);
}

void TDisplayP4Board::clearRadioIrqFlags(uint32_t flags)
{
    if (!ensureRadioReady())
    {
        return;
    }
    radio().clearIrqFlags(flags);
}

float TDisplayP4Board::getRadioRSSI()
{
    if (!ensureRadioReady())
    {
        return 0.0f;
    }
    return radio().readRssi();
}

float TDisplayP4Board::getRadioSNR()
{
#if defined(TRAIL_MATE_IDF_RADIO_LR2021)
    return radio().readSnr();
#else
    return 0.0f;
#endif
}

int TDisplayP4Board::configureLoraRadio(float freq_mhz,
                                        float bw_khz,
                                        uint8_t sf,
                                        uint8_t cr_denom,
                                        int8_t tx_power,
                                        uint16_t preamble_len,
                                        uint8_t sync_word,
                                        uint8_t crc_len)
{
    if (!ensureRadioReady())
    {
        ESP_LOGE(kTag, "%s LoRa configuration skipped: radio is not ready", platform::esp::idf_common::kSelectedRadioName);
        return -1;
    }

    const bool configured = radio().configureLoRaReceive(freq_mhz,
                                                         bw_khz,
                                                         sf,
                                                         cr_denom,
                                                         tx_power,
                                                         preamble_len,
                                                         sync_word,
                                                         crc_len);
    if (!configured)
    {
        ESP_LOGE(kTag,
                 "%s LoRa configuration failed: freq=%.3f bw=%.1f sf=%u cr=4/%u sync=0x%02X err=%s",
                 platform::esp::idf_common::kSelectedRadioName,
                 freq_mhz,
                 bw_khz,
                 static_cast<unsigned>(sf),
                 static_cast<unsigned>(cr_denom),
                 static_cast<unsigned>(sync_word),
                 radio().lastError());
    }
    return configured ? 0 : -1;
}

bool TDisplayP4Board::initializeI2cBuses()
{
    if (system_i2c_handle_ != nullptr && external_i2c_handle_ != nullptr)
    {
        return true;
    }

    if (system_i2c_handle_ == nullptr)
    {
        i2c_master_bus_config_t sys_cfg = {};
        static_assert(profile().sys_i2c.port == LP_I2C_NUM_0);
        sys_cfg.lp_source_clk = LP_I2C_SCLK_DEFAULT;
        sys_cfg.sda_io_num = static_cast<gpio_num_t>(systemI2c().sda);
        sys_cfg.scl_io_num = static_cast<gpio_num_t>(systemI2c().scl);
        sys_cfg.i2c_port = systemI2c().port;
        sys_cfg.flags.enable_internal_pullup = false;
        if (i2c_new_master_bus(&sys_cfg, &system_i2c_handle_) != ESP_OK)
        {
            ESP_LOGE(kTag, "Failed to initialize SYS I2C bus");
            return false;
        }
    }

    if (external_i2c_handle_ == nullptr)
    {
        i2c_master_bus_config_t ext_cfg = {};
        ext_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        ext_cfg.sda_io_num = static_cast<gpio_num_t>(externalI2c().sda);
        ext_cfg.scl_io_num = static_cast<gpio_num_t>(externalI2c().scl);
        ext_cfg.i2c_port = externalI2c().port;
        ext_cfg.flags.enable_internal_pullup = false;
        if (i2c_new_master_bus(&ext_cfg, &external_i2c_handle_) != ESP_OK)
        {
            ESP_LOGE(kTag, "Failed to initialize EXT I2C bus");
            return false;
        }
    }

    return true;
}

bool TDisplayP4Board::initializeExpander()
{
    if (expander_ready_)
    {
        return true;
    }
    if (!initializeI2cBuses())
    {
        return false;
    }

    const SystemI2cDeviceConfig config{"xl9535", profile().i2c.io_expander, 400000};
    ManagedSystemI2cGuard guard(*this, config, kI2cTimeoutMs);
    if (!guard)
    {
        ESP_LOGE(kTag, "Failed to access XL9535 expander");
        return false;
    }

    auto read_reg = [&](uint8_t reg, uint8_t* out) -> bool
    {
        return guard.transmitReceive(&reg, 1, out, 1, kI2cTimeoutMs) == ESP_OK;
    };
    auto write_reg = [&](uint8_t reg, uint8_t value) -> bool
    {
        const uint8_t payload[2] = {reg, value};
        return guard.transmit(payload, sizeof(payload), kI2cTimeoutMs) == ESP_OK;
    };

    if (!write_reg(kExpanderRegPolarity0, 0x00) || !write_reg(kExpanderRegPolarity1, 0x00))
    {
        ESP_LOGE(kTag, "Failed to configure XL9535 polarity registers");
        return false;
    }

    if (!read_reg(kExpanderRegOutput0, &expander_output_port0_) ||
        !read_reg(kExpanderRegOutput1, &expander_output_port1_) ||
        !read_reg(kExpanderRegConfig0, &expander_config_port0_) ||
        !read_reg(kExpanderRegConfig1, &expander_config_port1_))
    {
        ESP_LOGE(kTag, "Failed to read XL9535 state; refusing to change unknown output latches");
        return false;
    }

    expander_ready_ = true;
    ESP_LOGI(kTag,
             "XL9535 ready out=(0x%02X,0x%02X) cfg=(0x%02X,0x%02X)",
             expander_output_port0_,
             expander_output_port1_,
             expander_config_port0_,
             expander_config_port1_);
    return true;
}

bool TDisplayP4Board::runColdBootPowerSequence()
{
    const auto& io = ioExpanderPins();
    const auto& p = profile();

    // Preload the output latches before enabling their drivers. In particular,
    // XL9535's power-on HIGH latch must never briefly disable the active-low
    // 3.3 V rail when IO0 changes from input to output on P4 V1.0.
    if (!expanderWriteActive(io.power_3v3, true, p.power_3v3_active_high) ||
        !expanderWriteActive(io.gps_wake, false, p.gps_wake_active_high) ||
        !expanderWriteActive(io.c6_enable, false, p.c6_enable_active_high) ||
        !expanderWriteActive(io.p4_vcca, true, p.p4_vcca_active_high) ||
        !expanderWriteActive(io.power_5v, true, p.power_5v_active_high) ||
        !expanderWriteActive(io.screen_rst, true, !p.screen_reset_active_low) ||
        !expanderWriteActive(io.touch_rst, true, !p.touch_reset_active_low) ||
        !expanderWrite(io.ethernet_rst, false))
    {
        ESP_LOGE(kTag, "Failed to preload P4 power/reset output levels");
        return false;
    }

    if (!expanderPinMode(io.screen_rst, true) ||
        !expanderPinMode(io.touch_rst, true) ||
        !expanderPinMode(io.touch_int, false) ||
        !expanderPinMode(io.p4_vcca, true) ||
        !expanderPinMode(io.power_5v, true) ||
        !expanderPinMode(io.power_3v3, true) ||
        !expanderPinMode(io.gps_wake, true) ||
        !expanderPinMode(io.c6_enable, true) ||
        !expanderPinMode(io.ethernet_rst, true))
    {
        return false;
    }

    // Keep the board rails stable. LilyGO documents that switching the P4 V1.0
    // 3.3 V rail off can leave peripherals mis-reset or lock P4 in download mode.
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!expanderWriteActive(io.screen_rst, false, !p.screen_reset_active_low) ||
        !expanderWriteActive(io.touch_rst, false, !p.touch_reset_active_low))
    {
        ESP_LOGE(kTag, "Failed to release P4 display/touch reset");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    // No Ethernet driver owns the PHY in this product. Hold it in reset until
    // an actual Ethernet owner initializes the RMII pins and clock.
    return true;
}

bool TDisplayP4Board::initializeBatteryGauge()
{
    if (battery_gauge_ready_)
    {
        return true;
    }

    uint16_t voltage_mv = 0;
    if (!readBatteryGaugeWord(kBatteryRegVoltage, &voltage_mv))
    {
        ESP_LOGW(kTag, "Battery gauge probe failed");
        return false;
    }

    battery_gauge_ready_ = true;
    ESP_LOGI(kTag, "Battery gauge ready voltage=%umV", static_cast<unsigned>(voltage_mv));
    return true;
}

bool TDisplayP4Board::configureGpsUart(uint32_t baud_rate)
{
    if (gps_uart_configured_ && gps_uart_baud_rate_ == baud_rate)
    {
        return true;
    }

    if (gps_uart_configured_)
    {
        teardownGpsUart();
    }

    const auto& uart = gpsUart();
    if (uart.port < 0 || uart.tx < 0 || uart.rx < 0 || baud_rate == 0)
    {
        return false;
    }

    uart_config_t config = {};
    config.baud_rate = static_cast<int>(baud_rate);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;

    const uart_port_t port = static_cast<uart_port_t>(uart.port);
    (void)uart_driver_delete(port);
    if (uart_param_config(port, &config) != ESP_OK ||
        uart_set_pin(port, uart.tx, uart.rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
        uart_driver_install(port, 4096, 0, 0, nullptr, 0) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to configure GNSS UART");
        return false;
    }

    gps_uart_configured_ = true;
    gps_uart_baud_rate_ = baud_rate;
    ESP_LOGI(kTag,
             "GNSS UART configured port=%d tx=%d rx=%d baud=%lu",
             uart.port,
             uart.tx,
             uart.rx,
             static_cast<unsigned long>(baud_rate));
    return true;
}

void TDisplayP4Board::teardownGpsUart()
{
    if (!gps_uart_configured_)
    {
        return;
    }

    if (gpsUart().port >= 0)
    {
        (void)uart_driver_delete(static_cast<uart_port_t>(gpsUart().port));
    }
    gps_uart_configured_ = false;
    gps_uart_baud_rate_ = 0;
}

bool TDisplayP4Board::readBatteryGaugeWord(uint8_t reg, uint16_t* out_word) const
{
    if (out_word == nullptr)
    {
        return false;
    }

    const SystemI2cDeviceConfig config{"battery", profile().i2c.battery_gauge, 400000};
    ManagedSystemI2cGuard guard(const_cast<TDisplayP4Board&>(*this), config, kI2cTimeoutMs);
    if (!guard)
    {
        return false;
    }

    uint8_t buffer[2] = {};
    if (guard.transmitReceive(&reg, 1, buffer, sizeof(buffer), kI2cTimeoutMs) != ESP_OK)
    {
        return false;
    }

    *out_word = static_cast<uint16_t>(buffer[0] | (static_cast<uint16_t>(buffer[1]) << 8));
    return true;
}

bool TDisplayP4Board::readBatteryGaugeWordSigned(uint8_t reg, int16_t* out_word) const
{
    uint16_t raw = 0;
    if (out_word == nullptr || !readBatteryGaugeWord(reg, &raw))
    {
        return false;
    }
    *out_word = static_cast<int16_t>(raw);
    return true;
}

bool TDisplayP4Board::loraReady() const
{
    return expander_ready_;
}

bool TDisplayP4Board::ensureRadioReady() const
{
    static std::mutex mutex;
    static platform::shared::radio::BoardRadioLease lease;
    const std::lock_guard<std::mutex> lock(mutex);
    return lease.ensure(radio(), [] { return static_cast<uint32_t>(esp_timer_get_time() / 1000); });
}

void TDisplayP4Board::copyOwnerTag(const char* src, char* dst, size_t dst_len)
{
    if (dst == nullptr || dst_len == 0)
    {
        return;
    }
    if (src == nullptr)
    {
        dst[0] = '\0';
        return;
    }

    const size_t copy_len = std::min(std::strlen(src), dst_len - 1);
    std::memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

TDisplayP4Board::ManagedI2cSlot* TDisplayP4Board::findManagedI2cSlot(uint16_t address,
                                                                     uint32_t speed_hz)
{
    for (auto& slot : managed_system_i2c_)
    {
        if (slot.active && slot.address == address && slot.speed_hz == speed_hz && slot.handle != nullptr)
        {
            return &slot;
        }
    }
    return nullptr;
}

const TDisplayP4Board::ManagedI2cSlot* TDisplayP4Board::findManagedI2cSlot(uint16_t address,
                                                                           uint32_t speed_hz) const
{
    for (const auto& slot : managed_system_i2c_)
    {
        if (slot.active && slot.address == address && slot.speed_hz == speed_hz && slot.handle != nullptr)
        {
            return &slot;
        }
    }
    return nullptr;
}

TDisplayP4Board::ManagedI2cSlot* TDisplayP4Board::findFreeManagedI2cSlot()
{
    for (auto& slot : managed_system_i2c_)
    {
        if (!slot.active)
        {
            return &slot;
        }
    }
    return nullptr;
}

} // namespace boards::t_display_p4

BoardBase& board = ::boards::t_display_p4::TDisplayP4Board::instance();
