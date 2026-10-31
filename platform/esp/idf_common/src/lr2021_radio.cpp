#include "platform/esp/idf_common/lr2021_radio.h"
#include "boards/t_display_p4/t_display_p4_board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lr2021_iq.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/idf_common/lr2021_irq_mapping.h"
#include "platform/esp/idf_common/p4_fm_mode_handoff.h"
#include "platform/esp/idf_common/p4_iq_wait.h"
#include "platform/radio/radio_power.h"
#include "platform/shared/radio/fm_lr2021_stream.h"
#include "platform/shared/radio/lora_receive_config.h"
#include <RadioLib.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

namespace platform::esp::idf_common
{
namespace
{
constexpr uint32_t kResetPin = 0xfffffffeu;
constexpr size_t kTransferBytes = 1024;
constexpr int64_t kBusyTimeoutUs = 200000;
using Board = ::boards::t_display_p4::TDisplayP4Board;
// One singleton/lock owns this internal DMA storage. RadioLib's default heap
// buffers are retained; STATIC_ONLY would put large SPI arrays on task stacks.
struct SpiBuffers
{
    uint8_t tx[kTransferBytes];
    uint8_t rx[kTransferBytes];
};
DMA_ATTR SpiBuffers s_spi_buffers;

class P4Hal final : public RadioLibHal
{
  public:
    P4Hal() : RadioLibHal(0, 1, 0, 1, 1, 2) {}
    void init() override
    {
        if (device_) return;
        error_.store(nullptr);
        busy_since_ = 0;
        if (!Board::instance().prepareLoraRuntime())
        {
            fail("LR2021 board power/reset setup failed");
            return;
        }
        const auto& pins = Board::loraModulePins();
        if (pins.spi.host != SPI2_HOST)
        {
            fail("LR2021 P4 SPI host is invalid");
            return;
        }
        spi_bus_config_t bus{};
        bus.mosi_io_num = pins.spi.mosi;
        bus.miso_io_num = pins.spi.miso;
        bus.sclk_io_num = pins.spi.sck;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = kTransferBytes;
        const esp_err_t result = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
        if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
        {
            fail("LR2021 SPI bus initialization failed");
            return;
        }
        owns_bus_ = result == ESP_OK;
        spi_device_interface_config_t config{};
        config.clock_speed_hz = 10000000;
        config.spics_io_num = -1; // Module owns CS around complete command frames.
        config.queue_size = 1;
        if (spi_bus_add_device(SPI2_HOST, &config, &device_) != ESP_OK)
        {
            fail("LR2021 SPI device initialization failed");
            term();
        }
    }
    void term() override
    {
        spiEndTransaction();
        if (device_)
        {
            spi_bus_remove_device(device_);
            device_ = nullptr;
        }
        if (owns_bus_)
        {
            spi_bus_free(SPI2_HOST);
            owns_bus_ = false;
        }
    }
    void pinMode(uint32_t pin, uint32_t mode) override
    {
        if (pin == RADIOLIB_NC || pin == kResetPin) return;
        if (gpio_set_direction(static_cast<gpio_num_t>(pin), mode == GpioModeOutput ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT) != ESP_OK)
            fail("LR2021 GPIO configuration failed");
    }
    void digitalWrite(uint32_t pin, uint32_t value) override
    {
        if (pin == RADIOLIB_NC) return;
        if (pin == static_cast<uint32_t>(Board::loraModulePins().nss) && value == GpioLevelLow && !bus_acquired_) return;
        if (pin == kResetPin)
        {
            if (!Board::instance().setLoraResetAsserted(value == GpioLevelLow)) fail("LR2021 reset expander failed");
        }
        else if (gpio_set_level(static_cast<gpio_num_t>(pin), value == GpioLevelHigh) != ESP_OK)
            fail("LR2021 GPIO write failed");
    }
    uint32_t digitalRead(uint32_t pin) override
    {
        if (pin == RADIOLIB_NC || !healthy()) return 0;
        const bool high = gpio_get_level(static_cast<gpio_num_t>(pin));
        if (pin == static_cast<uint32_t>(Board::loraModulePins().busy))
        {
            if (!high) busy_since_ = 0;
            else
            {
                const auto now = esp_timer_get_time();
                if (!busy_since_) busy_since_ = now;
                if (now - busy_since_ >= kBusyTimeoutUs)
                {
                    // Also terminates RadioLib calibration's unbounded BUSY
                    // loop. Every enclosing operation checks HAL health.
                    fail("LR2021 BUSY timed out");
                    return 0;
                }
            }
        }
        return high ? GpioLevelHigh : GpioLevelLow;
    }
    void attachInterrupt(uint32_t, void (*)(void), uint32_t) override {}
    void detachInterrupt(uint32_t) override {}
    void delay(RadioLibTime_t ms) override
    {
        if (ms) vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1);
    }
    void delayMicroseconds(RadioLibTime_t us) override { esp_rom_delay_us(us); }
    RadioLibTime_t millis() override { return esp_timer_get_time() / 1000; }
    RadioLibTime_t micros() override { return esp_timer_get_time(); }
    long pulseIn(uint32_t, uint32_t, RadioLibTime_t) override { return 0; }
    void yield() override { vTaskDelay(1); }
    void spiBegin() override {}
    void spiEnd() override {}
    void spiBeginTransaction() override
    {
        if (!healthy() || !device_) return;
        if (spi_device_acquire_bus(device_, pdMS_TO_TICKS(250)) != ESP_OK) fail("LR2021 SPI bus acquisition timed out");
        else bus_acquired_ = true;
    }
    void spiEndTransaction() override
    {
        if (bus_acquired_)
        {
            spi_device_release_bus(device_);
            bus_acquired_ = false;
        }
    }
    void spiTransfer(uint8_t* output, size_t size, uint8_t* input) override
    {
        if (input) std::memset(input, 0, size);
        if (!healthy()) return;
        if (!device_ || !bus_acquired_ || size > kTransferBytes)
        {
            fail("LR2021 invalid SPI frame");
            return;
        }
        if (output) std::memcpy(s_spi_buffers.tx, output, size);
        else std::memset(s_spi_buffers.tx, 0, size);
        spi_transaction_t transaction{};
        transaction.length = size * 8;
        transaction.tx_buffer = s_spi_buffers.tx;
        transaction.rx_buffer = s_spi_buffers.rx;
        if (spi_device_polling_transmit(device_, &transaction) != ESP_OK)
        {
            fail("LR2021 SPI transfer failed");
            return;
        }
        if (input) std::memcpy(input, s_spi_buffers.rx, size);
    }
    bool healthy() const { return error_.load() == nullptr; }
    const char* error() const { return error_.load(); }

