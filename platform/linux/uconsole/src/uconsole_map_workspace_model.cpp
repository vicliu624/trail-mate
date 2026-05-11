#include "uconsole/uconsole_map_workspace_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include "app/linux_app_services.h"
#include "chat/usecase/contact_service.h"
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
constexpr const char* kShowMqttNodesKey = "show_mqtt_nodes";
constexpr const char* kContourUltraFineKey = "contour_ultra_fine";
constexpr const char* kEarthdataTokenKey = "earthdata_token";
constexpr const char* kManualCenterActiveKey = "manual_center_active";
constexpr const char* kManualCenterLatE7Key = "manual_center_lat_e7";
constexpr const char* kManualCenterLonE7Key = "manual_center_lon_e7";
constexpr int kMinZoom = 1;
constexpr int kMaxZoom = 18;
constexpr int kDefaultWorldZoom = 2;
constexpr double kDefaultWorldLat = 0.0;
constexpr double kDefaultWorldLon = 0.0;
constexpr int kLandscapeTileRadiusX = 2;
constexpr int kLandscapeTileRadiusY = 1;
constexpr int kTileSizePx = 256;
constexpr double kPi = 3.14159265358979323846;

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

double clamp_web_mercator_lat(double lat)
{
    return std::clamp(lat, -85.05112878, 85.05112878);
}

double longitude_to_world_px(double lon, int zoom)
{
    const double tiles = static_cast<double>(1U << zoom);
    return ((lon + 180.0) / 360.0) * tiles * kTileSizePx;
}

double latitude_to_world_px(double lat, int zoom)
{
    const double clamped_lat = clamp_web_mercator_lat(lat);
    const double lat_rad = clamped_lat * kPi / 180.0;
    const double tiles = static_cast<double>(1U << zoom);
    const double mercator =
        std::log(std::tan(lat_rad) + (1.0 / std::cos(lat_rad)));
    return ((1.0 - mercator / kPi) / 2.0) * tiles * kTileSizePx;
}

double world_px_to_longitude(double x, int zoom)
{
    const double world_px =
        static_cast<double>(1U << zoom) * static_cast<double>(kTileSizePx);
    double wrapped = std::fmod(x, world_px);
    if (wrapped < 0.0)
    {
        wrapped += world_px;
    }
    return (wrapped / world_px) * 360.0 - 180.0;
}

double world_px_to_latitude(double y, int zoom)
{
    const double world_px =
        static_cast<double>(1U << zoom) * static_cast<double>(kTileSizePx);
    const double clamped = std::clamp(y, 0.0, world_px);
    const double n = kPi - (2.0 * kPi * clamped / world_px);
    return std::atan(std::sinh(n)) * 180.0 / kPi;
}

int coordinate_to_e7(double value)
{
    return static_cast<int>(std::lround(value * 10000000.0));
}

double e7_to_coordinate(int value)
{
    return static_cast<double>(value) / 10000000.0;
}

std::string trim_copy(std::string value)
{
    auto not_space = [](unsigned char ch)
    {
        return std::isspace(ch) == 0;
    };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
                value.end());
    return value;
}

std::string node_label(const ::chat::contacts::NodeInfo& node)
{
    if (!node.display_name.empty())
    {
        return node.display_name;
    }
    if (node.short_name[0] != '\0')
    {
        return std::string(node.short_name);
    }

    char label[12] = {};
    std::snprintf(label, sizeof(label), "%04X",
                  static_cast<unsigned>(node.node_id & 0xFFFFU));
    return std::string(label);
}

