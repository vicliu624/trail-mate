#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "team/usecase/team_pairing_coordinator.h"
#include <cstddef>
#include <cstdint>

namespace team
{

class EspNowTeamPairingService final : public TeamPairingService,
                                       private team::ITeamPairingTransport::Receiver
{
  public:
    EspNowTeamPairingService(team::ITeamRuntime& runtime,
                             team::ITeamPairingEventSink& event_sink,
                             team::ITeamPairingTransport& transport);

    bool startLeader(const TeamId& team_id,
                     uint32_t key_id,
                     const uint8_t* psk,
                     size_t psk_len,
                     uint32_t leader_id,
                     const char* team_name) override;
    bool startMember(uint32_t self_id) override;
    void stop() override;
    void update() override;

    TeamPairingStatus getStatus() const override;

  private:
    struct RxPacket
    {
        uint8_t mac[6];
        uint8_t data[128];
        size_t len = 0;
    };

    bool ensureTransport();
    void shutdownTransport();
    void onPairingReceive(const uint8_t* mac, const uint8_t* data, size_t len) override;

    team::ITeamPairingTransport& transport_;
    team::TeamPairingCoordinator core_;
    bool transport_ready_ = false;

    mutable portMUX_TYPE rx_mux_ = portMUX_INITIALIZER_UNLOCKED;
    volatile bool rx_pending_ = false;
    RxPacket rx_packet_{};
};

} // namespace team
