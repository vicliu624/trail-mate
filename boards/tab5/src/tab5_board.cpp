#include "boards/tab5/tab5_board.h"

#include <algorithm>
#include <cstring>
#include <ctime>

#include "boards/tab5/rtc_runtime.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/esp/idf_common/gps_runtime.h"
#include "platform/esp/idf_common/sx126x_radio.h"
#include "platform/ui/device_runtime.h"

extern "C"
{
#include "bsp/trail_mate_tab5_runtime.h"

    i2c_master_bus_handle_t bsp_i2c_get_handle(void);
    i2c_master_bus_handle_t bsp_ext_i2c_get_handle(void);
    bool bsp_i2c_lock(uint32_t timeout_ms);
    void bsp_i2c_unlock(void);
    void bsp_set_ext_5v_en(bool en);
}

namespace
{

constexpr const char* kTag = "Tab5Board";

platform::esp::idf_common::Sx126xRadio& radio()
{
    return platform::esp::idf_common::Sx126xRadio::instance();
}

} // namespace

namespace boards::tab5
{

Tab5Board::SysI2cGuard::SysI2cGuard(Tab5Board& board, uint32_t timeout_ms)
    : board_(&board), locked_(board.lockSystemI2c(timeout_ms))
{
}

Tab5Board::SysI2cGuard::~SysI2cGuard()
{
    if (locked_ && board_ != nullptr)
    {
        board_->unlockSystemI2c();
    }
}

bool Tab5Board::SysI2cGuard::locked() const
{
    return locked_;
}

Tab5Board::SysI2cGuard::operator bool() const
{
    return locked_;
}

Tab5Board::ManagedSystemI2cGuard::ManagedSystemI2cGuard(Tab5Board& board,
                                                        const SystemI2cDeviceConfig& config,
                                                        uint32_t timeout_ms)
    : board_(&board)
{
    handle_ = board.getManagedSystemI2cDevice(config, timeout_ms);
    if (handle_ == nullptr)
    {
        return;
    }

    locked_ = board.lockSystemI2c(timeout_ms);
    if (!locked_)
    {
        ESP_LOGW(kTag,
                 "Failed to lock SYS I2C for managed device owner=%s addr=0x%02X",
                 config.owner ? config.owner : "unknown",
                 static_cast<unsigned>(config.address));
        handle_ = nullptr;
    }
}

Tab5Board::ManagedSystemI2cGuard::~ManagedSystemI2cGuard()
{
    if (locked_ && board_ != nullptr)
    {
        board_->unlockSystemI2c();
    }
}

bool Tab5Board::ManagedSystemI2cGuard::ok() const
{
    return locked_ && handle_ != nullptr;
}

Tab5Board::ManagedSystemI2cGuard::operator bool() const
{
    return ok();
}

i2c_master_dev_handle_t Tab5Board::ManagedSystemI2cGuard::handle() const
{
    return handle_;
}

esp_err_t Tab5Board::ManagedSystemI2cGuard::transmit(const uint8_t* data, size_t len, uint32_t timeout_ms) const
{
    if (!ok() || data == nullptr || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit(handle_, data, len, timeout_ms);
}

esp_err_t Tab5Board::ManagedSystemI2cGuard::transmitReceive(const uint8_t* tx_data, size_t tx_len,
                                                            uint8_t* rx_data, size_t rx_len,
                                                            uint32_t timeout_ms) const
{
    if (!ok() || tx_data == nullptr || tx_len == 0 || rx_data == nullptr || rx_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(handle_, tx_data, tx_len, rx_data, rx_len, timeout_ms);
}

Tab5Board& Tab5Board::instance()
{
    static Tab5Board board_instance;
    return board_instance;
}

uint32_t Tab5Board::begin(uint32_t disable_hw_init)
{
    (void)disable_hw_init;
    const auto& profile = Tab5Board::profile();
    ESP_LOGI(kTag,
             "begin name=%s short=%s ble=%s sys_i2c=(%d,%d,%d) gps_uart=(%d,%d,%d,%lu) rs485_uart=(%d,%d,%d,%d,%lu) "
             "sdmmc=(%d,%d,%d,%d,%d,%d) lora_spi=(host=%d sck=%d miso=%d mosi=%d) "
             "lora_ctrl=(nss=%d rst=%d irq=%d busy=%d pwr_en=%d) backlight=%d touch_int=%d",
             defaultLongName(),
             defaultShortName(),
             defaultBleName(),
             profile.sys_i2c.port,
             profile.sys_i2c.sda,
             profile.sys_i2c.scl,
             profile.gps_uart.port,
             profile.gps_uart.tx,
             profile.gps_uart.rx,
             static_cast<unsigned long>(profile.gps_uart.baud_rate),
             profile.rs485_uart.port,
             profile.rs485_uart.tx,
             profile.rs485_uart.rx,
             profile.rs485_uart.aux,
             static_cast<unsigned long>(profile.rs485_uart.baud_rate),
             profile.sdmmc.d0,
             profile.sdmmc.d1,
             profile.sdmmc.d2,
             profile.sdmmc.d3,
             profile.sdmmc.cmd,
             profile.sdmmc.clk,
             profile.lora_module.spi.host,
             profile.lora_module.spi.sck,
             profile.lora_module.spi.miso,
             profile.lora_module.spi.mosi,
             profile.lora_module.nss,
             profile.lora_module.rst,
             profile.lora_module.irq,
             profile.lora_module.busy,
             profile.lora_module.pwr_en,
             profile.lcd_backlight,
             profile.touch_int);
    ensureRadioReady();
    return 0;
}

void Tab5Board::wakeUp()
{
    (void)platform::esp::idf_common::bsp_runtime::wake_display();
}

void Tab5Board::handlePowerButton() {}

void Tab5Board::softwareShutdown() {}

void Tab5Board::enterScreenSleep()
{
    (void)platform::esp::idf_common::bsp_runtime::sleep_display();
}

void Tab5Board::exitScreenSleep()
{
    (void)platform::esp::idf_common::bsp_runtime::wake_display();
}

void Tab5Board::setBrightness(uint8_t level)
{
    brightness_level_ = level;
    const int percent = (DEVICE_MAX_BRIGHTNESS_LEVEL <= 0)
                            ? 100
                            : static_cast<int>((static_cast<uint32_t>(level) * 100U) /
                                               static_cast<uint32_t>(DEVICE_MAX_BRIGHTNESS_LEVEL));
    (void)platform::esp::idf_common::bsp_runtime::set_display_brightness(percent);
}

uint8_t Tab5Board::getBrightness()
{
    return brightness_level_;
}

bool Tab5Board::hasKeyboard()
{
    return false;
}

void Tab5Board::keyboardSetBrightness(uint8_t level)
{
    (void)level;
}

uint8_t Tab5Board::keyboardGetBrightness()
{
    return 0;
}

bool Tab5Board::isRTCReady() const
{
    return ::boards::tab5::rtc_runtime::is_valid_epoch(std::time(nullptr));
}

bool Tab5Board::isCharging()
{
    return platform::ui::device::battery_info().charging;
}

int Tab5Board::getBatteryLevel()
{
    const auto info = platform::ui::device::battery_info();
    return info.available ? info.level : -1;
}

bool Tab5Board::isSDReady() const
{
    return platform::esp::idf_common::bsp_runtime::sdcard_ready();
}

bool Tab5Board::isCardReady()
{
    return platform::esp::idf_common::bsp_runtime::sdcard_ready();
}

bool Tab5Board::isGPSReady() const
{
    return platform::esp::idf_common::gps_runtime::is_enabled() ||
           platform::esp::idf_common::gps_runtime::is_powered();
}

void Tab5Board::vibrator() {}

void Tab5Board::stopVibrator() {}

void Tab5Board::setMessageToneVolume(uint8_t volume_percent)
{
    message_tone_volume_ = volume_percent;
}

uint8_t Tab5Board::getMessageToneVolume() const
{
    return message_tone_volume_;
}

bool Tab5Board::lockSystemI2c(uint32_t timeout_ms)
{
    return bsp_i2c_lock(timeout_ms);
}

void Tab5Board::unlockSystemI2c()
{
    bsp_i2c_unlock();
}

i2c_master_bus_handle_t Tab5Board::systemI2cHandle() const
{
    return bsp_i2c_get_handle();
}

i2c_master_bus_handle_t Tab5Board::externalI2cHandle() const
{
    return bsp_ext_i2c_get_handle();
}

i2c_master_dev_handle_t Tab5Board::addSystemI2cDevice(uint16_t address, uint32_t speed_hz) const
{
    i2c_master_bus_handle_t bus = systemI2cHandle();
    if (bus == nullptr)
    {
        ESP_LOGW(kTag, "SYS I2C handle unavailable when adding device 0x%02X", address);
        return nullptr;
    }

    i2c_master_dev_handle_t handle = nullptr;
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = address;
    dev_cfg.scl_speed_hz = speed_hz;
    const esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(kTag,
                 "Failed to add SYS I2C device 0x%02X: %s",
                 address,
                 esp_err_to_name(err));
        return nullptr;
    }
    return handle;
}

void Tab5Board::removeSystemI2cDevice(i2c_master_dev_handle_t handle) const
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

i2c_master_dev_handle_t Tab5Board::getManagedSystemI2cDevice(const SystemI2cDeviceConfig& config,
                                                             uint32_t timeout_ms)
{
    if (config.address == 0)
    {
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        ManagedI2cSlot* existing = findManagedI2cSlot(config.address, config.speed_hz);
        if (existing != nullptr)
        {
            return existing->handle;
        }
    }

    SysI2cGuard bus_guard(*this, timeout_ms);
    if (!bus_guard)
    {
        ESP_LOGW(kTag,
                 "Failed to lock SYS I2C while creating managed device owner=%s addr=0x%02X",
                 config.owner ? config.owner : "unknown",
                 static_cast<unsigned>(config.address));
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(resource_mutex_);
    ManagedI2cSlot* existing = findManagedI2cSlot(config.address, config.speed_hz);
    if (existing != nullptr)
    {
        return existing->handle;
    }

    ManagedI2cSlot* slot = findFreeManagedI2cSlot();
    if (slot == nullptr)
    {
        ESP_LOGW(kTag,
                 "No free managed SYS I2C slot for owner=%s addr=0x%02X",
                 config.owner ? config.owner : "unknown",
                 static_cast<unsigned>(config.address));
        return nullptr;
    }

    i2c_master_dev_handle_t handle = addSystemI2cDevice(config.address, config.speed_hz);
    if (handle == nullptr)
    {
        return nullptr;
    }

    slot->active = true;
    slot->address = config.address;
    slot->speed_hz = config.speed_hz;
    slot->handle = handle;
    copyOwnerTag(config.owner, slot->owner, sizeof(slot->owner));
    logManagedResourcesLocked("i2c_register");
    return handle;
}

bool Tab5Board::setExt5vEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(resource_mutex_);
    updateExt5vStateLocked(enabled);
    return true;
}

bool Tab5Board::acquireExt5vRail(const char* owner)
{
    std::lock_guard<std::mutex> lock(resource_mutex_);
    ManagedLeaseSlot* slot = findLeaseSlot(ext5v_leases_, owner);
    if (slot == nullptr)
    {
        slot = findFreeLeaseSlot(ext5v_leases_);
        if (slot == nullptr)
        {
            ESP_LOGW(kTag, "No free Ext5V lease slot for owner=%s", owner ? owner : "unknown");
            return false;
        }
        slot->active = true;
        slot->refs = 0;
        copyOwnerTag(owner, slot->owner, sizeof(slot->owner));
    }

    ++slot->refs;
    updateExt5vStateLocked(true);
    logManagedResourcesLocked("ext5v_acquire");
    return true;
}

void Tab5Board::releaseExt5vRail(const char* owner)
{
    std::lock_guard<std::mutex> lock(resource_mutex_);
    ManagedLeaseSlot* slot = findLeaseSlot(ext5v_leases_, owner);
    if (slot == nullptr)
    {
        ESP_LOGW(kTag, "Ext5V release ignored for unknown owner=%s", owner ? owner : "unknown");
        return;
    }

    if (slot->refs > 0)
    {
        --slot->refs;
    }
    if (slot->refs == 0)
    {
        slot->active = false;
        slot->owner[0] = '\0';
    }

    bool still_needed = false;
    for (const auto& lease : ext5v_leases_)
    {
        if (lease.active && lease.refs > 0)
        {
            still_needed = true;
            break;
        }
    }

    updateExt5vStateLocked(still_needed);
    logManagedResourcesLocked("ext5v_release");
}

bool Tab5Board::isExt5vEnabled() const
{
    return ext_5v_enabled_;
}

bool Tab5Board::touchInterruptActive() const
{
    return trail_mate_tab5_touch_interrupt_active();
}

bool Tab5Board::configureGpsUart()
{
    const auto& uart = gpsUart();
    if (uart.port < 0 || uart.tx < 0 || uart.rx < 0 || uart.baud_rate == 0)
    {
        ESP_LOGW(kTag,
                 "Refusing to configure invalid GNSS UART: port=%d tx=%d rx=%d baud=%lu",
                 uart.port,
                 uart.tx,
                 uart.rx,
                 static_cast<unsigned long>(uart.baud_rate));
        return false;
    }

    if (gps_uart_configured_)
    {
        ESP_LOGI(kTag,
                 "GNSS UART already configured: port=%d tx=%d rx=%d baud=%lu",
                 uart.port,
                 uart.tx,
                 uart.rx,
                 static_cast<unsigned long>(uart.baud_rate));
        return true;
    }

    uart_config_t config = {};
    config.baud_rate = static_cast<int>(uart.baud_rate);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;

    const uart_port_t port = static_cast<uart_port_t>(uart.port);
    (void)uart_driver_delete(port);
    esp_err_t err = uart_param_config(port, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(kTag, "uart_param_config failed: %s", esp_err_to_name(err));
        return false;
    }
    err = uart_set_pin(port, uart.tx, uart.rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        ESP_LOGE(kTag, "uart_set_pin failed: %s", esp_err_to_name(err));
        return false;
    }
    err = uart_driver_install(port, 4096, 0, 0, nullptr, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(kTag, "uart_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }
    gps_uart_configured_ = true;
    ESP_LOGI(kTag,
             "GNSS UART configured: port=%d tx=%d rx=%d baud=%lu",
             uart.port,
             uart.tx,
             uart.rx,
             static_cast<unsigned long>(uart.baud_rate));
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        logManagedResourcesLocked("gps_uart_acquire");
    }
    return true;
}

void Tab5Board::teardownGpsUart()
{
    if (!gps_uart_configured_)
    {
        return;
    }

    const auto& uart = gpsUart();
    if (uart.port >= 0)
    {
        (void)uart_driver_delete(static_cast<uart_port_t>(uart.port));
    }
    gps_uart_configured_ = false;
    std::lock_guard<std::mutex> lock(resource_mutex_);
    logManagedResourcesLocked("gps_uart_release");
}

uart_port_t Tab5Board::gpsUartPort() const
{
    return static_cast<uart_port_t>(gpsUart().port);
}

bool Tab5Board::isRadioOnline() const
{
    return ensureRadioReady() && radio().isOnline();
}

int Tab5Board::transmitRadio(const uint8_t* data, size_t len)
{
    if (!ensureRadioReady())
    {
        return -1;
    }
    return radio().startTransmit(data, len);
}

int Tab5Board::startRadioReceive()
{
    if (!ensureRadioReady())
    {
        return -1;
    }
    return radio().startReceive() ? 0 : -1;
}

uint32_t Tab5Board::getRadioIrqFlags()
{
    if (!ensureRadioReady())
    {
        return 0;
    }
    return radio().getIrqFlags();
}

int Tab5Board::getRadioPacketLength(bool update)
{
    if (!ensureRadioReady())
    {
        return -1;
    }
    return radio().getPacketLength(update);
}

int Tab5Board::readRadioData(uint8_t* buf, size_t len)
{
    if (!ensureRadioReady())
    {
        return -1;
    }
    return radio().readPacket(buf, len);
}

void Tab5Board::clearRadioIrqFlags(uint32_t flags)
{
    if (!ensureRadioReady())
    {
        return;
    }
    radio().clearIrqFlags(flags);
}

float Tab5Board::getRadioRSSI()
{
    if (!ensureRadioReady())
    {
        return 0.0f;
    }
    return radio().readRssi();
}

float Tab5Board::getRadioSNR()
{
    return 0.0f;
}

void Tab5Board::configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                                   int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                                   uint8_t crc_len)
{
    if (!ensureRadioReady())
    {
        return;
    }

    (void)radio().configureLoRaReceive(freq_mhz,
                                       bw_khz,
                                       sf,
                                       cr_denom,
                                       tx_power,
                                       preamble_len,
                                       sync_word,
                                       crc_len);
}

bool Tab5Board::ensureRadioReady()
{
    static bool acquired = false;
    if (!acquired)
    {
        acquired = radio().acquire();
        if (acquired)
        {
            ESP_LOGI(kTag, "SX126x radio acquired");
        }
        else
        {
            ESP_LOGW(kTag, "SX126x radio acquire failed");
        }
    }
    return acquired;
}

void Tab5Board::copyOwnerTag(const char* src, char* dst, size_t dst_len)
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

Tab5Board::ManagedI2cSlot* Tab5Board::findManagedI2cSlot(uint16_t address, uint32_t speed_hz)
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

const Tab5Board::ManagedI2cSlot* Tab5Board::findManagedI2cSlot(uint16_t address, uint32_t speed_hz) const
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

Tab5Board::ManagedI2cSlot* Tab5Board::findFreeManagedI2cSlot()
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

Tab5Board::ManagedLeaseSlot* Tab5Board::findLeaseSlot(std::array<ManagedLeaseSlot, 6>& slots, const char* owner)
{
    if (owner == nullptr || owner[0] == '\0')
    {
        return nullptr;
    }
    for (auto& slot : slots)
    {
        if (slot.active && std::strncmp(slot.owner, owner, sizeof(slot.owner)) == 0)
        {
            return &slot;
        }
    }
    return nullptr;
}

const Tab5Board::ManagedLeaseSlot* Tab5Board::findLeaseSlot(const std::array<ManagedLeaseSlot, 6>& slots,
                                                            const char* owner) const
{
    if (owner == nullptr || owner[0] == '\0')
    {
        return nullptr;
    }
    for (const auto& slot : slots)
    {
        if (slot.active && std::strncmp(slot.owner, owner, sizeof(slot.owner)) == 0)
        {
            return &slot;
        }
    }
    return nullptr;
}

Tab5Board::ManagedLeaseSlot* Tab5Board::findFreeLeaseSlot(std::array<ManagedLeaseSlot, 6>& slots)
{
    for (auto& slot : slots)
    {
        if (!slot.active)
        {
            return &slot;
        }
    }
    return nullptr;
}

void Tab5Board::updateExt5vStateLocked(bool enabled)
{
    if (ext_5v_enabled_ == enabled)
    {
        return;
    }
    bsp_set_ext_5v_en(enabled);
    ext_5v_enabled_ = enabled;
    ESP_LOGI(kTag, "M5-Bus Ext5V %s", enabled ? "enabled" : "disabled");
}

void Tab5Board::logManagedResourcesLocked(const char* reason) const
{
    size_t i2c_count = 0;
    for (const auto& slot : managed_system_i2c_)
    {
        if (slot.active && slot.handle != nullptr)
        {
            ++i2c_count;
        }
    }

    size_t ext5v_count = 0;
    for (const auto& slot : ext5v_leases_)
    {
        if (slot.active && slot.refs > 0)
        {
            ++ext5v_count;
        }
    }

    ESP_LOGI(kTag,
             "managed resources reason=%s sys_i2c_devices=%u ext5v_leases=%u ext5v=%d gps_uart=%d",
             reason ? reason : "unknown",
             static_cast<unsigned>(i2c_count),
             static_cast<unsigned>(ext5v_count),
             ext_5v_enabled_ ? 1 : 0,
             gps_uart_configured_ ? 1 : 0);
}

} // namespace boards::tab5

BoardBase& board = ::boards::tab5::Tab5Board::instance();
