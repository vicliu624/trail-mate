/**
 * @file map_viewport.h
 * @brief Shared map viewport facade for map-based pages.
 */

#pragma once

#include "lvgl.h"
#include "ui_presentation/map/map_overlay_snapshot.h"

#include <cstdint>
#include <string>

namespace ui::widgets::map
{

constexpr int kDefaultZoom = 12;
constexpr int kMinZoom = 0;
constexpr int kMaxZoom = 18;

struct RuntimeImpl;

struct GeoPoint
{
    constexpr GeoPoint() = default;
    constexpr GeoPoint(bool valid_value, double lat_value, double lon_value)
        : valid(valid_value), lat(lat_value), lon(lon_value)
    {
    }

    bool valid = false;
    double lat = 0.0;
    double lon = 0.0;
};

struct Model
{
    GeoPoint focus_point{};
    int zoom = kDefaultZoom;
    int pan_x = 0;
    int pan_y = 0;
    uint8_t map_source = 0;
    bool contour_enabled = false;
    uint8_t coord_system = 0;
};

struct Widgets
{
    lv_obj_t* root = nullptr;
    lv_obj_t* tile_layer = nullptr;
    lv_obj_t* overlay_layer = nullptr;
    lv_obj_t* gesture_surface = nullptr;
};

struct Status
{
    bool alive = false;
    bool has_focus = false;
    bool anchor_valid = false;
    bool has_map_data = false;
    bool has_visible_map_data = false;
    int zoom = 0;
    int pan_x = 0;
    int pan_y = 0;
};

struct LayerState
{
    uint8_t map_source = 0;
    bool contour_enabled = false;
};

struct LayerNotice
{
    bool has_message = false;
    char message[64]{};
    uint32_t duration_ms = 0;
};

enum class GesturePhase : uint8_t
{
    Pressed = 0,
    DragBegin,
    DragUpdate,
    DragEnd,
    Cancel,
};

struct GestureEvent
{
    GesturePhase phase = GesturePhase::Pressed;
    lv_point_t point{};
    int total_dx = 0;
    int total_dy = 0;
    bool dragging = false;
};

using GestureCallback = void (*)(const GestureEvent& event, void* user_data);

class Runtime
{
  public:
    Runtime() = default;
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    Runtime(Runtime&& other) noexcept;
    Runtime& operator=(Runtime&& other) noexcept;

  private:
    RuntimeImpl* impl_ = nullptr;

    friend Widgets create(Runtime& runtime, lv_obj_t* parent, uint32_t loader_interval_ms);
    friend void destroy(Runtime& runtime);
    friend const Widgets& widgets(const Runtime& runtime);
    friend void set_size(Runtime& runtime, lv_coord_t width, lv_coord_t height);
    friend void apply_model(Runtime& runtime, const Model& model);
    friend void apply_overlay(Runtime& runtime, const ui::map::MapOverlaySnapshot& overlay);
    friend void clear(Runtime& runtime);
    friend bool project_point(const Runtime& runtime, const GeoPoint& point, lv_point_t& out_screen_point);
    friend Status status(const Runtime& runtime);
    friend bool take_missing_tile_notice(Runtime& runtime, uint8_t* out_map_source);
    friend void set_gesture_enabled(Runtime& runtime, bool enabled);
    friend void set_gesture_callback(Runtime& runtime, GestureCallback callback, void* user_data);
};

Widgets create(Runtime& runtime, lv_obj_t* parent, uint32_t loader_interval_ms = 200);
void destroy(Runtime& runtime);
const Widgets& widgets(const Runtime& runtime);
void set_size(Runtime& runtime, lv_coord_t width, lv_coord_t height);
void apply_model(Runtime& runtime, const Model& model);
void apply_overlay(Runtime& runtime, const ui::map::MapOverlaySnapshot& overlay);
void clear(Runtime& runtime);
bool project_point(const Runtime& runtime, const GeoPoint& point, lv_point_t& out_screen_point);
bool preview_project_point(lv_obj_t* viewport_root, const Model& model, const GeoPoint& point, lv_point_t& out_screen_point);
bool focus_tile_available(const Model& model);
Status status(const Runtime& runtime);
bool take_missing_tile_notice(Runtime& runtime, uint8_t* out_map_source);
bool transform_geo_point(const GeoPoint& point, uint8_t coord_system, GeoPoint& out_point);
void set_gesture_enabled(Runtime& runtime, bool enabled);
void set_gesture_callback(Runtime& runtime, GestureCallback callback, void* user_data);
LayerState current_layer_state();
bool set_layer_map_source(uint8_t map_source, LayerNotice* out_notice = nullptr);
bool toggle_layer_contour(LayerNotice* out_notice = nullptr);
const char* layer_map_source_label_key(uint8_t map_source);
const char* layer_contour_status_key(bool contour_enabled);
std::string layer_base_summary_text(uint8_t map_source);

} // namespace ui::widgets::map
