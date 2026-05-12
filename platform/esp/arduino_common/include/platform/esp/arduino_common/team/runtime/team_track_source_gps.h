#pragma once

#include "gps/ports/i_location_source.h"
#include "team/ports/i_team_track_source.h"

namespace team::infra
{

class TeamTrackSourceGps : public team::ITeamTrackSource
{
  public:
    TeamTrackSourceGps();
    explicit TeamTrackSourceGps(const gps::ILocationSource& location_source);

    bool readTrackPoint(proto::TeamTrackPoint* out_point) override;

  private:
    const gps::ILocationSource* location_source_ = nullptr;
};

} // namespace team::infra
