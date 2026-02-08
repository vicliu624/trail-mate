/**
 * @file map_tiles.cpp
 * @brief Map tile management and rendering implementation
 */

#include "map_tiles.h"
#include "../../screens/gps/gps_constants.h"
#include "../../../display/DisplayInterface.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"
#include <Arduino.h>
#include <SD.h>
#include <algorithm>
#include <cmath>
#include <cstring> // For memcpy

// Use LVGL's decoder API to decode PNG images
// We'll decode PNG to RGB565 and cache it in RAM to avoid re-decoding on every render
// This approach uses LVGL's built-in decoder, avoiding direct lodepng dependency

// Debug logging control
#define GPS_DEBUG 0
#if GPS_DEBUG
#define GPS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif

// Decoded tile image cache (LRU, max 8 tiles)
#define TILE_DECODE_CACHE_SIZE 12
static DecodedTileCache g_tile_decode_cache[TILE_DECODE_CACHE_SIZE];
static bool g_tile_cache_initialized = false;
static uint32_t g_cache_full_until_ms = 0;
static uint32_t g_cache_full_log_ms = 0;

namespace
{
class SpiLockGuard
{
  public:
    explicit SpiLockGuard(TickType_t wait_ticks)
        : locked_(display_spi_lock(wait_ticks))
    {
    }

    ~SpiLockGuard()
    {
        if (locked_)
        {
            display_spi_unlock();
        }
    }

    bool locked() const { return locked_; }

  private:
    bool locked_;
};
} // namespace

/**
 * Initialize tile decode cache
 */
static void init_tile_decode_cache()
{
    if (g_tile_cache_initialized) return;
    for (int i = 0; i < TILE_DECODE_CACHE_SIZE; i++)
    {
        g_tile_decode_cache[i].x = -1;
        g_tile_decode_cache[i].y = -1;
        g_tile_decode_cache[i].z = -1;
        g_tile_decode_cache[i].img_dsc = NULL;
        g_tile_decode_cache[i].last_used_ms = 0;
        g_tile_decode_cache[i].in_use = false;
    }
    g_tile_cache_initialized = true;
    GPS_LOG("[GPS] Tile decode cache initialized (size=%d)\n", TILE_DECODE_CACHE_SIZE);
}

/**
 * Find cached decoded tile image
 */
static DecodedTileCache* find_cached_tile(int x, int y, int z)
{
    if (!g_tile_cache_initialized) init_tile_decode_cache();

    for (int i = 0; i < TILE_DECODE_CACHE_SIZE; i++)
    {
        if (g_tile_decode_cache[i].x == x &&
            g_tile_decode_cache[i].y == y &&
            g_tile_decode_cache[i].z == z &&
            g_tile_decode_cache[i].img_dsc != NULL)
        {
            g_tile_decode_cache[i].last_used_ms = millis();
            return &g_tile_decode_cache[i];
        }
    }
    return NULL;
}

/**
 * Get least recently used cache slot
 * Returns NULL if all slots are in use (to avoid use-after-free)
 */
static DecodedTileCache* get_lru_cache_slot()
{
    if (!g_tile_cache_initialized) init_tile_decode_cache();

    uint32_t oldest_ms = UINT32_MAX;
    int lru_idx = -1;
    bool found_unused = false;

    for (int i = 0; i < TILE_DECODE_CACHE_SIZE; i++)
    {
        if (g_tile_decode_cache[i].img_dsc == NULL)
        {
            return &g_tile_decode_cache[i]; // Empty slot
        }
        if (!g_tile_decode_cache[i].in_use)
        {
            found_unused = true;
            if (g_tile_decode_cache[i].last_used_ms < oldest_ms)
            {
                oldest_ms = g_tile_decode_cache[i].last_used_ms;
                lru_idx = i;
            }
        }
    }

    // If all slots are in use, return NULL to avoid use-after-free
    if (!found_unused || lru_idx == -1)
    {
        uint32_t now_ms = millis();
        g_cache_full_until_ms = now_ms + 500;
        if (now_ms - g_cache_full_log_ms >= 1000)
        {
            GPS_LOG("[GPS] All cache slots are in use, cannot evict safely\n");
            g_cache_full_log_ms = now_ms;
        }
        return NULL;
    }

    // Evict LRU entry (only if it's not in use)
    if (g_tile_decode_cache[lru_idx].img_dsc != NULL)
    {
        GPS_LOG("[GPS] Evicting cached tile %d/%d/%d from decode cache\n",
                g_tile_decode_cache[lru_idx].z,
                g_tile_decode_cache[lru_idx].x,
                g_tile_decode_cache[lru_idx].y);
        // Free the image descriptor (data was allocated by us)
        if (g_tile_decode_cache[lru_idx].img_dsc->data != NULL)
        {
            lv_free((void*)g_tile_decode_cache[lru_idx].img_dsc->data);
        }
        lv_free(g_tile_decode_cache[lru_idx].img_dsc);
        g_tile_decode_cache[lru_idx].img_dsc = NULL;
    }

    return &g_tile_decode_cache[lru_idx];
}

/**
 * Normalize tile coordinates to valid range (wrap x, clamp y)
 */
void normalize_tile(int z, int& x, int& y)
{
    int n = 1 << z; // z<=18 OK, n = 2^z
    if (n <= 0) return;

    // Wrap x coordinate (longitude wraps around)
    x = ((x % n) + n) % n;

    // Clamp y coordinate (latitude is bounded)
    if (y < 0) y = 0;
    if (y >= n) y = n - 1;
}

/**
 * Convert latitude/longitude to tile coordinates
 * Returns wrapped/clamped tile coordinates
 */
