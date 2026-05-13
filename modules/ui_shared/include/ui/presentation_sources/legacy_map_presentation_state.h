#pragma once

#include "ui_presentation/map/map_workspace_snapshot.h"

namespace ui::presentation_sources
{

// LegacyMapPresentationState is adapter-local compatibility state for Phase 5.7.
//
// It is not a map engine, tile cache, settings store, or GPS runtime. It only
// carries presentation-level toggles that the Phase 5.7 legacy source/sink pair
// share while map renderer/runtime ownership remains legacy.
struct LegacyMapPresentationState
{
    ui::map::MapLayerState layers{};
    ui::map::MeasurementState measurement{};
    ui::map::MapViewport last_viewport{};
    ui::map::MapToolKind active_tool = ui::map::MapToolKind::Pan;

    bool has_viewport = false;
    bool can_zoom_in = true;
    bool can_zoom_out = true;
    bool can_toggle_layers = true;
};

LegacyMapPresentationState& legacy_map_presentation_state();

} // namespace ui::presentation_sources
