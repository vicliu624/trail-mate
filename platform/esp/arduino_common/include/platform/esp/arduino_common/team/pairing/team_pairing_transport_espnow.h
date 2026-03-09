#pragma once

#include "team/ports/i_team_pairing_transport.h"

namespace team::infra
{

class EspNowTeamPairingTransport final : public team::ITeamPairingTransport
{
  public:
    EspNowTeamPairingTransport() = default;

    bool begin(Receiver& receiver) override;
    void end() override;
    bool ensurePeer(const uint8_t* mac) override;
    bool send(const uint8_t* mac, const uint8_t* data, size_t len) override;

  private:
    Receiver* receiver_ = nullptr;
    bool initialized_ = false;
    static EspNowTeamPairingTransport* instance_;
};

} // namespace team::infra
