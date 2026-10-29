/**
 * @file map_tiles.cpp
 * @brief Linux map tile runtime for shared map viewport pages.
 */

#include "ui/widgets/map/map_tiles.h"

#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"

#include "ui_map_runtime/map_tiles/filesystem_map_tile_source.h"
#include "ui_map_runtime/map_tiles/map_tile_geometry.h"
#include "ui_map_runtime/map_tiles/map_tile_types.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <map>
#include <string>
#include <vector>

#include "platform/linux/map_contour_tile_generator.h"
#include "platform/linux/map_diagnostics.h"
#include "platform/linux/map_tile_cache.h"
#include "platform/linux/packaged_map.h"
#include "platform/linux/runtime_paths.h"
#include "platform/ui/settings_store.h"
#include "ui_map_runtime/map_tiles/native_pixel_buffer.h"

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxMercatorLat = 85.05112878;
constexpr std::size_t kMaxBaseFetchJobs = 3;
constexpr std::size_t kMaxContourFetchJobs = 1;
constexpr uint32_t kBaseFetchRetryDelayMs = 30000;
constexpr uint32_t kContourFetchRetryDelayMs = 120000;
constexpr const char* kMapSettingsNamespace = "uconsole_map";
constexpr const char* kEarthdataTokenKey = "earthdata_token";

uint8_t g_requested_map_source = 0;
bool g_requested_contour_enabled = false;
bool g_missing_tile_notice_pending = false;
bool g_missing_tile_notice_emitted = false;
bool g_contour_token_missing_logged = false;
uint8_t g_missing_tile_notice_source = 0;
std::weak_ptr<::platform::linux_runtime::PackagedMapSession> g_packaged_session;
std::shared_ptr<::platform::linux_runtime::PackagedMapSession> packaged_metadata_session()
{
    if (auto active = g_packaged_session.lock()) return active;
    static auto probe = std::make_shared<::platform::linux_runtime::PackagedMapSession>(::platform::linux_runtime::resolve_paths().sd_root);
    return probe;
}

struct BaseTileFetchJob
{
    ::platform::linux_runtime::MapTileId tile{};
    std::string key{};
    std::future<::platform::linux_runtime::MapTileResult> future{};
};

struct ContourTileFetchJob
{
    ::platform::linux_runtime::MapContourTileId tile{};
    std::string key{};
    std::future<::platform::linux_runtime::MapContourGenerationResult> future{};
};

struct TileFetchRuntime
{
    std::vector<BaseTileFetchJob> base_jobs{};
    std::vector<ContourTileFetchJob> contour_jobs{};
    std::map<std::string, uint32_t> base_retry_after_ms{};
    std::map<std::string, uint32_t> contour_retry_after_ms{};
};

void delete_tile_object(MapTile& tile);
MapTile* find_tile(TileContext& ctx, int32_t x, int32_t y, int z, uint8_t map_source);

class StdMapTileFileSystem final : public ui::map_tiles::IMapTileFileSystem
{
  public:
    bool exists(const char* path) const override
    {
        return path && std::filesystem::exists(std::filesystem::path(path));
    }

    bool isDirectory(const char* path) const override
    {
        return path && std::filesystem::is_directory(std::filesystem::path(path));
    }

    ui::map_tiles::MapTileReadResult readFile(
        const char* path,
        uint8_t* buffer,
        std::size_t capacity) const override
    {
        if (!path || !buffer || capacity == 0)
        {
            return {ui::map_tiles::MapTileReadStatus::Invalid, 0, -4};
        }

        FILE* file = std::fopen(path, "rb");
        if (!file)
        {
            return {errno == ENOENT ? ui::map_tiles::MapTileReadStatus::Missing
                                    : ui::map_tiles::MapTileReadStatus::Error,
                    0,
                    errno == ENOENT ? -1 : -2};
        }

        const std::size_t bytes_read = std::fread(buffer, 1, capacity, file);
        const bool ok = std::ferror(file) == 0;
        const int close_result = std::fclose(file);
        if (!ok || close_result != 0)
        {
            return {ui::map_tiles::MapTileReadStatus::Error,
                    bytes_read,
                    -2};
        }
        if (bytes_read == 0)
        {
            return {ui::map_tiles::MapTileReadStatus::Invalid, 0, -5};
        }
        return {ui::map_tiles::MapTileReadStatus::Ready, bytes_read, 0};
    }
};

