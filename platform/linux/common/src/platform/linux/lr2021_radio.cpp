#include "linux_radiolib_hal.h"
#include "platform/linux/packet_radio.h"

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
        if (!check(radio_.setCRC(settings_.crc_len), "CRC") ||
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
    void release() override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (users_ > 0) --users_;
        if (users_ == 0)
        {
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
                       "restore LoRa modem")) return false;
            fsk_ = false;
        }
        if (!check(radio_.standby(), "standby") ||
            !check(radio_.setFrequency(settings.freq_mhz), "frequency") ||
            !check(radio_.setBandwidth(settings.bw_khz), "bandwidth") ||
            !check(radio_.setSpreadingFactor(settings.sf), "spreading factor") ||
            !check(radio_.setCodingRate(settings.cr), "coding rate") ||
            !check(radio_.setSyncWord(settings.sync_word), "sync word") ||
            !check(radio_.setOutputPower(settings.tx_power_dbm), "TX power") ||
            !check(radio_.setPreambleLength(settings.preamble_len), "preamble") ||
            !check(radio_.setCRC(settings.crc_len), "CRC") ||
            !check(radio_.explicitHeader(), "explicit header") ||
            !check(radio_.invertIQ(false), "IQ polarity") ||
            !check(radio_.startReceive(), "start receive")) return false;
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
        auto sync = settings.sync_word;
        if (!check(radio_.beginGFSK(settings.freq_mhz, settings.bitrate_kbps, settings.deviation_khz,
                                    settings.rx_bandwidth_khz, settings.tx_power_dbm, settings.preamble_bits,
                                    hardware_.tcxo_voltage),
                   "initialize FSK") ||
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
        if (irq & RADIOLIB_LR2021_IRQ_CRC_ERROR)
        {
            ++statistics_.rx_crc_errors;
            check(radio_.startReceive(), "recover CRC");
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
        return fail(hal_.healthy() ? std::string("LR2021 ") + operation + " failed: " + std::to_string(code) : hal_.error());
    }
    PacketRadioHardware hardware_;
    LinuxRadioLibHal hal_;
    Module module_;
    LR2021 radio_;
    mutable std::mutex mutex_;
    RadioSettings settings_;
    bool fsk_ = false;
    FskSettings fsk_settings_;
    RadioStatistics statistics_;
    std::string error_;
    bool online_ = false;
    unsigned users_ = 0;
};
} // namespace

std::unique_ptr<IPacketRadio> make_lr2021_radio(const PacketRadioHardware& hardware)
{
    return std::make_unique<Lr2021Radio>(hardware);
}
} // namespace platform::linux_runtime
