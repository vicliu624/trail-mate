/**
 * @file map_tiles.cpp
 * @brief Linux map tile runtime for shared map viewport pages.
 */

#include "ui/widgets/map/map_tiles.h"

#include "lvgl.h"

#include "ui_map_runtime/map_tiles/filesystem_map_tile_source.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "platform/linux/runtime_paths.h"

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxMercatorLat = 85.05112878;

uint8_t g_requested_map_source = 0;
bool g_requested_contour_enabled = false;
bool g_missing_tile_notice_pending = false;
bool g_missing_tile_notice_emitted = false;
uint8_t g_missing_tile_notice_source = 0;

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

    bool readFile(const char* path,
                  uint8_t* buffer,
                  std::size_t capacity,
                  std::size_t& out_size) const override
    {
        out_size = 0;
        if (!path || !buffer || capacity == 0)
        {
            return false;
        }

        FILE* file = std::fopen(path, "rb");
        if (!file)
        {
            return false;
        }

        out_size = std::fread(buffer, 1, capacity, file);
        const bool ok = std::ferror(file) == 0;
        std::fclose(file);
        return ok;
    }
};

uint32_t now_ms()
{
    using clock = std::chrono::steady_clock;
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
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

bool contour_tile_ref(int z, int x, int y, ui::map_tiles::MapTileRef& out)
{
    bool supported = false;
    const auto layer = ui::map_tiles::mapTileContourLayerForZoom(z, &supported);
    if (!supported)
    {
        return false;
    }

    out.layer = layer;
    out.z = clamp_tile_zoom(z);
    out.x = static_cast<uint32_t>(x < 0 ? 0 : x);
    out.y = static_cast<uint32_t>(y < 0 ? 0 : y);
    return true;
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
    ui::map_tiles::MapTileRef ref{};
    if (!contour_tile_ref(z, x, y, ref))
    {
        return {};
    }

    char path[160]{};
    if (!tile_source().resolvePath(ref, path, sizeof(path)))
    {
        return {};
    }
    return std::filesystem::path(path);
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

    const auto base_actual_path =
        build_base_tile_actual_path(tile.z, static_cast<int>(tile.x), static_cast<int>(tile.y), tile.map_source);
    const auto base_result = tile_source().lookup(
        base_tile_ref(tile.z, static_cast<int>(tile.x), static_cast<int>(tile.y), tile.map_source));
    const bool has_base = base_result.status == ui::map_tiles::MapTileStatus::Available;

    const auto contour_actual_path =
        g_requested_contour_enabled ? build_contour_tile_actual_path(tile.z, static_cast<int>(tile.x), static_cast<int>(tile.y))
                                    : std::filesystem::path{};
    ui::map_tiles::MapTileRef contour_ref{};
    const bool has_contour =
        g_requested_contour_enabled &&
        contour_tile_ref(tile.z, static_cast<int>(tile.x), static_cast<int>(tile.y), contour_ref) &&
        tile_source().lookup(contour_ref).status == ui::map_tiles::MapTileStatus::Available;

    if (!tile.img_obj || !lv_obj_is_valid(tile.img_obj))
    {
        tile.img_obj = lv_obj_create(ctx.map_container);
        lv_obj_set_size(tile.img_obj, TILE_SIZE, TILE_SIZE);
        lv_obj_move_background(tile.img_obj);
    }

    tile.has_png_file = has_base;
    tile.base_missing = !has_base;
    tile.contour_checked = g_requested_contour_enabled;
    tile.contour_loaded = has_contour;
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

            if (has_contour)
            {
                char contour_path[LV_FS_MAX_PATH_LEN];
                if (build_lvgl_path_from_actual(contour_actual_path, contour_path, sizeof(contour_path)))
                {
                    lv_obj_t* contour = lv_image_create(tile.img_obj);
                    lv_image_set_src(contour, contour_path);
                    lv_obj_set_size(contour, TILE_SIZE, TILE_SIZE);
                    lv_obj_align(contour, LV_ALIGN_CENTER, 0, 0);
                    lv_obj_set_style_opa(contour, LV_OPA_80, 0);
                }
            }
            return;
        }
    }

    lv_obj_t* label = lv_label_create(tile.img_obj);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, TILE_SIZE - 18);

    char text[128];
    std::snprintf(text,
                  sizeof(text),
                  "%s %s\nz%d x%d\ny%d",
                  has_base ? "Decode failed:" : "Missing",
                  map_source_label(tile.map_source),
                  tile.z,
                  static_cast<int>(tile.x),
                  static_cast<int>(tile.y));
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
    const auto result = tile_source().lookup(base_tile_ref(z, x, y, map_source));
    return result.status == ui::map_tiles::MapTileStatus::Available;
}

bool map_source_directory_available(uint8_t map_source)
{
    return tile_source().layerDirectoryAvailable(
        ui::map_tiles::mapTileLayerFromBaseSource(sanitize_map_source(map_source)));
}

bool contour_directory_available()
{
    return tile_source().anyContourDirectoryAvailable();
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
    const int tiles = 1 << std::clamp(z, 0, 18);
    if (tiles <= 0)
    {
        x = 0;
        y = 0;
        return;
    }

    x %= tiles;
    if (x < 0)
    {
        x += tiles;
    }
    y = std::clamp(y, 0, tiles - 1);
}

void latLngToTile(double lat, double lng, int zoom, int& tile_x, int& tile_y)
{
    const double tiles = world_tiles(zoom);
    const double clamped_lat = clamp_lat(lat);
    const double lat_rad = clamped_lat * kPi / 180.0;
    const double x = (lng + 180.0) / 360.0 * tiles;
    const double y = (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / kPi) / 2.0 * tiles;

    tile_x = static_cast<int>(std::floor(x));
    tile_y = static_cast<int>(std::floor(y));
    normalize_tile(zoom, tile_x, tile_y);
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

    for (auto& tile : *ctx.tiles)
    {
        tile.visible = false;
    }

    for (int dy = -half_rows; dy <= half_rows; ++dy)
    {
        for (int dx = -half_cols; dx <= half_cols; ++dx)
        {
            int tile_x = static_cast<int>(ctx.anchor->gps_tile_x) + dx;
            int tile_y = static_cast<int>(ctx.anchor->gps_tile_y) + dy;
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

    MapTile* next_tile = nullptr;
    for (auto& tile : *ctx.tiles)
    {
        if (!tile.visible)
        {
            continue;
        }
        if (!tile.img_obj || !lv_obj_is_valid(tile.img_obj))
        {
            if (!next_tile || tile.priority < next_tile->priority)
            {
                next_tile = &tile;
            }
            continue;
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
