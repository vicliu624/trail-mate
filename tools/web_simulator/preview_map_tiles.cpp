// Synchronous browser filesystem adapter for the production MapViewport.
// Tile positions follow the existing Linux adapter and shared Mercator helpers.
// No page controls, overlays, labels, fonts or styles are defined here.
#include "ui/widgets/map/map_tiles.h"
#include "ui_map_runtime/map_tiles/map_tile_geometry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

static uint8_t source_id = 0;
static bool contours = false;
static bool missing = false;
static double world_size(int z) { return std::ldexp(256.0, z); }
static double world_x(double lon, int z) { return (lon + 180.0) / 360.0 * world_size(z); }
static double world_y(double lat, int z)
{
    lat = std::clamp(lat, -85.05112878, 85.05112878);
    const double s = std::sin(lat * M_PI / 180.0);
    return (.5 - std::log((1 + s) / (1 - s)) / (4 * M_PI)) * world_size(z);
}
static void world_geo(double x, double y, int z, double& lat, double& lon)
{
    const double size = world_size(z);
    lon = x / size * 360 - 180;
    lat = std::atan(std::sinh(M_PI * (1 - 2 * y / size))) * 180 / M_PI;
}
void normalize_tile(int z, int& x, int& y) { ui::map_tiles::normalizeTile(z, x, y); }
void latLngToTile(double lat, double lon, int zoom, int& x, int& y) { ui::map_tiles::latLngToTile(lat, lon, zoom, x, y); }
void tileToLatLng(int x, int y, int zoom, double& lat, double& lon) { world_geo(x * 256.0, y * 256.0, zoom, lat, lon); }
void tileToPixel(int x, int y, int, int& px, int& py)
{
    px = x * 256;
    py = y * 256;
}
uint8_t sanitize_map_source(uint8_t source) { return source < 3 ? source : 0; }
const char* map_source_label(uint8_t source) { return source == 1 ? "Terrain" : source == 2 ? "Satellite"
                                                                                            : "OSM"; }
