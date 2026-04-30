/**
 * @file map_viewport.cpp
 * @brief Shared map viewport facade backed by the platform tile runtime.
 */

#include "ui/widgets/map/map_viewport.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "platform/ui/device_runtime.h"
#include "ui/localization.h"
#include "ui/widgets/map/map_tiles.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ui::widgets::map
{

struct RuntimeImpl
{
    Widgets widgets{};
    Model model{};
    MapAnchor anchor{};
    std::vector<MapTile> tiles{};
    TileContext tile_ctx{};
    lv_timer_t* loader_timer = nullptr;
    uint32_t loader_interval_ms = 200;
    bool alive = false;
    bool has_map_data = false;
    bool has_visible_map_data = false;
    GestureCallback gesture_callback = nullptr;
    void* gesture_user_data = nullptr;
    bool gesture_enabled = false;
    bool gesture_pressed = false;
    bool gesture_dragging = false;
    lv_point_t gesture_start{};
    lv_point_t gesture_last{};
};

namespace
{

#define MAP_VIEWPORT_LOG(...) std::printf("[MapViewport] " __VA_ARGS__)

constexpr double kCoordPi = 3.14159265358979323846;
constexpr double kCoordA = 6378245.0;
constexpr double kCoordEe = 0.00669342162296594323;
constexpr int kGestureDragStartPx = 6;

bool coord_out_of_china(double lat, double lon)
{
    return (lon < 72.004 || lon > 137.8347 || lat < 0.8293 || lat > 55.8271);
}

double coord_transform_lat(double x, double y)
{
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y +
                 0.2 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * kCoordPi) + 20.0 * std::sin(2.0 * x * kCoordPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(y * kCoordPi) + 40.0 * std::sin(y / 3.0 * kCoordPi)) * 2.0 / 3.0;
    ret += (160.0 * std::sin(y / 12.0 * kCoordPi) + 320 * std::sin(y * kCoordPi / 30.0)) * 2.0 / 3.0;
    return ret;
}

double coord_transform_lon(double x, double y)
{
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y +
                 0.1 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * kCoordPi) + 20.0 * std::sin(2.0 * x * kCoordPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(x * kCoordPi) + 40.0 * std::sin(x / 3.0 * kCoordPi)) * 2.0 / 3.0;
    ret += (150.0 * std::sin(x / 12.0 * kCoordPi) + 300.0 * std::sin(x / 30.0 * kCoordPi)) * 2.0 / 3.0;
    return ret;
}

void wgs84_to_gcj02(double lat, double lon, double& out_lat, double& out_lon)
{
    if (coord_out_of_china(lat, lon))
    {
        out_lat = lat;
        out_lon = lon;
        return;
    }

    double dlat = coord_transform_lat(lon - 105.0, lat - 35.0);
    double dlon = coord_transform_lon(lon - 105.0, lat - 35.0);
    double radlat = lat / 180.0 * kCoordPi;
    double magic = std::sin(radlat);
    magic = 1 - kCoordEe * magic * magic;
    double sqrt_magic = std::sqrt(magic);
    dlat = (dlat * 180.0) / ((kCoordA * (1 - kCoordEe)) / (magic * sqrt_magic) * kCoordPi);
    dlon = (dlon * 180.0) / (kCoordA / sqrt_magic * std::cos(radlat) * kCoordPi);
    out_lat = lat + dlat;
    out_lon = lon + dlon;
}

void gcj02_to_bd09(double lat, double lon, double& out_lat, double& out_lon)
{
    double z = std::sqrt(lon * lon + lat * lat) + 0.00002 * std::sin(lat * kCoordPi);
    double theta = std::atan2(lat, lon) + 0.000003 * std::cos(lon * kCoordPi);
    out_lon = z * std::cos(theta) + 0.0065;
    out_lat = z * std::sin(theta) + 0.006;
}

void make_plain(lv_obj_t* obj)
{
    if (!obj)
    {
        return;
    }

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
}

