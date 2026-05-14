#include "ui_map_runtime/map_overlay/map_overlay_projector.h"

#include "ui_presentation/common/fixed_text.h"

#include <cmath>
#include <cstdio>

namespace ui
{
namespace map_overlay
{
namespace
{

bool validCoordinate(double lat, double lon, bool valid)
{
    return valid &&
           std::isfinite(lat) &&
           std::isfinite(lon) &&
           lat >= -90.0 &&
           lat <= 90.0 &&
           lon >= -180.0 &&
           lon <= 180.0;
}

uint32_t stableIdForCurrentPosition()
{
    return 0x01U;
}

uint32_t stableIdForTeamMember(uint32_t node_id)
{
    return 0x54000000U ^ node_id;
}

void copyCoordinateDetail(::ui::map::MapOverlayItem& item)
{
    char detail[64]{};
    std::snprintf(detail,
                  sizeof(detail),
                  "%.5f, %.5f",
                  item.point.lat,
                  item.point.lon);
    ui::copyText(item.detail, detail);
}

} // namespace

bool MapOverlayProjector::append(::ui::map::MapOverlaySnapshot& out,
                                 const ::ui::map::MapOverlayItem& item) const
{
    if (out.item_count >= ::ui::map::MapOverlaySnapshot::kMaxItems)
    {
        out.truncated = true;
        return false;
    }

    out.items[out.item_count] = item;
    ++out.item_count;
    return true;
}

bool MapOverlayProjector::projectCurrentPosition(
    double lat,
    double lon,
    bool valid,
    ::ui::map::MapOverlaySnapshot& out) const
{
    if (!validCoordinate(lat, lon, valid))
    {
        return false;
    }

    ::ui::map::MapOverlayItem item{};
    item.kind = ::ui::map::MapOverlayKind::CurrentPosition;
    item.style = ::ui::map::MapOverlayStyle::OwnPosition;
    item.point.lat = lat;
    item.point.lon = lon;
    item.point.valid = true;
    item.stable_id = stableIdForCurrentPosition();
    item.icon = 0;
    item.visible = true;
    ui::copyText(item.label, "You");
    copyCoordinateDetail(item);
    return append(out, item);
}

bool MapOverlayProjector::projectTeamMember(
    uint32_t node_id,
    const char* label,
    double lat,
    double lon,
    bool valid,
    ::ui::map::MapOverlaySnapshot& out) const
{
    if (node_id == 0 || !validCoordinate(lat, lon, valid))
    {
        return false;
    }

    ::ui::map::MapOverlayItem item{};
    item.kind = ::ui::map::MapOverlayKind::TeamMember;
    item.style = ::ui::map::MapOverlayStyle::Team;
    item.point.lat = lat;
    item.point.lon = lon;
    item.point.valid = true;
    item.stable_id = stableIdForTeamMember(node_id);
    item.icon = static_cast<uint8_t>(node_id & 0xFFU);
    item.visible = true;
    if (label && label[0] != '\0')
    {
        ui::copyText(item.label, label);
    }
    else
    {
        char fallback[32]{};
        std::snprintf(fallback, sizeof(fallback), "Node %lu", static_cast<unsigned long>(node_id));
        ui::copyText(item.label, fallback);
    }
    copyCoordinateDetail(item);
    return append(out, item);
}

} // namespace map_overlay
} // namespace ui
