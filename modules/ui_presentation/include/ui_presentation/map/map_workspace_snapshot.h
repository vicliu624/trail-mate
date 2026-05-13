#pragma once

#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/common/snapshot_header.h"

#include <stdint.h>

namespace ui::map
{

enum class MapLayerKind : uint8_t
{
    Osm,
    Terrain,
    Satellite,
    Contour,
};

enum class MapToolKind : uint8_t
{
    None,
    Pan,
    CenterOnSelf,
    MeasureDistance,
};

struct MapViewport
{
    double center_lat = 0.0;
    double center_lon = 0.0;
    uint8_t zoom = 0;
};

struct SelfLocationOverlay
{
    bool valid = false;
    double lat = 0.0;
    double lon = 0.0;
    float course_deg = 0.0f;
    float accuracy_m = 0.0f;
};

struct MapLayerState
{
    bool osm = true;
    bool terrain = false;
    bool satellite = false;
    bool contour = false;
};

struct TeamOverlaySummary
{
    bool available = false;
    uint16_t visible_members = 0;
    uint16_t stale_members = 0;
};

struct MeasurementState
{
    bool active = false;
    double start_lat = 0.0;
    double start_lon = 0.0;
    double end_lat = 0.0;
    double end_lon = 0.0;
    float distance_m = 0.0f;
};

struct MapWorkspaceSnapshot
{
    ui::SnapshotHeader header;

    MapViewport viewport;
    SelfLocationOverlay self;
    MapLayerState layers;
    TeamOverlaySummary team;
    MeasurementState measurement;

    MapToolKind active_tool = MapToolKind::Pan;

    bool can_center_on_self = false;
    bool can_zoom_in = false;
    bool can_zoom_out = false;
    bool can_toggle_layers = false;

    ui::FixedText<64> status_line;
};

} // namespace ui::map