  private:
    void fail(const char* message)
    {
        const char* empty = nullptr;
        error_.compare_exchange_strong(empty, message);
    }
    spi_device_handle_t device_ = nullptr;
    bool owns_bus_ = false;
    bool bus_acquired_ = false;
    int64_t busy_since_ = 0;
    std::atomic<const char*> error_{nullptr};
};

struct LoRaConfig
{
    float freq_mhz = 433.175f;
    float bw_khz = 250;
    uint8_t sf = 8, cr = 5;
    int8_t tx_power_dbm = 17;
    uint16_t preamble_len = 16;
    uint8_t sync_word = 0x12, crc_len = 2;
};

struct State
{
    std::mutex mutex;
    P4Hal hal;
    Module module{&hal, static_cast<uint32_t>(Board::loraModulePins().nss), RADIOLIB_NC,
                  kResetPin, static_cast<uint32_t>(Board::loraModulePins().busy)};
    LR2021 radio{&module};
    platform::shared::radio::LoRaReceiveConfig<LoRaConfig> lora_config;
    LoRaConfig applied{}, requested{};
    detail::GfskConfig fsk_profile{}, requested_fsk{};
    bool fsk_profile_valid = false;
    uint8_t sync[8]{};
    unsigned users = 0;
    bool fsk = false, transmitting = false, receiving = false;
    lr2021_iq_result iq_result{};
    // Singleton-owned scratch; the HAL copies through its internal DMA buffers.
    uint8_t iq_fifo_tx[258]{}, iq_fifo_rx[258]{};
    P4IqWait iq_wait;
    bool iq_wait_failed = false;
    std::atomic<bool> online{false};
    std::atomic<const char*> error{nullptr};

