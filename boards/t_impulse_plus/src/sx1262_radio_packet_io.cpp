#include "boards/t_impulse_plus/sx1262_radio_packet_io.h"

#include "app/app_config.h"
#include "boards/t_impulse_plus/board_profile.h"
#include "boards/t_impulse_plus/t_impulse_plus_board.h"
#include "chat/infra/meshtastic/mt_radio_config.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

#include <algorithm>

namespace boards::t_impulse_plus
{
namespace
{

constexpr uint8_t kMeshCoreSyncWord = 0x12;
constexpr uint16_t kDefaultPreambleLen = 16;
constexpr uint8_t kDefaultCrcLen = 2;
constexpr uint32_t kRadioDropLogIntervalMs = 5000UL;

SPIClass& radioSpi()
{
    const auto& spi = ::boards::t_impulse_plus::kBoardProfile.lora.spi;
    static SPIClass bus(NRF_SPIM3, spi.miso, spi.sck, spi.mosi);
    return bus;
}

float normalizeBandwidthKhz(float bw_khz)
{
    if (bw_khz == 31.0f) return 31.25f;
    if (bw_khz == 62.0f) return 62.5f;
    if (bw_khz == 200.0f) return 203.125f;
    if (bw_khz == 400.0f) return 406.25f;
    if (bw_khz == 800.0f) return 812.5f;
    if (bw_khz == 1600.0f) return 1625.0f;
    return bw_khz;
}

Sx1262RadioPacketIo::AppliedRadioConfig deriveMeshtasticRadioConfig(const ::chat::MeshConfig& config)
{
    Sx1262RadioPacketIo::AppliedRadioConfig out{};
    const ::chat::meshtastic::RadioConfig radio = ::chat::meshtastic::deriveRadioConfig(config);
    out.freq_mhz = radio.freq_mhz;
    out.bw_khz = radio.bw_khz;
    out.sf = radio.sf;
    out.cr = radio.cr_denom;
    out.tx_power = std::clamp<int8_t>(radio.tx_power_dbm, -9, app::AppConfig::kTxPowerMaxDbm);
    out.preamble_len = radio.preamble_len;
    out.sync_word = radio.sync_word;
    out.crc_len = radio.crc_len;
    return out;
}

Sx1262RadioPacketIo::AppliedRadioConfig deriveMeshCoreRadioConfig(const ::chat::MeshConfig& config)
{
    Sx1262RadioPacketIo::AppliedRadioConfig out{};
    out.freq_mhz = std::clamp<float>(config.meshcore_freq_mhz, 400.0f, 2500.0f);
    out.bw_khz = std::clamp<float>(normalizeBandwidthKhz(config.meshcore_bw_khz), 7.8f, 500.0f);
    out.sf = std::clamp<uint8_t>(config.meshcore_sf, 5, 12);
    out.cr = std::clamp<uint8_t>(config.meshcore_cr, 5, 8);
    out.tx_power = std::clamp<int8_t>(config.tx_power, -9, app::AppConfig::kTxPowerMaxDbm);
    out.preamble_len = kDefaultPreambleLen;
    out.sync_word = kMeshCoreSyncWord;
    out.crc_len = kDefaultCrcLen;
    return out;
}

} // namespace

Sx1262RadioPacketIo::Sx1262RadioPacketIo() = default;
Sx1262RadioPacketIo::~Sx1262RadioPacketIo() = default;

bool Sx1262RadioPacketIo::begin()
{
    if (initialized_)
    {
        return radio_online_;
    }

    initialized_ = true;
    const auto& profile = ::boards::t_impulse_plus::kBoardProfile;
    auto& board = ::boards::t_impulse_plus::TImpulsePlusBoard::instance();
    (void)board.begin();
    delay(10);
    (void)board.prepareRadioHardware();
    radioSpi().begin();
    module_.reset(new Module(profile.lora.spi.cs,
                             profile.lora.dio1,
                             profile.lora.reset,
                             profile.lora.busy,
                             radioSpi()));
    radio_.reset(new SX1262(module_.get()));

    radio_online_ = initializeRadioChip();
    Serial.printf("[t-impulse-plus][radio] begin state=%s\n", radio_online_ ? "ok" : "fail");
    if (radio_online_)
    {
        applyConfig(active_protocol_, active_config_);
    }
    return radio_online_;
}

void Sx1262RadioPacketIo::applyConfig(::chat::MeshProtocol protocol, const ::chat::MeshConfig& config)
{
    active_protocol_ = protocol;
    active_config_ = config;
    if (!radio_online_ || !radio_)
    {
        return;
    }

    (void)applyRadioConfig(deriveRadioConfig(protocol, config));
}

bool Sx1262RadioPacketIo::transmit(const uint8_t* data, size_t size)
{
    if (!radio_online_ || !radio_ || !data || size == 0)
    {
        return false;
    }

    receiving_ = false;
    const int state = radio_->transmit(data, size);
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.printf("[t-impulse-plus][radio] tx failed state=%d len=%u\n",
                      state,
                      static_cast<unsigned>(size));
        (void)enterReceiveMode();
        return false;
    }
    return enterReceiveMode();
}