void latLngToTile(double lat, double lng, int zoom, int& tile_x, int& tile_y)
{
    // Clamp latitude to WebMercator valid range to avoid pole issues
    const double MAX_LAT = 85.05112878;
    if (lat > MAX_LAT) lat = MAX_LAT;
    if (lat < -MAX_LAT) lat = -MAX_LAT;

    // Wrap longitude to [-180, 180) to handle GPS errors and date line crossing
    while (lng < -180.0) lng += 360.0;
    while (lng >= 180.0) lng -= 360.0;

    double n = pow(2.0, zoom);
    double lat_rad = lat * M_PI / 180.0;
    tile_x = (int)((lng + 180.0) / 360.0 * n);
    tile_y = (int)((1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * n);

    // Use normalize_tile for consistency
    normalize_tile(zoom, tile_x, tile_y);
}

/**
 * Convert tile coordinates to latitude/longitude (inverse of latLngToTile)
 * This is used to calculate the center of the current map view
 */
void tileToLatLng(int tile_x, int tile_y, int zoom, double& lat, double& lng)
{
    double n = pow(2.0, zoom);

    // Convert tile coordinates to longitude (center of tile, not top-left corner)
    // Add 0.5 to get tile center instead of top-left corner
    lng = ((tile_x + 0.5) / n) * 360.0 - 180.0;

    // Convert tile coordinates to latitude (center of tile, not top-left corner)
    // Inverse of: tile_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * n
    // Add 0.5 to get tile center instead of top-left corner
    double y_ratio = (tile_y + 0.5) / n;
    double lat_rad = atan(sinh(M_PI * (1.0 - 2.0 * y_ratio)));
    lat = lat_rad * 180.0 / M_PI;

    // Clamp latitude to valid range
    const double MAX_LAT = 85.05112878;
    if (lat > MAX_LAT) lat = MAX_LAT;
    if (lat < -MAX_LAT) lat = -MAX_LAT;
}

/**
 * Calculate the latitude/longitude of the current screen center
 * Uses the current anchor and pan offsets to determine what's at screen center
 */
void get_screen_center_lat_lng(const TileContext& ctx, double& lat, double& lng)
{
    if (!ctx.map_container || !ctx.anchor || !ctx.anchor->valid)
    {
        // If anchor is invalid, return default London coordinates
        lat = gps_ui::kDefaultLat;
        lng = gps_ui::kDefaultLng;
        return;
    }

    lv_coord_t w = lv_obj_get_width(ctx.map_container);
    lv_coord_t h = lv_obj_get_height(ctx.map_container);

    // CRITICAL FIX: Calculate pan from anchor
    // GPS point is placed at (w/2 + pan_x) on screen
    // So: pan_x = gps_tile_screen_x + gps_offset_x - w/2
    int32_t pan_x = (int32_t)(ctx.anchor->gps_tile_screen_x + ctx.anchor->gps_offset_x) - (int32_t)(w / 2);
    int32_t pan_y = (int32_t)(ctx.anchor->gps_tile_screen_y + ctx.anchor->gps_offset_y) - (int32_t)(h / 2);

    // Screen center corresponds to: GPS global pixel - pan
    // This is the correct geometric relationship from update_map_anchor()
    int64_t cx = (int64_t)ctx.anchor->gps_global_pixel_x - (int64_t)pan_x;
    int64_t cy = (int64_t)ctx.anchor->gps_global_pixel_y - (int64_t)pan_y;

    // World pixel width at current zoom level
    int32_t world_px = (int32_t)(ctx.anchor->n * TILE_SIZE);

    // X must wrap (world is a cylinder, longitude wraps)
    int64_t x = cx % world_px;
    if (x < 0) x += world_px;

    // Y must clamp (Mercator projection has bounds at poles)
    int64_t y = cy;
    if (y < 0) y = 0;
    if (y >= world_px) y = world_px - 1;

    // Convert world pixel coordinates to lat/lng (WebMercator inverse)
    lng = ((double)x / (double)world_px) * 360.0 - 180.0;

    double y_ratio = (double)y / (double)world_px;
    double lat_rad = atan(sinh(M_PI * (1.0 - 2.0 * y_ratio)));
    lat = lat_rad * 180.0 / M_PI;

    // Clamp latitude to valid range (safety check)
    const double MAX_LAT = 85.05112878;
    if (lat > MAX_LAT) lat = MAX_LAT;
    if (lat < -MAX_LAT) lat = -MAX_LAT;
}

/**
 * Convert tile coordinates to pixel coordinates
 */
void tileToPixel(int tile_x, int tile_y, int zoom, int& pixel_x, int& pixel_y)
{
    (void)zoom; // Unused but kept for API consistency
    // TILE_SIZE = 256 = 2^8, so tile_x * 256 = tile_x << 8
    pixel_x = tile_x << 8;
    pixel_y = tile_y << 8;
}

/**
 * Calculate screen position for tile by xyz coordinates
 */
bool tile_screen_pos_xyz(const TileContext& ctx, int x, int y, int z, int& sx, int& sy)
{
    if (!ctx.map_container) return false;

    lv_coord_t w = lv_obj_get_width(ctx.map_container);
    lv_coord_t h = lv_obj_get_height(ctx.map_container);

    if (!ctx.anchor || !ctx.anchor->valid)
    {
        // No GPS: center tile 0/0/0
        sx = (w - TILE_SIZE) / 2;
        sy = (h - TILE_SIZE) / 2;
        return true;
    }

    // CRITICAL FIX: Handle tile coordinate wrapping (normalize_tile can cause neighbor tiles to wrap)
    // Use shortest wrap distance to preserve neighbor relationships across date line
    int n = 1 << z; // Number of tiles at this zoom level

    // Calculate dx with shortest wrap (handles date line crossing)
    int dx = x - ctx.anchor->gps_tile_x;
    if (dx > n / 2) dx -= n;  // Wrap: if dx > n/2, go the other way
    if (dx < -n / 2) dx += n; // Wrap: if dx < -n/2, go the other way

    // For y, no wrapping needed (latitude is clamped, not wrapped)
    int dy = y - ctx.anchor->gps_tile_y;

    // Calculate tile pixel coordinates using wrapped dx/dy
    // This preserves neighbor relationships even when normalize_tile wraps x
    int tile_px = (ctx.anchor->gps_tile_x + dx) << 8;
    int tile_py = (ctx.anchor->gps_tile_y + dy) << 8;

    // Use cached anchor for screen position calculation
    sx = ctx.anchor->gps_tile_screen_x + (tile_px - ctx.anchor->gps_tile_pixel_x);
    sy = ctx.anchor->gps_tile_screen_y + (tile_py - ctx.anchor->gps_tile_pixel_y);
    return true;
}

/**
 * Calculate screen position for GPS coordinates (lat/lng)
 * Uses the same algorithm as update_map_anchor to ensure consistency
 */
bool gps_screen_pos(const TileContext& ctx, double lat, double lng, int& sx, int& sy)
{
    if (!ctx.map_container || !ctx.anchor || !ctx.anchor->valid)
    {
        return false;
    }

    lv_coord_t screen_width = lv_obj_get_width(ctx.map_container);
    lv_coord_t screen_height = lv_obj_get_height(ctx.map_container);
    int zoom = ctx.anchor->z;

    // Clamp latitude to WebMercator valid range
    double lat_clamped = lat;
    const double MAX_LAT = 85.05112878;
    if (lat_clamped > MAX_LAT) lat_clamped = MAX_LAT;
    if (lat_clamped < -MAX_LAT) lat_clamped = -MAX_LAT;

    // Wrap longitude to [-180, 180)
    double lng_wrapped = lng;
    while (lng_wrapped < -180.0) lng_wrapped += 360.0;
    while (lng_wrapped >= 180.0) lng_wrapped -= 360.0;

    // Calculate GPS global pixel coordinates (same as update_map_anchor)
    double n = pow(2.0, zoom);
    double lat_rad = lat_clamped * M_PI / 180.0;
    double gps_pixel_x = ((lng_wrapped + 180.0) / 360.0 * n) * TILE_SIZE;
    double gps_pixel_y = ((1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * n) * TILE_SIZE;

    int32_t gps_global_pixel_x = (int32_t)floor(gps_pixel_x);
    int32_t gps_global_pixel_y = (int32_t)floor(gps_pixel_y);

    // Calculate screen position relative to anchor
    // GPS position = anchor position + (GPS pixel - anchor pixel)
    int32_t dx = gps_global_pixel_x - ctx.anchor->gps_global_pixel_x;
    int32_t dy = gps_global_pixel_y - ctx.anchor->gps_global_pixel_y;

    sx = ctx.anchor->gps_tile_screen_x + ctx.anchor->gps_offset_x + dx;
    sy = ctx.anchor->gps_tile_screen_y + ctx.anchor->gps_offset_y + dy;

    return true;
}

/**
 * Unified visibility check
 */
bool tile_in_rect(int sx, int sy, int w, int h, int margin)
{
    return (sx + TILE_SIZE >= -margin && sx < w + margin &&
            sy + TILE_SIZE >= -margin && sy < h + margin);
}

/**
 * Find existing tile by coordinates
 */
static MapTile* find_tile(TileContext& ctx, int x, int y, int z)
{
    if (!ctx.tiles) return NULL;
    for (auto& tile : *ctx.tiles)
    {
        if (tile.x == x && tile.y == y && tile.z == z)
        {
            return &tile;
        }
    }
    return NULL;
}

/**
 * Ensure tile record exists and set fields
 */
static MapTile& ensure_tile(TileContext& ctx, int x, int y, int z, int priority)
{
    if (!ctx.tiles)
    {
        GPS_LOG("[GPS] ERROR: ctx.tiles is NULL in ensure_tile\n");
        static MapTile dummy;
        return dummy;
    }

    MapTile* existing = find_tile(ctx, x, y, z);
    if (existing != NULL)
    {
        existing->visible = true;
        existing->ever_visible = true;
        existing->last_used_ms = millis();
        existing->priority = priority;
        return *existing;
    }

    // Create new tile record
    MapTile t{};
    t.x = x;
    t.y = y;
    t.z = z;
    t.img_obj = NULL;
    t.visible = true;
    t.ever_visible = true;
    t.last_used_ms = millis();
    t.obj_evicted_ms = 0;
    t.record_evicted = false;
    t.priority = priority;
    t.has_png_file = false;
    t.cached_img = NULL; // No cached image initially
    ctx.tiles->push_back(t);
    return ctx.tiles->back();
}

/**
 * Style helper functions
 */
static void style_tile_obj(lv_obj_t* o)
{
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_all(o, 0, LV_PART_MAIN);
}

static void style_placeholder_label(lv_obj_t* label)
{
    style_tile_obj(label);
    lv_obj_set_style_bg_color(label, lv_color_hex(0xFFF0D3), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x3A2A1A), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

/**
 * Load tile image from SD card
 */
static void load_tile_image(TileContext& ctx, MapTile& tile)
{
    if (!ctx.map_container || !ctx.tiles)
    {
        GPS_LOG("[GPS] ERROR: Invalid context in load_tile_image\n");
        return;
    }

    // If tile already has a loaded image (not just placeholder), skip
    if (tile.img_obj != NULL && tile.has_png_file)
    {
        tile.last_used_ms = millis();
        GPS_LOG("[GPS] load_tile_image: Tile %d/%d/%d already loaded\n", tile.z, tile.x, tile.y);
        return;
    }

    SpiLockGuard spi_lock(pdMS_TO_TICKS(20));
    if (!spi_lock.locked())
    {
        GPS_LOG("[GPS] load_tile_image: SPI lock busy, deferring tile %d/%d/%d\n",
                tile.z, tile.x, tile.y);
        tile.last_used_ms = millis();
        return;
    }

    // If tile already has a placeholder label, check if file exists before recreating
    // This prevents repeatedly creating placeholders for missing tiles
    if (tile.img_obj != NULL && !tile.has_png_file)
    {
        // Already has placeholder - check if file now exists
        char path[64];
        snprintf(path, sizeof(path), "A:/maps/%d/%d/%d.png", tile.z, tile.x, tile.y);
        lv_fs_file_t f;
        lv_fs_res_t res = lv_fs_open(&f, path, LV_FS_MODE_RD);
        bool file_exists = (res == LV_FS_RES_OK);
        if (file_exists)
        {
            lv_fs_close(&f);
            // File now exists - proceed to load it (will delete placeholder below)
            GPS_LOG("[GPS] load_tile_image: Tile %d/%d/%d file now exists, replacing placeholder\n",
                    tile.z, tile.x, tile.y);
        }
        else
        {
            // File still doesn't exist - skip to avoid repeated placeholder creation
            tile.last_used_ms = millis();
            GPS_LOG("[GPS] load_tile_image: Tile %d/%d/%d already has placeholder, file still missing, skipping\n",
                    tile.z, tile.x, tile.y);
            return;
        }
    }

    char path[64];
    snprintf(path, sizeof(path), "A:/maps/%d/%d/%d.png", tile.z, tile.x, tile.y);
    GPS_LOG("[GPS] load_tile_image: Loading tile %d/%d/%d, path=%s\n", tile.z, tile.x, tile.y, path);

    // Always recalculate screen position (don't use old placeholder position)
    // This ensures correct position after panning/zooming
    int screen_x, screen_y;
    if (!tile_screen_pos_xyz(ctx, tile.x, tile.y, tile.z, screen_x, screen_y))
    {
        GPS_LOG("[GPS] ERROR: tile_screen_pos_xyz failed in load_tile_image\n");
        return;
    }

    // Check visibility
    lv_coord_t screen_width = lv_obj_get_width(ctx.map_container);
    lv_coord_t screen_height = lv_obj_get_height(ctx.map_container);
    tile.visible = tile_in_rect(screen_x, screen_y, screen_width, screen_height, 0);

    // Check if tile file exists
    lv_fs_file_t f;
    lv_fs_res_t res = lv_fs_open(&f, path, LV_FS_MODE_RD);
    bool file_exists = (res == LV_FS_RES_OK);
    if (file_exists)
    {
        lv_fs_close(&f);
        GPS_LOG("[GPS] Tile file EXISTS: %s (res=%d)\n", path, res);
    }
    else
    {
        GPS_LOG("[GPS] Tile file NOT found: %s (res=%d)\n", path, res);
    }

    if (file_exists)
    {
        // Check cache first
        DecodedTileCache* cached = find_cached_tile(tile.x, tile.y, tile.z);
        DecodedTileCache* cache_slot = nullptr;
        if (cached == NULL)
        {
            // No cached image: require an available cache slot, otherwise defer
            cache_slot = get_lru_cache_slot();
            if (cache_slot == NULL)
            {
                GPS_LOG("[GPS] Cache full (all slots in use), deferring tile %d/%d/%d\n",
                        tile.z, tile.x, tile.y);
                tile.last_used_ms = millis();
                return;
            }
        }

        // If tile has a placeholder label, delete it (but always recalculate position)
        lv_obj_t* old_obj = tile.img_obj;
        if (old_obj != NULL)
        {
            // Delete placeholder label before creating image
            lv_obj_del(old_obj);
            tile.img_obj = NULL;
        }

        // File exists: use lv_image
        tile.img_obj = lv_image_create(ctx.map_container);
        lv_obj_set_size(tile.img_obj, TILE_SIZE, TILE_SIZE);
        lv_obj_set_pos(tile.img_obj, screen_x, screen_y);
        style_tile_obj(tile.img_obj);
        lv_obj_move_background(tile.img_obj);

        if (cached && cached->img_dsc != NULL)
        {
            // Use cached decoded image (no PNG decode needed)
            GPS_LOG("[GPS] Using cached decoded image for tile %d/%d/%d\n", tile.z, tile.x, tile.y);
            lv_image_set_src(tile.img_obj, cached->img_dsc);
            cached->in_use = true;
            cached->last_used_ms = millis();
            tile.cached_img = cached;
        }
        else
        {
            // Decode PNG and cache it in RAM
            GPS_LOG("[GPS] Decoding and caching tile %d/%d/%d\n", tile.z, tile.x, tile.y);

            // Use LVGL's decoder to decode PNG
            lv_image_decoder_dsc_t decoder_dsc;
            memset(&decoder_dsc, 0, sizeof(decoder_dsc));

            lv_result_t decode_res = lv_image_decoder_open(&decoder_dsc, path, NULL);
            if (decode_res == LV_RESULT_OK && decoder_dsc.decoded != NULL)
            {
                // Decode successful - copy decoded data to our cache

                // Get decoded image info (decoded is const)
                const lv_draw_buf_t* decoded_buf = decoder_dsc.decoded;
                uint32_t width = decoded_buf->header.w;
                uint32_t height = decoded_buf->header.h;
                lv_color_format_t cf = (lv_color_format_t)decoded_buf->header.cf;
                uint32_t stride = decoded_buf->header.stride;
                uint32_t data_size = decoded_buf->data_size;

                GPS_LOG("[GPS] Decoded tile %d/%d/%d: %dx%d, cf=%d, stride=%d, size=%d\n",
                        tile.z, tile.x, tile.y, width, height, cf, stride, data_size);

                // Allocate image descriptor (cache_slot->img_dsc should be NULL after eviction)
                cache_slot->img_dsc = (lv_image_dsc_t*)lv_malloc(sizeof(lv_image_dsc_t));
                if (cache_slot->img_dsc == NULL)
                {
                    GPS_LOG("[GPS] ERROR: Failed to allocate memory for img_dsc\n");
                    lv_image_decoder_close(&decoder_dsc);
                    // Fall back to file path
                    lv_image_set_src(tile.img_obj, path);
                    tile.cached_img = NULL;
                }
                else
                {
                    // Allocate memory for image data
                    uint8_t* img_data = (uint8_t*)lv_malloc(data_size);
                    if (img_data == NULL)
                    {
                        GPS_LOG("[GPS] ERROR: Failed to allocate memory for image data (%d bytes)\n", data_size);
                        lv_free(cache_slot->img_dsc);
                        cache_slot->img_dsc = NULL;
                        lv_image_decoder_close(&decoder_dsc);
                        // Fall back to file path
                        lv_image_set_src(tile.img_obj, path);
                        tile.cached_img = NULL;
                    }
                    else
                    {
                        // Copy decoded data to our cache
                        memcpy(img_data, decoded_buf->data, data_size);

                        // Fill image descriptor (LVGL v9 structure)
                        cache_slot->img_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
                        cache_slot->img_dsc->header.w = width;
                        cache_slot->img_dsc->header.h = height;
                        cache_slot->img_dsc->header.cf = cf;
                        cache_slot->img_dsc->header.flags = 0;
                        cache_slot->img_dsc->header.stride = stride;
                        cache_slot->img_dsc->data_size = data_size;
                        cache_slot->img_dsc->data = (const uint8_t*)img_data;

                        // Update cache entry
                        cache_slot->x = tile.x;
                        cache_slot->y = tile.y;
                        cache_slot->z = tile.z;
                        cache_slot->last_used_ms = millis();
                        cache_slot->in_use = true;
                        tile.cached_img = cache_slot;

                        // Close decoder (we've copied the data)
                        lv_image_decoder_close(&decoder_dsc);

                        // Use cached decoded image
                        lv_image_set_src(tile.img_obj, cache_slot->img_dsc);
                        GPS_LOG("[GPS] Tile %d/%d/%d decoded and cached successfully\n", tile.z, tile.x, tile.y);
                    }
                }
            }
            else
            {
                // Decode failed - fall back to file path
                GPS_LOG("[GPS] WARNING: Failed to decode tile %d/%d/%d, using file path\n", tile.z, tile.x, tile.y);
                lv_image_decoder_close(&decoder_dsc);

                // Create cache entry placeholder (will be decoded later if possible)
                cache_slot->img_dsc = NULL; // Not yet decoded
                cache_slot->x = tile.x;
                cache_slot->y = tile.y;
                cache_slot->z = tile.z;
                cache_slot->last_used_ms = millis();
                cache_slot->in_use = false;
                tile.cached_img = cache_slot;

                lv_image_set_src(tile.img_obj, path);
            }
        }

        tile.has_png_file = true;
        *ctx.has_map_data = true; // Global flag - any tile ever loaded
        GPS_LOG("[GPS] Tile %d/%d/%d image loaded successfully\n", tile.z, tile.x, tile.y);
    }
    else
    {
        // File missing: use lv_label directly as tile object
        GPS_LOG("[GPS] Creating placeholder label for missing tile %d/%d/%d\n", tile.z, tile.x, tile.y);
        tile.has_png_file = false;
        tile.img_obj = lv_label_create(ctx.map_container);
        lv_obj_set_size(tile.img_obj, TILE_SIZE, TILE_SIZE);
        lv_obj_set_pos(tile.img_obj, screen_x, screen_y);
        style_placeholder_label(tile.img_obj);

        // Set text content: z/x/y format
        char coord_text[32];
        snprintf(coord_text, sizeof(coord_text), "%d/%d/%d", tile.z, tile.x, tile.y);
        lv_label_set_text(tile.img_obj, coord_text);
        GPS_LOG("[GPS] Created placeholder label for tile %d/%d/%d\n", tile.z, tile.x, tile.y);
    }

    tile.last_used_ms = millis();
    tile.obj_evicted_ms = 0;
    tile.record_evicted = false;
    GPS_LOG("[GPS] Tile %d/%d/%d loaded, visible=%d\n", tile.z, tile.x, tile.y, tile.visible);
}

/**
 * Calculate and cache map anchor (GPS pixel coordinates)
 */
void update_map_anchor(TileContext& ctx, double lat, double lng, int zoom, int pan_x, int pan_y, bool has_fix)
{
    if (!ctx.map_container || !ctx.anchor)
    {
        if (ctx.anchor) ctx.anchor->valid = false;
        return;
    }

    lv_coord_t screen_width = lv_obj_get_width(ctx.map_container);
    lv_coord_t screen_height = lv_obj_get_height(ctx.map_container);

    // Even without GPS fix, if lat/lng are provided (e.g., default London location),
    // calculate anchor to support rendering at that location
    // Use epsilon comparison for floating point
    const double EPSILON = 0.0001;
    if (!has_fix && (fabs(lat) < EPSILON && fabs(lng) < EPSILON))
    {
        // Only skip calculation if coordinates are truly zero (no default location set)
        GPS_LOG("[GPS] update_map_anchor: No GPS fix and coordinates are zero, skipping anchor calculation\n");
        ctx.anchor->valid = false;
        return;
    }

    GPS_LOG("[GPS] update_map_anchor: Calculating anchor (has_fix=%d, lat=%.6f, lng=%.6f, zoom=%d)\n",
            has_fix, lat, lng, zoom);

    // Use latLngToTile to ensure consistency with tile coordinate calculation
    // This ensures the same algorithm is used everywhere
    latLngToTile(lat, lng, zoom, ctx.anchor->gps_tile_x, ctx.anchor->gps_tile_y);

    // Calculate GPS global pixel coordinates for positioning
    ctx.anchor->n = pow(2.0, zoom);

    // Clamp latitude to WebMercator valid range before pixel calculation
    double lat_clamped = lat;
    const double MAX_LAT = 85.05112878;
    if (lat_clamped > MAX_LAT) lat_clamped = MAX_LAT;
    if (lat_clamped < -MAX_LAT) lat_clamped = -MAX_LAT;

    // Wrap longitude to [-180, 180)
    double lng_wrapped = lng;
    while (lng_wrapped < -180.0) lng_wrapped += 360.0;
    while (lng_wrapped >= 180.0) lng_wrapped -= 360.0;

    // Calculate GPS global pixel coordinates (expensive operations - done once)
    double lat_rad = lat_clamped * M_PI / 180.0;
    double gps_pixel_x = ((lng_wrapped + 180.0) / 360.0 * ctx.anchor->n) * TILE_SIZE;
    double gps_pixel_y = ((1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * ctx.anchor->n) * TILE_SIZE;

    // CRITICAL: Cache global pixel coordinates (0..world_px-1)
    // This is used by get_screen_center_lat_lng() to correctly calculate screen center
    ctx.anchor->gps_global_pixel_x = (int32_t)floor(gps_pixel_x);
    ctx.anchor->gps_global_pixel_y = (int32_t)floor(gps_pixel_y);

    // Calculate GPS tile pixel coordinates
    ctx.anchor->gps_tile_pixel_x = ctx.anchor->gps_tile_x << 8;
    ctx.anchor->gps_tile_pixel_y = ctx.anchor->gps_tile_y << 8;

    // Calculate GPS offset within tile
    // Use floor() instead of (int) truncation to avoid 1px jitter at boundaries
    ctx.anchor->gps_offset_x = ctx.anchor->gps_global_pixel_x - ctx.anchor->gps_tile_pixel_x;
    ctx.anchor->gps_offset_y = ctx.anchor->gps_global_pixel_y - ctx.anchor->gps_tile_pixel_y;

    // Calculate GPS tile screen position
    ctx.anchor->gps_tile_screen_x = screen_width / 2 - ctx.anchor->gps_offset_x + pan_x;
    ctx.anchor->gps_tile_screen_y = screen_height / 2 - ctx.anchor->gps_offset_y + pan_y;

    ctx.anchor->z = zoom;
    ctx.anchor->valid = true;
}

/**
 * Stage 1: Mark all tiles invisible
 * Aggressively delete tile objects from different zoom levels to free memory immediately
 */
static void mark_all_invisible(TileContext& ctx, int target_zoom)
{
    if (!ctx.tiles) return;

    // First pass: immediately delete objects from different zoom levels
    // This frees memory immediately when zoom changes, preventing accumulation
    for (auto& tile : *ctx.tiles)
    {
        // Mark cache entry as not in use before marking invisible
        if (tile.cached_img != NULL)
        {
            tile.cached_img->in_use = false;
        }

        tile.visible = false;
        // Delete tile objects that don't match target zoom level immediately
        // This prevents memory buildup when switching zoom levels frequently
        if (tile.img_obj != NULL && tile.z != target_zoom)
        {
            lv_obj_del(tile.img_obj);
            tile.img_obj = NULL;
            tile.has_png_file = false;
            tile.cached_img = NULL;         // Clear cache reference
            tile.obj_evicted_ms = millis(); // Mark as evicted for record cleanup protection
        }
    }

    if (ctx.has_visible_map_data)
    {
        *ctx.has_visible_map_data = false;
    }
}

/**
 * Stage 2: Collect required tiles
 */
static void collect_required_tiles(TileContext& ctx, double lat, double lng, int zoom, int pan_x, int pan_y, bool has_fix)
{
    if (!ctx.map_container || !ctx.tiles || !ctx.anchor)
    {
        GPS_LOG("[GPS] collect_required_tiles: Invalid context\n");
        return;
    }

    lv_coord_t screen_width = lv_obj_get_width(ctx.map_container);
    lv_coord_t screen_height = lv_obj_get_height(ctx.map_container);

    GPS_LOG("[GPS] collect_required_tiles: has_fix=%d, zoom=%d, lat=%.6f, lng=%.6f, screen=%dx%d\n",
            has_fix, zoom, lat, lng, screen_width, screen_height);

    // Check if anchor is valid (either from GPS fix or from default location like London)
    if (!ctx.anchor->valid)
    {
        // If anchor is invalid and no GPS fix, fall back to world map tile 0/0/zoom
        // Use the current zoom level, not hardcoded 0
        if (!has_fix)
        {
            GPS_LOG("[GPS] No GPS fix and invalid anchor: rendering world map tile 0/0/%d\n", zoom);
            ensure_tile(ctx, 0, 0, zoom, 0); // Priority 0 = center tile, use current zoom
            return;
        }
        else
        {
            GPS_LOG("[GPS] ERROR: cached_anchor invalid in collect_required_tiles (has_fix=true)\n");
            return;
        }
    }

    int gps_tile_x = ctx.anchor->gps_tile_x;
    int gps_tile_y = ctx.anchor->gps_tile_y;

    // Ensure GPS center tile exists
    ensure_tile(ctx, gps_tile_x, gps_tile_y, zoom, 0); // Priority 0 = center

    // Dynamic tile collection based on screen viewport
    // Calculate which tiles are needed to cover the entire screen (with buffer for preloading)
    // Start from screen corners and work inward to find all tiles that intersect the viewport

    // Calculate screen bounds with buffer (TILE_SIZE buffer for preloading)
    int screen_left = -TILE_SIZE;
    int screen_right = screen_width + TILE_SIZE;
    int screen_top = -TILE_SIZE;
    int screen_bottom = screen_height + TILE_SIZE;

    // Calculate tile range needed to cover screen
    // Convert screen coordinates to tile coordinates
    // For each possible tile position, check if it intersects the screen

    // Start from GPS tile and expand outward until we cover the entire screen
    // Use a reasonable maximum range (e.g., 10 tiles in each direction)
    const int MAX_TILE_RANGE = 10;

    // Collect all tiles that intersect the viewport
    for (int dy = -MAX_TILE_RANGE; dy <= MAX_TILE_RANGE; dy++)
    {
        for (int dx = -MAX_TILE_RANGE; dx <= MAX_TILE_RANGE; dx++)
        {
            int tile_x = gps_tile_x + dx;
            int tile_y = gps_tile_y + dy;

            // Normalize tile coordinates
            normalize_tile(zoom, tile_x, tile_y);

            // Calculate screen position for this tile
            int screen_x, screen_y;
            if (!tile_screen_pos_xyz(ctx, tile_x, tile_y, zoom, screen_x, screen_y))
            {
                continue; // Skip if position calculation fails
            }

            // Check if tile intersects viewport (with buffer for preloading)
            if (tile_in_rect(screen_x, screen_y, screen_width, screen_height, TILE_SIZE))
            {
                // Calculate priority (Manhattan distance from GPS tile)
                int priority = abs(dx) + abs(dy);
                ensure_tile(ctx, tile_x, tile_y, zoom, priority);
            }
        }
    }
}

/**
 * Stage 3: Layout loaded tile objects
 */
static void layout_loaded_tile_objects(TileContext& ctx)
{
    if (!ctx.map_container || !ctx.tiles || !ctx.anchor) return;

    lv_coord_t screen_width = lv_obj_get_width(ctx.map_container);
    lv_coord_t screen_height = lv_obj_get_height(ctx.map_container);
    int current_zoom = ctx.anchor->z; // Current zoom level

    for (auto& tile : *ctx.tiles)
    {
        // CRITICAL: Skip tiles from different zoom levels
        if (tile.z != current_zoom)
        {
            if (tile.img_obj != NULL)
            {
                lv_obj_add_flag(tile.img_obj, LV_OBJ_FLAG_HIDDEN);
            }
            tile.visible = false;
            continue;
        }

        // Calculate screen position
        int screen_x, screen_y;
        if (!tile_screen_pos_xyz(ctx, tile.x, tile.y, tile.z, screen_x, screen_y))
        {
            // If position calculation fails, hide the tile
            if (tile.img_obj != NULL)
            {
                lv_obj_add_flag(tile.img_obj, LV_OBJ_FLAG_HIDDEN);
            }
            tile.visible = false;
            continue;
        }

        // Check strict visibility (margin = 0 for actual display)
        bool is_visible = tile_in_rect(screen_x, screen_y, screen_width, screen_height, 0);
        tile.visible = is_visible;

        // Update cache in_use flag based on visibility
        if (tile.cached_img != NULL)
        {
            tile.cached_img->in_use = is_visible;
            if (is_visible)
            {
                tile.cached_img->last_used_ms = millis();
            }
        }

        if (is_visible)
        {
            // If tile is visible but has no object yet, create lightweight placeholder
            // Actual loading (I/O + decode) will be done asynchronously by tile_loader_step()
            if (tile.img_obj == NULL)
            {
                // Create lightweight placeholder label (no I/O, no decode, no blocking)
                tile.img_obj = lv_label_create(ctx.map_container);
                lv_obj_set_size(tile.img_obj, TILE_SIZE, TILE_SIZE);
                lv_obj_set_pos(tile.img_obj, screen_x, screen_y);
                style_placeholder_label(tile.img_obj);
                lv_obj_move_background(tile.img_obj);

                // Set text content: z/x/y format
                char coord_text[32];
                snprintf(coord_text, sizeof(coord_text), "%d/%d/%d", tile.z, tile.x, tile.y);
                lv_label_set_text(tile.img_obj, coord_text);

                // Mark that this tile needs loading (will be done by tile_loader_step)
                tile.has_png_file = false; // Will be set to true when actually loaded
            }
            else
            {
                // Update position for existing objects
                lv_obj_set_pos(tile.img_obj, screen_x, screen_y);
                lv_obj_clear_flag(tile.img_obj, LV_OBJ_FLAG_HIDDEN);
            }
            tile.last_used_ms = millis();
        }
        else
        {
            // Hide invisible tiles
            if (tile.img_obj != NULL)
            {
                lv_obj_add_flag(tile.img_obj, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // Check if any visible tile has PNG file
    bool visible_png_found = false;
    int visible_count = 0;
    int visible_with_png = 0;
    for (auto& tile : *ctx.tiles)
    {
        if (tile.visible)
        {
            visible_count++;
            if (tile.has_png_file)
            {
                visible_png_found = true;
                visible_with_png++;
            }
        }
    }
    if (ctx.has_visible_map_data)
    {
        bool old_value = *ctx.has_visible_map_data;
        *ctx.has_visible_map_data = visible_png_found;
        if (old_value != visible_png_found)
        {
            GPS_LOG("[GPS] has_visible_map_data changed: %d -> %d (visible=%d, with_png=%d)\n",
                    old_value, visible_png_found, visible_count, visible_with_png);
        }
    }
}

/**
 * Stage 4: Evict cache (two-tier LRU)
 */
static void evict_cache(TileContext& ctx)
{
    if (!ctx.tiles) return;

    // Tier 1: Limit lv_obj count
    size_t obj_count = 0;
    for (const auto& tile : *ctx.tiles)
    {
        if (tile.img_obj != NULL) obj_count++;
    }

    if (obj_count > TILE_CACHE_LIMIT)
    {
        // Collect invisible tiles with objects, sorted by last_used_ms (oldest first)
        // Priority: different zoom level tiles are already deleted in mark_all_invisible,
        // so we only need to handle same-zoom invisible tiles here
        std::vector<std::pair<uint32_t, size_t>> obj_candidates;
        int current_zoom = ctx.anchor && ctx.anchor->valid ? ctx.anchor->z : -1;
        for (size_t i = 0; i < ctx.tiles->size(); i++)
        {
            // Only consider invisible tiles from current zoom level
            // (different zoom tiles are already deleted in mark_all_invisible)
            if (!(*ctx.tiles)[i].visible &&
                (*ctx.tiles)[i].img_obj != NULL &&
                (*ctx.tiles)[i].z == current_zoom)
            {
                obj_candidates.push_back({(*ctx.tiles)[i].last_used_ms, i});
            }
        }

        // Sort by last_used_ms (oldest first)
        std::sort(obj_candidates.begin(), obj_candidates.end(),
                  [](const std::pair<uint32_t, size_t>& a, const std::pair<uint32_t, size_t>& b)
                  {
                      return a.first < b.first;
                  });

        // Delete oldest invisible tiles until under limit
        size_t to_delete = obj_count - TILE_CACHE_LIMIT;
        for (size_t i = 0; i < to_delete && i < obj_candidates.size(); i++)
        {
            size_t idx = obj_candidates[i].second;
            if ((*ctx.tiles)[idx].img_obj != NULL)
            {
                // Mark cache entry as not in use
                if ((*ctx.tiles)[idx].cached_img != NULL)
                {
                    (*ctx.tiles)[idx].cached_img->in_use = false;
                }
                lv_obj_del((*ctx.tiles)[idx].img_obj);
                (*ctx.tiles)[idx].img_obj = NULL;
                (*ctx.tiles)[idx].cached_img = NULL; // Clear cache reference
                (*ctx.tiles)[idx].has_png_file = false;
                (*ctx.tiles)[idx].obj_evicted_ms = millis();
            }
        }
    }

    // Tier 2: Limit tile record count
    if (ctx.tiles->size() > TILE_RECORD_LIMIT)
    {
        // Collect candidates for record eviction
        std::vector<std::pair<uint32_t, size_t>> record_candidates_never;
        std::vector<std::pair<uint32_t, size_t>> record_candidates_ever;

        uint32_t now = millis();
        for (size_t i = 0; i < ctx.tiles->size(); i++)
        {
            if ((*ctx.tiles)[i].img_obj == NULL && !(*ctx.tiles)[i].record_evicted)
            {
                // Protect recently obj_evicted tiles (within 3 seconds)
                if ((*ctx.tiles)[i].obj_evicted_ms > 0 && (now - (*ctx.tiles)[i].obj_evicted_ms) < 3000)
                {
                    continue;
                }

                if (!(*ctx.tiles)[i].ever_visible)
                {
                    record_candidates_never.push_back({(*ctx.tiles)[i].last_used_ms, i});
                }
                else
                {
                    record_candidates_ever.push_back({(*ctx.tiles)[i].last_used_ms, i});
                }
            }
        }

        // Sort by last_used_ms (oldest first)
        std::sort(record_candidates_never.begin(), record_candidates_never.end(),
                  [](const std::pair<uint32_t, size_t>& a, const std::pair<uint32_t, size_t>& b)
                  {
                      return a.first < b.first;
                  });
        std::sort(record_candidates_ever.begin(), record_candidates_ever.end(),
                  [](const std::pair<uint32_t, size_t>& a, const std::pair<uint32_t, size_t>& b)
                  {
                      return a.first < b.first;
                  });

        // Mark records for deletion: first never-visible, then ever-visible
        size_t to_delete = ctx.tiles->size() - TILE_RECORD_LIMIT;
        size_t deleted = 0;

        for (const auto& candidate : record_candidates_never)
        {
            if (deleted >= to_delete) break;
            (*ctx.tiles)[candidate.second].record_evicted = true;
            deleted++;
        }

        for (const auto& candidate : record_candidates_ever)
        {
            if (deleted >= to_delete) break;
            (*ctx.tiles)[candidate.second].record_evicted = true;
            deleted++;
        }

        // Remove evicted records
        ctx.tiles->erase(std::remove_if(ctx.tiles->begin(), ctx.tiles->end(),
                                        [](const MapTile& t)
                                        { return t.record_evicted; }),
                         ctx.tiles->end());
    }
}

/**
 * Calculate which tiles are needed to fill the screen
 * 4-stage pipeline
 */
void calculate_required_tiles(TileContext& ctx, double lat, double lng, int zoom, int pan_x, int pan_y, bool has_fix)
{
    if (!ctx.map_container || !ctx.tiles || !ctx.anchor)
    {
        GPS_LOG("[GPS] calculate_required_tiles: Invalid context\n");
        return;
    }

    GPS_LOG("[GPS] calculate_required_tiles: has_fix=%d, zoom=%d, lat=%.6f, lng=%.6f\n",
            has_fix, zoom, lat, lng);

    // Mark all tiles invisible and hide objects from different zoom levels
    mark_all_invisible(ctx, zoom);

    update_map_anchor(ctx, lat, lng, zoom, pan_x, pan_y, has_fix);

    collect_required_tiles(ctx, lat, lng, zoom, pan_x, pan_y, has_fix);

    layout_loaded_tile_objects(ctx);

    evict_cache(ctx);

    // Update GPS marker position after tiles are laid out (rendered after map)
    // This ensures marker is on top and moves with the map
    // Note: update_gps_marker_position() is defined in gps_page_map.cpp
    // It will be called from there after map updates to avoid circular dependencies

    // Count tiles to load for logging
    // Count visible tiles that don't have PNG loaded yet (may have placeholder)
    int tiles_to_load = 0;
    if (ctx.tiles)
    {
        for (auto& tile : *ctx.tiles)
        {
            if (tile.visible && !tile.has_png_file)
            {
                tiles_to_load++;
            }
        }
    }

    GPS_LOG("[GPS] Finished calculating tiles: to_load=%d, total=%d\n",
            tiles_to_load, ctx.tiles ? ctx.tiles->size() : 0);
}

/**
 * Load one tile (called by timer, not in calculate_required_tiles)
 */
void tile_loader_step(TileContext& ctx)
{
    if (!ctx.map_container || !ctx.tiles)
    {
        return;
    }
    if (g_cache_full_until_ms != 0)
    {
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - g_cache_full_until_ms) < 0)
        {
            return;
        }
    }

    // Find visible unloaded tiles with minimum priority
    // Look for tiles that are visible but don't have PNG loaded yet
    // (they may have a placeholder label, but not the actual image)
    MapTile* best = nullptr;
    for (auto& tile : *ctx.tiles)
    {
        if (tile.visible && !tile.has_png_file)
        {
            // If tile already has a placeholder, quickly check if file exists
            // to avoid repeatedly trying to load missing files
            if (tile.img_obj != NULL)
            {
                // Has placeholder - check if file exists before selecting
                char path[64];
                snprintf(path, sizeof(path), "A:/maps/%d/%d/%d.png", tile.z, tile.x, tile.y);
                lv_fs_file_t f;
                lv_fs_res_t res = lv_fs_open(&f, path, LV_FS_MODE_RD);
                bool file_exists = (res == LV_FS_RES_OK);
                if (file_exists)
                {
                    lv_fs_close(&f);
                    // File exists - this tile can be upgraded from placeholder to image
                }
                else
                {
                    // File doesn't exist - skip this tile to avoid repeated attempts
                    continue;
                }
            }

            if (best == nullptr ||
                tile.priority < best->priority ||
                (tile.priority == best->priority && tile.last_used_ms < best->last_used_ms))
            {
                best = &tile;
            }
        }
    }

    if (best != nullptr)
    {
        // Save old object position for invalidation
        lv_obj_t* old_obj = best->img_obj;
        int old_screen_x = 0, old_screen_y = 0;
        if (old_obj != NULL)
        {
            old_screen_x = lv_obj_get_x(old_obj);
            old_screen_y = lv_obj_get_y(old_obj);
        }

        load_tile_image(ctx, *best);

        // Invalidate only the tile area, not the entire container
        if (best->img_obj != NULL)
        {
            int new_screen_x = lv_obj_get_x(best->img_obj);
            int new_screen_y = lv_obj_get_y(best->img_obj);

            // Invalidate old position (if placeholder was at different location)
            if (old_obj != NULL && (old_screen_x != new_screen_x || old_screen_y != new_screen_y))
            {
                lv_area_t old_area;
                old_area.x1 = old_screen_x;
                old_area.y1 = old_screen_y;
                old_area.x2 = old_screen_x + TILE_SIZE - 1;
                old_area.y2 = old_screen_y + TILE_SIZE - 1;
                lv_obj_invalidate_area(ctx.map_container, &old_area);
            }

            // Invalidate new position (just the tile, not entire container)
            lv_obj_invalidate(best->img_obj);
        }

        // After loading a tile, update has_visible_map_data flag
        // This ensures the flag is updated immediately when tiles are loaded
        if (ctx.has_visible_map_data)
        {
            bool visible_png_found = false;
            for (auto& tile : *ctx.tiles)
            {
                if (tile.visible && tile.has_png_file)
                {
                    visible_png_found = true;
                    break;
                }
            }
            bool old_value = *ctx.has_visible_map_data;
            *ctx.has_visible_map_data = visible_png_found;
            if (old_value != visible_png_found)
            {
                GPS_LOG("[GPS] tile_loader_step: has_visible_map_data changed: %d -> %d\n",
                        old_value, visible_png_found);
            }
        }
    }
}

/**
 * Initialize tile context
 */
void init_tile_context(TileContext& ctx, lv_obj_t* map_container, MapAnchor* anchor,
                       std::vector<MapTile>* tiles, bool* has_map_data, bool* has_visible_map_data)
{
    ctx.map_container = map_container;
    ctx.anchor = anchor;
    ctx.tiles = tiles;
    ctx.has_map_data = has_map_data;
    ctx.has_visible_map_data = has_visible_map_data;
}

/**
 * Cleanup tiles
 */
void cleanup_tiles(TileContext& ctx)
{
    if (!ctx.tiles) return;

    for (auto& tile : *ctx.tiles)
    {
        if (tile.img_obj != NULL)
        {
            lv_obj_del(tile.img_obj);
            tile.img_obj = NULL;
        }
    }
    ctx.tiles->clear();
    ctx.tiles->shrink_to_fit();
}