bool is_runtime_alive(const RuntimeImpl& impl)
{
    return impl.alive &&
           impl.widgets.root &&
           lv_obj_is_valid(impl.widgets.root) &&
           impl.widgets.tile_layer &&
           lv_obj_is_valid(impl.widgets.tile_layer);
}

void reset_gesture_state(RuntimeImpl& impl)
{
    impl.gesture_pressed = false;
    impl.gesture_dragging = false;
    impl.gesture_start = lv_point_t{};
    impl.gesture_last = lv_point_t{};
}

void emit_gesture_event(RuntimeImpl& impl, GesturePhase phase, const lv_point_t& point)
{
    if (!impl.gesture_callback)
    {
        return;
    }

    GestureEvent event{};
    event.phase = phase;
    event.point = point;
    event.total_dx = static_cast<int>(point.x - impl.gesture_start.x);
    event.total_dy = static_cast<int>(point.y - impl.gesture_start.y);
    event.dragging = impl.gesture_dragging;
    impl.gesture_callback(event, impl.gesture_user_data);
}

void update_gesture_surface_visibility(RuntimeImpl& impl)
{
    if (!impl.widgets.gesture_surface || !lv_obj_is_valid(impl.widgets.gesture_surface))
    {
        return;
    }

    const bool visible = impl.gesture_enabled && impl.gesture_callback != nullptr;
    if (visible)
    {
        lv_obj_clear_flag(impl.widgets.gesture_surface, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(impl.widgets.gesture_surface, LV_OBJ_FLAG_HIDDEN);
        reset_gesture_state(impl);
    }

    MAP_VIEWPORT_LOG("gesture_surface enabled=%d callback=%d visible=%d obj=%p\n",
                     impl.gesture_enabled ? 1 : 0,
                     impl.gesture_callback ? 1 : 0,
                     visible ? 1 : 0,
                     impl.widgets.gesture_surface);
}

lv_indev_t* resolve_event_indev(lv_event_t* e)
{
    if (e)
    {
        if (lv_indev_t* indev = lv_event_get_indev(e))
        {
            return indev;
        }
    }
    return lv_indev_active();
}

bool resolve_event_point(lv_event_t* e, lv_point_t& out_point)
{
    lv_indev_t* indev = resolve_event_indev(e);
    if (!indev)
    {
        out_point = {};
        return false;
    }

    lv_indev_get_point(indev, &out_point);
    return true;
}

void gesture_surface_event_cb(lv_event_t* e)
{
    auto* impl = static_cast<RuntimeImpl*>(lv_event_get_user_data(e));
    if (!impl || !impl->gesture_enabled || !impl->gesture_callback || !is_runtime_alive(*impl))
    {
        return;
    }

    lv_point_t point{};
    const bool has_point = resolve_event_point(e, point);
    const lv_event_code_t code = lv_event_get_code(e);

    switch (code)
    {
    case LV_EVENT_PRESSED:
        if (!has_point)
        {
            return;
        }
        impl->gesture_pressed = true;
        impl->gesture_dragging = false;
        impl->gesture_start = point;
        impl->gesture_last = point;
        emit_gesture_event(*impl, GesturePhase::Pressed, point);
        break;

    case LV_EVENT_PRESSING:
        if (!impl->gesture_pressed || !has_point)
        {
            return;
        }

        impl->gesture_last = point;
        if (!impl->gesture_dragging)
        {
            const int dx = static_cast<int>(point.x - impl->gesture_start.x);
            const int dy = static_cast<int>(point.y - impl->gesture_start.y);
            if (std::abs(dx) < kGestureDragStartPx && std::abs(dy) < kGestureDragStartPx)
            {
                return;
            }

            impl->gesture_dragging = true;
            MAP_VIEWPORT_LOG("drag_begin root=%p dx=%d dy=%d\n",
                             impl->widgets.root,
                             dx,
                             dy);
            emit_gesture_event(*impl, GesturePhase::DragBegin, point);
        }

        if (impl->gesture_dragging)
        {
            if (lv_indev_t* indev = resolve_event_indev(e))
            {
                lv_indev_stop_processing(indev);
            }
            MAP_VIEWPORT_LOG("drag_update root=%p dx=%d dy=%d\n",
                             impl->widgets.root,
                             static_cast<int>(point.x - impl->gesture_start.x),
                             static_cast<int>(point.y - impl->gesture_start.y));
            emit_gesture_event(*impl, GesturePhase::DragUpdate, point);
        }
        break;

    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        if (!impl->gesture_pressed)
        {
            return;
        }

        if (!has_point)
        {
            point = impl->gesture_last;
        }

        if (impl->gesture_dragging)
        {
            if (lv_indev_t* indev = resolve_event_indev(e))
            {
                lv_indev_stop_processing(indev);
            }
            MAP_VIEWPORT_LOG("drag_end root=%p dx=%d dy=%d code=%d\n",
                             impl->widgets.root,
                             static_cast<int>(point.x - impl->gesture_start.x),
                             static_cast<int>(point.y - impl->gesture_start.y),
                             static_cast<int>(code));
            emit_gesture_event(*impl,
                               code == LV_EVENT_PRESS_LOST ? GesturePhase::Cancel : GesturePhase::DragEnd,
                               point);
        }

        reset_gesture_state(*impl);
        break;

    default:
        break;
    }
}

void prime_visible_tiles(RuntimeImpl& impl, const char* reason)
{
    if (!is_runtime_alive(impl) || !impl.model.focus_point.valid)
    {
        MAP_VIEWPORT_LOG("prime_visible_tiles skipped reason=%s alive=%d focus=%d\n",
                         reason ? reason : "<none>",
                         impl.alive ? 1 : 0,
                         impl.model.focus_point.valid ? 1 : 0);
        return;
    }

    MAP_VIEWPORT_LOG("prime_visible_tiles begin reason=%s map_data=%d visible_map=%d\n",
                     reason ? reason : "<none>",
                     impl.has_map_data ? 1 : 0,
                     impl.has_visible_map_data ? 1 : 0);

    for (int attempt = 0; attempt < 2 && !impl.has_visible_map_data; ++attempt)
    {
        tile_loader_step(impl.tile_ctx);
        MAP_VIEWPORT_LOG("prime_visible_tiles step=%d visible_map=%d map_data=%d\n",
                         attempt + 1,
                         impl.has_visible_map_data ? 1 : 0,
                         impl.has_map_data ? 1 : 0);
    }

    MAP_VIEWPORT_LOG("prime_visible_tiles end reason=%s map_data=%d visible_map=%d\n",
                     reason ? reason : "<none>",
                     impl.has_map_data ? 1 : 0,
                     impl.has_visible_map_data ? 1 : 0);
}

GeoPoint transformed_focus(const Model& model)
{
    GeoPoint out{};
    transform_geo_point(model.focus_point, model.coord_system, out);
    return out;
}

void refresh_tiles(RuntimeImpl& impl, const char* reason)
{
    if (!is_runtime_alive(impl))
    {
        MAP_VIEWPORT_LOG("refresh_tiles skipped reason=%s alive=%d root=%p tile=%p\n",
                         reason ? reason : "<none>",
                         impl.alive ? 1 : 0,
                         impl.widgets.root,
                         impl.widgets.tile_layer);
        return;
    }

    if (!impl.model.focus_point.valid)
    {
        MAP_VIEWPORT_LOG("refresh_tiles skipped reason=%s focus=invalid\n",
                         reason ? reason : "<none>");
        cleanup_tiles(impl.tile_ctx);
        impl.anchor.valid = false;
        return;
    }

    GeoPoint focus = transformed_focus(impl.model);
    if (!focus.valid)
    {
        MAP_VIEWPORT_LOG("refresh_tiles skipped reason=%s transformed_focus=invalid\n",
                         reason ? reason : "<none>");
        cleanup_tiles(impl.tile_ctx);
        impl.anchor.valid = false;
        return;
    }

    lv_obj_update_layout(impl.widgets.root);
    set_map_render_options(impl.model.map_source, impl.model.contour_enabled);
    calculate_required_tiles(impl.tile_ctx,
                             focus.lat,
                             focus.lon,
                             impl.model.zoom,
                             impl.model.pan_x,
                             impl.model.pan_y,
                             true);
    prime_visible_tiles(impl, reason);
    MAP_VIEWPORT_LOG("refresh_tiles reason=%s zoom=%d pan=%d,%d src=%u contour=%d anchor=%d size=%dx%d map_data=%d visible_map=%d\n",
                     reason ? reason : "<none>",
                     impl.model.zoom,
                     impl.model.pan_x,
                     impl.model.pan_y,
                     sanitize_map_source(impl.model.map_source),
                     impl.model.contour_enabled ? 1 : 0,
                     impl.anchor.valid ? 1 : 0,
                     static_cast<int>(lv_obj_get_width(impl.widgets.tile_layer)),
                     static_cast<int>(lv_obj_get_height(impl.widgets.tile_layer)),
                     impl.has_map_data ? 1 : 0,
                     impl.has_visible_map_data ? 1 : 0);
}

void loader_timer_cb(lv_timer_t* timer)
{
    auto* impl = static_cast<RuntimeImpl*>(lv_timer_get_user_data(timer));
    if (!impl || !is_runtime_alive(*impl) || !impl->model.focus_point.valid)
    {
        return;
    }

    tile_loader_step(impl->tile_ctx);

    uint8_t missing_source = 0;
    if (::take_missing_tile_notice(&missing_source))
    {
        MAP_VIEWPORT_LOG("missing_tiles source=%u label=%s\n",
                         static_cast<unsigned>(missing_source),
                         map_source_label(missing_source));
    }
}

const Widgets& empty_widgets()
{
    static Widgets kEmpty{};
    return kEmpty;
}

void clear_layer_notice(LayerNotice* notice)
{
    if (!notice)
    {
        return;
    }

    notice->has_message = false;
    notice->message[0] = '\0';
    notice->duration_ms = 0;
}

void set_layer_notice(LayerNotice* notice, const char* message, uint32_t duration_ms)
{
    clear_layer_notice(notice);
    if (!notice || !message || message[0] == '\0')
    {
        return;
    }

    notice->has_message = true;
    std::snprintf(notice->message, sizeof(notice->message), "%s", message);
    notice->duration_ms = duration_ms;
}

} // namespace

