#pragma once

#include "ui/map_overlay/map_overlay_projector.h"
#include "ui_presentation/map/map_overlay_source.h"

#include <cstddef>
#include <cstdint>

namespace ui
{
namespace presentation_sources
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

class LegacyMapOverlaySource final : public ::ui::map::IMapOverlayPresentationSource
{
  public:
    LegacyMapOverlaySource(IMapOverlayGpsSource* gps,
                           IMapOverlayTeamSource* team);

    bool buildMapOverlaySnapshot(::ui::map::MapOverlaySnapshot& out) const override;

  private:
    IMapOverlayGpsSource* gps_ = nullptr;
    IMapOverlayTeamSource* team_ = nullptr;
    ::ui::map_overlay::MapOverlayProjector projector_;
};

} // namespace presentation_sources
} // namespace ui
