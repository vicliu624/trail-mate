#include "platform/esp/idf_common/sx126x_radio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform/esp/boards/tab5_board_profile.h"

namespace platform::esp::idf_common
{
namespace
{

constexpr const char* kTag = "idf-sx126x";
constexpr uint8_t kCmdSetStandby = 0x80;
constexpr uint8_t kCmdSetRx = 0x82;
constexpr uint8_t kCmdSetTx = 0x83;
constexpr uint8_t kCmdSetPacketType = 0x8A;
constexpr uint8_t kCmdSetRfFrequency = 0x86;
constexpr uint8_t kCmdSetTxParams = 0x8E;
constexpr uint8_t kCmdSetModulationParams = 0x8B;
constexpr uint8_t kCmdSetPacketParams = 0x8C;
constexpr uint8_t kCmdSetBufferBaseAddress = 0x8F;
constexpr uint8_t kCmdSetRegulatorMode = 0x96;
constexpr uint8_t kCmdSetPaConfig = 0x95;
constexpr uint8_t kCmdSetDioIrqParams = 0x08;
constexpr uint8_t kCmdGetIrqStatus = 0x12;
constexpr uint8_t kCmdClearIrqStatus = 0x02;
constexpr uint8_t kCmdSetDio2AsRfSwitchCtrl = 0x9D;
constexpr uint8_t kCmdGetRssiInst = 0x15;
constexpr uint8_t kCmdGetRxBufferStatus = 0x13;
constexpr uint8_t kCmdReadBuffer = 0x1E;
constexpr uint8_t kCmdWriteBuffer = 0x0E;
constexpr uint8_t kCmdCalibrateImage = 0x98;
constexpr uint8_t kCmdSetRxTxFallbackMode = 0x93;
constexpr uint8_t kCmdCalibrate = 0x89;
constexpr uint8_t kCmdReadRegister = 0x1D;
constexpr uint8_t kCmdWriteRegister = 0x0D;

constexpr uint8_t kPacketTypeGfsk = 0x00;
constexpr uint8_t kPacketTypeLoRa = 0x01;
constexpr uint8_t kStandbyRc = 0x00;
constexpr uint8_t kRegulatorDcDc = 0x01;
constexpr uint8_t kFallbackStandbyRc = 0x20;
constexpr uint8_t kPaRamp200u = 0x04;
constexpr uint8_t kPaConfigDeviceSelSx1262 = 0x00;
constexpr uint8_t kPaConfigPaLut = 0x01;
constexpr uint8_t kLoRaHeaderExplicit = 0x00;
constexpr uint8_t kLoRaCrcOff = 0x00;
constexpr uint8_t kLoRaCrcOn = 0x01;
constexpr uint8_t kLoRaIqStandard = 0x00;
constexpr uint8_t kFskFilterNone = 0x00;
constexpr uint8_t kFskPacketVariable = 0x01;
constexpr uint8_t kFskAddressFilterOff = 0x00;
constexpr uint8_t kFskWhiteningOff = 0x00;
constexpr uint8_t kFskPreambleDetectOff = 0x00;
constexpr uint8_t kFskPreambleDetect8 = 0x04;
constexpr uint8_t kFskPreambleDetect16 = 0x05;
constexpr uint8_t kFskPreambleDetect24 = 0x06;
constexpr uint8_t kFskPreambleDetect32 = 0x07;
constexpr uint8_t kFskCrcOff = 0x01;
constexpr uint8_t kFskCrc2ByteInv = 0x06;
constexpr uint32_t kRxTimeoutInf = 0xFFFFFF;
constexpr uint16_t kIrqTxDone = 0x0001;
constexpr uint16_t kIrqRxDone = 0x0002;
constexpr uint16_t kIrqPreambleDetected = 0x0004;
constexpr uint16_t kIrqSyncWordValid = 0x0008;
constexpr uint16_t kIrqHeaderValid = 0x0010;
constexpr uint16_t kIrqHeaderErr = 0x0020;
constexpr uint16_t kIrqCrcErr = 0x0040;
constexpr uint16_t kIrqTimeout = 0x0200;
constexpr uint16_t kIrqAll = 0x43FF;
constexpr uint16_t kRegOcpConfiguration = 0x08E7;
constexpr uint16_t kRegSyncWord0 = 0x06C0;
constexpr uint16_t kRegLoraSyncWordMsb = 0x0740;
constexpr uint16_t kRegCrcInitialMsb = 0x06BC;
constexpr uint16_t kRegCrcPolynomialMsb = 0x06BE;
constexpr uint16_t kRegVersionString = 0x0320;
constexpr float kFrequencyStepHz = 0.9536743164f;
constexpr uint32_t kCrystalFreqHz = 32000000UL;

const char* spi_host_name(int host)
{
    switch (static_cast<spi_host_device_t>(host))
    {
    case SPI2_HOST:
        return "SPI2_HOST";
    case SPI3_HOST:
        return "SPI3_HOST";
    default:
        return "INVALID_SPI_HOST";
    }
}

bool is_supported_spi_host(int host)
{
    return host == static_cast<int>(SPI2_HOST) || host == static_cast<int>(SPI3_HOST);
}

spi_device_handle_t device_handle(void* raw)
{
    return reinterpret_cast<spi_device_handle_t>(raw);
}

SemaphoreHandle_t radio_mutex(void* raw)
{
    return reinterpret_cast<SemaphoreHandle_t>(raw);
}

bool take_mutex(void* raw)
{
    if (!raw)
    {
        return false;
    }
    return xSemaphoreTake(radio_mutex(raw), portMAX_DELAY) == pdTRUE;
}

void give_mutex(void* raw)
{
    if (raw)
    {
        xSemaphoreGive(radio_mutex(raw));
    }
}

uint8_t map_lora_bw(float bw_khz)
{
    const float half = bw_khz / 2.0f;
    const int bw_div2 = static_cast<int>(half + 0.01f);
    switch (bw_div2)
    {
    case 3:
        return 0x00;
    case 5:
        return 0x08;
    case 7:
        return 0x01;
    case 10:
        return 0x09;
    case 15:
        return 0x02;
    case 20:
        return 0x0A;
    case 31:
        return 0x03;
    case 62:
        return 0x04;
    case 125:
        return 0x05;
    case 250:
        return 0x06;
    default:
        return 0x04;
    }
}

uint8_t map_fsk_rx_bw(float rx_bw_khz)
{
    struct Entry
    {
        float bw;
        uint8_t code;
    };
    static constexpr Entry table[] = {
        {4.8f, 0x1F},   {5.8f, 0x17},   {7.3f, 0x0F},   {9.7f, 0x1E},   {11.7f, 0x16},
        {14.6f, 0x0E},  {19.5f, 0x1D},  {23.4f, 0x15},  {29.3f, 0x0D},  {39.0f, 0x1C},
        {46.9f, 0x14},  {58.6f, 0x0C},  {78.2f, 0x1B},  {93.8f, 0x13},  {117.3f, 0x0B},
        {156.2f, 0x1A}, {187.2f, 0x12}, {234.3f, 0x0A}, {312.0f, 0x19}, {373.6f, 0x11},
        {467.0f, 0x09},
    };

    for (const auto& entry : table)
    {
        if (std::fabs(entry.bw - rx_bw_khz) <= 0.001f)
        {
            return entry.code;
        }
    }
    return 0x1A;
}

uint8_t map_preamble_detect(uint16_t preamble_len, size_t sync_bits)
{
    const size_t max_detect = std::min<size_t>(sync_bits, preamble_len);
    if (max_detect >= 32)
    {
        return kFskPreambleDetect32;
    }
    if (max_detect >= 24)
    {
        return kFskPreambleDetect24;
    }
    if (max_detect >= 16)
    {
        return kFskPreambleDetect16;
    }
    if (max_detect > 0)
    {
        return kFskPreambleDetect8;
    }
    return kFskPreambleDetectOff;
}

uint8_t map_lora_cr(uint8_t cr)
{
    if (cr < 5)
    {
        return 0x01;
    }
    if (cr > 8)
    {
        return 0x04;
    }
    return static_cast<uint8_t>(cr - 4);
}

uint8_t calc_ldro(uint8_t sf, float bw_khz)
{
    const float symbol_ms = (static_cast<float>(1UL << sf) / bw_khz);
    return symbol_ms >= 16.0f ? 0x01 : 0x00;
}

uint32_t fsk_bitrate_raw(float bit_rate_kbps)
{
    return static_cast<uint32_t>((static_cast<double>(kCrystalFreqHz) * 32.0) /
                                 (static_cast<double>(bit_rate_kbps) * 1000.0));
}

uint32_t fsk_freq_dev_raw(float freq_dev_khz)
{
    return static_cast<uint32_t>(((static_cast<double>(freq_dev_khz) * 1000.0) *
                                  static_cast<double>(1UL << 25)) /
                                 static_cast<double>(kCrystalFreqHz));
}

uint32_t rf_frequency_raw(float freq_mhz)
{
    return static_cast<uint32_t>((static_cast<double>(freq_mhz) * 1000000.0) /
                                 static_cast<double>(kFrequencyStepHz));
}

uint8_t ocp_for_60ma()
{
    return static_cast<uint8_t>(60.0f / 2.5f);
}

const auto& lora_pins()
{
    return platform::esp::boards::tab5::kBoardProfile.lora_module;
}

} // namespace

Sx126xRadio& Sx126xRadio::instance()
{
    static Sx126xRadio radio;
    return radio;
}

bool Sx126xRadio::acquire()
{
#if !defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return false;
#else
    if (!mutex_)
    {
        mutex_ = xSemaphoreCreateMutex();
        if (!mutex_)
        {
            return false;
        }
    }

    if (!take_mutex(mutex_))
    {
        return false;
    }

    const bool ok = init_locked();
    if (ok)
    {
        ++users_;
    }
    give_mutex(mutex_);
    return ok;
#endif
}

void Sx126xRadio::release()
{
    if (!take_mutex(mutex_))
    {
        return;
    }

    if (users_ > 0)
    {
        --users_;
    }
    if (users_ == 0 && online_)
    {
        const uint8_t mode = kStandbyRc;
        write_command_locked(kCmdSetStandby, &mode, 1, true);
    }
    give_mutex(mutex_);
}

bool Sx126xRadio::isOnline() const
{
    return online_;
}

bool Sx126xRadio::init_locked()
{
#if !defined(TRAIL_MATE_ESP_BOARD_TAB5)
    set_error_locked("Tab5 LoRa radio unavailable");
    return false;
#else
    if (initialized_)
    {
        return online_;
    }

    spi_bus_config_t bus_cfg{};
    bus_cfg.mosi_io_num = lora_pins().spi.mosi;
    bus_cfg.miso_io_num = lora_pins().spi.miso;
    bus_cfg.sclk_io_num = lora_pins().spi.sck;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 260;

    if (!is_supported_spi_host(lora_pins().spi.host))
    {
        ESP_LOGE(kTag,
                 "SX126x init rejected invalid SPI host=%d (%s); expected SPI2_HOST=%d or SPI3_HOST=%d",
                 lora_pins().spi.host,
                 spi_host_name(lora_pins().spi.host),
                 static_cast<int>(SPI2_HOST),
                 static_cast<int>(SPI3_HOST));
        set_error_locked("invalid spi host");
        return false;
    }

    const auto host = static_cast<spi_host_device_t>(lora_pins().spi.host);
    ESP_LOGI(kTag, "SX126x init: host=%d(%s) sck=%d miso=%d mosi=%d nss=%d rst=%d irq=%d busy=%d pwr_en=%d",
             lora_pins().spi.host,
             spi_host_name(lora_pins().spi.host),
             lora_pins().spi.sck,
             lora_pins().spi.miso,
             lora_pins().spi.mosi,
             lora_pins().nss,
             lora_pins().rst,
             lora_pins().irq,
             lora_pins().busy,
             lora_pins().pwr_en);
    esp_err_t err = spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        set_error_locked("spi_bus_initialize failed");
        return false;
    }