void append_projected_node(MapWorkspaceSnapshot& out,
                           const ::chat::contacts::NodeInfo& node,
                           const ::platform::linux_runtime::MapTileId& top_left)
{
    if (!node.position.valid)
    {
        return;
    }

    const double lat =
        static_cast<double>(node.position.latitude_i) / 10000000.0;
    const double lon =
        static_cast<double>(node.position.longitude_i) / 10000000.0;
    if (!std::isfinite(lat) || !std::isfinite(lon))
    {
        return;
    }

    if (node.via_mqtt && !out.show_mqtt_nodes)
    {
        ++out.hidden_mqtt_node_count;
        return;
    }

    const double map_width_px =
        static_cast<double>(std::max<std::size_t>(1U, out.columns)) *
        static_cast<double>(kTileSizePx);
    const double map_height_px =
        static_cast<double>(std::max<std::size_t>(1U, out.rows)) *
        static_cast<double>(kTileSizePx);
    const double world_width_px =
        static_cast<double>(1U << out.zoom) * static_cast<double>(kTileSizePx);
    const double left_px =
        static_cast<double>(top_left.x) * static_cast<double>(kTileSizePx);
    const double top_px =
        static_cast<double>(top_left.y) * static_cast<double>(kTileSizePx);

    double x = longitude_to_world_px(lon, out.zoom) - left_px;
    if (x < 0.0)
    {
        x += world_width_px;
    }
    if (x > map_width_px && (x - world_width_px) >= 0.0)
    {
        x -= world_width_px;
    }
    const double y = latitude_to_world_px(lat, out.zoom) - top_px;

    if (x < 0.0 || x > map_width_px || y < 0.0 || y > map_height_px)
    {
        return;
    }

    MapNodeOverlayItem item{};
    item.node_id = node.node_id;
    item.label = node_label(node);
    item.lat = lat;
    item.lon = lon;
    item.x_fraction = map_width_px > 0.0 ? x / map_width_px : 0.0;
    item.y_fraction = map_height_px > 0.0 ? y / map_height_px : 0.0;
    item.via_mqtt = node.via_mqtt;
    item.is_contact = node.is_contact;
    item.last_seen = node.last_seen;
    item.rssi = node.rssi;
    item.snr = node.snr;
    item.hops_away = node.hops_away;
    item.channel = node.channel;
    item.has_altitude = node.position.has_altitude;
    item.altitude_m = node.position.altitude;
    out.nodes.push_back(std::move(item));
    ++out.visible_node_count;
    if (node.via_mqtt)
    {
        ++out.visible_mqtt_node_count;
    }
}

} // namespace

UConsoleMapWorkspaceModel::UConsoleMapWorkspaceModel(
    linux_app::LinuxAppServices& services)
    : services_(services),
      zoom_(std::clamp(
          platform::ui::settings_store::get_int(kMapNamespace, kZoomKey, 14),
          kMinZoom,
          kMaxZoom)),
      manual_center_active_(platform::ui::settings_store::get_bool(
          kMapNamespace, kManualCenterActiveKey, false)),
      manual_center_lat_(e7_to_coordinate(platform::ui::settings_store::get_int(
          kMapNamespace, kManualCenterLatE7Key, 0))),
      manual_center_lon_(e7_to_coordinate(platform::ui::settings_store::get_int(
          kMapNamespace, kManualCenterLonE7Key, 0)))
{
}

MapWorkspaceSnapshot UConsoleMapWorkspaceModel::snapshot() const
{
    MapWorkspaceSnapshot out{};
    out.zoom = zoom_;
    out.source_label =
        ::platform::linux_runtime::map_base_source_label(source());
    out.cache_stats = tile_cache_.stats();
    out.show_mqtt_nodes = showMqttNodes();
    out.contour_enabled = contourEnabled();
    out.contour_ultra_fine_enabled = contourUltraFineEnabled();
    out.earthdata_token_configured = earthdataTokenConfigured();

    double configured_lat = 0.0;
    double configured_lon = 0.0;
    const bool has_configured_center =
        configured_map_center(configured_lat, configured_lon);
    if (manual_center_active_)
    {
        out.has_center = true;
        out.has_fix = false;
        out.has_manual_center = true;
        out.lat = manual_center_lat_;
        out.lon = manual_center_lon_;
        out.fix_label = "Panned map center";
    }
    else if (has_configured_center)
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

    if (out.contour_enabled)
    {
        const auto profiles =
            ::platform::linux_runtime::contour_profiles_for_zoom(
                out.zoom, out.contour_ultra_fine_enabled);
        out.contour_profiles.reserve(profiles.size());
        out.contour_tiles.reserve(out.tiles.size() * profiles.size());

        for (const auto& profile : profiles)
        {
            out.contour_profiles.push_back(
                ::platform::linux_runtime::map_contour_profile_key(profile));
        }

        for (std::size_t index = 0; index < out.tiles.size(); ++index)
        {
            const auto& base_id = out.tiles[index].id;
            for (const auto& profile : profiles)
            {
                ::platform::linux_runtime::MapContourTileId id{};
                id.profile = profile;
                id.z = base_id.z;
                id.x = base_id.x;
                id.y = base_id.y;

                MapContourTileItem item{};
                item.id = id;
                item.path = contour_store_.existing_tile_path(id);
                item.base_tile_index = index;
                item.available = contour_store_.tile_available(id);
                if (item.available)
                {
                    ++out.contour_available_count;
                }
                else
                {
                    ++out.contour_missing_count;
                }
                out.contour_tiles.push_back(std::move(item));
            }
        }
    }

    if (!out.tiles.empty())
    {
        const auto top_left = out.tiles.front().id;
        const auto contacts = services_.contacts().getContacts();
        const auto nearby = services_.contacts().getNearby();
        out.nodes.reserve(contacts.size() + nearby.size());
        for (const auto& node : contacts)
        {
            append_projected_node(out, node, top_left);
        }
        for (const auto& node : nearby)
        {
            append_projected_node(out, node, top_left);
        }
    }

    return out;
}

