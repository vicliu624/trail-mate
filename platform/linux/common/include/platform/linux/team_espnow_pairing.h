#pragma once

#include "team/ports/i_team_pairing_transport.h"
#include "team/usecase/team_pairing_coordinator.h"
#include <array>

namespace platform::linux_runtime
{

// The application uses an unprivileged Unix socket. A socket-activated helper
// owns the radio lease and an independent systemd timer owns Wi-Fi recovery.
class LinuxEspNowPairing final : public team::TeamPairingService,
                                 private team::ITeamPairingTransport,
                                 private team::ITeamPairingTransport::Receiver
{
  public:
    LinuxEspNowPairing(team::ITeamRuntime& runtime, team::ITeamPairingEventSink& events);
    ~LinuxEspNowPairing() override;
    static bool available();
    bool startLeader(const team::TeamId& id, uint32_t key_id, const uint8_t* psk,
                     size_t psk_len, uint32_t leader_id, const char* name) override;
    bool startMember(uint32_t self_id) override;
    void stop() override;
    void update() override;
    team::TeamPairingStatus getStatus() const override;

  private:
    bool begin(team::ITeamPairingTransport::Receiver& receiver) override;
    void end() override;
    bool ensurePeer(const uint8_t* mac) override;
    bool send(const uint8_t* mac, const uint8_t* data, size_t len) override;
    void onPairingReceive(const uint8_t* mac, const uint8_t* data, size_t len) override;
    team::TeamPairingCoordinator core_;
    int socket_ = -1;
    std::array<uint8_t, 4096> pending_{};
    size_t pending_size_ = 0;
};

} // namespace platform::linux_runtime
