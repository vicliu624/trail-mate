#include "boards/gat562_mesh_evb_pro/sx1262_radio_packet_io.h"

#include "boards/gat562_mesh_evb_pro/gat562_board.h"
#include "boards/gat562_mesh_evb_pro/board_profile.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

#include <algorithm>
#include <cmath>

namespace boards::gat562_mesh_evb_pro
{
namespace
{

constexpr uint8_t kMeshtasticSyncWord = 0x2B;
constexpr uint8_t kMeshCoreSyncWord = 0x12;
constexpr uint16_t kDefaultPreambleLen = 16;
constexpr uint8_t kDefaultCrcLen = 2;

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

void modemPresetToParams(meshtastic_Config_LoRaConfig_ModemPreset preset,
                         bool wide_lora,
                         float& bw_khz,
                         uint8_t& sf,
                         uint8_t& cr_denom)
{
    switch (preset)
    {
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        bw_khz = 125.0f;
        sf = 12;
        cr_denom = 8;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
        bw_khz = 250.0f;
        sf = 11;
        cr_denom = 5;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        bw_khz = 250.0f;
        sf = 10;
        cr_denom = 8;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        bw_khz = 250.0f;
        sf = 9;
        cr_denom = 5;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        bw_khz = 250.0f;
        sf = 8;
        cr_denom = 8;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
    default:
        bw_khz = wide_lora ? 500.0f : 250.0f;
        sf = wide_lora ? 7 : 8;
        cr_denom = 5;
        break;
    }
}

Sx1262RadioPacketIo::AppliedRadioConfig deriveMeshtasticRadioConfig(const ::chat::MeshConfig& config)
{
    Sx1262RadioPacketIo::AppliedRadioConfig out{};
    auto region_code = static_cast<meshtastic_Config_LoRaConfig_RegionCode>(config.region);
    if (region_code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
    {
        region_code = meshtastic_Config_LoRaConfig_RegionCode_CN;
    }

    const ::chat::meshtastic::RegionInfo* region = ::chat::meshtastic::findRegion(region_code);
    if (!region)
    {
        region = ::chat::meshtastic::findRegion(meshtastic_Config_LoRaConfig_RegionCode_CN);
    }

    float bw_khz = 250.0f;
    uint8_t sf = 11;
    uint8_t cr_denom = 5;
    if (config.use_preset && region)
    {
        modemPresetToParams(static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(config.modem_preset),
                            region->wide_lora,
                            bw_khz,
                            sf,
                            cr_denom);
    }
    else
    {
        bw_khz = normalizeBandwidthKhz(config.bandwidth_khz);
        sf = std::clamp<uint8_t>(config.spread_factor, 5, 12);
        cr_denom = std::clamp<uint8_t>(config.coding_rate, 5, 8);
        if (region)
        {
            if (bw_khz < 7.8f) bw_khz = 7.8f;
            if (!region->wide_lora && bw_khz > 500.0f) bw_khz = 500.0f;
            if (region->wide_lora && bw_khz > 1625.0f) bw_khz = 1625.0f;
        }
    }

    float freq_mhz = ::chat::meshtastic::estimateFrequencyMhz(config.region, config.modem_preset);
    if (config.override_frequency_mhz > 0.0f)
    {
        freq_mhz = config.override_frequency_mhz;
    }
    freq_mhz += config.frequency_offset_mhz;

    out.freq_mhz = freq_mhz;
    out.bw_khz = bw_khz;
    out.sf = sf;
    out.cr = cr_denom;
    out.tx_power = std::clamp<int8_t>(config.tx_power == 0 ? 17 : config.tx_power, -9, 20);
    if (region && region->power_limit_dbm > 0 && out.tx_power > static_cast<int8_t>(region->power_limit_dbm))
    {
        out.tx_power = static_cast<int8_t>(region->power_limit_dbm);
    }
    out.preamble_len = kDefaultPreambleLen;
    out.sync_word = kMeshtasticSyncWord;
    out.crc_len = kDefaultCrcLen;
    return out;
}

Sx1262RadioPacketIo::AppliedRadioConfig deriveMeshCoreRadioConfig(const ::chat::MeshConfig& config)
{
    Sx1262RadioPacketIo::AppliedRadioConfig out{};
    float freq_mhz = config.meshcore_freq_mhz;
    float bw_khz = config.meshcore_bw_khz;
    uint8_t sf = config.meshcore_sf;
    uint8_t cr = config.meshcore_cr;

    if (config.meshcore_region_preset > 0)
    {
        if (const auto* preset = ::chat::meshcore::findRegionPresetById(config.meshcore_region_preset))
        {
            freq_mhz = preset->freq_mhz;
            bw_khz = preset->bw_khz;
            sf = preset->sf;
            cr = preset->cr;
        }
    }

    out.freq_mhz = std::clamp<float>(freq_mhz, 400.0f, 2500.0f);
    out.bw_khz = std::clamp<float>(normalizeBandwidthKhz(bw_khz), 7.8f, 500.0f);
    out.sf = std::clamp<uint8_t>(sf, 5, 12);
    out.cr = std::clamp<uint8_t>(cr, 5, 8);
    out.tx_power = std::clamp<int8_t>(config.tx_power, -9, 20);
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
    const auto& profile = ::boards::gat562_mesh_evb_pro::kBoardProfile;
    auto& board = ::boards::gat562_mesh_evb_pro::Gat562Board::instance();
    (void)board.begin();
    delay(10);
    (void)board.prepareRadioHardware();
    module_.reset(new Module(profile.lora.spi.cs,
                             profile.lora.dio1,
                             profile.lora.reset,
                             profile.lora.busy));
    radio_.reset(new SX1262(module_.get()));

    radio_online_ = initializeRadioChip();
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

    const auto& profile = ::boards::gat562_mesh_evb_pro::kBoardProfile;
    if (profile.lora.dio1 < 0 || digitalRead(profile.lora.dio1) == LOW)
    {
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
    if (packet_len <= 0 || packet_len > static_cast<int>(sizeof(out_packet->data)))
    {
        (void)radio_->finishReceive();
        (void)enterReceiveMode();
        return false;
    }

    const int state = radio_->readData(out_packet->data, packet_len);
    out_packet->size = ((state == RADIOLIB_ERR_NONE || state == RADIOLIB_ERR_CRC_MISMATCH) && packet_len > 0)
                           ? packet_len
                           : 0;
    out_packet->rx_meta.rssi_dbm_x10 = static_cast<int16_t>(radio_->getRSSI() * 10.0f);
    out_packet->rx_meta.snr_db_x10 = static_cast<int16_t>(radio_->getSNR() * 10.0f);

    (void)radio_->finishReceive();
    (void)enterReceiveMode();
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
        return false;
    }

    radio_->setDio2AsRfSwitch(true);
    radio_->setTCXO(1.8f);
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

} // namespace boards::gat562_mesh_evb_pro