MapWorkspaceSnapshot UConsoleMapWorkspaceModel::snapshotAround(
    double lat,
    double lon,
    int zoom,
    int radius_x,
    int radius_y) const
{
    MapWorkspaceSnapshot out{};
    if (!std::isfinite(lat) || !std::isfinite(lon))
    {
        return out;
    }

    const int clamped_radius_x = std::clamp(radius_x, 0, 4);
    const int clamped_radius_y = std::clamp(radius_y, 0, 4);
    out.has_center = true;
    out.has_configured_center = true;
    out.lat = clamp_web_mercator_lat(lat);
    out.lon = std::clamp(lon, -180.0, 180.0);
    out.zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    out.source_label =
        ::platform::linux_runtime::map_base_source_label(source());
    out.fix_label = "NodeInfo";
    out.columns = static_cast<std::size_t>(clamped_radius_x * 2 + 1);
    out.rows = static_cast<std::size_t>(clamped_radius_y * 2 + 1);
    out.center_tile_index =
        static_cast<std::size_t>(clamped_radius_y) * out.columns +
        static_cast<std::size_t>(clamped_radius_x);
    out.tiles.reserve(out.columns * out.rows);

    const auto tiles = ::platform::linux_runtime::map_tiles_around(
        out.lat,
        out.lon,
        out.zoom,
        source(),
        clamped_radius_x,
        clamped_radius_y);
    for (const auto& tile : tiles)
    {
        MapTileItem item{};
        item.id = tile;
        item.path = tile_cache_.tile_path(tile);
        item.available = tile_cache_.tile_available(tile);
        out.tiles.push_back(std::move(item));
    }
    out.cache_stats = tile_cache_.stats();
    return out;
}

::platform::linux_runtime::MapTileResult
UConsoleMapWorkspaceModel::ensureTile(
    const ::platform::linux_runtime::MapTileId& tile) const
{
    return tile_cache_.ensure_tile(tile);
}

