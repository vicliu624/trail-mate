#pragma once

#include "../protocol/team_track.h"

namespace team
{

class ITeamTrackSource
{
  public:
    virtual ~ITeamTrackSource() = default;
    virtual bool readTrackPoint(proto::TeamTrackPoint* out_point) = 0;
};

} // namespace team
