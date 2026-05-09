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
#include "uconsole/uconsole_hardware_probe.h"

namespace trailmate::uconsole
{
namespace
{

constexpr const char* kMapNamespace = "uconsole_map";
constexpr const char* kZoomKey = "zoom";
constexpr int kMinZoom = 1;
constexpr int kMaxZoom = 18;
constexpr int kDefaultWorldZoom = 2;
constexpr double kDefaultWorldLat = 0.0;
constexpr double kDefaultWorldLon = 0.0;
constexpr int kLandscapeTileRadiusX = 2;
constexpr int kLandscapeTileRadiusY = 1;

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
    std::string auto_path{};
    return uconsoleAutoGpsSerialPath(auto_path);
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
        out.has_center = true;
        out.has_fix = false;
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
        out.has_center = out.has_fix;
        out.lat = gps.lat;
        out.lon = gps.lng;
        out.altitude_m = gps.alt_m;
        out.has_altitude = gps.has_alt;
        out.speed_mps = gps.speed_mps;
        out.has_speed = gps.has_speed;
        out.satellites = gps.satellites;
        out.fix_label = out.has_fix ? "GPS fix" : "Default map center";
        if (!out.has_center)
        {
            out.has_center = true;
            out.using_default_center = true;
            out.zoom = kDefaultWorldZoom;
            out.lat = kDefaultWorldLat;
            out.lon = kDefaultWorldLon;
            out.altitude_m = 0.0;
            out.has_altitude = false;
            out.speed_mps = 0.0;
            out.has_speed = false;
            out.fix_label =
                has_external_source ? "GPS waiting; OSM world view"
                                    : "OSM world view";
        }
    }

    if (!out.has_center)
    {
        return out;
    }

    out.columns = static_cast<std::size_t>(kLandscapeTileRadiusX * 2 + 1);
    out.rows = static_cast<std::size_t>(kLandscapeTileRadiusY * 2 + 1);
    out.center_tile_index =
        static_cast<std::size_t>(kLandscapeTileRadiusY) * out.columns +
        static_cast<std::size_t>(kLandscapeTileRadiusX);

    const auto ids = ::platform::linux_runtime::map_tiles_around(
        out.lat,
        out.lon,
        out.zoom,
        source(),
        kLandscapeTileRadiusX,
        kLandscapeTileRadiusY);
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

void UConsoleMapWorkspaceModel::setZoom(int zoom)
{
    zoom_ = std::clamp(zoom, kMinZoom, kMaxZoom);
    persistZoom();
}

void UConsoleMapWorkspaceModel::zoomIn()
{
    setZoom(zoom_ + 1);
}

void UConsoleMapWorkspaceModel::zoomOut()
{
    setZoom(zoom_ - 1);
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
