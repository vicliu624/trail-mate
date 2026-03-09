#pragma once

#include "team/ports/i_team_track_source.h"

namespace team::infra
{

class TeamTrackSourceGps : public team::ITeamTrackSource
{
  public:
    bool readTrackPoint(proto::TeamTrackPoint* out_point) override;
};

} // namespace team::infra