    if (!device_)
    {
        spi_device_interface_config_t dev_cfg{};
        dev_cfg.mode = 0;
        dev_cfg.clock_speed_hz = 4000000;
        dev_cfg.spics_io_num = lora_pins().nss;
        dev_cfg.queue_size = 1;
        dev_cfg.flags = 0;
        spi_device_handle_t handle = nullptr;
        err = spi_bus_add_device(host, &dev_cfg, &handle);
        if (err == ESP_OK)
        {
            device_ = handle;
        }
        if (err != ESP_OK)
        {
            set_error_locked("spi_bus_add_device failed");
            return false;
        }
    }

    gpio_config_t io_cfg{};
    io_cfg.pin_bit_mask = (1ULL << lora_pins().rst);
    io_cfg.mode = GPIO_MODE_OUTPUT;
    io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_cfg);

    if (lora_pins().irq >= 0)
    {
        gpio_config_t irq_cfg{};
        irq_cfg.pin_bit_mask = (1ULL << lora_pins().irq);
        irq_cfg.mode = GPIO_MODE_INPUT;
        irq_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        irq_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_config(&irq_cfg);
    }

    gpio_set_level(static_cast<gpio_num_t>(lora_pins().rst), 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(static_cast<gpio_num_t>(lora_pins().rst), 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    initialized_ = true;
    online_ = probe_locked();
    if (!online_)
    {
        set_error_locked("SX126x probe failed");
        return false;
    }

    uint8_t calibrate = 0x7F;
    write_command_locked(kCmdCalibrate, &calibrate, 1, true);

    set_packet_type_locked(kPacketTypeLoRa);
    set_buffer_base_locked(0x00, 0x00);
    const uint8_t regulator = kRegulatorDcDc;
    write_command_locked(kCmdSetRegulatorMode, &regulator, 1, true);
    const uint8_t dio2_rf_switch = 0x01;
    write_command_locked(kCmdSetDio2AsRfSwitchCtrl, &dio2_rf_switch, 1, true);
    const uint8_t fallback = kFallbackStandbyRc;
    write_command_locked(kCmdSetRxTxFallbackMode, &fallback, 1, true);
    clear_irq_locked(kIrqAll);

    const uint8_t ocp = ocp_for_60ma();
    write_register_locked(kRegOcpConfiguration, &ocp, 1);
    return true;
#endif
}

bool Sx126xRadio::probe_locked()
{
#if !defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return false;
#else
    uint8_t version[6] = {0};
    if (!read_register_locked(kRegVersionString, version, sizeof(version)))
    {
        return false;
    }
    bool all_zero = true;
    bool all_ff = true;
    for (uint8_t value : version)
    {
        all_zero = all_zero && value == 0x00;
        all_ff = all_ff && value == 0xFF;
    }
    if (all_zero || all_ff)
    {
        ESP_LOGW(kTag, "SX126x probe failed: version register returned invalid data (all_zero=%d all_ff=%d)", all_zero ? 1 : 0, all_ff ? 1 : 0);
        return false;
    }
    ESP_LOGI(kTag, "SX126x probe ok: version bytes=%02X %02X %02X %02X %02X %02X",
             version[0], version[1], version[2], version[3], version[4], version[5]);
    return true;
#endif
}

void Sx126xRadio::wait_ready_locked() const
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    if (lora_pins().busy >= 0)
    {
        const TickType_t start = xTaskGetTickCount();
        while (gpio_get_level(static_cast<gpio_num_t>(lora_pins().busy)) != 0)
        {
            if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(50))
            {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    else
    {
        esp_rom_delay_us(200);
    }
#endif
}

bool Sx126xRadio::write_command_locked(uint8_t cmd, const uint8_t* data, size_t size, bool wait)
{
#if !defined(TRAIL_MATE_ESP_BOARD_TAB5)
    (void)cmd;
    (void)data;
    (void)size;
    (void)wait;
    return false;
#else
    wait_ready_locked();

    const size_t total = 1 + size;
    uint8_t tx[260] = {0};
    if (total > sizeof(tx))
    {
        set_error_locked("command too large");
        return false;
    }
    tx[0] = cmd;
    if (data && size > 0)
    {
        std::memcpy(tx + 1, data, size);
    }

    spi_transaction_t trans{};
    trans.length = total * 8;
    trans.tx_buffer = tx;
    const esp_err_t err = spi_device_transmit(device_handle(device_), &trans);
    if (err != ESP_OK)
    {
        set_error_locked("spi write failed");
        return false;
    }

    if (wait)
    {
        wait_ready_locked();
    }
    return true;
#endif
}

bool Sx126xRadio::read_command_locked(uint8_t cmd,
                                      const uint8_t* prefix,
                                      size_t prefix_size,
                                      uint8_t* data,
                                      size_t size,
                                      bool wait)
{
#if !defined(TRAIL_MATE_ESP_BOARD_TAB5)
    (void)cmd;
    (void)prefix;
    (void)prefix_size;
    (void)data;
    (void)size;
    (void)wait;
    return false;
#else
    wait_ready_locked();

    const size_t total = 1 + prefix_size + 1 + size;
    uint8_t tx[260] = {0};
    uint8_t rx[260] = {0};
    if (total > sizeof(tx))
    {
        set_error_locked("command too large");
        return false;
    }

    tx[0] = cmd;
    if (prefix && prefix_size > 0)
    {
        std::memcpy(tx + 1, prefix, prefix_size);
    }

    spi_transaction_t trans{};
    trans.length = total * 8;
    trans.tx_buffer = tx;
    trans.rx_buffer = rx;
    const esp_err_t err = spi_device_transmit(device_handle(device_), &trans);
    if (err != ESP_OK)
    {
        set_error_locked("spi read failed");
        return false;
    }

    if (data && size > 0)
    {
        std::memcpy(data, rx + 1 + prefix_size + 1, size);
    }

    if (wait)
    {
        wait_ready_locked();
    }
    return true;
#endif
}

bool Sx126xRadio::write_register_locked(uint16_t addr, const uint8_t* data, size_t size)
{
    const uint8_t prefix[2] = {
        static_cast<uint8_t>((addr >> 8) & 0xFF),
        static_cast<uint8_t>(addr & 0xFF),
    };

    uint8_t tx[260] = {0};
    const size_t total = 1 + sizeof(prefix) + size;
    if (total > sizeof(tx))
    {
        set_error_locked("register write too large");
        return false;
    }
    tx[0] = kCmdWriteRegister;
    std::memcpy(tx + 1, prefix, sizeof(prefix));
    if (data && size > 0)
    {
        std::memcpy(tx + 1 + sizeof(prefix), data, size);
    }

    wait_ready_locked();
    spi_transaction_t trans{};
    trans.length = total * 8;
    trans.tx_buffer = tx;
    const esp_err_t err = spi_device_transmit(device_handle(device_), &trans);
    if (err != ESP_OK)
    {
        set_error_locked("register write failed");
        return false;
    }
    wait_ready_locked();
    return true;
}

bool Sx126xRadio::read_register_locked(uint16_t addr, uint8_t* data, size_t size)
{
    const uint8_t prefix[2] = {
        static_cast<uint8_t>((addr >> 8) & 0xFF),
        static_cast<uint8_t>(addr & 0xFF),
    };
    return read_command_locked(kCmdReadRegister, prefix, sizeof(prefix), data, size, true);
}

bool Sx126xRadio::set_packet_type_locked(uint8_t packet_type)
{
    if (packet_type_ == packet_type)
    {
        return true;
    }
    if (!write_command_locked(kCmdSetPacketType, &packet_type, 1, true))
    {
        return false;
    }
    packet_type_ = packet_type;
    return true;
}

bool Sx126xRadio::set_rf_frequency_locked(float freq_mhz)
{
    if (std::fabs(freq_mhz_ - freq_mhz) >= 20.0f)
    {
        uint8_t cal[2] = {0xE1, 0xE9};
        if (freq_mhz < 779.0f)
        {
            cal[0] = 0xC1;
            cal[1] = 0xC5;
        }
        else if (freq_mhz < 902.0f)
        {
            cal[0] = 0xD7;
            cal[1] = 0xDB;
        }
        write_command_locked(kCmdCalibrateImage, cal, sizeof(cal), true);
    }

    const uint32_t raw = rf_frequency_raw(freq_mhz);
    const uint8_t data[4] = {
        static_cast<uint8_t>((raw >> 24) & 0xFF),
        static_cast<uint8_t>((raw >> 16) & 0xFF),
        static_cast<uint8_t>((raw >> 8) & 0xFF),
        static_cast<uint8_t>(raw & 0xFF),
    };
    if (!write_command_locked(kCmdSetRfFrequency, data, sizeof(data), true))
    {
        return false;
    }
    freq_mhz_ = freq_mhz;
    return true;
}

bool Sx126xRadio::set_tx_power_locked(int8_t tx_power)
{
    int8_t clipped = std::max<int8_t>(-9, std::min<int8_t>(22, tx_power));
    uint8_t ocp = 0;
    read_register_locked(kRegOcpConfiguration, &ocp, 1);
    const uint8_t pa_config[4] = {0x04, 0x07, kPaConfigDeviceSelSx1262, kPaConfigPaLut};
    if (!write_command_locked(kCmdSetPaConfig, pa_config, sizeof(pa_config), true))
    {
        return false;
    }
    const uint8_t tx_params[2] = {static_cast<uint8_t>(clipped), kPaRamp200u};
    const bool ok = write_command_locked(kCmdSetTxParams, tx_params, sizeof(tx_params), true);
    write_register_locked(kRegOcpConfiguration, &ocp, 1);
    return ok;
}

bool Sx126xRadio::set_dio_irq_params_locked(uint16_t irq_mask, uint16_t dio1_mask)
{
    const uint8_t data[8] = {
        static_cast<uint8_t>((irq_mask >> 8) & 0xFF),
        static_cast<uint8_t>(irq_mask & 0xFF),
        static_cast<uint8_t>((dio1_mask >> 8) & 0xFF),
        static_cast<uint8_t>(dio1_mask & 0xFF),
        0x00,
        0x00,
        0x00,
        0x00,
    };
    return write_command_locked(kCmdSetDioIrqParams, data, sizeof(data), true);
}

bool Sx126xRadio::clear_irq_locked(uint16_t flags)
{
    const uint8_t data[2] = {
        static_cast<uint8_t>((flags >> 8) & 0xFF),
        static_cast<uint8_t>(flags & 0xFF),
    };
    return write_command_locked(kCmdClearIrqStatus, data, sizeof(data), true);
}

bool Sx126xRadio::set_buffer_base_locked(uint8_t tx_base, uint8_t rx_base)
{
    const uint8_t data[2] = {tx_base, rx_base};
    return write_command_locked(kCmdSetBufferBaseAddress, data, sizeof(data), true);
}

bool Sx126xRadio::set_rx_locked(uint32_t timeout_raw)
{
    const uint8_t data[3] = {
        static_cast<uint8_t>((timeout_raw >> 16) & 0xFF),
        static_cast<uint8_t>((timeout_raw >> 8) & 0xFF),
        static_cast<uint8_t>(timeout_raw & 0xFF),
    };
    return write_command_locked(kCmdSetRx, data, sizeof(data), true);
}

bool Sx126xRadio::set_tx_locked(uint32_t timeout_raw)
{
    const uint8_t data[3] = {
        static_cast<uint8_t>((timeout_raw >> 16) & 0xFF),
        static_cast<uint8_t>((timeout_raw >> 8) & 0xFF),
        static_cast<uint8_t>(timeout_raw & 0xFF),
    };
    return write_command_locked(kCmdSetTx, data, sizeof(data), true);
}

bool Sx126xRadio::configure_lora_locked(float freq_mhz,
                                        float bw_khz,
                                        uint8_t sf,
                                        uint8_t cr,
                                        int8_t tx_power,
                                        uint16_t preamble_len,
                                        uint8_t sync_word,
                                        uint8_t crc_len)
{
    if (!set_packet_type_locked(kPacketTypeLoRa))
    {
        return false;
    }
    const uint8_t standby_mode = kStandbyRc;
    if (!write_command_locked(kCmdSetStandby, &standby_mode, 1, true))
    {
        return false;
    }
    if (!set_rf_frequency_locked(freq_mhz) || !set_tx_power_locked(tx_power))
    {
        return false;
    }

    const uint8_t mod[4] = {sf, map_lora_bw(bw_khz), map_lora_cr(cr), calc_ldro(sf, bw_khz)};
    if (!write_command_locked(kCmdSetModulationParams, mod, sizeof(mod), true))
    {
        return false;
    }

    const uint8_t packet[6] = {
        static_cast<uint8_t>((preamble_len >> 8) & 0xFF),
        static_cast<uint8_t>(preamble_len & 0xFF),
        kLoRaHeaderExplicit,
        0xFF,
        crc_len ? kLoRaCrcOn : kLoRaCrcOff,
        kLoRaIqStandard,
    };
    if (!write_command_locked(kCmdSetPacketParams, packet, sizeof(packet), true))
    {
        return false;
    }

    const uint8_t sync[2] = {
        static_cast<uint8_t>((sync_word & 0xF0) | 0x04),
        static_cast<uint8_t>(((sync_word & 0x0F) << 4) | 0x04),
    };
    if (!write_register_locked(kRegLoraSyncWordMsb, sync, sizeof(sync)))
    {
        return false;
    }

    return true;
}

bool Sx126xRadio::configure_fsk_locked(float freq_mhz,
                                       int8_t tx_power,
                                       float bit_rate_kbps,
                                       float freq_dev_khz,
                                       float rx_bw_khz,
                                       uint16_t preamble_len,
                                       const uint8_t* sync_word,
                                       size_t sync_word_len,
                                       uint8_t crc_len)
{
    if (!set_packet_type_locked(kPacketTypeGfsk))
    {
        return false;
    }
    const uint8_t standby_mode = kStandbyRc;
    if (!write_command_locked(kCmdSetStandby, &standby_mode, 1, true))
    {
        return false;
    }
    if (!set_rf_frequency_locked(freq_mhz) || !set_tx_power_locked(tx_power))
    {
        return false;
    }

    const uint32_t br_raw = fsk_bitrate_raw(bit_rate_kbps);
    const uint32_t fd_raw = fsk_freq_dev_raw(freq_dev_khz);
    const uint8_t mod[8] = {
        static_cast<uint8_t>((br_raw >> 16) & 0xFF),
        static_cast<uint8_t>((br_raw >> 8) & 0xFF),
        static_cast<uint8_t>(br_raw & 0xFF),
        kFskFilterNone,
        map_fsk_rx_bw(rx_bw_khz),
        static_cast<uint8_t>((fd_raw >> 16) & 0xFF),
        static_cast<uint8_t>((fd_raw >> 8) & 0xFF),
        static_cast<uint8_t>(fd_raw & 0xFF),
    };
    if (!write_command_locked(kCmdSetModulationParams, mod, sizeof(mod), true))
    {
        return false;
    }

    const uint8_t sync_bits = static_cast<uint8_t>(sync_word_len * 8U);
    const uint8_t packet[9] = {
        static_cast<uint8_t>((preamble_len >> 8) & 0xFF),
        static_cast<uint8_t>(preamble_len & 0xFF),
        map_preamble_detect(preamble_len, sync_bits),
        sync_bits,
        kFskAddressFilterOff,
        kFskPacketVariable,
        0xFF,
        crc_len ? kFskCrc2ByteInv : kFskCrcOff,
        kFskWhiteningOff,
    };
    if (!write_command_locked(kCmdSetPacketParams, packet, sizeof(packet), true))
    {
        return false;
    }

    if (sync_word && sync_word_len > 0)
    {
        if (!write_register_locked(kRegSyncWord0, sync_word, sync_word_len))
        {
            return false;
        }
    }

    if (crc_len)
    {
        const uint8_t init[2] = {0x1D, 0x0F};
        const uint8_t poly[2] = {0x10, 0x21};
        write_register_locked(kRegCrcInitialMsb, init, sizeof(init));
        write_register_locked(kRegCrcPolynomialMsb, poly, sizeof(poly));
    }

    return true;
}

bool Sx126xRadio::configureLoRaReceive(float freq_mhz,
                                       float bw_khz,
                                       uint8_t sf,
                                       uint8_t cr,
                                       int8_t tx_power,
                                       uint16_t preamble_len,
                                       uint8_t sync_word,
                                       uint8_t crc_len)
{
    if (!take_mutex(mutex_))
    {
        return false;
    }
    const bool ok = init_locked() &&
                    configure_lora_locked(freq_mhz, bw_khz, sf, cr, tx_power, preamble_len, sync_word, crc_len) &&
                    set_dio_irq_params_locked(kIrqRxDone | kIrqTimeout | kIrqCrcErr | kIrqHeaderErr, kIrqRxDone) &&
                    clear_irq_locked(kIrqAll) &&
                    set_buffer_base_locked(0x00, 0x00) &&
                    set_rx_locked(kRxTimeoutInf);
    give_mutex(mutex_);
    return ok;
}

bool Sx126xRadio::configureFsk(float freq_mhz,
                               int8_t tx_power,
                               float bit_rate_kbps,
                               float freq_dev_khz,
                               float rx_bw_khz,
                               uint16_t preamble_len,
                               const uint8_t* sync_word,
                               size_t sync_word_len,
                               uint8_t crc_len)
{
    if (!take_mutex(mutex_))
    {
        return false;
    }
    const bool ok = init_locked() &&
                    configure_fsk_locked(freq_mhz,
                                         tx_power,
                                         bit_rate_kbps,
                                         freq_dev_khz,
                                         rx_bw_khz,
                                         preamble_len,
                                         sync_word,
                                         sync_word_len,
                                         crc_len);
    give_mutex(mutex_);
    return ok;
}

bool Sx126xRadio::startReceive()
{
    if (!take_mutex(mutex_))
    {
        return false;
    }
    const bool ok = set_dio_irq_params_locked(kIrqRxDone | kIrqTimeout | kIrqCrcErr, kIrqRxDone) &&
                    clear_irq_locked(kIrqAll) &&
                    set_buffer_base_locked(0x00, 0x00) &&
                    set_rx_locked(kRxTimeoutInf);
    give_mutex(mutex_);
    return ok;
}

void Sx126xRadio::standby()
{
    if (!take_mutex(mutex_))
    {
        return;
    }
    const uint8_t mode = kStandbyRc;
    write_command_locked(kCmdSetStandby, &mode, 1, true);
    give_mutex(mutex_);
}

float Sx126xRadio::readRssi()
{
    if (!take_mutex(mutex_))
    {
        return NAN;
    }
    uint8_t raw = 0;
    const bool ok = read_command_locked(kCmdGetRssiInst, nullptr, 0, &raw, 1, true);
    give_mutex(mutex_);
    return ok ? (static_cast<float>(raw) / -2.0f) : NAN;
}

int Sx126xRadio::startTransmit(const uint8_t* data, size_t size)
{
    if (!take_mutex(mutex_))
    {
        return -1;
    }

    uint8_t packet[9] = {0};
    bool ok = set_buffer_base_locked(0x00, 0x00);
    if (packet_type_ == kPacketTypeLoRa)
    {
        const uint8_t lo[6] = {0x00, 0x08, kLoRaHeaderExplicit, static_cast<uint8_t>(size), kLoRaCrcOn, kLoRaIqStandard};
        ok = ok && write_command_locked(kCmdSetPacketParams, lo, sizeof(lo), true);
    }
    else
    {
        packet[0] = 0x00;
        packet[1] = 0x10;
        packet[2] = kFskPreambleDetect16;
        packet[3] = 16;
        packet[4] = kFskAddressFilterOff;
        packet[5] = kFskPacketVariable;
        packet[6] = static_cast<uint8_t>(size);
        packet[7] = kFskCrc2ByteInv;
        packet[8] = kFskWhiteningOff;
        ok = ok && write_command_locked(kCmdSetPacketParams, packet, sizeof(packet), true);
    }

    if (ok)
    {
        uint8_t tx[260] = {0};
        if (size + 2 > sizeof(tx))
        {
            ok = false;
            set_error_locked("payload too large");
        }
        else
        {
            tx[0] = kCmdWriteBuffer;
            tx[1] = 0x00;
            std::memcpy(tx + 2, data, size);
            spi_transaction_t trans{};
            trans.length = (size + 2) * 8;
            trans.tx_buffer = tx;
            const esp_err_t err = spi_device_transmit(device_handle(device_), &trans);
            ok = err == ESP_OK;
            if (!ok)
            {
                set_error_locked("write buffer failed");
            }
        }
    }

    ok = ok && set_dio_irq_params_locked(kIrqTxDone | kIrqTimeout, kIrqTxDone) && clear_irq_locked(kIrqAll) &&
         set_tx_locked(0x000000);

    give_mutex(mutex_);
    return ok ? 0 : -1;
}

uint32_t Sx126xRadio::getIrqFlags()
{
    if (!take_mutex(mutex_))
    {
        return 0;
    }
    uint8_t irq[2] = {0};
    const bool ok = read_command_locked(kCmdGetIrqStatus, nullptr, 0, irq, sizeof(irq), true);
    give_mutex(mutex_);
    if (!ok)
    {
        return 0;
    }
    return (static_cast<uint32_t>(irq[0]) << 8) | irq[1];
}

void Sx126xRadio::clearIrqFlags(uint32_t flags)
{
    if (!take_mutex(mutex_))
    {
        return;
    }
    clear_irq_locked(static_cast<uint16_t>(flags));
    give_mutex(mutex_);
}

int Sx126xRadio::getPacketLength(bool update)
{
    (void)update;
    if (!take_mutex(mutex_))
    {
        return -1;
    }
    uint8_t status[2] = {0};
    const bool ok = read_command_locked(kCmdGetRxBufferStatus, nullptr, 0, status, sizeof(status), true);
    if (ok)
    {
        last_rx_offset_ = status[1];
    }
    give_mutex(mutex_);
    return ok ? static_cast<int>(status[0]) : -1;
}

int Sx126xRadio::readPacket(uint8_t* buffer, size_t size)
{
    if (!take_mutex(mutex_))
    {
        return -1;
    }

    uint8_t status[2] = {0};
    bool ok = read_command_locked(kCmdGetRxBufferStatus, nullptr, 0, status, sizeof(status), true);
    if (!ok)
    {
        give_mutex(mutex_);
        return -1;
    }
    last_rx_offset_ = status[1];
    const size_t packet_length = std::min<size_t>(status[0], size);
    ok = read_command_locked(kCmdReadBuffer, &last_rx_offset_, 1, buffer, packet_length, true);
    give_mutex(mutex_);
    return ok ? 0 : -1;
}

const char* Sx126xRadio::lastError() const
{
    return last_error_;
}

void Sx126xRadio::set_error_locked(const char* error)
{
    if (!error)
    {
        last_error_[0] = '\0';
        return;
    }
    std::snprintf(last_error_, sizeof(last_error_), "%s", error);
    ESP_LOGW(kTag, "%s", last_error_);
}

} // namespace platform::esp::idf_common