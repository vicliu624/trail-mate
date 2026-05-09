#include "uconsole/uconsole_map_workspace_model.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

#include "app/linux_app_services.h"
#include "platform/linux/env_config.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/settings_store.h"

namespace trailmate::uconsole
{
namespace
{

constexpr const char* kMapNamespace = "uconsole_map";
constexpr const char* kZoomKey = "zoom";
constexpr int kMinZoom = 1;
constexpr int kMaxZoom = 18;

bool parse_env_double(const char* name, double& out)
{
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return false;
    }

    char* end = nullptr;
    const double parsed = std::strtod(value, &end);
    if (end == value || (end != nullptr && *end != '\0') ||
        !std::isfinite(parsed))
    {
        return false;
    }

    out = parsed;
    return true;
}

bool external_gps_source_configured()
{
    const char* names[] = {
        "TRAIL_MATE_GPS_DEVICE",
        "TRAIL_MATE_GPS_NMEA_FILE",
        "TRAIL_MATE_GPS_VALID",
        "TRAIL_MATE_GPS_LAT",
        "TRAIL_MATE_GPS_LNG",
    };

    for (const char* name : names)
    {
        const char* value = std::getenv(name);
        if (value != nullptr && value[0] != '\0')
        {
            return true;
        }
    }
    return false;
}

bool configured_map_center(double& lat, double& lon)
{
    return parse_env_double("TRAIL_MATE_MAP_LAT", lat) &&
           parse_env_double("TRAIL_MATE_MAP_LNG", lon);
}

} // namespace

UConsoleMapWorkspaceModel::UConsoleMapWorkspaceModel(
    linux_app::LinuxAppServices& services)
    : services_(services),
      zoom_(std::clamp(
          platform::ui::settings_store::get_int(kMapNamespace, kZoomKey, 14),
          kMinZoom,
          kMaxZoom))
{
}

MapWorkspaceSnapshot UConsoleMapWorkspaceModel::snapshot() const
{
    MapWorkspaceSnapshot out{};
    out.zoom = zoom_;
    out.source_label =
        ::platform::linux_runtime::map_base_source_label(source());
    out.cache_stats = tile_cache_.stats();

    double configured_lat = 0.0;
    double configured_lon = 0.0;
    const bool has_configured_center =
        configured_map_center(configured_lat, configured_lon);
    if (has_configured_center)
    {
        out.has_fix = true;
        out.has_configured_center = true;
        out.lat = configured_lat;
        out.lon = configured_lon;
        out.fix_label = "Configured center";
    }
    else
    {
        const bool has_external_source = external_gps_source_configured();
        const auto gps = ::platform::ui::gps::get_data();
        out.has_fix = has_external_source && gps.valid;
        out.lat = gps.lat;
        out.lon = gps.lng;
        out.altitude_m = gps.alt_m;
        out.has_altitude = gps.has_alt;
        out.speed_mps = gps.speed_mps;
        out.has_speed = gps.has_speed;
        out.satellites = gps.satellites;
        out.fix_label = out.has_fix ? "GPS fix" : "Waiting for GPS/NMEA";
    }

    if (!out.has_fix)
    {
        return out;
    }

    const auto ids = ::platform::linux_runtime::map_tiles_around(
        out.lat, out.lon, out.zoom, source(), 1, 1);
    out.tiles.reserve(ids.size());
    for (const auto& id : ids)
    {
        MapTileItem item{};
        item.id = id;
        item.path = tile_cache_.tile_path(id);
        item.available = tile_cache_.tile_available(id);
        out.tiles.push_back(std::move(item));
    }

    return out;
}

::platform::linux_runtime::MapTileResult
UConsoleMapWorkspaceModel::ensureTile(
    const ::platform::linux_runtime::MapTileId& tile) const
{
    return tile_cache_.ensure_tile(tile);
}

void UConsoleMapWorkspaceModel::setSource(
    ::platform::linux_runtime::MapBaseSource source_value)
{
    services_.config().map_source = static_cast<std::uint8_t>(source_value);
    services_.saveConfig();
}

void UConsoleMapWorkspaceModel::zoomIn()
{
    zoom_ = std::clamp(zoom_ + 1, kMinZoom, kMaxZoom);
    persistZoom();
}

void UConsoleMapWorkspaceModel::zoomOut()
{
    zoom_ = std::clamp(zoom_ - 1, kMinZoom, kMaxZoom);
    persistZoom();
}

::platform::linux_runtime::MapBaseSource
UConsoleMapWorkspaceModel::source() const
{
    return ::platform::linux_runtime::sanitize_map_base_source(
        services_.config().map_source);
}

void UConsoleMapWorkspaceModel::persistZoom() const
{
    ::platform::ui::settings_store::put_int(kMapNamespace, kZoomKey, zoom_);
}

} // namespace trailmate::uconsole
