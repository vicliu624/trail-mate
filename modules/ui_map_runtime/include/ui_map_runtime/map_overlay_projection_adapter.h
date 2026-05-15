#pragma once

#include "ui_map_runtime/map_overlay/map_overlay_projector.h"
#include "ui_presentation/map/map_overlay_snapshot.h"

#include <cstddef>
#include <cstdint>

namespace ui
{
namespace map_overlay
{

class IMapOverlayGpsSource
{
  public:
    virtual ~IMapOverlayGpsSource() = default;

    virtual bool currentFix(double& lat,
                            double& lon,
                            bool& valid) const = 0;
};

class IMapOverlayTeamSource
{
  public:
    struct TeamPoint
    {
        uint32_t node_id = 0;
        const char* label = nullptr;
        double lat = 0.0;
        double lon = 0.0;
        bool valid = false;
    };

    virtual ~IMapOverlayTeamSource() = default;

    virtual std::size_t latestTeamPoints(TeamPoint* out,
                                         std::size_t capacity) const = 0;
};

class MapOverlayProjectionAdapter
{
  public:
    bool project(const IMapOverlayGpsSource* gps,
                 const IMapOverlayTeamSource* team,
                 ::ui::map::MapOverlaySnapshot& out) const;

  private:
    MapOverlayProjector projector_{};
};

} // namespace map_overlay
} // namespace ui
