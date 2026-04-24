/**
 * @file map_tiles.h
 * @brief Linux map tile management and geometry helpers for shared map pages.
 */

#pragma once

#include "lvgl.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#define TILE_SIZE 256
#define TILE_CACHE_LIMIT 12
#define TILE_RECORD_LIMIT 48

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct MapAnchor
{
    int z = 0;
    int32_t gps_tile_x = 0;
    int32_t gps_tile_y = 0;
    int32_t gps_tile_pixel_x = 0;
    int32_t gps_tile_pixel_y = 0;
    int32_t gps_offset_x = 0;
    int32_t gps_offset_y = 0;
    int32_t gps_global_pixel_x = 0;
    int32_t gps_global_pixel_y = 0;
    int gps_tile_screen_x = 0;
    int gps_tile_screen_y = 0;
    double n = 0.0;
    bool valid = false;
};

struct DecodedTileCache
{
    int32_t x = -1;
    int32_t y = -1;
    int32_t z = -1;
    uint8_t map_source = 0;
    lv_image_dsc_t* img_dsc = nullptr;
    uint32_t last_used_ms = 0;
    bool in_use = false;
};

struct MapTile
{
    int32_t x = 0;
    int32_t y = 0;
    int z = 0;
    uint8_t map_source = 0;
    lv_obj_t* img_obj = nullptr;
    lv_obj_t* contour_obj = nullptr;
    bool visible = false;
    bool ever_visible = false;
    uint32_t last_used_ms = 0;
    uint32_t obj_evicted_ms = 0;
    bool record_evicted = false;
    int priority = 0;
    bool has_png_file = false;
    bool base_missing = false;
    bool contour_checked = false;
    bool contour_loaded = false;
    DecodedTileCache* cached_img = nullptr;
};

struct TileContext
{
    lv_obj_t* map_container = nullptr;
    MapAnchor* anchor = nullptr;
    std::vector<MapTile>* tiles = nullptr;
    bool* has_map_data = nullptr;
    bool* has_visible_map_data = nullptr;
};

void normalize_tile(int z, int& x, int& y);
void latLngToTile(double lat, double lng, int zoom, int& tile_x, int& tile_y);
void tileToLatLng(int tile_x, int tile_y, int zoom, double& lat, double& lng);
void get_screen_center_lat_lng(const TileContext& ctx, double& lat, double& lng);
void tileToPixel(int tile_x, int tile_y, int zoom, int& pixel_x, int& pixel_y);
bool gps_screen_pos(const TileContext& ctx, double lat, double lng, int& sx, int& sy);
bool tile_screen_pos_xyz(const TileContext& ctx, int x, int y, int z, int& sx, int& sy);
bool tile_in_rect(int sx, int sy, int w, int h, int margin);
void update_map_anchor(TileContext& ctx, double lat, double lng, int zoom, int pan_x, int pan_y, bool has_fix);
void calculate_required_tiles(TileContext& ctx, double lat, double lng, int zoom, int pan_x, int pan_y, bool has_fix);
void tile_loader_step(TileContext& ctx);
uint8_t sanitize_map_source(uint8_t map_source);
const char* map_source_label(uint8_t map_source);
bool build_base_tile_path(int z, int x, int y, uint8_t map_source, char* out_path, size_t out_size);
bool build_contour_tile_path(int z, int x, int y, char* out_path, size_t out_size);
bool base_tile_available(int z, int x, int y, uint8_t map_source);
bool map_source_directory_available(uint8_t map_source);
bool contour_directory_available();
bool take_missing_tile_notice(uint8_t* out_map_source);
void set_map_render_options(uint8_t map_source, bool contour_enabled);
void init_tile_context(TileContext& ctx,
                       lv_obj_t* map_container,
                       MapAnchor* anchor,
                       std::vector<MapTile>* tiles,
                       bool* has_map_data,
                       bool* has_visible_map_data);
void cleanup_tiles(TileContext& ctx);