bool build_base_tile_path(int z, int x, int y, uint8_t source, char* out, size_t n)
{
    const char* key = source == 1 ? "terrain" : source == 2 ? "satellite"
                                                            : "osm";
    return std::snprintf(out, n, "A:/maps/base/%s/%d/%d/%d.png", key, z, x, y) > 0;
}
bool build_contour_tile_path(int z, int x, int y, char* out, size_t n) { return std::snprintf(out, n, "A:/maps/contour/major-50/%d/%d/%d.png", z, x, y) > 0; }
static bool exists(const char* path)
{
    FILE* f = std::fopen(path + 2, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}
bool base_tile_available(int z, int x, int y, uint8_t source)
{
    char path[180];
    build_base_tile_path(z, x, y, source, path, sizeof(path));
    return exists(path);
}
bool map_source_directory_available(uint8_t source) { return source < 3; }
bool contour_directory_available() { return false; }
bool take_missing_tile_notice(uint8_t* out)
{
    if (out) *out = source_id;
    const bool result = missing;
    missing = false;
    return result;
}
void set_map_render_options(uint8_t source, bool contour)
{
    source_id = sanitize_map_source(source);
    contours = contour;
}
void update_map_anchor(TileContext& c, double lat, double lon, int zoom, int pan_x, int pan_y, bool fix)
{
    if (!c.anchor || !c.map_container) return;
    auto& a = *c.anchor;
    a.valid = fix;
    if (!fix) return;
    a.z = zoom;
    a.n = std::ldexp(1.0, zoom);
    int x, y;
    latLngToTile(lat, lon, zoom, x, y);
    a.gps_tile_x = x;
    a.gps_tile_y = y;
    a.gps_global_pixel_x = static_cast<int>(std::lround(world_x(lon, zoom)));
    a.gps_global_pixel_y = static_cast<int>(std::lround(world_y(lat, zoom)));
    a.gps_tile_pixel_x = x * 256;
    a.gps_tile_pixel_y = y * 256;
    a.gps_offset_x = a.gps_global_pixel_x - a.gps_tile_pixel_x;
    a.gps_offset_y = a.gps_global_pixel_y - a.gps_tile_pixel_y;
    a.gps_tile_screen_x = lv_obj_get_width(c.map_container) / 2 - a.gps_offset_x + pan_x;
    a.gps_tile_screen_y = lv_obj_get_height(c.map_container) / 2 - a.gps_offset_y + pan_y;
}
bool gps_screen_pos(const TileContext& c, double lat, double lon, int& x, int& y)
{
    if (!c.anchor || !c.anchor->valid) return false;
    const auto& a = *c.anchor;
    double dx = world_x(lon, a.z) - a.gps_global_pixel_x;
    const double size = world_size(a.z);
    if (dx > size / 2) dx -= size;
    if (dx < -size / 2) dx += size;
    x = std::lround(a.gps_tile_screen_x + a.gps_offset_x + dx);
    y = std::lround(a.gps_tile_screen_y + a.gps_offset_y + world_y(lat, a.z) - a.gps_global_pixel_y);
    return true;
}
bool tile_screen_pos_xyz(const TileContext& c, int x, int y, int z, int& sx, int& sy)
{
    if (!c.anchor || !c.anchor->valid || c.anchor->z != z) return false;
    const auto& a = *c.anchor;
    int dx = x - a.gps_tile_x, n = 1 << z;
    if (dx > n / 2) dx -= n;
    if (dx < -n / 2) dx += n;
    sx = a.gps_tile_screen_x + dx * 256;
    sy = a.gps_tile_screen_y + (y - a.gps_tile_y) * 256;
    return true;
}
void get_screen_center_lat_lng(const TileContext& c, double& lat, double& lon)
{
    if (!c.anchor || !c.anchor->valid)
    {
        lat = lon = 0;
        return;
    }
    const auto& a = *c.anchor;
    world_geo(a.gps_global_pixel_x + lv_obj_get_width(c.map_container) / 2 - a.gps_tile_screen_x - a.gps_offset_x, a.gps_global_pixel_y + lv_obj_get_height(c.map_container) / 2 - a.gps_tile_screen_y - a.gps_offset_y, a.z, lat, lon);
}
bool tile_in_rect(int x, int y, int w, int h, int margin) { return x < w + margin && y < h + margin && x + 256 > -margin && y + 256 > -margin; }
void cleanup_tiles(TileContext& c)
{
    if (c.tiles)
    {
        for (auto& t : *c.tiles)
        {
            if (t.img_obj) lv_obj_delete(t.img_obj);
            if (t.contour_obj) lv_obj_delete(t.contour_obj);
        }
        c.tiles->clear();
    }
    if (c.has_map_data) *c.has_map_data = false;
    if (c.has_visible_map_data) *c.has_visible_map_data = false;
}
void calculate_required_tiles(TileContext& c, double lat, double lon, int z, int px, int py, bool fix)
{
    update_map_anchor(c, lat, lon, z, px, py, fix);
    if (!c.tiles || !c.anchor || !c.anchor->valid) return;
    bool any = false;
    for (auto& t : *c.tiles)
    {
        t.visible = false;
        if (t.img_obj) lv_obj_add_flag(t.img_obj, LV_OBJ_FLAG_HIDDEN);
    }
    const int w = lv_obj_get_width(c.map_container), h = lv_obj_get_height(c.map_container);
    double center_lat, center_lon;
    get_screen_center_lat_lng(c, center_lat, center_lon);
    for (auto xy : ui::map_tiles::tilesAround(center_lat, center_lon, z, w / 256 + 2, h / 256 + 2))
    {
        int sx, sy;
        if (!tile_screen_pos_xyz(c, xy.x, xy.y, z, sx, sy) || !tile_in_rect(sx, sy, w, h, 0)) continue;
        auto found = std::find_if(c.tiles->begin(), c.tiles->end(), [&](const auto& t)
                                  { return t.x == xy.x && t.y == xy.y && t.z == z && t.map_source == source_id; });
        if (found == c.tiles->end())
        {
            MapTile t;
            t.x = xy.x;
            t.y = xy.y;
            t.z = z;
            t.map_source = source_id;
            t.has_png_file = base_tile_available(z, t.x, t.y, source_id);
            c.tiles->push_back(t);
            found = c.tiles->end() - 1;
        }
        auto& tile = *found;
        if (!tile.has_png_file)
        {
            missing = true;
            continue;
        }
        if (!tile.img_obj)
        {
            tile.img_obj = lv_image_create(c.map_container);
            char path[180];
            build_base_tile_path(z, tile.x, tile.y, source_id, path, sizeof(path));
            lv_image_set_src(tile.img_obj, path);
        }
        lv_obj_set_pos(tile.img_obj, sx, sy);
        lv_obj_clear_flag(tile.img_obj, LV_OBJ_FLAG_HIDDEN);
        tile.visible = true;
        any = true;
    }
    if (c.has_map_data) *c.has_map_data = any;
    if (c.has_visible_map_data) *c.has_visible_map_data = any;
}
void tile_loader_step(TileContext&) {}
void init_tile_context(TileContext& c, lv_obj_t* obj, MapAnchor* anchor, std::vector<MapTile>* tiles, ui::map_tiles::MapTileRenderQueue* queue, bool* has, bool* visible)
{
    c = {};
    c.map_container = obj;
    c.anchor = anchor;
    c.tiles = tiles;
    c.render_queue = queue;
    c.has_map_data = has;
    c.has_visible_map_data = visible;
}
void release_tile_context(TileContext& c)
{
    cleanup_tiles(c);
    c = {};
}
