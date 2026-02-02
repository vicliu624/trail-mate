/**
 * @file map_tiles.h
 * @brief Map tile management and rendering
 */

#ifndef MAP_TILES_H
#define MAP_TILES_H

#include "lvgl.h"
#include <cstdint> // For int32_t
#include <vector>

// Map tile constants
#define TILE_SIZE 256

// Configuration
#define TILE_CACHE_LIMIT 12
#define TILE_RECORD_LIMIT 48

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Map anchor cache to avoid repeated pow/log calculations
// Note: Using int32_t for pixel coordinates ensures sufficient precision:
// - At zoom 18: max_pixel = (2^18 - 1) * 256 = 67,108,608 << int32_t max (2,147,483,647)
// - int32_t is sufficient for all zoom levels 0-18 and prevents platform-dependent int size issues
struct MapAnchor
{
    int z;
    int32_t gps_tile_x, gps_tile_y;                 // Tile coordinates (max 262,143 at zoom 18)
    int32_t gps_tile_pixel_x, gps_tile_pixel_y;     // Tile pixel coordinates (tile_x * 256)
    int32_t gps_offset_x, gps_offset_y;             // Offset within tile (range -255 to 255)
    int32_t gps_global_pixel_x, gps_global_pixel_y; // Cached global pixel coordinates (0..world_px-1)
    int gps_tile_screen_x, gps_tile_screen_y;       // Screen coordinates (typically < 2000px, int is fine)
    double n;
    bool valid;
};

// Decoded tile image cache entry (for RAM cache to avoid re-decoding)
struct DecodedTileCache
{
    int32_t x, y, z;         // Tile coordinates
    lv_image_dsc_t* img_dsc; // Decoded image descriptor (RGB565 data in RAM)
    uint32_t last_used_ms;   // For LRU eviction
    bool in_use;             // True if currently used by a visible tile
};

// Map tile structure
struct MapTile
{
    int32_t x;         // Tile X coordinate (wrapped, max 262,143 at zoom 18)
    int32_t y;         // Tile Y coordinate (clamped, max 262,143 at zoom 18)
    int z;             // Zoom level (0-18)
    lv_obj_t* img_obj; // NULL = not loaded, non-NULL = loaded (image or label placeholder)
    bool visible;
    bool ever_visible;            // Track if tile was ever visible (for eviction priority)
    uint32_t last_used_ms;        // For LRU cache eviction
    uint32_t obj_evicted_ms;      // Timestamp when object was evicted (0 = not evicted)
    bool record_evicted;          // Record should be removed from vector
    int priority;                 // Loading priority (distance from center, lower = higher priority)
    bool has_png_file;            // True if tile has PNG file (not placeholder)
    DecodedTileCache* cached_img; // Pointer to decoded image cache entry (NULL if not cached)
};

// Tile management context (passed to functions instead of using global state)
struct TileContext
{
    lv_obj_t* map_container; // Only UI dependency - for creating objects and getting size
    MapAnchor* anchor;
    std::vector<MapTile>* tiles;
    bool* has_map_data;         // Global: any tile ever loaded
    bool* has_visible_map_data; // Viewport: current visible tiles have PNG
};

// Core tile functions - implemented in map_tiles.cpp

/**
 * Normalize tile coordinates to valid range (wrap x, clamp y)
 */
void normalize_tile(int z, int& x, int& y);

/**
 * Convert latitude/longitude to tile coordinates
 */
void latLngToTile(double lat, double lng, int zoom, int& tile_x, int& tile_y);

/**
 * Convert tile coordinates to latitude/longitude (inverse of latLngToTile)
 * Returns the center of the tile
 */
void tileToLatLng(int tile_x, int tile_y, int zoom, double& lat, double& lng);

/**
 * Calculate the latitude/longitude of the current screen center
 * Uses the current anchor and pan offsets to determine what's at screen center
 */
void get_screen_center_lat_lng(const TileContext& ctx, double& lat, double& lng);

/**
 * Convert tile coordinates to pixel coordinates
 */
void tileToPixel(int tile_x, int tile_y, int zoom, int& pixel_x, int& pixel_y);

/**
 * Calculate screen position for GPS coordinates (lat/lng)
 * Returns true if position was calculated successfully
 */
bool gps_screen_pos(const TileContext& ctx, double lat, double lng, int& sx, int& sy);

/**
 * Calculate screen position for tile by xyz coordinates
 */
bool tile_screen_pos_xyz(const TileContext& ctx, int x, int y, int z, int& sx, int& sy);

/**
 * Check if tile is in rectangle (with margin for preloading)
 */
bool tile_in_rect(int sx, int sy, int w, int h, int margin);

/**
 * Calculate and cache map anchor (GPS pixel coordinates)
 */
void update_map_anchor(TileContext& ctx, double lat, double lng, int zoom, int pan_x, int pan_y, bool has_fix);

/**
 * Calculate which tiles are needed to fill the screen
 */
void calculate_required_tiles(TileContext& ctx, double lat, double lng, int zoom, int pan_x, int pan_y, bool has_fix);

/**
 * Load one tile (called by timer, not in calculate_required_tiles)
 */
void tile_loader_step(TileContext& ctx);

/**
 * Initialize tile context
 */
void init_tile_context(TileContext& ctx, lv_obj_t* map_container, MapAnchor* anchor,
                       std::vector<MapTile>* tiles, bool* has_map_data, bool* has_visible_map_data);

/**
 * Cleanup tiles
 */
void cleanup_tiles(TileContext& ctx);

#endif // MAP_TILES_H
