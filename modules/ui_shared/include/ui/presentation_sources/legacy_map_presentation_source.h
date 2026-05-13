#pragma once

#include "platform/ui/team_ui_store_runtime.h"
#include "ui/presentation_sources/legacy_map_presentation_state.h"
#include "ui_presentation/gps/gps_status_source.h"
#include "ui_presentation/map/map_presentation_source.h"

namespace ui::presentation_sources
{

// LegacyMapPresentationSource is a Phase 5.7 compatibility read adapter.
//
// Pattern:
//   CQRS read model / projection / anti-corruption adapter.
//
// It may read legacy GPS and Team UI compatibility surfaces and project them
// into ui_presentation::map snapshots.
//
// It must not:
//   - load map tiles
//   - mutate viewport state
//   - access LVGL widgets
//   - open SD card tile paths
//   - perform route planning
//   - expose Team raw payloads or rich marker objects
class LegacyMapPresentationSource final : public ui::map::IMapPresentationSource
{
  public:
    LegacyMapPresentationSource(ui::gps::IGpsStatusSource& gps_source,
                                const LegacyMapPresentationState& state,
                                team::ui::ITeamUiStore* team_store = nullptr);

    bool buildMapWorkspaceSnapshot(
        const ui::map::MapWorkspaceRequest& request,
        ui::map::MapWorkspaceSnapshot& out) const override;

  private:
    ui::gps::IGpsStatusSource& gps_source_;
    const LegacyMapPresentationState& state_;
    team::ui::ITeamUiStore* team_store_ = nullptr;
};

} // namespace ui::presentation_sources
