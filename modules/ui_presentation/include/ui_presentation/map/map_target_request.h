#pragma once

#include "ui_presentation/map/map_overlay_snapshot.h"
#include "ui_presentation/map/map_workspace_model.h"

#include <cmath>

namespace ui::map
{
// Caller-owned context for displaying a WGS84 destination in the existing Map.
// Must outlive that page. It does not calculate a route or own renderer state.
struct MapTargetRequest
{
    MapViewport viewport{};
    ui::FixedText<32> label{};
    bool entered = false;

    bool valid() const
    {
        return std::isfinite(viewport.center_lat) && std::isfinite(viewport.center_lon) &&
               viewport.center_lat >= -90.0 && viewport.center_lat <= 90.0 &&
               viewport.center_lon >= -180.0 && viewport.center_lon <= 180.0;
    }

    ui::UiActionResult focus(MapWorkspaceModel& model, uint8_t default_zoom) const
    {
        if (!valid()) return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        if (model.locationSelection().state == MapLocationSelectionState::Selecting)
            return ui::UiActionResult::fail(ui::UiActionFailure::Busy);
        auto initial = viewport;
        if (initial.zoom == 0) initial.zoom = default_zoom;
        return model.setViewport(initial);
    }

    // Append after optional information filtering. The requested destination
    // has priority over the last non-self annotation if the fixed list is full.
    void appendOverlay(MapOverlaySnapshot& snapshot) const
    {
        if (!valid()) return;
        std::size_t index = snapshot.item_count;
        if (index >= MapOverlaySnapshot::kMaxItems)
        {
            index = MapOverlaySnapshot::kMaxItems - 1;
            while (index > 0 && snapshot.items[index].kind == MapOverlayKind::CurrentPosition) --index;
            snapshot.item_count = MapOverlaySnapshot::kMaxItems;
            snapshot.truncated = true;
        }
        else
        {
            ++snapshot.item_count;
        }
        auto& item = snapshot.items[index];
        item = {};
        item.kind = MapOverlayKind::SelectedTarget;
        item.style = MapOverlayStyle::Warning;
        item.point = {viewport.center_lat, viewport.center_lon, true};
        item.label = label;
        item.label.data[sizeof(item.label.data) - 1] = '\0';
        item.selected = true;
    }
};
static_assert(sizeof(MapTargetRequest) <= 64, "Destination route context must remain bounded");
} // namespace ui::map
