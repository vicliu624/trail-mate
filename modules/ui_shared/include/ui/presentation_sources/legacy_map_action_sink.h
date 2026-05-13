#pragma once

#include "ui/presentation_sources/legacy_map_presentation_state.h"
#include "ui_presentation/gps/gps_status_source.h"
#include "ui_presentation/map/map_action_sink.h"

namespace ui::presentation_sources
{

// LegacyMapActionSink is a Phase 5.7 compatibility command adapter.
//
// Pattern:
//   Command Sink / anti-corruption adapter.
//
// It translates presentation-level map actions into compatibility state while
// tile rendering, GPS runtime, and rich overlay ownership remain legacy.
//
// It must not:
//   - build MapWorkspaceSnapshot
//   - touch LVGL widgets
//   - load tile images
//   - parse GPS data
//   - write map storage directly
//   - perform route planning or packet/radio work
class LegacyMapActionSink final : public ui::map::IMapActionSink
{
  public:
    LegacyMapActionSink(ui::gps::IGpsStatusSource& gps_source,
                        LegacyMapPresentationState& state);

    ui::UiActionResult centerOnSelf() override;
    ui::UiActionResult setViewport(const ui::map::MapViewport& viewport) override;
    ui::UiActionResult setLayer(ui::map::MapLayerKind layer,
                                bool enabled) override;
    ui::UiActionResult setActiveTool(ui::map::MapToolKind tool) override;
    ui::UiActionResult clearMeasurement() override;

  private:
    ui::gps::IGpsStatusSource& gps_source_;
    LegacyMapPresentationState& state_;
};

} // namespace ui::presentation_sources