    State() { radio.irqDioNum = 11; }
    bool fail(const char* operation)
    {
        error.store(operation);
        return false;
    }
    bool check(int code, const char* operation)
    {
        if (code == RADIOLIB_ERR_NONE && hal.healthy())
        {
            error.store(nullptr);
            return true;
        }
        online.store(false);
        transmitting = false;
        receiving = false;
        lora_config.invalidate();
        // A lost SPI/status response may follow an accepted TX command.
        // Hold reset on failure rather than merely changing software state.
        (void)Board::instance().setLoraResetAsserted(true);
        error.store(hal.healthy() ? operation : hal.error());
        ESP_LOGE("idf-lr2021", "%s code=%d", error.load(), code);
        return false;
    }
    bool rfSwitch()
    {
        if (!check(radio.setRegulatorDCDC(), "LR2021 regulator setup failed")) return false;
        // P4 V1 LR2021 module: DIO6 RX-HF, DIO7 TX-HF, DIO8 LF path,
        // DIO10 HF path. Use checked writes; setRfSwitchTable discards errors.
        static constexpr uint8_t config[][2] = {{6, 8}, {7, 16}, {8, 6}, {10, 24}};
        for (const auto& item : config)
        {
            const uint8_t function[] = {item[0], RADIOLIB_LR2021_DIO_FUNCTION_RF_SWITCH | RADIOLIB_LR2021_DIO_SLEEP_PULL_NONE};
            if (!check(module.SPIwriteStream(RADIOLIB_LR2021_CMD_SET_DIO_FUNCTION, function, sizeof(function)), "LR2021 RF DIO setup failed") ||
                !check(module.SPIwriteStream(RADIOLIB_LR2021_CMD_SET_DIO_RF_SWITCH_CONFIG, item, sizeof(item)), "LR2021 RF path setup failed")) return false;
        }
        return true;
    }
    bool receive()
    {
        if (!online.load()) return false;
        if (!Board::instance().setLoraRfSwitchTransmit(false)) return check(-1, "LR2021 antenna RX path failed");
        if (transmitting && !check(radio.finishTransmit(), "LR2021 finish TX failed")) return false;
        transmitting = false;
        receiving = check(radio.startReceive(), "LR2021 start RX failed");
        return receiving;
    }
    bool beginLoRa(const LoRaConfig& config)
    {
        lora_config.invalidate();
        receiving = false;
        return check(radio.begin(config.freq_mhz, config.bw_khz, config.sf, config.cr, config.sync_word,
                                 config.tx_power_dbm, config.preamble_len, 3.0f),
                     "LR2021 LoRa initialization failed") &&
               rfSwitch();
    }
    bool beginFsk(const detail::GfskConfig& config)
    {
        lora_config.invalidate();
        transmitting = receiving = false;
        if (!config.sync_size || config.sync_size > sizeof(sync))
            return check(-1, "Invalid saved LR2021 FSK sync word");
        std::memcpy(sync, config.sync.data(), config.sync_size);
        if (!check(radio.beginGFSK(config.frequency, config.bitrate, config.deviation, config.bandwidth,
                                   config.power, config.preamble, 3.0f),
                   "LR2021 FSK initialization failed") ||
            !rfSwitch() || !check(radio.setSyncWord(sync, config.sync_size), "LR2021 FSK sync word failed") ||
            !check(radio.setCRC(config.crc, 0x1D0F, 0x1021, true), "LR2021 FSK CRC failed") ||
            !check(radio.setWhitening(false), "LR2021 FSK whitening failed")) return false;
        fsk = true;
        return true;
    }
    bool prepareFmPacketMode()
    {
        if (!beginLoRa(applied) ||
            !Board::instance().setLoraRfSwitchTransmit(false))
            return check(-1, "FM LoRa staging failed");
        if (!lora_config.apply(radio, applied, [this](int code, const char* operation)
                               { return check(code, operation); })) return false;
        fsk = false;
        receiving = true;
        return true;
    }
    bool restoreFsk(bool was_receiving)
    {
        // Recover the original packet mode even after staging/capture failure.
        hal.term();
        hal.init();
        if (!hal.healthy() || !beginFsk(fsk_profile))
            return check(-1, "FM FSK restoration failed");
        online.store(true);
        if (was_receiving) return receive();
        return check(radio.standby(), "FM FSK standby restoration failed");
    }
    // LR2021 reads are two frames: command/arguments, then status + payload
    // without a command word. Use Module's public checked transport; do not
    // expose RadioLib private APIs or enable GODMODE.
    int iqFifoRead(uint8_t* data, size_t bytes)
    {
        if (!data || !bytes || bytes > 256) return LR2021_IQ_ARGUMENT;
        std::memset(iq_fifo_tx, 0, sizeof(iq_fifo_tx));
        iq_fifo_tx[1] = 1;
        // ReadRxFifo is one CS frame: command/status bytes precede payload.
        hal.spiBeginTransaction();
        hal.digitalWrite(static_cast<uint32_t>(Board::loraModulePins().nss), hal.GpioLevelLow);
        hal.spiTransfer(iq_fifo_tx, bytes + 2, iq_fifo_rx);
        hal.digitalWrite(static_cast<uint32_t>(Board::loraModulePins().nss), hal.GpioLevelHigh);
        hal.spiEndTransaction();
        if (!hal.healthy()) return LR2021_IQ_IO;
        const unsigned status = static_cast<unsigned>(iq_fifo_rx[0]) << 8 | iq_fifo_rx[1];
        if (!status || (status & 0xf008) || ((status >> 9) & 7) < 2 || ((status >> 9) & 7) > 3 ||
            ((status >> 4) & 7) > 2 || (status & 7) > 6) return LR2021_IQ_DATA;
        std::memcpy(data, iq_fifo_rx + 2, bytes);
        return 0;
    }
    int iqRead(uint16_t command, uint8_t* data, size_t size,
               const uint8_t* arguments = nullptr, size_t argument_size = 0)
    {
        int code = module.SPIwriteStream(command, arguments, argument_size, true, false);
        if (code || !hal.healthy()) return code ? code : LR2021_IQ_IO;
        const auto width = module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD];
        module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = Module::BITS_0;
        code = module.SPIreadStream(0, data, size, true, false);
        module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = width;
        return hal.healthy() ? code : LR2021_IQ_IO;
    }
    int iqWrite(uint16_t command, const uint8_t* data, size_t size)
    {
        const int code = module.SPIwriteStream(command, data, size, true, true);
        return hal.healthy() ? code : LR2021_IQ_IO;
    }
    int iqRead32(uint32_t address, uint32_t* value)
    {
        const uint8_t request[] = {static_cast<uint8_t>(address >> 16), static_cast<uint8_t>(address >> 8),
                                   static_cast<uint8_t>(address), 1};
        uint8_t reply[4]{};
        const int code = iqRead(RADIOLIB_LR2021_CMD_READ_REG_MEM_32, reply, sizeof(reply), request, sizeof(request));
        if (!code) *value = static_cast<uint32_t>(reply[0]) << 24 | static_cast<uint32_t>(reply[1]) << 16 |
                            static_cast<uint32_t>(reply[2]) << 8 | reply[3];
        return code;
    }
    int iqWrite32(uint32_t address, uint32_t value)
    {
        const uint8_t request[] = {static_cast<uint8_t>(address >> 16), static_cast<uint8_t>(address >> 8),
                                   static_cast<uint8_t>(address), static_cast<uint8_t>(value >> 24),
                                   static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
        return iqWrite(RADIOLIB_LR2021_CMD_WRITE_REG_MEM_32, request, sizeof(request));
    }
    int iqSnapshot(lr2021_iq_profile* profile)
    {
        uint8_t version[2]{};
        const int code = iqRead(RADIOLIB_LR2021_CMD_GET_VERSION, version, sizeof(version));
        if (code) return code;
        *profile = {};
        profile->lr2021 = true;
        profile->configured = online.load();
        profile->exclusive_lora = ::app::AppTasks::areRadioTasksPaused() && users && !fsk && !transmitting;
        profile->receiving = receiving;
        profile->fw_major = version[0];
        profile->fw_minor = version[1];
        profile->frequency_hz = static_cast<uint32_t>(std::lround(applied.freq_mhz * 1000000.0));
        profile->bandwidth_hz = static_cast<uint32_t>(std::lround(applied.bw_khz * 1000.0));
        profile->spreading_factor = applied.sf;
        return 0;
    }
    int iqReset()
    {
        // begin() performs hardware reset, clock/regulator and RF-switch setup
        // without starting RX. Preserve the saved application configuration.
        hal.term();
        hal.init();
        return hal.healthy() && beginLoRa(applied) ? 0 : LR2021_IQ_IO;
    }
    int iqRestore(const lr2021_iq_profile* profile)
    {
        if (!Board::instance().setLoraRfSwitchTransmit(false)) return LR2021_IQ_IO;
        if (!lora_config.apply(radio, applied, [this](int code, const char* operation)
                               { return check(code, operation); })) return LR2021_IQ_IO;
        receiving = true;
        if (!profile->receiving)
        {
            receiving = false;
            if (!check(radio.standby(), "LR2021 IQ restore standby failed")) return LR2021_IQ_IO;
        }
        return 0;
    }
};
State& state()
{
    static State value;
    return value;
}
} // namespace