Runtime::~Runtime()
{
    destroy(*this);
}

Runtime::Runtime(Runtime&& other) noexcept
    : impl_(other.impl_)
{
    other.impl_ = nullptr;
}

Runtime& Runtime::operator=(Runtime&& other) noexcept
{
    if (this != &other)
    {
        destroy(*this);
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

Widgets create(Runtime& runtime, lv_obj_t* parent, uint32_t loader_interval_ms)
{
    destroy(runtime);

    if (!runtime.impl_)
    {
        runtime.impl_ = new RuntimeImpl();
        runtime.impl_->tiles.reserve(TILE_RECORD_LIMIT);
    }
    RuntimeImpl* impl = runtime.impl_;
    impl->loader_interval_ms = loader_interval_ms;

    impl->widgets.root = lv_obj_create(parent);
    lv_obj_set_size(impl->widgets.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(impl->widgets.root, 0, 0);
    make_plain(impl->widgets.root);
#ifdef LV_OBJ_FLAG_CLIP_CHILDREN
    lv_obj_add_flag(impl->widgets.root, LV_OBJ_FLAG_CLIP_CHILDREN);
#endif

    impl->widgets.tile_layer = lv_obj_create(impl->widgets.root);
    lv_obj_set_size(impl->widgets.tile_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(impl->widgets.tile_layer, 0, 0);
    make_plain(impl->widgets.tile_layer);

    impl->widgets.overlay_layer = lv_obj_create(impl->widgets.root);
    lv_obj_set_size(impl->widgets.overlay_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(impl->widgets.overlay_layer, 0, 0);
    make_plain(impl->widgets.overlay_layer);

    impl->widgets.gesture_surface = lv_obj_create(impl->widgets.root);
    lv_obj_set_size(impl->widgets.gesture_surface, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(impl->widgets.gesture_surface, 0, 0);
    make_plain(impl->widgets.gesture_surface);
    lv_obj_add_flag(impl->widgets.gesture_surface, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(impl->widgets.gesture_surface, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(impl->widgets.gesture_surface, gesture_surface_event_cb, LV_EVENT_ALL, impl);

    init_tile_context(impl->tile_ctx,
                      impl->widgets.tile_layer,
                      &impl->anchor,
                      &impl->tiles,
                      &impl->has_map_data,
                      &impl->has_visible_map_data);

    impl->loader_timer = lv_timer_create(loader_timer_cb, loader_interval_ms, impl);
    impl->alive = true;

    MAP_VIEWPORT_LOG("create root=%p tile=%p overlay=%p loader_timer=%p interval=%u\n",
                     impl->widgets.root,
                     impl->widgets.tile_layer,
                     impl->widgets.overlay_layer,
                     impl->loader_timer,
                     static_cast<unsigned>(loader_interval_ms));
    return impl->widgets;
}

void destroy(Runtime& runtime)
{
    RuntimeImpl* impl = runtime.impl_;
    if (!impl)
    {
        return;
    }

    MAP_VIEWPORT_LOG("destroy begin root=%p timer=%p alive=%d\n",
                     impl->widgets.root,
                     impl->loader_timer,
                     impl->alive ? 1 : 0);
    impl->alive = false;

    if (impl->loader_timer)
    {
        lv_timer_del(impl->loader_timer);
        impl->loader_timer = nullptr;
    }

    cleanup_tiles(impl->tile_ctx);

    if (impl->widgets.root && lv_obj_is_valid(impl->widgets.root))
    {
        lv_obj_del(impl->widgets.root);
    }

    delete impl;
    runtime.impl_ = nullptr;
    MAP_VIEWPORT_LOG("destroy end\n");
}

const Widgets& widgets(const Runtime& runtime)
{
    const RuntimeImpl* impl = runtime.impl_;
    return impl ? impl->widgets : empty_widgets();
}

void set_size(Runtime& runtime, lv_coord_t width, lv_coord_t height)
{
    if (!runtime.impl_)
    {
        runtime.impl_ = new RuntimeImpl();
        runtime.impl_->tiles.reserve(TILE_RECORD_LIMIT);
    }
    RuntimeImpl* impl = runtime.impl_;
    if (!impl->widgets.root)
    {
        return;
    }

    lv_obj_set_size(impl->widgets.root, width, height);
    lv_obj_set_size(impl->widgets.tile_layer, width, height);
    lv_obj_set_size(impl->widgets.overlay_layer, width, height);
    lv_obj_set_size(impl->widgets.gesture_surface, width, height);
    MAP_VIEWPORT_LOG("set_size root=%p size=%dx%d\n",
                     impl->widgets.root,
                     static_cast<int>(width),
                     static_cast<int>(height));
}

void apply_model(Runtime& runtime, const Model& model)
{
    if (!runtime.impl_)
    {
        runtime.impl_ = new RuntimeImpl();
        runtime.impl_->tiles.reserve(TILE_RECORD_LIMIT);
    }
    RuntimeImpl* impl = runtime.impl_;
    impl->model = model;
    impl->model.map_source = sanitize_map_source(impl->model.map_source);
    MAP_VIEWPORT_LOG("apply_model focus_valid=%d lat=%.7f lon=%.7f zoom=%d pan=%d,%d src=%u contour=%d coord=%u\n",
                     impl->model.focus_point.valid ? 1 : 0,
                     impl->model.focus_point.lat,
                     impl->model.focus_point.lon,
                     impl->model.zoom,
                     impl->model.pan_x,
                     impl->model.pan_y,
                     static_cast<unsigned>(impl->model.map_source),
                     impl->model.contour_enabled ? 1 : 0,
                     static_cast<unsigned>(impl->model.coord_system));
    refresh_tiles(*impl, "apply_model");
}

void clear(Runtime& runtime)
{
    RuntimeImpl* impl = runtime.impl_;
    if (!impl)
    {
        return;
    }

    impl->model.focus_point = GeoPoint{};
    cleanup_tiles(impl->tile_ctx);
    impl->anchor.valid = false;
    reset_gesture_state(*impl);
    MAP_VIEWPORT_LOG("clear root=%p\n", impl->widgets.root);
}

bool project_point(const Runtime& runtime, const GeoPoint& point, lv_point_t& out_screen_point)
{
    const RuntimeImpl* impl = runtime.impl_;
    if (!impl || !is_runtime_alive(*impl) || !impl->anchor.valid || !point.valid)
    {
        return false;
    }

    GeoPoint transformed{};
    if (!transform_geo_point(point, impl->model.coord_system, transformed) || !transformed.valid)
    {
        return false;
    }

    int screen_x = 0;
    int screen_y = 0;
    if (!gps_screen_pos(impl->tile_ctx, transformed.lat, transformed.lon, screen_x, screen_y))
    {
        return false;
    }

    out_screen_point.x = static_cast<lv_coord_t>(screen_x);
    out_screen_point.y = static_cast<lv_coord_t>(screen_y);
    return true;
}

bool preview_project_point(lv_obj_t* viewport_root, const Model& model, const GeoPoint& point, lv_point_t& out_screen_point)
{
    if (!viewport_root || !lv_obj_is_valid(viewport_root) || !model.focus_point.valid || !point.valid)
    {
        return false;
    }

    GeoPoint transformed_focus{};
    GeoPoint transformed_point{};
    if (!transform_geo_point(model.focus_point, model.coord_system, transformed_focus) || !transformed_focus.valid)
    {
        return false;
    }
    if (!transform_geo_point(point, model.coord_system, transformed_point) || !transformed_point.valid)
    {
        return false;
    }

    MapAnchor anchor{};
    TileContext ctx{};
    ctx.map_container = viewport_root;
    ctx.anchor = &anchor;
    update_map_anchor(ctx,
                      transformed_focus.lat,
                      transformed_focus.lon,
                      model.zoom,
                      model.pan_x,
                      model.pan_y,
                      true);
    if (!anchor.valid)
    {
        return false;
    }

    int screen_x = 0;
    int screen_y = 0;
    if (!gps_screen_pos(ctx, transformed_point.lat, transformed_point.lon, screen_x, screen_y))
    {
        return false;
    }

    out_screen_point.x = static_cast<lv_coord_t>(screen_x);
    out_screen_point.y = static_cast<lv_coord_t>(screen_y);
    return true;
}

bool focus_tile_available(const Model& model)
{
    if (!model.focus_point.valid)
    {
        return false;
    }

    GeoPoint transformed{};
    if (!transform_geo_point(model.focus_point, model.coord_system, transformed) || !transformed.valid)
    {
        return false;
    }

    int tile_x = 0;
    int tile_y = 0;
    latLngToTile(transformed.lat, transformed.lon, model.zoom, tile_x, tile_y);
    normalize_tile(model.zoom, tile_x, tile_y);

    return base_tile_available(model.zoom,
                               tile_x,
                               tile_y,
                               sanitize_map_source(model.map_source));
}

Status status(const Runtime& runtime)
{
    Status out{};
    const RuntimeImpl* impl = runtime.impl_;
    if (!impl)
    {
        return out;
    }

    out.alive = impl->alive;
    out.has_focus = impl->model.focus_point.valid;
    out.anchor_valid = impl->anchor.valid;
    out.has_map_data = impl->has_map_data;
    out.has_visible_map_data = impl->has_visible_map_data;
    out.zoom = impl->model.zoom;
    out.pan_x = impl->model.pan_x;
    out.pan_y = impl->model.pan_y;
    return out;
}

bool take_missing_tile_notice(Runtime& runtime, uint8_t* out_map_source)
{
    (void)runtime;
    return ::take_missing_tile_notice(out_map_source);
}

void set_gesture_enabled(Runtime& runtime, bool enabled)
{
    RuntimeImpl* impl = runtime.impl_;
    if (!impl)
    {
        return;
    }

    impl->gesture_enabled = enabled;
    update_gesture_surface_visibility(*impl);
}

void set_gesture_callback(Runtime& runtime, GestureCallback callback, void* user_data)
{
    RuntimeImpl* impl = runtime.impl_;
    if (!impl)
    {
        return;
    }

    impl->gesture_callback = callback;
    impl->gesture_user_data = user_data;
    update_gesture_surface_visibility(*impl);
}

bool transform_geo_point(const GeoPoint& point, uint8_t coord_system, GeoPoint& out_point)
{
    out_point = {};
    if (!point.valid)
    {
        return false;
    }

    out_point.valid = true;
    if (coord_system == 1)
    {
        wgs84_to_gcj02(point.lat, point.lon, out_point.lat, out_point.lon);
        return true;
    }
    if (coord_system == 2)
    {
        double gcj_lat = 0.0;
        double gcj_lon = 0.0;
        wgs84_to_gcj02(point.lat, point.lon, gcj_lat, gcj_lon);
        gcj02_to_bd09(gcj_lat, gcj_lon, out_point.lat, out_point.lon);
        return true;
    }

    out_point.lat = point.lat;
    out_point.lon = point.lon;
    return true;
}

LayerState current_layer_state()
{
    const auto& cfg = app::configFacade().getConfig();
    LayerState state{};
    state.map_source = sanitize_map_source(cfg.map_source);
    state.contour_enabled = cfg.map_contour_enabled;
    return state;
}

const char* layer_map_source_label_key(uint8_t map_source)
{
    return map_source_label(sanitize_map_source(map_source));
}

const char* layer_contour_status_key(bool contour_enabled)
{
    return contour_enabled ? "Contour: ON" : "Contour: OFF";
}

std::string layer_base_summary_text(uint8_t map_source)
{
    return ::ui::i18n::format("Base: %s",
                              ::ui::i18n::tr(layer_map_source_label_key(map_source)));
}

bool set_layer_map_source(uint8_t map_source, LayerNotice* out_notice)
{
    clear_layer_notice(out_notice);

    app::IAppConfigFacade& config_api = app::configFacade();
    const uint8_t previous = sanitize_map_source(config_api.getConfig().map_source);
    const uint8_t normalized = sanitize_map_source(map_source);
    const bool changed = previous != normalized;

    MAP_VIEWPORT_LOG("set_layer_map_source from=%u to=%u contour=%d changed=%d\n",
                     static_cast<unsigned>(previous),
                     static_cast<unsigned>(normalized),
                     config_api.getConfig().map_contour_enabled ? 1 : 0,
                     changed ? 1 : 0);

    if (changed)
    {
        config_api.getConfig().map_source = normalized;
        config_api.saveConfig();
    }

    if (!platform::ui::device::sd_ready())
    {
        set_layer_notice(out_notice, ::ui::i18n::tr("No SD Card"), 1200);
    }
    else if (!map_source_directory_available(normalized))
    {
        const std::string message = ::ui::i18n::format(
            "%s layer missing",
            ::ui::i18n::tr(layer_map_source_label_key(normalized)));
        set_layer_notice(out_notice, message.c_str(), 1600);
    }

    return changed;
}

bool toggle_layer_contour(LayerNotice* out_notice)
{
    clear_layer_notice(out_notice);

    app::IAppConfigFacade& config_api = app::configFacade();
    const bool previous = config_api.getConfig().map_contour_enabled;
    const bool enabled = !previous;

    MAP_VIEWPORT_LOG("toggle_layer_contour from=%d to=%d src=%u\n",
                     previous ? 1 : 0,
                     enabled ? 1 : 0,
                     static_cast<unsigned>(sanitize_map_source(config_api.getConfig().map_source)));

    config_api.getConfig().map_contour_enabled = enabled;
    config_api.saveConfig();

    if (enabled)
    {
        if (!platform::ui::device::sd_ready())
        {
            set_layer_notice(out_notice, ::ui::i18n::tr("No SD Card"), 1200);
        }
        else if (!contour_directory_available())
        {
            set_layer_notice(out_notice, ::ui::i18n::tr("Contour data missing"), 1600);
        }
    }

    return true;
}

} // namespace ui::widgets::map