::platform::linux_runtime::MapContourGenerationResult
UConsoleMapWorkspaceModel::ensureContourTiles(
    const std::vector<::platform::linux_runtime::MapContourTileId>& tiles) const
{
    return contour_generator_.ensure_tiles(tiles, earthdataToken());
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

void UConsoleMapWorkspaceModel::setShowMqttNodes(bool enabled)
{
    ::platform::ui::settings_store::put_bool(
        kMapNamespace, kShowMqttNodesKey, enabled);
}

void UConsoleMapWorkspaceModel::setContourEnabled(bool enabled)
{
    services_.config().map_contour_enabled = enabled;
    services_.saveConfig();
}

void UConsoleMapWorkspaceModel::setContourUltraFineEnabled(bool enabled)
{
    ::platform::ui::settings_store::put_bool(
        kMapNamespace, kContourUltraFineKey, enabled);
}

void UConsoleMapWorkspaceModel::setEarthdataToken(const std::string& token)
{
    const std::string trimmed = trim_copy(token);
    (void)::platform::ui::settings_store::put_string(
        kMapNamespace, kEarthdataTokenKey, trimmed.c_str());
}

bool UConsoleMapWorkspaceModel::contourUltraFineEnabled() const
{
    return ::platform::ui::settings_store::get_bool(
        kMapNamespace, kContourUltraFineKey, false);
}

std::string UConsoleMapWorkspaceModel::earthdataToken() const
{
    std::string token{};
    (void)::platform::ui::settings_store::get_string(
        kMapNamespace, kEarthdataTokenKey, token);
    return token;
}

bool UConsoleMapWorkspaceModel::earthdataTokenConfigured() const
{
    return !earthdataToken().empty();
}

MapCoordinate UConsoleMapWorkspaceModel::coordinateAtDisplayPoint(
    const MapWorkspaceSnapshot& snapshot,
    double display_x,
    double display_y,
    int display_width,
    int display_height) const
{
    MapCoordinate out{};
    if (!snapshot.has_center || snapshot.tiles.empty() ||
        display_width <= 0 || display_height <= 0 ||
        !std::isfinite(display_x) || !std::isfinite(display_y))
    {
        return out;
    }

    const auto top_left = snapshot.tiles.front().id;
    const double viewport_width_px =
        static_cast<double>(std::max<std::size_t>(1U, snapshot.columns)) *
        static_cast<double>(kTileSizePx);
    const double viewport_height_px =
        static_cast<double>(std::max<std::size_t>(1U, snapshot.rows)) *
        static_cast<double>(kTileSizePx);
    const double world_px =
        static_cast<double>(1U << snapshot.zoom) *
        static_cast<double>(kTileSizePx);
    const double normalized_x =
        std::clamp(display_x / static_cast<double>(display_width), 0.0, 1.0);
    const double normalized_y =
        std::clamp(display_y / static_cast<double>(display_height), 0.0, 1.0);
    const double world_x =
        static_cast<double>(top_left.x) * static_cast<double>(kTileSizePx) +
        normalized_x * viewport_width_px;
    const double world_y = std::clamp(
        static_cast<double>(top_left.y) * static_cast<double>(kTileSizePx) +
            normalized_y * viewport_height_px,
        0.0,
        world_px);

    out.valid = true;
    out.lat = world_px_to_latitude(world_y, snapshot.zoom);
    out.lon = world_px_to_longitude(world_x, snapshot.zoom);
    return out;
}

void UConsoleMapWorkspaceModel::centerOn(double lat,
                                         double lon,
                                         bool persist)
{
    if (!std::isfinite(lat) || !std::isfinite(lon))
    {
        return;
    }

    manual_center_active_ = true;
    manual_center_lat_ = clamp_web_mercator_lat(lat);
    manual_center_lon_ = std::clamp(lon, -180.0, 180.0);
    if (persist)
    {
        persistManualCenter();
    }
}

void UConsoleMapWorkspaceModel::zoomInAt(double lat, double lon)
{
    setZoom(zoom_ + 1);
    centerOn(lat, lon, true);
}

void UConsoleMapWorkspaceModel::zoomOutAt(double lat, double lon)
{
    setZoom(zoom_ - 1);
    centerOn(lat, lon, true);
}

void UConsoleMapWorkspaceModel::panByDisplayDelta(double drag_dx,
                                                  double drag_dy,
                                                  int display_width,
                                                  int display_height,
                                                  double start_lat,
                                                  double start_lon,
                                                  int start_zoom,
                                                  bool persist)
{
    if (display_width <= 0 || display_height <= 0 ||
        !std::isfinite(drag_dx) || !std::isfinite(drag_dy) ||
        !std::isfinite(start_lat) || !std::isfinite(start_lon))
    {
        return;
    }

    zoom_ = std::clamp(start_zoom, kMinZoom, kMaxZoom);

    const double viewport_width_px =
        static_cast<double>(kLandscapeTileRadiusX * 2 + 1) *
        static_cast<double>(kTileSizePx);
    const double viewport_height_px =
        static_cast<double>(kLandscapeTileRadiusY * 2 + 1) *
        static_cast<double>(kTileSizePx);
    const double world_px =
        static_cast<double>(1U << zoom_) * static_cast<double>(kTileSizePx);

    double center_x = longitude_to_world_px(start_lon, zoom_);
    double center_y = latitude_to_world_px(start_lat, zoom_);
    center_x -= (drag_dx / static_cast<double>(display_width)) *
                viewport_width_px;
    center_y -= (drag_dy / static_cast<double>(display_height)) *
                viewport_height_px;
    center_y = std::clamp(center_y, 0.0, world_px);

    centerOn(world_px_to_latitude(center_y, zoom_),
             world_px_to_longitude(center_x, zoom_),
             persist);
    if (persist)
    {
        persistZoom();
    }
}

void UConsoleMapWorkspaceModel::clearManualCenter()
{
    manual_center_active_ = false;
    manual_center_lat_ = 0.0;
    manual_center_lon_ = 0.0;
    clearPersistedManualCenter();
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

bool UConsoleMapWorkspaceModel::showMqttNodes() const
{
    return ::platform::ui::settings_store::get_bool(
        kMapNamespace, kShowMqttNodesKey, true);
}

bool UConsoleMapWorkspaceModel::contourEnabled() const
{
    return services_.config().map_contour_enabled;
}

void UConsoleMapWorkspaceModel::persistZoom() const
{
    ::platform::ui::settings_store::put_int(kMapNamespace, kZoomKey, zoom_);
}

void UConsoleMapWorkspaceModel::persistManualCenter() const
{
    ::platform::ui::settings_store::put_bool(
        kMapNamespace, kManualCenterActiveKey, manual_center_active_);
    ::platform::ui::settings_store::put_int(
        kMapNamespace, kManualCenterLatE7Key,
        coordinate_to_e7(manual_center_lat_));
    ::platform::ui::settings_store::put_int(
        kMapNamespace, kManualCenterLonE7Key,
        coordinate_to_e7(manual_center_lon_));
}

void UConsoleMapWorkspaceModel::clearPersistedManualCenter() const
{
    constexpr const char* keys[] = {
        kManualCenterActiveKey,
        kManualCenterLatE7Key,
        kManualCenterLonE7Key,
    };
    ::platform::ui::settings_store::remove_keys(
        kMapNamespace, keys, sizeof(keys) / sizeof(keys[0]));
}

} // namespace trailmate::uconsole