Lr2021Radio& Lr2021Radio::instance()
{
    static Lr2021Radio value;
    return value;
}

bool Lr2021Radio::acquire()
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!platform::radio::power_policy().enabled()) return s.fail("LR2021 radio is disabled");
    if (!s.online.load())
    {
        s.hal.term();
        s.hal.init();
        s.lora_config.invalidate();
        if (!s.hal.healthy()) return s.check(-1, "LR2021 transport initialization failed");
        if (!s.beginLoRa(s.applied)) return false;
        s.fsk = false;
        s.online.store(true);
        if (!s.check(s.radio.setCRC(s.applied.crc_len), "LR2021 CRC configuration failed") || !s.receive()) return false;
    }
    ++s.users;
    return true;
}
void Lr2021Radio::release()
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.users) --s.users;
    if (!s.users)
    {
        s.lora_config.invalidate();
        if (s.online.load()) s.check(s.radio.standby(), "LR2021 release standby failed");
        s.transmitting = false;
        s.receiving = false;
    }
}
bool Lr2021Radio::isOnline() const { return state().online.load(); }
const char* Lr2021Radio::lastError() const
{
    const auto* error = state().error.load();
    return error ? error : "";
}

bool Lr2021Radio::configureLoRaReceive(float freq, float bw, uint8_t sf, uint8_t cr,
                                       int8_t power, uint16_t preamble, uint8_t sync, uint8_t crc)
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.online.load()) return s.fail("LR2021 radio is offline");
    if (!std::isfinite(freq) || !std::isfinite(bw) || sf < 5 || sf > 12 || cr < 5 || cr > 8 ||
        preamble < 4 || (crc != 0 && crc != 2)) return s.fail("Invalid LR2021 LoRa configuration");
    s.requested = {freq, bw, sf, cr, power, preamble, sync, crc};
    if (s.fsk && !s.beginLoRa(s.requested)) return false;
    if (!Board::instance().setLoraRfSwitchTransmit(false)) return s.check(-1, "LR2021 antenna RX path failed");
    if (!s.lora_config.apply(s.radio, s.requested, [&s](int code, const char* operation)
                             { return s.check(code, operation); })) return false;
    s.applied = s.requested;
    s.fsk = false;
    s.transmitting = false;
    s.receiving = true;
    return true;
}
bool Lr2021Radio::configureFsk(float freq, int8_t power, float bitrate, float deviation, float bandwidth,
                               uint16_t preamble, const uint8_t* sync, size_t sync_size, uint8_t crc)
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.online.load() || !sync || !sync_size || sync_size > sizeof(s.sync) ||
        !std::isfinite(freq) || !std::isfinite(bitrate) || !std::isfinite(deviation) || !std::isfinite(bandwidth) ||
        bitrate <= 0 || deviation < 0 || bandwidth <= 0 || !preamble || (crc != 0 && crc != 2))
        return s.fail("Invalid LR2021 FSK configuration or radio offline");
    s.requested_fsk.frequency = freq;
    s.requested_fsk.bitrate = bitrate;
    s.requested_fsk.deviation = deviation;
    s.requested_fsk.bandwidth = bandwidth;
    s.requested_fsk.power = power;
    s.requested_fsk.preamble = preamble;
    s.requested_fsk.sync.fill(0);
    std::memcpy(s.requested_fsk.sync.data(), sync, sync_size);
    s.requested_fsk.sync_size = sync_size;
    s.requested_fsk.crc = crc;
    if (!s.beginFsk(s.requested_fsk) || !s.receive()) return false;
    s.fsk_profile = s.requested_fsk;
    s.fsk_profile_valid = true;
    return true;
}
bool Lr2021Radio::startReceive()
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.receive();
}
bool Lr2021Radio::standby()
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.online.load()) return false;
    const bool result = s.check(s.radio.standby(), "LR2021 standby failed");
    s.transmitting = false;
    s.receiving = false;
    return result;
}
float Lr2021Radio::readRssi()
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.online.load()) return std::numeric_limits<float>::quiet_NaN();
    const float value = s.radio.getRSSI(false, true);
    return s.check(0, "LR2021 RSSI read failed") ? value : std::numeric_limits<float>::quiet_NaN();
}
float Lr2021Radio::readSnr()
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.online.load() || s.fsk) return std::numeric_limits<float>::quiet_NaN();
    const float value = s.radio.getSNR();
    return s.check(0, "LR2021 SNR read failed") ? value : std::numeric_limits<float>::quiet_NaN();
}
int Lr2021Radio::startTransmit(const uint8_t* data, size_t size)
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.online.load() || s.transmitting || !data || !size || size > 255)
    {
        s.fail("Invalid LR2021 TX request");
        return -1;
    }
    if (!Board::instance().setLoraRfSwitchTransmit(true))
    {
        s.check(-1, "LR2021 antenna TX path failed");
        return -1;
    }
    const int code = s.radio.startTransmit(data, size);
    if (!s.check(code, "LR2021 start TX failed")) return code ? code : -1;
    s.transmitting = true;
    s.receiving = false;
    return 0;
}
uint32_t Lr2021Radio::getIrqFlags()
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.online.load()) return 0;
    const uint32_t raw = s.radio.getIrqFlags();
    if (!s.check(0, "LR2021 IRQ read failed")) return 0;
    if (raw & (RADIOLIB_LR2021_IRQ_ERROR | RADIOLIB_LR2021_IRQ_CMD_ERROR | RADIOLIB_LR2021_IRQ_PA_OCP_OVP))
    {
        s.check(-1, "LR2021 hardware IRQ error");
        return 0;
    }
    return lr2021_irq::logical(raw);
}
void Lr2021Radio::clearIrqFlags(uint32_t flags)
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.online.load()) s.check(s.radio.clearIrqFlags(lr2021_irq::physical(flags)), "LR2021 clear IRQ failed");
}
int Lr2021Radio::getPacketLength(bool update)
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.online.load()) return -1;
    const size_t size = s.radio.getPacketLength(update);
    if (!s.check(0, "LR2021 packet length read failed") || size > 255) return -1;
    return static_cast<int>(size);
}
int Lr2021Radio::readPacket(uint8_t* buffer, size_t size)
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.online.load() || !buffer || !size || size > 255) return -1;
    const uint32_t irq = s.radio.getIrqFlags();
    if (!s.check(0, "LR2021 packet IRQ read failed")) return -1;
    const int code = s.radio.readData(buffer, size);
    if (!s.hal.healthy())
    {
        s.check(-1, "LR2021 packet transport failed");
        return -1;
    }
    // readData drains the FIFO and clears IRQs, but does not reject these
    // hardware packet errors. Never deliver their payload as a valid frame.
    if (code == RADIOLIB_ERR_NONE &&
        (irq & (RADIOLIB_LR2021_IRQ_LEN_ERROR | RADIOLIB_LR2021_IRQ_ADDR_ERROR)))
    {
        s.fail("LR2021 received an invalid packet");
        return RADIOLIB_ERR_LORA_HEADER_DAMAGED;
    }
    if (code == RADIOLIB_ERR_CRC_MISMATCH || code == RADIOLIB_ERR_LORA_HEADER_DAMAGED)
    {
        s.fail("LR2021 received a damaged packet");
        return code;
    }
    return s.check(code, "LR2021 packet read failed") ? 0 : (code ? code : -1);
}
bool Lr2021Radio::streamFmIq(const platform::shared::radio::FmIqRequest& request,
                             platform::shared::radio::FmIqResult& result)
{
    using namespace platform::shared::radio;
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    result = {};
    if (!request.accept || !request.cancelled || !request.staging || request.staging_bytes < 1280)
    {
        result.capture_error = LR2021_IQ_ARGUMENT;
        return s.fail("FM IQ callbacks and persistent staging are required");
    }
    if (!platform::radio::power_policy().enabled() || !s.online.load() || !s.users ||
        s.transmitting || !::app::AppTasks::areRadioTasksPaused())
    {
        result.capture_error = LR2021_IQ_STATE;
        return s.fail("FM IQ requires a paused foreground packet owner");
    }
    if (!FmLr2021Rx::frequencySupported(request.frequency_hz))
    {
        result.capture_error = LR2021_IQ_ARGUMENT;
        return s.fail("Unsupported FM frequency");
    }
    if (request.cancelled(request.context))
    {
        result.cancelled = true;
        return true;
    }
    return detail::withFmPacketMode(s, result, [&]() -> bool
                                    {
    struct Port final : FmLr2021Rx::Port
    {
        State& s;
        explicit Port(State& state) : s(state) {}
        bool exclusivelyOwned() const override
        {
            return s.online.load() && s.users && !s.fsk && !s.transmitting && ::app::AppTasks::areRadioTasksPaused();
        }
        uint32_t spiClockHz() const override { return 10000000; } // Existing P4 SPI device configuration.
        int read(uint16_t op, uint8_t* p, size_t n) override { return s.iqRead(op, p, n); }
        int write(uint16_t op, const uint8_t* p, size_t n) override { return s.iqWrite(op, p, n); }
    } port(s);
    if (!s.iq_wait.prepare())
    {
        result.capture_error = LR2021_IQ_IO;
        return s.fail("FM IQ service timer unavailable");
    }
    s.iq_wait_failed = false;
    struct Cancellation
    {
        State& state;
        const FmIqRequest& original;
    } cancellation{s, request};
    auto paced = request;
    paced.context = &cancellation;
    paced.cancelled = [](void* c)
    {
        auto& context = *static_cast<Cancellation*>(c);
        return context.state.iq_wait_failed ||
               (context.original.cancelled && context.original.cancelled(context.original.context));
    };
    paced.accept = [](void* c, const uint8_t* data, size_t bytes, uint64_t first)
    {
        auto& context = *static_cast<Cancellation*>(c);
        return context.original.accept && context.original.accept(context.original.context, data, bytes, first);
    };
    lr2021_iq_hal hooks{};
    hooks.context = &s;
    // The outer singleton mutex covers setup, streaming and restoration.
    hooks.lock = [](void*) {};
    hooks.unlock = [](void*) {};
    hooks.snapshot = [](void* c, lr2021_iq_profile* p)
    { return static_cast<State*>(c)->iqSnapshot(p); };
    hooks.read32 = [](void* c, uint32_t a, uint32_t* p)
    { return static_cast<State*>(c)->iqRead32(a, p); };
    hooks.write32 = [](void* c, uint32_t a, uint32_t v)
    { return static_cast<State*>(c)->iqWrite32(a, v); };
    hooks.read = [](void* c, uint16_t op, uint8_t* p, size_t n)
    { return static_cast<State*>(c)->iqRead(op, p, n); };
    hooks.write = [](void* c, uint16_t op, const uint8_t* p, size_t n) -> int
    {
        auto& s = *static_cast<State*>(c);
        if (op == 0x020c && !Board::instance().setLoraRfSwitchTransmit(false)) return LR2021_IQ_IO;
        const int code = s.iqWrite(op, p, n);
        if (op == 0x020c) s.receiving = !code;
        return code;
    };
    hooks.standby = [](void* c) -> int
    {
        auto& s = *static_cast<State*>(c);
        const int code = s.radio.standby();
        s.receiving = false;
        return s.hal.healthy() ? code : LR2021_IQ_IO;
    };
    // The shared FM bridge replaces this with its checked raw SetRx command.
    hooks.receive = [](void*) -> int
    { return LR2021_IQ_STATE; };
    hooks.reset = [](void* c)
    { return static_cast<State*>(c)->iqReset(); };
    hooks.restore = [](void* c, const lr2021_iq_profile* p)
    { return static_cast<State*>(c)->iqRestore(p); };
    // P4 keeps its existing 10 MHz device throughout; no mode switch is needed.
    // This satisfies only the wire-clock floor, not measured throughput.
    hooks.fast = [](void* c, bool) -> int
    { return static_cast<State*>(c)->hal.healthy() ? 0 : LR2021_IQ_IO; };
    hooks.clear_fifo = [](void* c)
    { return static_cast<State*>(c)->iqWrite(0x011e, nullptr, 0); };
    hooks.read_fifo = [](void* c, uint8_t* p, size_t n)
    { return static_cast<State*>(c)->iqFifoRead(p, n); };
    hooks.now_us = [](void*) -> uint64_t
    { return esp_timer_get_time(); };
    hooks.sleep_us = [](void* c, unsigned us)
    { auto& s = *static_cast<State*>(c); if (!s.iq_wait.wait(us)) s.iq_wait_failed = true; };
    const int code = run_fm_lr2021_stream(hooks, port, paced, result);
    if (result.recovery_error || !s.hal.healthy()) return s.check(-1, "FM IQ recovery failed");
    if (s.iq_wait_failed)
    {
        if (!result.capture_error) result.capture_error = LR2021_IQ_IO;
        return s.fail("FM IQ service timer stalled");
    }
    if (code) return s.fail("FM IQ stream rejected");
    s.error.store(nullptr);
    return true; });
}
bool Lr2021Radio::captureIq(uint32_t* words, uint32_t* scratch, size_t capacity, IqCaptureResult& result)
{
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    result = {};
    if (!platform::radio::power_policy().enabled() || !s.online.load() || !s.users ||
        s.fsk || s.transmitting || !::app::AppTasks::areRadioTasksPaused())
    {
        result.capture_error = LR2021_IQ_STATE;
        return s.fail("LR2021 IQ requires a paused exclusive LoRa owner");
    }
    lr2021_iq_hal hooks{};
    hooks.context = &s;
    // The public entry owns the existing singleton mutex for the entire
    // capture/recovery transaction. Hooks must never lock it recursively.
    hooks.lock = [](void*) {};
    hooks.unlock = [](void*) {};
    hooks.snapshot = [](void* c, lr2021_iq_profile* p)
    { return static_cast<State*>(c)->iqSnapshot(p); };
    hooks.read32 = [](void* c, uint32_t address, uint32_t* value)
    { return static_cast<State*>(c)->iqRead32(address, value); };
    hooks.write32 = [](void* c, uint32_t address, uint32_t value)
    { return static_cast<State*>(c)->iqWrite32(address, value); };
    hooks.write = [](void* c, uint16_t command, const uint8_t* data, size_t size)
    { return static_cast<State*>(c)->iqWrite(command, data, size); };
    hooks.read = [](void* c, uint16_t command, uint8_t* data, size_t size)
    { return static_cast<State*>(c)->iqRead(command, data, size); };
    hooks.standby = [](void* c) -> int
    {
        auto& state = *static_cast<State*>(c);
        const int code = state.radio.standby();
        state.receiving = false;
        return state.hal.healthy() ? code : LR2021_IQ_IO;
    };
    hooks.receive = [](void* c) -> int
    {
        auto& state = *static_cast<State*>(c);
        if (!Board::instance().setLoraRfSwitchTransmit(false)) return LR2021_IQ_IO;
        const int code = state.radio.startReceive();
        state.receiving = !code && state.hal.healthy();
        return state.hal.healthy() ? code : LR2021_IQ_IO;
    };
    hooks.reset = [](void* c)
    { return static_cast<State*>(c)->iqReset(); };
    hooks.restore = [](void* c, const lr2021_iq_profile* p)
    { return static_cast<State*>(c)->iqRestore(p); };
    hooks.now_us = [](void*) -> uint64_t
    { return esp_timer_get_time(); };
    hooks.sleep_us = [](void*, unsigned us)
    {
        if (us >= 1000) vTaskDelay(pdMS_TO_TICKS((us + 999) / 1000) ? pdMS_TO_TICKS((us + 999) / 1000) : 1);
        else esp_rom_delay_us(us);
    };
    const int code = lr2021_iq_capture(&hooks, words, scratch, capacity, &s.iq_result);
    result.capture_error = s.iq_result.capture_error;
    result.restore_error = s.iq_result.restore_error;
    result.recovery_error = s.iq_result.recovery_error;
    result.elapsed_us = s.iq_result.elapsed_us;
    if (s.iq_result.restore_error || s.iq_result.recovery_error || !s.hal.healthy())
        return s.check(-1, "LR2021 IQ recovery failed");
    if (code) return s.fail("LR2021 IQ capture rejected");
    result.bytes = s.iq_result.bytes;
    result.nominal_rate_hz = s.iq_result.nominal_rate_hz;
    return true;
}
} // namespace platform::esp::idf_common
