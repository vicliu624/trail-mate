#pragma once

#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"

#include <memory>

class Module;
class SX1262;

namespace boards::gat562_mesh_evb_pro
{

class Sx1262RadioPacketIo final : public platform::nrf52::arduino_common::chat::infra::IRadioPacketIo
{
  public:
    struct AppliedRadioConfig
    {
        float freq_mhz = 0.0f;
        float bw_khz = 0.0f;
        uint8_t sf = 0;
        uint8_t cr = 0;
        int8_t tx_power = 0;
        uint16_t preamble_len = 0;
        uint8_t sync_word = 0;
        uint8_t crc_len = 0;
    };

    Sx1262RadioPacketIo();
    ~Sx1262RadioPacketIo() override;

    bool begin() override;
    void applyConfig(::chat::MeshProtocol protocol, const ::chat::MeshConfig& config) override;
    bool transmit(const uint8_t* data, size_t size) override;
    bool pollReceive(platform::nrf52::arduino_common::chat::infra::RadioPacket* out_packet) override;
    uint32_t appliedFrequencyHz() const { return applied_freq_hz_; }
    uint32_t appliedBandwidthHz() const { return applied_bw_hz_; }

  private:
    bool initializeRadioChip();
    bool enterReceiveMode();
    bool applyRadioConfig(const AppliedRadioConfig& config);
    AppliedRadioConfig deriveRadioConfig(::chat::MeshProtocol protocol, const ::chat::MeshConfig& config) const;

    std::unique_ptr<Module> module_;
    std::unique_ptr<SX1262> radio_;
    bool initialized_ = false;
    bool receiving_ = false;
    bool radio_online_ = false;
    ::chat::MeshProtocol active_protocol_ = ::chat::MeshProtocol::Meshtastic;
    ::chat::MeshConfig active_config_{};
    AppliedRadioConfig applied_config_{};
    uint32_t applied_freq_hz_ = 0;
    uint32_t applied_bw_hz_ = 0;
};

Sx1262RadioPacketIo& sx1262RadioPacketIo();

} // namespace boards::gat562_mesh_evb_pro