uint32_t now_ms()
{
    using clock = std::chrono::steady_clock;
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
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

std::string earthdata_token()
{
    const char* env_names[] = {
        "TRAIL_MATE_EARTHDATA_TOKEN",
        "TRAIL_MATE_EARTH_DATA_TOKEN",
    };
    for (const char* name : env_names)
    {
        if (const char* value = std::getenv(name))
        {
            std::string token = trim_copy(value);
            if (!token.empty())
            {
                return token;
            }
        }
    }

    std::string persisted{};
    if (::platform::ui::settings_store::get_string(kMapSettingsNamespace,
                                                   kEarthdataTokenKey,
                                                   persisted))
    {
        return trim_copy(persisted);
    }
    return {};
}

std::filesystem::path default_storage_root()
{
    return ::platform::linux_runtime::resolve_paths().sd_root;
}

StdMapTileFileSystem& tile_file_system()
{
    static StdMapTileFileSystem fs;
    return fs;
}

ui::map_tiles::FilesystemMapTileSource& tile_source()
{
    static const std::string root = default_storage_root().string();
    static ui::map_tiles::FilesystemMapTileSource source(
        tile_file_system(),
        root.c_str());
    return source;
}

::platform::linux_runtime::MapTileCache& online_tile_cache()
{
    static ::platform::linux_runtime::MapTileCache cache;
    return cache;
}

::platform::linux_runtime::MapContourTileStore& contour_tile_store()
{
    static ::platform::linux_runtime::MapContourTileStore store;
    return store;
}

::platform::linux_runtime::MapContourTileGenerator& contour_tile_generator()
{
    static ::platform::linux_runtime::MapContourTileGenerator generator;
    return generator;
}

TileFetchRuntime& tile_fetch_runtime()
{
    static TileFetchRuntime runtime;
    return runtime;
}

uint8_t clamp_tile_zoom(int z)
{
    if (z < 0)
    {
        return 0;
    }
    if (z > 255)
    {
        return 255;
    }
    return static_cast<uint8_t>(z);
}

ui::map_tiles::MapTileRef base_tile_ref(int z, int x, int y, uint8_t map_source)
{
    ui::map_tiles::MapTileRef ref{};
    ref.layer = ui::map_tiles::mapTileLayerFromBaseSource(sanitize_map_source(map_source));
    ref.z = clamp_tile_zoom(z);
    ref.x = static_cast<uint32_t>(x < 0 ? 0 : x);
    ref.y = static_cast<uint32_t>(y < 0 ? 0 : y);
    return ref;
}

::platform::linux_runtime::MapTileId base_tile_id(int z, int x, int y, uint8_t map_source)
{
    ::platform::linux_runtime::MapTileId tile{};
    tile.source = ::platform::linux_runtime::sanitize_map_base_source(
        sanitize_map_source(map_source));
    tile.z = z;
    tile.x = x;
    tile.y = y;
    ::platform::linux_runtime::normalize_map_tile(tile);
    return tile;
}

std::string base_fetch_key(const ::platform::linux_runtime::MapTileId& tile)
{
    return std::string(::platform::linux_runtime::map_base_source_key(tile.source)) +
           ":" + std::to_string(tile.z) +
           ":" + std::to_string(tile.x) +
           ":" + std::to_string(tile.y);
}

std::string contour_fetch_key(const ::platform::linux_runtime::MapContourTileId& tile)
{
    return ::platform::linux_runtime::map_contour_profile_key(tile.profile) +
           ":" + std::to_string(tile.z) +
           ":" + std::to_string(tile.x) +
           ":" + std::to_string(tile.y);
}

bool contour_layer_for_profile(
    const ::platform::linux_runtime::MapContourProfile& profile,
    ui::map_tiles::MapTileLayer& out)
{
    using ::platform::linux_runtime::MapContourKind;
    if (profile.kind == MapContourKind::Major)
    {
        switch (profile.interval_m)
        {
        case 500:
            out = ui::map_tiles::MapTileLayer::ContourMajor500;
            return true;
        case 200:
            out = ui::map_tiles::MapTileLayer::ContourMajor200;
            return true;
        case 100:
            out = ui::map_tiles::MapTileLayer::ContourMajor100;
            return true;
        case 50:
            out = ui::map_tiles::MapTileLayer::ContourMajor50;
            return true;
        case 25:
            out = ui::map_tiles::MapTileLayer::ContourMajor25;
            return true;
        default:
            return false;
        }
    }

    switch (profile.interval_m)
    {
    case 100:
        out = ui::map_tiles::MapTileLayer::ContourMinor100;
        return true;
    case 50:
        out = ui::map_tiles::MapTileLayer::ContourMinor50;
        return true;
    case 20:
        out = ui::map_tiles::MapTileLayer::ContourMinor20;
        return true;
    case 10:
        out = ui::map_tiles::MapTileLayer::ContourMinor10;
        return true;
    case 5:
        out = ui::map_tiles::MapTileLayer::ContourMinor5;
        return true;
    default:
        return false;
    }
}

std::vector<::platform::linux_runtime::MapContourTileId> contour_tile_ids(
    int z,
    int x,
    int y)
{
    const auto profiles = ::platform::linux_runtime::contour_profiles_for_zoom(
        z,
        false);
    std::vector<::platform::linux_runtime::MapContourTileId> out;
    out.reserve(profiles.size());
    for (const auto& profile : profiles)
    {
        ui::map_tiles::MapTileLayer layer{};
        if (!contour_layer_for_profile(profile, layer))
        {
            continue;
        }
        ::platform::linux_runtime::MapContourTileId id{};
        id.profile = profile;
        id.z = z;
        id.x = x;
        id.y = y;
        ::platform::linux_runtime::normalize_map_contour_tile(id);
        out.push_back(id);
    }
    return out;
}

template <typename T>
bool future_ready(std::future<T>& future)
{
    return future.valid() &&
           future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

uint8_t map_source_from_base_id(::platform::linux_runtime::MapBaseSource source)
{
    return static_cast<uint8_t>(source);
}

void mark_visible_tile_for_reload(TileContext& ctx,
                                  const ::platform::linux_runtime::MapTileId& tile)
{
    MapTile* visible = find_tile(ctx,
                                 tile.x,
                                 tile.y,
                                 tile.z,
                                 map_source_from_base_id(tile.source));
    if (visible && visible->visible)
    {
        delete_tile_object(*visible);
    }
}

void mark_visible_contour_for_reload(TileContext& ctx,
                                     const ::platform::linux_runtime::MapContourTileId& tile)
{
    if (!ctx.tiles)
    {
        return;
    }

    for (auto& visible : *ctx.tiles)
    {
        if (visible.visible &&
            visible.z == tile.z &&
            visible.x == tile.x &&
            visible.y == tile.y)
        {
            delete_tile_object(visible);
        }
    }
}

bool retry_is_active(const std::map<std::string, uint32_t>& retry_after,
                     const std::string& key,
                     uint32_t now)
{
    const auto it = retry_after.find(key);
    return it != retry_after.end() && it->second > now;
}

void schedule_base_tile_fetch(int z, int x, int y, uint8_t map_source)
{
    auto& runtime = tile_fetch_runtime();
    if (runtime.base_jobs.size() >= kMaxBaseFetchJobs)
    {
        return;
    }

    const auto tile = base_tile_id(z, x, y, map_source);
    if (online_tile_cache().tile_available(tile))
    {
        return;
    }

    const std::string key = base_fetch_key(tile);
    const uint32_t now = now_ms();
    if (retry_is_active(runtime.base_retry_after_ms, key, now))
    {
        return;
    }

    const auto already_queued = std::find_if(
        runtime.base_jobs.begin(),
        runtime.base_jobs.end(),
        [&](const BaseTileFetchJob& job)
        {
            return job.key == key;
        });
    if (already_queued != runtime.base_jobs.end())
    {
        return;
    }

    runtime.base_jobs.push_back(BaseTileFetchJob{
        tile,
        key,
        std::async(std::launch::async,
                   [tile]()
                   {
                       return online_tile_cache().ensure_tile(tile);
                   })});
    ::platform::linux_runtime::append_map_diagnostic(
        "tile",
        "queued " + key);
}

void schedule_contour_tile_fetch(int z, int x, int y)
{
    auto& runtime = tile_fetch_runtime();
    if (runtime.contour_jobs.size() >= kMaxContourFetchJobs)
    {
        return;
    }

    const auto tiles = contour_tile_ids(z, x, y);
    if (tiles.empty())
    {
        return;
    }

    const std::string token = earthdata_token();
    if (token.empty())
    {
        if (!g_contour_token_missing_logged)
        {
            g_contour_token_missing_logged = true;
            ::platform::linux_runtime::append_map_diagnostic(
                "contour",
                "skipped: Earthdata token missing");
        }
        return;
    }

    const uint32_t now = now_ms();
    for (const auto& tile : tiles)
    {
        if (contour_tile_store().tile_available(tile))
        {
            continue;
        }

        const std::string key = contour_fetch_key(tile);
        if (retry_is_active(runtime.contour_retry_after_ms, key, now))
        {
            continue;
        }

        const auto already_queued = std::find_if(
            runtime.contour_jobs.begin(),
            runtime.contour_jobs.end(),
            [&](const ContourTileFetchJob& job)
            {
                return job.key == key;
            });
        if (already_queued != runtime.contour_jobs.end())
        {
            continue;
        }

        runtime.contour_jobs.push_back(ContourTileFetchJob{
            tile,
            key,
            std::async(std::launch::async,
                       [tile, token]()
                       {
                           return contour_tile_generator().ensure_tiles({tile}, token);
                       })});
        ::platform::linux_runtime::append_map_diagnostic(
            "contour",
            "queued " + key);
        return;
    }
}

void poll_tile_fetch_jobs(TileContext& ctx)
{
    auto& runtime = tile_fetch_runtime();
    const uint32_t now = now_ms();

    for (auto it = runtime.base_jobs.begin(); it != runtime.base_jobs.end();)
    {
        if (!future_ready(it->future))
        {
            ++it;
            continue;
        }

        const auto result = it->future.get();
        const bool available =
            result.status == ::platform::linux_runtime::MapTileStatus::Cached ||
            result.status == ::platform::linux_runtime::MapTileStatus::Downloaded;
        if (available)
        {
            runtime.base_retry_after_ms.erase(it->key);
            mark_visible_tile_for_reload(ctx, it->tile);
        }
        else
        {
            runtime.base_retry_after_ms[it->key] = now + kBaseFetchRetryDelayMs;
        }
        it = runtime.base_jobs.erase(it);
    }

    for (auto it = runtime.contour_jobs.begin(); it != runtime.contour_jobs.end();)
    {
        if (!future_ready(it->future))
        {
            ++it;
            continue;
        }

        const auto result = it->future.get();
        if (!result.message.empty())
        {
            ::platform::linux_runtime::append_map_diagnostic(
                "contour",
                result.message);
        }
        if (result.generated_tiles > 0 || result.cached_tiles > 0)
        {
            runtime.contour_retry_after_ms.erase(it->key);
            mark_visible_contour_for_reload(ctx, it->tile);
        }
        else
        {
            runtime.contour_retry_after_ms[it->key] = now + kContourFetchRetryDelayMs;
        }
        it = runtime.contour_jobs.erase(it);
    }
}

std::filesystem::path build_base_tile_actual_path(int z, int x, int y, uint8_t map_source)
{
    char path[160]{};
    if (!tile_source().resolvePath(base_tile_ref(z, x, y, map_source),
                                   path,
                                   sizeof(path)))
    {
        return {};
    }
    return std::filesystem::path(path);
}

std::filesystem::path build_contour_tile_actual_path(int z, int x, int y)
{
    const auto tiles = contour_tile_ids(z, x, y);
    if (tiles.empty())
    {
        return {};
    }

    return contour_tile_store().existing_tile_path(tiles.front());
}

struct ContourOverlayPath
{
    std::filesystem::path path{};
    bool major = false;
};

std::vector<ContourOverlayPath> available_contour_overlay_paths(
    int z,
    int x,
    int y,
    bool* out_supported,
    bool* out_complete)
{
    const auto tiles = contour_tile_ids(z, x, y);
    if (out_supported)
    {
        *out_supported = !tiles.empty();
    }
    if (out_complete)
    {
        *out_complete = !tiles.empty();
    }

    std::vector<ContourOverlayPath> out;
    out.reserve(tiles.size());
    for (const auto& tile : tiles)
    {
        if (!contour_tile_store().tile_available(tile))
        {
            if (out_complete)
            {
                *out_complete = false;
            }
            continue;
        }

        out.push_back(ContourOverlayPath{
            contour_tile_store().existing_tile_path(tile),
            tile.profile.kind ==
                ::platform::linux_runtime::MapContourKind::Major,
        });
    }

    std::stable_sort(out.begin(),
                     out.end(),
                     [](const ContourOverlayPath& lhs,
                        const ContourOverlayPath& rhs)
                     {
                         if (lhs.major != rhs.major)
                         {
                             return !lhs.major && rhs.major;
                         }
                         return false;
                     });
    return out;
}

bool build_lvgl_path_from_actual(const std::filesystem::path& actual_path, char* out_path, size_t out_size)
{
    if (!out_path || out_size == 0 || actual_path.empty())
    {
        return false;
    }

    const std::string actual = actual_path.string();
    std::snprintf(out_path, out_size, "A:%s", actual.c_str());
    out_path[out_size - 1] = '\0';
    return true;
}

int16_t clamp_screen_coord(int value)
{
    if (value < -32768)
    {
        return -32768;
    }
    if (value > 32767)
    {
        return 32767;
    }
    return static_cast<int16_t>(value);
}

ui::map_tiles::MapTileRef tile_render_ref(const MapTile& tile)
{
    ui::map_tiles::MapTileRef ref{};
    ref.layer = ui::map_tiles::mapTileLayerFromBaseSource(sanitize_map_source(tile.map_source));
    ref.z = clamp_tile_zoom(tile.z);
    ref.x = static_cast<uint32_t>(tile.x < 0 ? 0 : tile.x);
    ref.y = static_cast<uint32_t>(tile.y < 0 ? 0 : tile.y);
    return ref;
}

ui::map_tiles::MapTileRenderState tile_render_state(const MapTile& tile)
{
    if (tile.has_png_file)
    {
        return ui::map_tiles::MapTileRenderState::Ready;
    }
    if (tile.base_missing)
    {
        return ui::map_tiles::MapTileRenderState::Missing;
    }
    return ui::map_tiles::MapTileRenderState::Loading;
}

void rebuild_render_queue(TileContext& ctx)
{
    if (!ctx.render_queue)
    {
        return;
    }

    ctx.render_queue->clear();
    if (!ctx.tiles || !ctx.anchor || !ctx.anchor->valid)
    {
        return;
    }

    for (const auto& tile : *ctx.tiles)
    {
        if (!tile.visible)
        {
            continue;
        }

        int screen_x = 0;
        int screen_y = 0;
        if (!tile_screen_pos_xyz(ctx,
                                 static_cast<int>(tile.x),
                                 static_cast<int>(tile.y),
                                 tile.z,
                                 screen_x,
                                 screen_y))
        {
            continue;
        }

        ui::map_tiles::MapTileRenderRef item{};
        item.tile = tile_render_ref(tile);
        item.rect.x = clamp_screen_coord(screen_x);
        item.rect.y = clamp_screen_coord(screen_y);
        item.rect.width = TILE_SIZE;
        item.rect.height = TILE_SIZE;
        item.state = tile_render_state(tile);
        ctx.render_queue->push(item);
    }
}

double clamp_lat(double lat)
{
    return std::clamp(lat, -kMaxMercatorLat, kMaxMercatorLat);
}

double world_tiles(int zoom)
{
    return static_cast<double>(1U << std::clamp(zoom, 0, 18));
}

double world_size_px(int zoom)
{
    return world_tiles(zoom) * static_cast<double>(TILE_SIZE);
}

double lon_to_world_x(double lon, int zoom)
{
    const double wrapped_lon = std::fmod(lon + 180.0, 360.0);
    const double normalized_lon = wrapped_lon < 0.0 ? wrapped_lon + 360.0 : wrapped_lon;
    return (normalized_lon / 360.0) * world_size_px(zoom);
}

double lat_to_world_y(double lat, int zoom)
{
    const double clamped_lat = clamp_lat(lat);
    const double lat_rad = clamped_lat * kPi / 180.0;
    const double mercator = std::log(std::tan(kPi / 4.0 + lat_rad / 2.0));
    const double normalized = (1.0 - mercator / kPi) / 2.0;
    const double size = world_size_px(zoom);
    return std::clamp(normalized * size, 0.0, size - 1.0);
}

void world_to_lat_lng(double world_x, double world_y, int zoom, double& lat, double& lng)
{
    const double size = world_size_px(zoom);
    if (size <= 0.0)
    {
        lat = 0.0;
        lng = 0.0;
        return;
    }

    const double wrapped_x = std::fmod(world_x, size);
    const double normalized_x = wrapped_x < 0.0 ? wrapped_x + size : wrapped_x;
    const double normalized_y = std::clamp(world_y / size, 0.0, 1.0);

    lng = (normalized_x / size) * 360.0 - 180.0;

    const double mercator = kPi * (1.0 - 2.0 * normalized_y);
    lat = std::atan(std::sinh(mercator)) * 180.0 / kPi;
}

void delete_tile_object(MapTile& tile)
{
    if (tile.img_obj && lv_obj_is_valid(tile.img_obj))
    {
        lv_obj_del(tile.img_obj);
    }
    tile.img_obj = nullptr;
    tile.contour_obj = nullptr;
    tile.contour_loaded = false;
    tile.contour_checked = false;
    tile.has_png_file = false;
    tile.base_missing = false;
    tile.packaged_base.reset();
    tile.packaged_major.reset();
    tile.packaged_minor.reset();
    tile.poi.reset();
    tile.packaged_pending = false;
}

MapTile* find_tile(TileContext& ctx, int32_t x, int32_t y, int z, uint8_t map_source)
{
    if (!ctx.tiles)
    {
        return nullptr;
    }

    for (auto& tile : *ctx.tiles)
    {
        if (tile.x == x && tile.y == y && tile.z == z && tile.map_source == map_source)
        {
            return &tile;
        }
    }
    return nullptr;
}

void refresh_status_flags(TileContext& ctx)
{
    if (ctx.has_map_data)
    {
        *ctx.has_map_data = false;
    }
    if (ctx.has_visible_map_data)
    {
        *ctx.has_visible_map_data = false;
    }

    if (!ctx.tiles)
    {
        return;
    }

    for (const auto& tile : *ctx.tiles)
    {
        if (tile.has_png_file && ctx.has_map_data)
        {
            *ctx.has_map_data = true;
        }
        if (tile.visible && tile.has_png_file && ctx.has_visible_map_data)
        {
            *ctx.has_visible_map_data = true;
        }
    }
}

void mark_missing_tile(uint8_t map_source)
{
    if (g_missing_tile_notice_emitted)
    {
        return;
    }

    g_missing_tile_notice_pending = true;
    g_missing_tile_notice_emitted = true;
    g_missing_tile_notice_source = sanitize_map_source(map_source);
}

void style_tile_card(lv_obj_t* obj, uint8_t map_source, bool has_base)
{
    if (!obj)
    {
        return;
    }

    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_pad_all(obj, 6, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    if (!has_base)
    {
        lv_obj_set_style_bg_color(obj, lv_color_hex(0xE3D7C2), 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(0xB08F6A), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        return;
    }

    switch (sanitize_map_source(map_source))
    {
    case 1:
        lv_obj_set_style_bg_color(obj, lv_color_hex(0xD7E9C7), 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(0x6F9E56), 0);
        break;
    case 2:
        lv_obj_set_style_bg_color(obj, lv_color_hex(0xD4DFE9), 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(0x587A9C), 0);
        break;
    case 0:
    default:
        lv_obj_set_style_bg_color(obj, lv_color_hex(0xF5ECD8), 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(0xD2B075), 0);
        break;
    }
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

struct PackagedImage
{
    lv_image_dsc_t descriptor{};
    std::shared_ptr<const ::platform::linux_runtime::PackagedMapTile> payload;
};
void add_packaged_image(lv_obj_t* parent, const std::shared_ptr<const ::platform::linux_runtime::PackagedMapTile>& payload)
{
    if (!payload || !payload->pixels || payload->result.status != ui::map_tiles::MapTileReadStatus::Ready) return;
    const bool rgba = payload->result.format == ui::map_tiles::MapTileFormat::Rgba8888;
    if (!rgba && payload->result.format != ui::map_tiles::MapTileFormat::Rgb565) return;
    auto view = std::make_unique<PackagedImage>();
    view->payload = payload;
    view->descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    view->descriptor.header.w = view->descriptor.header.h = TILE_SIZE;
    view->descriptor.header.cf = rgba ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_RGB565;
    view->descriptor.header.stride = TILE_SIZE * (rgba ? 4 : 2);
    view->descriptor.data_size = payload->result.size;
    if (rgba) ui::map_tiles::NativePixelBuffer::convertRgbaToBgra(payload->data(), payload->result.size);
    view->descriptor.data = payload->data();
    auto* image = lv_image_create(parent);
    auto* owned = view.release();
    lv_obj_add_event_cb(
        image, [](lv_event_t* event)
        {
                           auto* data = static_cast<PackagedImage*>(lv_event_get_user_data(event));
                           lv_image_cache_drop(&data->descriptor);
                           delete data; },
        LV_EVENT_DELETE, owned);
    lv_image_set_src(image, &owned->descriptor);
    lv_obj_set_size(image, TILE_SIZE, TILE_SIZE);
    lv_obj_align(image, LV_ALIGN_TOP_LEFT, 0, 0);
}
bool create_packaged_tile_card(TileContext& ctx, MapTile& tile, int screen_x, int screen_y)
{
    namespace native = ::platform::linux_runtime;
    namespace mt = ::ui::map_tiles;
    const auto metadata = ctx.packaged->snapshot();
    if (metadata.mode == native::PackagedMapMode::Legacy) return false;
    if (metadata.mode == native::PackagedMapMode::Pending) return true;
    auto base = metadata.mode == native::PackagedMapMode::Tmap ? ctx.packaged->request(base_tile_ref(tile.z, tile.x, tile.y, tile.map_source)) : nullptr;
    std::shared_ptr<const native::PackagedMapTile> major, minor, poi;
    bool pending = metadata.mode == native::PackagedMapMode::Tmap && !base;
    if (metadata.mode == native::PackagedMapMode::Tmap)
    {
        auto ref = base_tile_ref(tile.z, tile.x, tile.y, tile.map_source);
        if (g_requested_contour_enabled && tile.z > 7)
        {
            for (const auto& profile : native::contour_profiles_for_zoom(tile.z, true))
            {
                if (!contour_layer_for_profile(profile, ref.layer)) continue;
                auto result = ctx.packaged->request(ref);
                pending = pending || !result;
                if (profile.kind == native::MapContourKind::Major) major = std::move(result);
                else minor = std::move(result);
            }
        }
        ref.layer = mt::MapTileLayer::Poi;
        poi = ctx.packaged->request(ref);
        pending = pending || !poi;
    }
    tile.packaged_pending = pending;
    if (tile.poi != poi)
    {
        tile.poi = std::move(poi);
        ++ctx.poi_revision;
    }
    tile.contour_checked = g_requested_contour_enabled;
    tile.contour_loaded = !pending;
    if (tile.img_obj && lv_obj_is_valid(tile.img_obj) && base == tile.packaged_base && major == tile.packaged_major && minor == tile.packaged_minor) return true;
    if (!tile.img_obj || !lv_obj_is_valid(tile.img_obj))
    {
        tile.img_obj = lv_obj_create(ctx.map_container);
        lv_obj_set_size(tile.img_obj, TILE_SIZE, TILE_SIZE);
        lv_obj_move_background(tile.img_obj);
    }
    while (lv_obj_get_child_cnt(tile.img_obj)) lv_obj_del(lv_obj_get_child(tile.img_obj, 0));
    tile.packaged_base = std::move(base);
    tile.packaged_major = std::move(major);
    tile.packaged_minor = std::move(minor);
    tile.has_png_file = tile.packaged_base && tile.packaged_base->result.status == mt::MapTileReadStatus::Ready;
    tile.base_missing = tile.packaged_base && tile.packaged_base->result.status == mt::MapTileReadStatus::Missing;
    lv_obj_set_pos(tile.img_obj, screen_x, screen_y);
    style_tile_card(tile.img_obj, tile.map_source, tile.has_png_file);
    if (tile.has_png_file)
    {
        lv_obj_set_style_pad_all(tile.img_obj, 0, 0);
        lv_obj_set_style_border_width(tile.img_obj, 0, 0);
        add_packaged_image(tile.img_obj, tile.packaged_base);
        add_packaged_image(tile.img_obj, tile.packaged_minor);
        add_packaged_image(tile.img_obj, tile.packaged_major);
    }
    else
    {
        auto* label = lv_label_create(tile.img_obj);
        char identity[48];
        mt::formatMapTileCoordinateLabel(tile.z, tile.x, tile.y, identity, sizeof(identity));
        const char* status = !tile.packaged_base && metadata.mode == native::PackagedMapMode::Tmap ? "Loading" : tile.base_missing ? "Missing"
                                                                                                                                   : "Map data error";
        lv_label_set_text_fmt(label, "%s %s\n%s", status, map_source_label(tile.map_source), identity);
        lv_obj_center(label);
        if (tile.base_missing) mark_missing_tile(tile.map_source);
    }
    return true;
}

void create_or_refresh_tile_card(TileContext& ctx, MapTile& tile)
{
    if (!ctx.map_container || !ctx.anchor)
    {
        return;
    }

    int screen_x = 0;
    int screen_y = 0;
    if (!tile_screen_pos_xyz(ctx, static_cast<int>(tile.x), static_cast<int>(tile.y), tile.z, screen_x, screen_y))
    {
        return;
    }
    if (ctx.packaged && create_packaged_tile_card(ctx, tile, screen_x, screen_y)) return;

    const auto base_actual_path =
        build_base_tile_actual_path(tile.z, static_cast<int>(tile.x), static_cast<int>(tile.y), tile.map_source);
    const auto base_result = tile_source().lookup(
        base_tile_ref(tile.z, static_cast<int>(tile.x), static_cast<int>(tile.y), tile.map_source));
    const bool has_base = base_result.status == ui::map_tiles::MapTileStatus::Available;
    if (!has_base)
    {
        schedule_base_tile_fetch(tile.z,
                                 static_cast<int>(tile.x),
                                 static_cast<int>(tile.y),
                                 tile.map_source);
    }

    bool contour_supported = false;
    bool contour_complete = false;
    const auto contour_paths =
        g_requested_contour_enabled
            ? available_contour_overlay_paths(tile.z,
                                              static_cast<int>(tile.x),
                                              static_cast<int>(tile.y),
                                              &contour_supported,
                                              &contour_complete)
            : std::vector<ContourOverlayPath>{};
    if (has_base && g_requested_contour_enabled && contour_supported && !contour_complete)
    {
        schedule_contour_tile_fetch(tile.z,
                                    static_cast<int>(tile.x),
                                    static_cast<int>(tile.y));
    }

    if (!tile.img_obj || !lv_obj_is_valid(tile.img_obj))
    {
        tile.img_obj = lv_obj_create(ctx.map_container);
        lv_obj_set_size(tile.img_obj, TILE_SIZE, TILE_SIZE);
        lv_obj_move_background(tile.img_obj);
    }

    tile.has_png_file = has_base;
    tile.base_missing = !has_base;
    tile.contour_checked = g_requested_contour_enabled;
    tile.contour_loaded = !g_requested_contour_enabled || !contour_supported || contour_complete;
    tile.last_used_ms = now_ms();

    lv_obj_set_pos(tile.img_obj, screen_x, screen_y);
    style_tile_card(tile.img_obj, tile.map_source, has_base);

    while (lv_obj_get_child_cnt(tile.img_obj) > 0)
    {
        lv_obj_t* child = lv_obj_get_child(tile.img_obj, 0);
        lv_obj_del(child);
    }

    if (has_base)
    {
        char base_path[LV_FS_MAX_PATH_LEN];
        if (build_lvgl_path_from_actual(base_actual_path, base_path, sizeof(base_path)))
        {
            lv_obj_set_style_pad_all(tile.img_obj, 0, 0);
            lv_obj_set_style_border_width(tile.img_obj, 0, 0);
            lv_obj_set_style_bg_opa(tile.img_obj, LV_OPA_TRANSP, 0);

            lv_obj_t* image = lv_image_create(tile.img_obj);
            lv_image_set_src(image, base_path);
            lv_obj_set_size(image, TILE_SIZE, TILE_SIZE);
            lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);

            for (const auto& contour_entry : contour_paths)
            {
                char contour_path[LV_FS_MAX_PATH_LEN];
                if (build_lvgl_path_from_actual(contour_entry.path, contour_path, sizeof(contour_path)))
                {
                    lv_obj_t* contour = lv_image_create(tile.img_obj);
                    lv_image_set_src(contour, contour_path);
                    lv_obj_set_size(contour, TILE_SIZE, TILE_SIZE);
                    lv_obj_align(contour, LV_ALIGN_CENTER, 0, 0);
                    lv_obj_set_style_opa(contour, LV_OPA_COVER, 0);
                }
            }
            return;
        }
    }

    lv_obj_t* label = lv_label_create(tile.img_obj);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, TILE_SIZE - 18);

    char coordinate_text[48] = {};
    ui::map_tiles::formatMapTileCoordinateLabel(
        static_cast<std::uint8_t>(tile.z),
        static_cast<std::uint32_t>(tile.x),
        static_cast<std::uint32_t>(tile.y),
        coordinate_text,
        sizeof(coordinate_text));

    char text[128];
    std::snprintf(text,
                  sizeof(text),
                  "%s %s\n%s",
                  has_base ? "Decode failed:" : "Missing",
                  map_source_label(tile.map_source),
                  coordinate_text);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    if (!has_base)
    {
        mark_missing_tile(tile.map_source);
    }
}

} // namespace

uint8_t sanitize_map_source(uint8_t map_source)
{
    return map_source <= 2 ? map_source : 0;
}

const char* map_source_label(uint8_t map_source)
{
    switch (sanitize_map_source(map_source))
    {
    case 1:
        return "Terrain";
    case 2:
        return "Satellite";
    case 0:
    default:
        return "OSM";
    }
}

bool build_base_tile_path(int z, int x, int y, uint8_t map_source, char* out_path, size_t out_size)
{
    if (!out_path || out_size == 0)
    {
        return false;
    }
    return build_lvgl_path_from_actual(build_base_tile_actual_path(z, x, y, map_source), out_path, out_size);
}

bool build_contour_tile_path(int z, int x, int y, char* out_path, size_t out_size)
{
    if (!out_path || out_size == 0)
    {
        return false;
    }
    return build_lvgl_path_from_actual(build_contour_tile_actual_path(z, x, y), out_path, out_size);
}

bool base_tile_available(int z, int x, int y, uint8_t map_source)
{
    auto packaged = packaged_metadata_session();
    const auto mode = packaged->snapshot().mode;
    if (mode != ::platform::linux_runtime::PackagedMapMode::Legacy)
    {
        const auto result = packaged->request(base_tile_ref(z, x, y, map_source));
        return result && result->result.status == ui::map_tiles::MapTileReadStatus::Ready;
    }
    const auto result = tile_source().lookup(base_tile_ref(z, x, y, map_source));
    return result.status == ui::map_tiles::MapTileStatus::Available;
}

bool map_source_directory_available(uint8_t map_source)
{
    const auto metadata = packaged_metadata_session()->snapshot();
    if (metadata.mode == ::platform::linux_runtime::PackagedMapMode::Pending) return true;
    if (metadata.mode == ::platform::linux_runtime::PackagedMapMode::Tmap) return metadata.coverage[sanitize_map_source(map_source)] != 0;
    if (metadata.mode == ::platform::linux_runtime::PackagedMapMode::Error) return false;
    return tile_source().layerDirectoryAvailable(
        ui::map_tiles::mapTileLayerFromBaseSource(sanitize_map_source(map_source)));
}

bool contour_directory_available()
{
    const auto metadata = packaged_metadata_session()->snapshot();
    if (metadata.mode == ::platform::linux_runtime::PackagedMapMode::Pending) return true;
    if (metadata.mode == ::platform::linux_runtime::PackagedMapMode::Tmap)
        return std::any_of(metadata.coverage.begin() + 3, metadata.coverage.end(), [](uint32_t mask)
                           { return mask != 0; });
    if (metadata.mode == ::platform::linux_runtime::PackagedMapMode::Error) return false;
    return tile_source().anyContourDirectoryAvailable() || !earthdata_token().empty();
}

bool take_missing_tile_notice(uint8_t* out_map_source)
{
    if (!g_missing_tile_notice_pending)
    {
        return false;
    }

    g_missing_tile_notice_pending = false;
    if (out_map_source)
    {
        *out_map_source = g_missing_tile_notice_source;
    }
    return true;
}

void set_map_render_options(uint8_t map_source, bool contour_enabled)
{
    const uint8_t normalized = sanitize_map_source(map_source);
    if (g_requested_map_source != normalized || g_requested_contour_enabled != contour_enabled)
    {
        g_missing_tile_notice_pending = false;
        g_missing_tile_notice_emitted = false;
    }

    g_requested_map_source = normalized;
    g_requested_contour_enabled = contour_enabled;
}

void normalize_tile(int z, int& x, int& y)
{
    ::ui::map_tiles::normalizeTile(z, x, y);
}

void latLngToTile(double lat, double lng, int zoom, int& tile_x, int& tile_y)
{
    ::ui::map_tiles::latLngToTile(lat, lng, zoom, tile_x, tile_y);
}

void tileToLatLng(int tile_x, int tile_y, int zoom, double& lat, double& lng)
{
    const double tiles = world_tiles(zoom);
    lng = ((static_cast<double>(tile_x) + 0.5) / tiles) * 360.0 - 180.0;
    const double mercator = kPi * (1.0 - 2.0 * ((static_cast<double>(tile_y) + 0.5) / tiles));
    lat = std::atan(std::sinh(mercator)) * 180.0 / kPi;
}

void get_screen_center_lat_lng(const TileContext& ctx, double& lat, double& lng)
{
    lat = 0.0;
    lng = 0.0;

    if (!ctx.anchor || !ctx.anchor->valid || !ctx.map_container)
    {
        return;
    }

    const int width = static_cast<int>(lv_obj_get_width(ctx.map_container));
    const int height = static_cast<int>(lv_obj_get_height(ctx.map_container));
    const double focus_screen_x = static_cast<double>(ctx.anchor->gps_tile_screen_x + ctx.anchor->gps_offset_x);
    const double focus_screen_y = static_cast<double>(ctx.anchor->gps_tile_screen_y + ctx.anchor->gps_offset_y);
    const double center_world_x =
        static_cast<double>(ctx.anchor->gps_global_pixel_x) + (static_cast<double>(width) / 2.0 - focus_screen_x);
    const double center_world_y =
        static_cast<double>(ctx.anchor->gps_global_pixel_y) + (static_cast<double>(height) / 2.0 - focus_screen_y);

    world_to_lat_lng(center_world_x, center_world_y, ctx.anchor->z, lat, lng);
}

void tileToPixel(int tile_x, int tile_y, int /*zoom*/, int& pixel_x, int& pixel_y)
{
    pixel_x = tile_x * TILE_SIZE;
    pixel_y = tile_y * TILE_SIZE;
}

bool gps_screen_pos(const TileContext& ctx, double lat, double lng, int& sx, int& sy)
{
    if (!ctx.anchor || !ctx.anchor->valid)
    {
        return false;
    }

    const double size = world_size_px(ctx.anchor->z);
    const double world_x = lon_to_world_x(lng, ctx.anchor->z);
    const double world_y = lat_to_world_y(lat, ctx.anchor->z);
    double dx = world_x - static_cast<double>(ctx.anchor->gps_global_pixel_x);
    if (dx > size / 2.0)
    {
        dx -= size;
    }
    else if (dx < -size / 2.0)
    {
        dx += size;
    }
    const double dy = world_y - static_cast<double>(ctx.anchor->gps_global_pixel_y);

    const double focus_screen_x = static_cast<double>(ctx.anchor->gps_tile_screen_x + ctx.anchor->gps_offset_x);
    const double focus_screen_y = static_cast<double>(ctx.anchor->gps_tile_screen_y + ctx.anchor->gps_offset_y);
    sx = static_cast<int>(std::lround(focus_screen_x + dx));
    sy = static_cast<int>(std::lround(focus_screen_y + dy));
    return true;
}

bool tile_screen_pos_xyz(const TileContext& ctx, int x, int y, int z, int& sx, int& sy)
{
    if (!ctx.anchor || !ctx.anchor->valid || ctx.anchor->z != z)
    {
        return false;
    }

    const int tiles = 1 << std::clamp(z, 0, 18);
    int dx = x - static_cast<int>(ctx.anchor->gps_tile_x);
    if (dx > tiles / 2)
    {
        dx -= tiles;
    }
    else if (dx < -tiles / 2)
    {
        dx += tiles;
    }
    const int dy = y - static_cast<int>(ctx.anchor->gps_tile_y);

    sx = ctx.anchor->gps_tile_screen_x + (dx * TILE_SIZE);
    sy = ctx.anchor->gps_tile_screen_y + (dy * TILE_SIZE);
    return true;
}

bool tile_in_rect(int sx, int sy, int w, int h, int margin)
{
    return sx < (w + margin) && sy < (h + margin) && (sx + TILE_SIZE) > -margin && (sy + TILE_SIZE) > -margin;
}

void update_map_anchor(TileContext& ctx, double lat, double lng, int zoom, int pan_x, int pan_y, bool has_fix)
{
    if (!ctx.anchor || !ctx.map_container || !has_fix)
    {
        if (ctx.anchor)
        {
            ctx.anchor->valid = false;
        }
        return;
    }

    const int width = static_cast<int>(lv_obj_get_width(ctx.map_container));
    const int height = static_cast<int>(lv_obj_get_height(ctx.map_container));
    const double world_x = lon_to_world_x(lng, zoom);
    const double world_y = lat_to_world_y(lat, zoom);
    int tile_x = static_cast<int>(std::floor(world_x / TILE_SIZE));
    int tile_y = static_cast<int>(std::floor(world_y / TILE_SIZE));
    normalize_tile(zoom, tile_x, tile_y);

    ctx.anchor->z = zoom;
    ctx.anchor->gps_tile_x = tile_x;
    ctx.anchor->gps_tile_y = tile_y;
    ctx.anchor->gps_tile_pixel_x = tile_x * TILE_SIZE;
    ctx.anchor->gps_tile_pixel_y = tile_y * TILE_SIZE;
    ctx.anchor->gps_global_pixel_x = static_cast<int32_t>(std::lround(world_x));
    ctx.anchor->gps_global_pixel_y = static_cast<int32_t>(std::lround(world_y));
    ctx.anchor->gps_offset_x = ctx.anchor->gps_global_pixel_x - ctx.anchor->gps_tile_pixel_x;
    ctx.anchor->gps_offset_y = ctx.anchor->gps_global_pixel_y - ctx.anchor->gps_tile_pixel_y;
    ctx.anchor->gps_tile_screen_x = width / 2 - ctx.anchor->gps_offset_x + pan_x;
    ctx.anchor->gps_tile_screen_y = height / 2 - ctx.anchor->gps_offset_y + pan_y;
    ctx.anchor->n = world_tiles(zoom);
    ctx.anchor->valid = true;
}

void calculate_required_tiles(TileContext& ctx, double lat, double lng, int zoom, int pan_x, int pan_y, bool has_fix)
{
    ++ctx.poi_revision;
    if (!ctx.tiles)
    {
        return;
    }

    update_map_anchor(ctx, lat, lng, zoom, pan_x, pan_y, has_fix);
    if (!ctx.anchor || !ctx.anchor->valid || !ctx.map_container)
    {
        cleanup_tiles(ctx);
        return;
    }

    const int width = static_cast<int>(lv_obj_get_width(ctx.map_container));
    const int height = static_cast<int>(lv_obj_get_height(ctx.map_container));
    const int cols = std::max(3, width / TILE_SIZE + 3);
    const int rows = std::max(3, height / TILE_SIZE + 3);
    const int half_cols = cols / 2 + 1;
    const int half_rows = rows / 2 + 1;

    double center_lat = 0.0;
    double center_lng = 0.0;
    get_screen_center_lat_lng(ctx, center_lat, center_lng);
    int center_tile_x = static_cast<int>(ctx.anchor->gps_tile_x);
    int center_tile_y = static_cast<int>(ctx.anchor->gps_tile_y);
    if (std::isfinite(center_lat) && std::isfinite(center_lng))
    {
        latLngToTile(center_lat, center_lng, zoom, center_tile_x, center_tile_y);
    }

    for (auto& tile : *ctx.tiles)
    {
        tile.visible = false;
    }

    for (int dy = -half_rows; dy <= half_rows; ++dy)
    {
        for (int dx = -half_cols; dx <= half_cols; ++dx)
        {
            int tile_x = center_tile_x + dx;
            int tile_y = center_tile_y + dy;
            normalize_tile(zoom, tile_x, tile_y);

            int screen_x = 0;
            int screen_y = 0;
            if (!tile_screen_pos_xyz(ctx, tile_x, tile_y, zoom, screen_x, screen_y) ||
                !tile_in_rect(screen_x, screen_y, width, height, TILE_SIZE / 2))
            {
                continue;
            }

            MapTile* tile = find_tile(ctx, tile_x, tile_y, zoom, g_requested_map_source);
            if (!tile)
            {
                ctx.tiles->push_back(MapTile{});
                tile = &ctx.tiles->back();
                tile->x = tile_x;
                tile->y = tile_y;
                tile->z = zoom;
                tile->map_source = g_requested_map_source;
            }
            else if (tile->map_source != g_requested_map_source)
            {
                delete_tile_object(*tile);
                tile->map_source = g_requested_map_source;
            }

            tile->visible = true;
            tile->ever_visible = true;
            tile->priority = std::abs(dx) + std::abs(dy);
            tile->last_used_ms = now_ms();

            if (tile->img_obj && lv_obj_is_valid(tile->img_obj))
            {
                lv_obj_set_pos(tile->img_obj, screen_x, screen_y);
            }
        }
    }

    auto& tiles = *ctx.tiles;
    for (auto it = tiles.begin(); it != tiles.end();)
    {
        if (!it->visible)
        {
            delete_tile_object(*it);
            it = tiles.erase(it);
            continue;
        }
        ++it;
    }

    refresh_status_flags(ctx);
    rebuild_render_queue(ctx);
}

void tile_loader_step(TileContext& ctx)
{
    if (!ctx.tiles || !ctx.map_container)
    {
        return;
    }

    poll_tile_fetch_jobs(ctx);
    const auto packaged = ctx.packaged ? ctx.packaged->snapshot() : ::platform::linux_runtime::PackagedMapSnapshot{};
    const bool legacy = packaged.mode == ::platform::linux_runtime::PackagedMapMode::Legacy;
    if (ctx.packaged && ctx.packaged_session != packaged.session)
    {
        for (auto& tile : *ctx.tiles) delete_tile_object(tile);
        ctx.packaged_session = packaged.session;
        ++ctx.poi_revision;
    }

    MapTile* next_tile = nullptr;
    for (auto& tile : *ctx.tiles)
    {
        if (!tile.visible)
        {
            continue;
        }
        if (!tile.img_obj || !lv_obj_is_valid(tile.img_obj) || tile.packaged_pending)
        {
            if (!next_tile || tile.priority < next_tile->priority)
            {
                next_tile = &tile;
            }
            continue;
        }

        if (tile.contour_checked != g_requested_contour_enabled)
        {
            delete_tile_object(tile);
            if (!next_tile || tile.priority < next_tile->priority)
            {
                next_tile = &tile;
            }
            continue;
        }

        if (legacy && tile.base_missing)
        {
            schedule_base_tile_fetch(tile.z,
                                     static_cast<int>(tile.x),
                                     static_cast<int>(tile.y),
                                     tile.map_source);
        }

        if (legacy && g_requested_contour_enabled && tile.has_png_file && !tile.contour_loaded)
        {
            schedule_contour_tile_fetch(tile.z,
                                        static_cast<int>(tile.x),
                                        static_cast<int>(tile.y));
        }

        int screen_x = 0;
        int screen_y = 0;
        if (tile_screen_pos_xyz(ctx, static_cast<int>(tile.x), static_cast<int>(tile.y), tile.z, screen_x, screen_y))
        {
            lv_obj_set_pos(tile.img_obj, screen_x, screen_y);
        }
    }

    if (next_tile)
    {
        create_or_refresh_tile_card(ctx, *next_tile);
    }

    refresh_status_flags(ctx);
    rebuild_render_queue(ctx);
}

void init_tile_context(TileContext& ctx,
                       lv_obj_t* map_container,
                       MapAnchor* anchor,
                       std::vector<MapTile>* tiles,
                       ui::map_tiles::MapTileRenderQueue* render_queue,
                       bool* has_map_data,
                       bool* has_visible_map_data)
{
    if (!ctx.packaged) ctx.packaged = std::make_shared<::platform::linux_runtime::PackagedMapSession>(default_storage_root());
    g_packaged_session = ctx.packaged;
    ctx.map_container = map_container;
    ctx.anchor = anchor;
    ctx.tiles = tiles;
    ctx.render_queue = render_queue;
    ctx.has_map_data = has_map_data;
    ctx.has_visible_map_data = has_visible_map_data;
    if (ctx.render_queue)
    {
        ctx.render_queue->clear();
    }
    if (ctx.has_map_data)
    {
        *ctx.has_map_data = false;
    }
    if (ctx.has_visible_map_data)
    {
        *ctx.has_visible_map_data = false;
    }
    if (ctx.anchor)
    {
        ctx.anchor->valid = false;
    }
}

void cleanup_tiles(TileContext& ctx)
{
    if (ctx.packaged) ctx.packaged->cancel();
    ++ctx.poi_revision;
    if (ctx.tiles)
    {
        for (auto& tile : *ctx.tiles)
        {
            delete_tile_object(tile);
        }
        ctx.tiles->clear();
    }
    if (ctx.has_map_data)
    {
        *ctx.has_map_data = false;
    }
    if (ctx.has_visible_map_data)
    {
        *ctx.has_visible_map_data = false;
    }
    if (ctx.anchor)
    {
        ctx.anchor->valid = false;
    }
    if (ctx.render_queue)
    {
        ctx.render_queue->clear();
    }
}

void release_tile_context(TileContext& ctx)
{
    ctx.packaged.reset();
}

void map_poi_snapshot(TileContext& ctx, ui::map::MapPoiSnapshot& out)
{
    out.header.valid = false;
    out.header.version = 1;
    out.header.generated_at_ms = now_ms();
    out.enabled = false;
    out.loading = out.truncated = out.layout_ready = false;
    out.candidate_count = out.item_count = 0;
    if (!ctx.tiles || !ctx.anchor || !ctx.anchor->valid || !ctx.map_container) return;
    out.width = int16_t(lv_obj_get_width(ctx.map_container));
    out.height = int16_t(lv_obj_get_height(ctx.map_container));
    const int32_t x = ctx.anchor->gps_tile_screen_x + ctx.anchor->gps_offset_x - ctx.anchor->gps_global_pixel_x;
    const int32_t y = ctx.anchor->gps_tile_screen_y + ctx.anchor->gps_offset_y - ctx.anchor->gps_global_pixel_y;
    out.view_key = uint64_t(uint32_t(x)) << 32 | uint32_t(y);
    out.compatibility_key = uint64_t(ctx.packaged_session) << 16 | uint64_t(uint8_t(ctx.anchor->z)) << 8 | g_requested_map_source;
    for (const auto& tile : *ctx.tiles)
    {
        if (!tile.visible || tile.z != ctx.anchor->z) continue;
        out.loading = out.loading || tile.packaged_pending;
        if (!tile.poi || tile.poi->result.status != ui::map_tiles::MapTileReadStatus::Ready || !ui::map_poi::validPayload(tile.poi->data(), tile.poi->result.size)) continue;
        const auto* header = reinterpret_cast<const ui::map_poi::TileHeader*>(tile.poi->data());
        out.header.valid = out.header.valid || header->manifest_valid;
        out.enabled = out.enabled || (header->manifest_valid && header->policy.enabled(ctx.anchor->z));
        out.labels = header->policy.labels;
        out.candidate_count += header->count;
        out.truncated = out.truncated || header->truncated;
    }
}
void visit_map_annotations(TileContext& ctx, ui::map_poi::AnnotationConsumer consume, void* user)
{
    if (!consume || !ctx.tiles || !ctx.anchor || !ctx.anchor->valid) return;
    for (const auto& tile : *ctx.tiles)
    {
        if (!tile.visible || tile.z != ctx.anchor->z || !tile.poi || tile.poi->result.status != ui::map_tiles::MapTileReadStatus::Ready || !ui::map_poi::validPayload(tile.poi->data(), tile.poi->result.size)) continue;
        const auto* header = reinterpret_cast<const ui::map_poi::TileHeader*>(tile.poi->data());
        const auto* records = ui::map_poi::payloadRecords(tile.poi->data());
        int tile_x = 0, tile_y = 0;
        if (!tile_screen_pos_xyz(ctx, tile.x, tile.y, tile.z, tile_x, tile_y)) continue;
        for (size_t index = 0; index < header->count; ++index)
        {
            const auto& record = records[index];
            int x = 0, y = 0;
            if (!gps_screen_pos(ctx, record.lat, record.lon, x, y)) continue;
            ui::map_poi::AnnotationCandidate candidate;
            candidate.key = record.key;
            candidate.feature_key = record.feature_key;
            candidate.name = header->policy.labels ? record.name : "";
            candidate.category = record.category;
            candidate.kind = record.kind;
            candidate.priority = record.priority;
            candidate.x = clamp_screen_coord(x);
            candidate.y = clamp_screen_coord(y);
            candidate.path_points = std::min<uint8_t>(8, record.path_points);
            for (unsigned point = 0; point < candidate.path_points; ++point)
            {
                candidate.path[point * 2] = clamp_screen_coord(tile_x + record.path[point * 2]);
                candidate.path[point * 2 + 1] = clamp_screen_coord(tile_y + record.path[point * 2 + 1]);
            }
            consume(user, candidate);
        }
    }
}
