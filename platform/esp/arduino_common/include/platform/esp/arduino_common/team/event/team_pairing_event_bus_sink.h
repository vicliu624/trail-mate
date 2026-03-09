#pragma once

#include "team/ports/i_team_pairing_event_sink.h"

namespace team::infra
{

class TeamPairingEventBusSink : public team::ITeamPairingEventSink
{
  public:
    void onTeamPairingStateChanged(const TeamPairingEvent& event) override;
    void onTeamPairingKeyDist(const TeamKeyDistEvent& event) override;
};

} // namespace team::infra
