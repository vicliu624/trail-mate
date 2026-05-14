#pragma once

#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/common/snapshot_header.h"

#include <cstddef>
#include <cstdint>

namespace ui
{
namespace map
{

enum class MapOverlayKind : uint8_t
{
    CurrentPosition,
    TeamMember,
    RoutePoint,
    TrackPoint,
    MeasurementPoint,
    SelectedTarget,
    Warning,
};

enum class MapOverlayStyle : uint8_t
{
    Default,
    OwnPosition,
    Team,
    Route,
    Track,
    Warning,
};

struct MapGeoPoint
{
    double lat = 0.0;
    double lon = 0.0;
    bool valid = false;
};

struct MapOverlayItem
{
    MapOverlayKind kind = MapOverlayKind::CurrentPosition;
    MapOverlayStyle style = MapOverlayStyle::Default;

    MapGeoPoint point;

    ui::FixedText<32> label;
    ui::FixedText<64> detail;

    uint32_t stable_id = 0;
    uint8_t icon = 0;
    bool selected = false;
    bool visible = true;
};

struct MapOverlaySnapshot
{
    ui::SnapshotHeader header;

    static constexpr std::size_t kMaxItems = 64;
    MapOverlayItem items[kMaxItems]{};
    std::size_t item_count = 0;

    bool truncated = false;
};

} // namespace map
} // namespace ui
