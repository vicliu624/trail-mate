#pragma once

#include "../domain/team_events.h"

namespace team
{

class ITeamPairingEventSink
{
  public:
    virtual ~ITeamPairingEventSink() = default;

    virtual void onTeamPairingStateChanged(const TeamPairingEvent& event) = 0;
    virtual void onTeamPairingKeyDist(const TeamKeyDistEvent& event) = 0;
};

} // namespace team
