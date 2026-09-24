#pragma once

#include "ui_presentation/map/map_workspace_snapshot.h"

namespace ui::map
{
// Caller-owned route context. Must outlive the temporary Map page; no widgets,
// storage or Agenda dependencies. Result coordinates are always WGS84.
struct MapLocationRequest
{
    MapViewport initial_viewport{};
    MapLocationSelection result{};
    bool has_initial_viewport = false;
};
static_assert(sizeof(MapLocationRequest) <= 64, "Map request must remain a small return context");
} // namespace ui::map