bool Sx1262RadioPacketIo::pollReceive(platform::nrf52::arduino_common::chat::infra::RadioPacket* out_packet)
{
    if (!radio_online_ || !radio_ || !out_packet)
    {
        return false;
    }

    if (!receiving_)
    {
        (void)enterReceiveMode();
        return false;
    }

    const uint32_t irq = radio_->getIrqFlags();
    if ((irq & RADIOLIB_SX126X_IRQ_RX_DONE) == 0)
    {
        if (irq & RADIOLIB_SX126X_IRQ_TIMEOUT)
        {
            (void)radio_->finishReceive();
            (void)enterReceiveMode();
        }
        return false;
    }

    const size_t packet_len = radio_->getPacketLength();
    if (packet_len <= 0 || packet_len > sizeof(out_packet->data))
    {
        (void)radio_->finishReceive();
        (void)enterReceiveMode();
        return false;
    }

    const int state = radio_->readData(out_packet->data, packet_len);
    out_packet->rx_meta.rssi_dbm_x10 = static_cast<int16_t>(radio_->getRSSI() * 10.0f);
    out_packet->rx_meta.snr_db_x10 = static_cast<int16_t>(radio_->getSNR() * 10.0f);

    (void)radio_->finishReceive();
    (void)enterReceiveMode();
    if (state != RADIOLIB_ERR_NONE)
    {
        static uint32_t drop_count = 0;
        static uint32_t last_drop_log_ms = 0;
        ++drop_count;
        const uint32_t now_ms = millis();
        if (drop_count <= 4 || (now_ms - last_drop_log_ms) >= kRadioDropLogIntervalMs)
        {
            last_drop_log_ms = now_ms;
            Serial.printf("[t-impulse-plus][radio] rx drop state=%d len=%u drops=%lu rssi_x10=%d snr_x10=%d\n",
                          state,
                          static_cast<unsigned>(packet_len),
                          static_cast<unsigned long>(drop_count),
                          static_cast<int>(out_packet->rx_meta.rssi_dbm_x10),
                          static_cast<int>(out_packet->rx_meta.snr_db_x10));
        }
        out_packet->size = 0;
        return false;
    }

    out_packet->size = packet_len;
    return out_packet->size > 0;
}

bool Sx1262RadioPacketIo::initializeRadioChip()
{
    if (!radio_)
    {
        return false;
    }

    const int state = radio_->begin();
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.printf("[t-impulse-plus][radio] chip init failed state=%d\n", state);
        return false;
    }

    const auto& lora = ::boards::t_impulse_plus::kBoardProfile.lora;
    if (lora.rf_vc2 >= 0 && lora.rf_vc1 >= 0)
    {
        radio_->setRfSwitchPins(lora.rf_vc2, lora.rf_vc1);
    }
    radio_->setTCXO(lora.tcxo_voltage);
    radio_->setCurrentLimit(140.0f);
    return true;
}

bool Sx1262RadioPacketIo::enterReceiveMode()
{
    if (!radio_online_ || !radio_)
    {
        return false;
    }
    receiving_ = (radio_->startReceive() == RADIOLIB_ERR_NONE);
    return receiving_;
}

bool Sx1262RadioPacketIo::applyRadioConfig(const AppliedRadioConfig& config)
{
    if (!radio_)
    {
        return false;
    }

    if (radio_->setFrequency(config.freq_mhz) != RADIOLIB_ERR_NONE) return false;
    if (radio_->setBandwidth(config.bw_khz) != RADIOLIB_ERR_NONE) return false;
    if (radio_->setSpreadingFactor(config.sf) != RADIOLIB_ERR_NONE) return false;
    if (radio_->setCodingRate(config.cr) != RADIOLIB_ERR_NONE) return false;
    if (radio_->setOutputPower(config.tx_power) != RADIOLIB_ERR_NONE) return false;
    if (radio_->setPreambleLength(config.preamble_len) != RADIOLIB_ERR_NONE) return false;
    if (radio_->setSyncWord(config.sync_word) != RADIOLIB_ERR_NONE) return false;
    if (radio_->setCRC(config.crc_len) != RADIOLIB_ERR_NONE) return false;

    applied_config_ = config;
    applied_freq_hz_ = static_cast<uint32_t>(config.freq_mhz * 1000000.0f + 0.5f);
    applied_bw_hz_ = static_cast<uint32_t>(config.bw_khz * 1000.0f + 0.5f);
    Serial.printf("[t-impulse-plus][radio] cfg f=%.3f bw=%.3f sf=%u cr=%u tx=%d sw=0x%02X\n",
                  static_cast<double>(config.freq_mhz),
                  static_cast<double>(config.bw_khz),
                  static_cast<unsigned>(config.sf),
                  static_cast<unsigned>(config.cr),
                  static_cast<int>(config.tx_power),
                  static_cast<unsigned>(config.sync_word));
    return enterReceiveMode();
}

Sx1262RadioPacketIo::AppliedRadioConfig Sx1262RadioPacketIo::deriveRadioConfig(::chat::MeshProtocol protocol,
                                                                               const ::chat::MeshConfig& config) const
{
    return protocol == ::chat::MeshProtocol::MeshCore
               ? deriveMeshCoreRadioConfig(config)
               : deriveMeshtasticRadioConfig(config);
}

Sx1262RadioPacketIo& sx1262RadioPacketIo()
{
    static Sx1262RadioPacketIo io;
    return io;
}

} // namespace boards::t_impulse_plus
