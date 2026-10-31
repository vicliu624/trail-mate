#include "linux_radiolib_hal.h"
#include "lr2021_iq.h"
#include "oneseg_lr2021_stream.h"
#include "platform/linux/packet_radio.h"
#include "platform/shared/radio/fm_lr2021_stream.h"
#include "platform/shared/radio/lora_receive_config.h"

#include <RadioLib.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <thread>

namespace platform::linux_runtime
{
namespace
{
static_assert(static_cast<int>(IqCaptureError::Argument) == LR2021_IQ_ARGUMENT);
static_assert(static_cast<int>(IqCaptureError::State) == LR2021_IQ_STATE);
static_assert(static_cast<int>(IqCaptureError::Timeout) == LR2021_IQ_TIMEOUT);
static_assert(static_cast<int>(IqCaptureError::Data) == LR2021_IQ_DATA);
static_assert(static_cast<int>(IqCaptureError::Io) == LR2021_IQ_IO);
class Lr2021Radio final : public IPacketRadio
{
  public:
    explicit Lr2021Radio(PacketRadioHardware hardware)
        : hardware_(std::move(hardware)), hal_(hardware_),
          module_(&hal_, LinuxRadioLibHal::kKernelChipSelect,
                  hardware_.bus.irq_gpio < 0 ? RADIOLIB_NC : static_cast<uint32_t>(hardware_.bus.irq_gpio),
                  hardware_.bus.reset_gpio, hardware_.bus.busy_gpio),
          radio_(&module_) {}
    ~Lr2021Radio() override
    {
        if (online_ && hal_.healthy()) radio_.standby();
        hal_.term();
    }
    const char* name() const override { return "LR2021"; }
    bool hardwareCandidatePresent() const override
    {
        std::error_code error;
        return std::filesystem::exists(hardware_.bus.spi_device, error) &&
               std::filesystem::exists(hardware_.bus.gpiochip, error);
    }
    bool acquire() override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (online_)
        {
            ++users_;
            return true;
        }
        lora_config_.invalidate();
        hal_.term();
        hal_.init();
        if (!hal_.healthy()) return check(-1, "open transport");
        const auto code = radio_.begin(settings_.freq_mhz, settings_.bw_khz, settings_.sf,
                                       settings_.cr, settings_.sync_word, settings_.tx_power_dbm,
                                       settings_.preamble_len, hardware_.tcxo_voltage);
        if (!check(code, "initialize"))
        {
            hal_.term();
            return false;
        }
        if (!check(configureBoardRfSwitch(), "RF switch") ||
            !check(radio_.setCRC(settings_.crc_len), "CRC") ||
            !check(radio_.startReceive(), "start receive"))
        {
            hal_.term();
            return false;
        }
        online_ = true;
        fsk_ = false;
        statistics_.online = true;
        ++users_;
        return true;
    }
    bool supportsIqCapture() const override { return true; }
    bool captureIq(uint32_t* words, uint32_t* scratch, size_t capacity, RadioIqCaptureResult& result) override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        result = {};
        if (!online_ || users_ != 1 || fsk_)
        {
            result.capture_error = LR2021_IQ_STATE;
            return fail("LR2021 IQ requires one acquired LoRa owner");
        }
        auto hooks = iqHooks();
        const int code = lr2021_iq_capture(&hooks, words, scratch, capacity, &iq_result_);
        result.capture_error = iq_result_.capture_error;
        result.restore_error = iq_result_.restore_error;
        result.recovery_error = iq_result_.recovery_error;
        result.elapsed_us = iq_result_.elapsed_us;
        if (result.restore_error || result.recovery_error || !hal_.healthy())
        {
            if (hardware_.bus.reset_gpio >= 0)
                hal_.digitalWrite(static_cast<uint32_t>(hardware_.bus.reset_gpio), hal_.GpioLevelLow);
            return check(-1, "IQ recovery");
        }
        if (code) return fail("LR2021 IQ capture rejected: " + std::to_string(code));
        result.bytes = iq_result_.bytes;
        result.nominal_rate_hz = iq_result_.nominal_rate_hz;
        return true;
    }
    bool supportsOneSegIq() const override
    {
        return hardware_.rf_switch_profile == Lr2021RfSwitchProfile::Cm0_16E8;
    }
    bool supportsFmIq() const override { return hardware_.rf_switch_profile == Lr2021RfSwitchProfile::Cm0_16E8; }
    bool streamFmIq(const FmIqRequest& request, FmIqResult& result) override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        result = {};
        if (!online_ || users_ != 1 || fsk_ || !supportsFmIq())
        {
            result.capture_error = LR2021_IQ_STATE;
            return fail("FM IQ requires the CM0 profile and one acquired LoRa owner");
        }
        struct Port final : platform::shared::radio::FmLr2021Rx::Port
        {
            Lr2021Radio& self;
            explicit Port(Lr2021Radio& radio) : self(radio) {}
            bool exclusivelyOwned() const override { return self.online_ && self.users_ == 1 && !self.fsk_; }
            uint32_t spiClockHz() const override { return self.hal_.spiClockHz(); }
            int read(uint16_t op, uint8_t* data, size_t bytes) override { return self.iqRead(op, data, bytes); }
            int write(uint16_t op, const uint8_t* data, size_t bytes) override { return self.iqWrite(op, data, bytes); }
        } port(*this);
        auto hooks = iqHooks();
        hooks.fast = [](void* c, bool enabled) -> int
        { return static_cast<Lr2021Radio*>(c)->hal_.setIqTransferMode(enabled) ? 0 : LR2021_IQ_IO; };
        hooks.clear_fifo = [](void* c)
        { return static_cast<Lr2021Radio*>(c)->iqWrite(0x011e, nullptr, 0); };
        hooks.read_fifo = [](void* c, uint8_t* data, size_t bytes)
        { return static_cast<Lr2021Radio*>(c)->iqFifoRead(data, bytes); };
        const int code = platform::shared::radio::run_fm_lr2021_stream(hooks, port, request, result);
        if (result.recovery_error || !hal_.healthy())
        {
            if (hardware_.bus.reset_gpio >= 0)
                hal_.digitalWrite(static_cast<uint32_t>(hardware_.bus.reset_gpio), hal_.GpioLevelLow);
            return check(-1, "FM IQ recovery");
        }
        if (code) return fail("FM IQ stream rejected: " + std::to_string(code));
        error_.clear();
        return true;
    }
    bool streamOneSegIq(const OneSegIqRequest& request, OneSegIqResult& result) override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        result = {};
        if (!online_ || users_ != 1 || fsk_ || !supportsOneSegIq())
        {
            result.capture_error = LR2021_IQ_STATE;
            return fail("TV IQ requires the CM0 profile and one acquired LoRa owner");
        }
        struct Port final : OneSegLr2021Rx::Port
        {
            Lr2021Radio& self;
            explicit Port(Lr2021Radio& radio) : self(radio) {}
            bool exclusivelyOwned() const override { return self.online_ && self.users_ == 1 && !self.fsk_; }
            uint32_t spiClockHz() const override { return self.hal_.spiClockHz(); }
            int read(uint16_t op, uint8_t* data, size_t bytes) override { return self.iqRead(op, data, bytes); }
            int write(uint16_t op, const uint8_t* data, size_t bytes) override { return self.iqWrite(op, data, bytes); }
        } port(*this);
        auto hooks = iqHooks();
        hooks.fast = [](void* c, bool enabled) -> int
        { return static_cast<Lr2021Radio*>(c)->hal_.setIqTransferMode(enabled) ? 0 : LR2021_IQ_IO; };
        hooks.clear_fifo = [](void* c)
        { return static_cast<Lr2021Radio*>(c)->iqWrite(0x011e, nullptr, 0); };
        hooks.read_fifo = [](void* c, uint8_t* data, size_t bytes)
        { return static_cast<Lr2021Radio*>(c)->iqFifoRead(data, bytes); };
        const int code = run_oneseg_lr2021_stream(hooks, port, request, result);
        if (result.recovery_error || !hal_.healthy())
        {
            if (hardware_.bus.reset_gpio >= 0)
                hal_.digitalWrite(static_cast<uint32_t>(hardware_.bus.reset_gpio), hal_.GpioLevelLow);
            return check(-1, "TV IQ recovery");
        }
        if (code) return fail("TV IQ stream rejected: " + std::to_string(code));
        error_.clear();
        return true;
    }
    void release() override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (users_ > 0) --users_;
        if (users_ == 0)
        {
            lora_config_.invalidate();
            if (online_ && hal_.healthy()) radio_.standby();
            hal_.term();
            online_ = false;
            statistics_.online = false;
        }
    }
    bool configureLoRa(const RadioSettings& settings) override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!online_) return fail("LR2021 is not acquired");
        if (!std::isfinite(settings.freq_mhz) || !std::isfinite(settings.bw_khz) ||
            settings.sf < 5 || settings.sf > 12 || settings.cr < 5 || settings.cr > 8 ||
            (settings.crc_len != 0 && settings.crc_len != 2) || settings.preamble_len < 4)
            return fail("Invalid LoRa settings");
        if (fsk_)
        {
            if (!check(radio_.begin(settings.freq_mhz, settings.bw_khz, settings.sf, settings.cr,
                                    settings.sync_word, settings.tx_power_dbm, settings.preamble_len,
                                    hardware_.tcxo_voltage),
                       "restore LoRa modem") ||
                !check(configureBoardRfSwitch(), "restore LoRa RF switch")) return false;
            fsk_ = false;
        }
        if (!lora_config_.apply(radio_, settings, [this](int code, const char* operation)
                                { return check(code, operation); })) return false;
        settings_ = settings;
        statistics_.lora_config = settings;
        return true;
    }
    bool startReceive() override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return online_ && check(radio_.startReceive(), "start receive");
    }
    bool supportsFsk() const override { return true; }
    bool supportsFskConfiguration(const FskSettings& settings) const override { return valid_lr2021_fsk_settings(settings); }
    bool configureFsk(const FskSettings& settings) override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!online_ || !supportsFskConfiguration(settings)) return fail("Invalid FSK settings or radio offline");
        lora_config_.invalidate();
        auto sync = settings.sync_word;
        if (!check(radio_.beginGFSK(settings.freq_mhz, settings.bitrate_kbps, settings.deviation_khz,
                                    settings.rx_bandwidth_khz, settings.tx_power_dbm, settings.preamble_bits,
                                    hardware_.tcxo_voltage),
                   "initialize FSK") ||
            !check(configureBoardRfSwitch(), "FSK RF switch") ||
            !check(radio_.setSyncWord(sync.data(), settings.sync_word_size), "FSK sync word") ||
            !check(radio_.setCRC(2, 0x1D0F, 0x1021, true), "FSK CRC") ||
            !check(radio_.setWhitening(false), "FSK whitening") ||
            !check(radio_.startReceive(), "FSK receive")) return false;
        fsk_ = true;
        fsk_settings_ = settings;
        return true;
    }
    bool transmit(const uint8_t* data, std::size_t size) override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!online_ || !data || size == 0 || size > 255) return fail("Invalid TX packet or radio offline");
        if (!check(radio_.startTransmit(data, size), "start transmit")) return false;
        const auto air_time = std::chrono::microseconds(radio_.getTimeOnAir(size));
        const auto deadline = std::chrono::steady_clock::now() + air_time * 2 + std::chrono::seconds(2);
        bool sent = false;
        while (hal_.healthy() && std::chrono::steady_clock::now() < deadline)
        {
            const uint32_t irq = radio_.getIrqFlags();
            statistics_.last_irq_flags = irq;
            if (irq & RADIOLIB_LR2021_IRQ_TX_DONE)
            {
                sent = true;
                break;
            }
            if (irq & RADIOLIB_LR2021_IRQ_TIMEOUT) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const bool finished = check(radio_.finishTransmit(), "finish transmit");
        const bool receiving = finished && check(radio_.startReceive(), "resume receive");
        if (!sent) return fail(hal_.healthy() ? "LR2021 TX timed out" : hal_.error());
        if (!finished || !receiving) return false;
        ++statistics_.tx_packets;
        return true;
    }
    bool pollReceive(RadioPacket* packet) override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!online_ || !packet) return false;
        const uint32_t irq = radio_.getIrqFlags();
        if (!hal_.healthy()) return check(-1, "read IRQ");
        statistics_.last_irq_flags = irq;
        if (irq & (RADIOLIB_LR2021_IRQ_ERROR | RADIOLIB_LR2021_IRQ_CMD_ERROR | RADIOLIB_LR2021_IRQ_PA_OCP_OVP))
        {
            // RX_DONE must never hide a hardware failure. Best-effort stop and
            // hold reset before marking the driver offline for reacquisition.
            (void)radio_.standby();
            if (hardware_.bus.reset_gpio >= 0)
                hal_.digitalWrite(static_cast<uint32_t>(hardware_.bus.reset_gpio), hal_.GpioLevelLow);
            return check(-1, "hardware IRQ");
        }
        if (irq & (RADIOLIB_LR2021_IRQ_CRC_ERROR | RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR |
                   RADIOLIB_LR2021_IRQ_LEN_ERROR | RADIOLIB_LR2021_IRQ_ADDR_ERROR))
        {
            // RadioLib readData checks CRC/header validity, but does not reject
            // length/address IRQs. Discard before copying any damaged payload.
            if (irq & RADIOLIB_LR2021_IRQ_CRC_ERROR) ++statistics_.rx_crc_errors;
            if (irq & (RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR | RADIOLIB_LR2021_IRQ_ADDR_ERROR))
                ++statistics_.rx_header_errors;
            if (irq & RADIOLIB_LR2021_IRQ_LEN_ERROR) ++statistics_.rx_invalid_lengths;
            check(radio_.startReceive(), "recover damaged packet");
            return false;
        }
        if (irq & RADIOLIB_LR2021_IRQ_TIMEOUT)
        {
            ++statistics_.rx_timeouts;
            check(radio_.startReceive(), "recover RX timeout");
            return false;
        }
        if (!(irq & RADIOLIB_LR2021_IRQ_RX_DONE)) return false;
        const std::size_t size = radio_.getPacketLength();
        if (!size || size > packet->data.size())
        {
            ++statistics_.rx_invalid_lengths;
            check(radio_.startReceive(), "recover length");
            return false;
        }
        const float rssi = radio_.getRSSI();
        const float snr = fsk_ ? 0.0f : radio_.getSNR();
        const auto code = radio_.readData(packet->data.data(), size);
        if (code != RADIOLIB_ERR_NONE || !hal_.healthy())
        {
            ++statistics_.rx_read_errors;
            check(radio_.startReceive(), "recover read");
            return false;
        }
        packet->size = size;
        packet->rssi_dbm = rssi;
        packet->snr_db = snr;
        packet->freq_hz = static_cast<uint32_t>(settings_.freq_mhz * 1000000.0f);
        packet->bw_hz = static_cast<uint32_t>(settings_.bw_khz * 1000.0f);
        packet->sf = settings_.sf;
        packet->cr = settings_.cr;
        if (fsk_)
        {
            packet->freq_hz = static_cast<uint32_t>(fsk_settings_.freq_mhz * 1000000.0f);
            packet->bw_hz = 0;
            packet->sf = packet->cr = 0;
        }
        ++statistics_.rx_packets;
        return check(radio_.startReceive(), "resume receive");
    }
    float readRssi() override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        float value = -127.0f;
        if (online_)
        {
            value = radio_.getRSSI(false, true);
            if (!hal_.healthy()) check(-1, "instant RSSI");
        }
        return value;
    }
    bool isOnline() const override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return online_;
    }
    const char* lastError() const override
    {
        thread_local std::string message;
        std::lock_guard<std::mutex> guard(mutex_);
        message = error_;
        return message.c_str();
    }
    RadioSettings appliedLoRaConfig() const override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return settings_;
    }
    RadioStatistics stats() const override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return statistics_;
    }

  private:
    int configureBoardRfSwitch()
    {
        return apply_lr2021_rf_switch(hardware_.rf_switch_profile,
                                      [this](uint16_t op, const uint8_t* data, size_t bytes)
                                      { return iqWrite(op, data, bytes); });
    }
    lr2021_iq_hal iqHooks()
    {
        lr2021_iq_hal hooks{};
        hooks.context = this;
        // Public operations hold the singleton mutex. Never lock recursively.
        hooks.lock = [](void*) {};
        hooks.unlock = [](void*) {};
        hooks.snapshot = [](void* c, lr2021_iq_profile* p)
        { return static_cast<Lr2021Radio*>(c)->iqSnapshot(p); };
        hooks.read32 = [](void* c, uint32_t a, uint32_t* v)
        { return static_cast<Lr2021Radio*>(c)->iqRead32(a, v); };
        hooks.write32 = [](void* c, uint32_t a, uint32_t v)
        { return static_cast<Lr2021Radio*>(c)->iqWrite32(a, v); };
        hooks.write = [](void* c, uint16_t op, const uint8_t* p, size_t n)
        { return static_cast<Lr2021Radio*>(c)->iqWrite(op, p, n); };
        hooks.read = [](void* c, uint16_t op, uint8_t* p, size_t n)
        { return static_cast<Lr2021Radio*>(c)->iqRead(op, p, n); };
        hooks.standby = [](void* c) -> int
        {
            auto& self = *static_cast<Lr2021Radio*>(c);
            const int code = self.radio_.standby();
            return self.hal_.healthy() ? code : LR2021_IQ_IO;
        };
        hooks.receive = [](void* c) -> int
        {
            auto& self = *static_cast<Lr2021Radio*>(c);
            const int code = self.radio_.startReceive();
            return self.hal_.healthy() ? code : LR2021_IQ_IO;
        };
        hooks.reset = [](void* c)
        { return static_cast<Lr2021Radio*>(c)->iqReset(); };
        hooks.restore = [](void* c, const lr2021_iq_profile* p)
        { return static_cast<Lr2021Radio*>(c)->iqRestore(p); };
        hooks.now_us = [](void*) -> uint64_t
        { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
        hooks.sleep_us = [](void*, unsigned us)
        { std::this_thread::sleep_for(std::chrono::microseconds(us)); };
        return hooks;
    }
    int iqFifoRead(uint8_t* data, size_t bytes)
    {
        if (!data || !bytes || bytes > 256) return LR2021_IQ_ARGUMENT;
        std::fill(iq_fifo_tx_.begin(), iq_fifo_tx_.end(), 0);
        iq_fifo_tx_[1] = 1;
        // FIFO payload follows the two command/status bytes in this one frame;
        // unlike register reads, there is no second commandless transaction.
        hal_.spiTransfer(iq_fifo_tx_.data(), bytes + 2, iq_fifo_rx_.data());
        if (!hal_.healthy()) return LR2021_IQ_IO;
        const unsigned status = static_cast<unsigned>(iq_fifo_rx_[0]) << 8 | iq_fifo_rx_[1];
        if (!status || (status & 0xf008) || ((status >> 9) & 7) < 2 || ((status >> 9) & 7) > 3 ||
            ((status >> 4) & 7) > 2 || (status & 7) > 6) return LR2021_IQ_DATA;
        std::copy_n(iq_fifo_rx_.begin() + 2, bytes, data);
        return 0;
    }
    int iqRead(uint16_t command, uint8_t* data, size_t bytes,
               const uint8_t* arguments = nullptr, size_t argument_bytes = 0)
    {
        int code = module_.SPIwriteStream(command, arguments, argument_bytes, true, false);
        if (code || !hal_.healthy()) return code ? code : LR2021_IQ_IO;
        const auto width = module_.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD];
        module_.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = Module::BITS_0;
        code = module_.SPIreadStream(0, data, bytes, true, false);
        module_.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = width;
        return hal_.healthy() ? code : LR2021_IQ_IO;
    }
    int iqWrite(uint16_t command, const uint8_t* data, size_t bytes)
    {
        const int code = module_.SPIwriteStream(command, data, bytes, true, true);
        return hal_.healthy() ? code : LR2021_IQ_IO;
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
        profile->configured = online_;
        profile->exclusive_lora = users_ == 1 && !fsk_;
        // All online LoRa operations in this port finish in RX; there is no
        // public standby operation. TX is synchronous under the same mutex.
        profile->receiving = true;
        profile->fw_major = version[0];
        profile->fw_minor = version[1];
        profile->frequency_hz = static_cast<uint32_t>(std::lround(settings_.freq_mhz * 1000000.0));
        profile->bandwidth_hz = static_cast<uint32_t>(std::lround(settings_.bw_khz * 1000.0));
        profile->spreading_factor = settings_.sf;
        return 0;
    }
    int iqReset()
    {
        lora_config_.invalidate();
        hal_.term();
        hal_.init();
        if (!hal_.healthy()) return LR2021_IQ_IO;
        // begin() resets and reinitialises clocks/calibration without arming RX.
        return check(radio_.begin(settings_.freq_mhz, settings_.bw_khz, settings_.sf,
                                  settings_.cr, settings_.sync_word, settings_.tx_power_dbm,
                                  settings_.preamble_len, hardware_.tcxo_voltage),
                     "IQ reset") &&
                       check(configureBoardRfSwitch(), "IQ RF switch restore")
                   ? 0
                   : LR2021_IQ_IO;
    }
    int iqRestore(const lr2021_iq_profile* profile)
    {
        if (!lora_config_.apply(radio_, settings_, [this](int code, const char* operation)
                                { return check(code, operation); })) return LR2021_IQ_IO;
        if (!profile->receiving && !check(radio_.standby(), "IQ restore standby")) return LR2021_IQ_IO;
        return 0;
    }
    bool fail(std::string message)
    {
        error_ = std::move(message);
        return false;
    }
    bool check(int code, const char* operation)
    {
        if (code == RADIOLIB_ERR_NONE && hal_.healthy())
        {
            error_.clear();
            return true;
        }
        online_ = false;
        statistics_.online = false;
        lora_config_.invalidate();
        return fail(hal_.healthy() ? std::string("LR2021 ") + operation + " failed: " + std::to_string(code) : hal_.error());
    }
    PacketRadioHardware hardware_;
    LinuxRadioLibHal hal_;
    Module module_;
    LR2021 radio_;
    mutable std::mutex mutex_;
    RadioSettings settings_;
    platform::shared::radio::LoRaReceiveConfig<RadioSettings> lora_config_;
    bool fsk_ = false;
    FskSettings fsk_settings_;
    RadioStatistics statistics_;
    std::string error_;
    bool online_ = false;
    unsigned users_ = 0;
    lr2021_iq_result iq_result_{};
    std::array<uint8_t, 258> iq_fifo_tx_{}, iq_fifo_rx_{};
};
} // namespace

std::unique_ptr<IPacketRadio> make_lr2021_radio(const PacketRadioHardware& hardware)
{
    return std::make_unique<Lr2021Radio>(hardware);
}
} // namespace platform::linux_runtime
