#include "ui/support/route_preview_viewport.h"
#include "ui/support/tmroute_map.h"
#include "ui/widgets/map/map_viewport.h"
#include <cassert>
#include <cmath>
#include <string>
#include <vector>

namespace app
{
struct Config
{
    uint8_t map_coord_system = 0;
};
struct Facade
{
    Config readConfig() { return {}; }
};
Facade& configFacade()
{
    static Facade facade;
    return facade;
}
} // namespace app
namespace ui::widgets::map
{
Widgets test_widgets;
Model test_model;
GestureCallback test_gesture = nullptr;
bool test_enabled = false;
unsigned full_updates = 0, light_updates = 0;
Runtime::~Runtime() {}
const Widgets& widgets(const Runtime&) { return test_widgets; }
Widgets create(Runtime&, lv_obj_t* parent, uint32_t)
{
    test_widgets.root = lv_obj_create(parent);
    test_widgets.overlay_layer = lv_obj_create(test_widgets.root);
    return test_widgets;
}
void set_size(Runtime&, lv_coord_t width, lv_coord_t height) { lv_obj_set_size(test_widgets.root, width, height); }
void set_gesture_callback(Runtime&, GestureCallback callback, void*) { test_gesture = callback; }
void set_gesture_enabled(Runtime&, bool enabled) { test_enabled = enabled; }
void apply_model(Runtime&, const Model& model)
{
    test_model = model;
    ++full_updates;
}
void apply_model_lightweight(Runtime&, const Model& model)
{
    test_model = model;
    ++light_updates;
}
LayerState current_layer_state() { return {}; }
bool preview_project_point(lv_obj_t* host, const Model& model, const GeoPoint& point, lv_point_t& output)
{
    tmap::ScreenPoint pixel;
    const bool okay = tmap::project({{int32_t(model.focus_point.lat * 1e7), int32_t(model.focus_point.lon * 1e7)},
                                     uint8_t(model.zoom),
                                     float(lv_obj_get_content_width(host)),
                                     float(lv_obj_get_content_height(host))},
                                    {int32_t(point.lat * 1e7), int32_t(point.lon * 1e7)}, pixel);
    output = {int32_t(std::lround(pixel.x)) + model.pan_x, int32_t(std::lround(pixel.y)) + model.pan_y};
    return okay;
}
bool project_point(const Runtime&, const GeoPoint& point, lv_point_t& output)
{
    return preview_project_point(lv_obj_get_parent(test_widgets.root), test_model, point, output);
}
namespace unused
{
}
} // namespace ui::widgets::map
namespace ui::widgets::route_image_strip
{
bool handle_key(int, uint32_t) { return false; }
bool is_visible(int) { return false; }
} // namespace ui::widgets::route_image_strip

struct
{
    lv_obj_t* route_preview_map_host = nullptr;
    lv_obj_t* route_preview_page = nullptr;
} g_tracker_state;
struct Point
{
    double lat, lon;
};
struct Image
{
    bool has_position;
    double lat, lon;
};
std::vector<Point> s_preview_route_points{{0, 0}, {0.002, 0.002}};
std::vector<Image> s_preview_images;
ui::support::RoutePreviewViewport s_preview_view;
ui::widgets::map::Runtime s_preview_map_runtime;
ui::support::TmRouteMap* s_preview_native_route = nullptr;
lv_timer_t* s_preview_native_timer = nullptr;
lv_obj_t* s_preview_map_controls = nullptr;
bool s_preview_map_loader_paused = false, s_preview_image_strip_visible = false;
int s_preview_image_strip = 0;
int s_preview_selected_image = 0;
std::string s_preview_status_text;
void refresh_route_preview_map();
void create_preview_map_controls();
void draw_preview_map_overlay() {}
void sync_preview_native_view() {}
void sync_route_preview_map_loader_pause() {}
void sync_route_preview_image_strip() {}
void sync_route_preview_download_status() {}
void update_route_preview_status() {}
void show_route_preview_notice(const char*, uint32_t) {}
void close_route_preview_page() {}
void on_route_preview_download_clicked(lv_event_t*) {}
void on_route_preview_load_clicked(lv_event_t*) {}
void cycle_route_preview_map_layer() {}
void toggle_route_preview_elevation() {}
void toggle_route_preview_image_strip() {}
void open_route_preview_help_modal() {}
void consume_route_preview_key_event(lv_event_t* event) { lv_event_stop_bubbling(event); }
struct
{
    bool dense = false;
    const lv_font_t* tiny_font = &lv_font_montserrat_14;
} test_profile;
const auto& page_profile() { return test_profile; }
lv_obj_t* create_preview_page_button(lv_obj_t* parent, const char* text, lv_event_cb_t, lv_coord_t width)
{
    auto* button = lv_button_create(parent);
    lv_obj_set_width(button, width);
    auto* label = lv_label_create(button);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}
// Real Tracker bounds/model builder, refresh/gesture handlers, controls and keys.
// Tile loading, app facades and image maintenance above are deterministic ports.
#include "preview_interaction_actual.inc"

int main()
{
    lv_init();
    auto* display = lv_display_create(320, 240);
    std::vector<uint16_t> pixels(320 * 240);
    lv_display_set_buffers(display, pixels.data(), nullptr, uint32_t(pixels.size() * 2), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* disp, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(disp); });
    auto* host = lv_obj_create(lv_screen_active());
    lv_obj_set_size(host, 320, 200);
    lv_obj_set_style_pad_all(host, 0, 0);
    lv_obj_set_style_border_width(host, 0, 0);
    g_tracker_state.route_preview_page = host;
    g_tracker_state.route_preview_map_host = host;
    refresh_route_preview_map();
    const auto fit_zoom = s_preview_view.zoom;
    assert(fit_zoom >= 0 && fit_zoom < 18 && ui::widgets::map::test_enabled && ui::widgets::map::test_gesture);
    assert(lv_obj_get_child_count(s_preview_map_controls) == 3);
    auto callback = ui::widgets::map::test_gesture;
    using Phase = ui::widgets::map::GesturePhase;
    callback({Phase::DragBegin}, nullptr);
    ui::widgets::map::GestureEvent drag;
    drag.phase = Phase::DragUpdate;
    drag.total_dx = 48;
    drag.total_dy = -24;
    callback(drag, nullptr);
    assert(s_preview_view.pan_x == 48 && s_preview_view.pan_y == -24 && ui::widgets::map::light_updates == 1);
    callback({Phase::DragEnd}, nullptr);
    assert(ui::widgets::map::test_model.pan_x == 48 && ui::widgets::map::test_model.pan_y == -24);
    lv_obj_send_event(lv_obj_get_child(s_preview_map_controls, 0), LV_EVENT_CLICKED, nullptr);
    assert(s_preview_view.zoom == fit_zoom + 1 && s_preview_view.pan_x == 96 && s_preview_view.pan_y == -48);
    refresh_route_preview_map(); // Image/layer refresh must not refit the route.
    assert(s_preview_view.zoom == fit_zoom + 1 && s_preview_view.pan_x == 96);
    lv_obj_send_event(lv_obj_get_child(s_preview_map_controls, 1), LV_EVENT_CLICKED, nullptr);
    assert(s_preview_view.zoom == fit_zoom && s_preview_view.pan_x == 48);
    lv_obj_send_event(lv_obj_get_child(s_preview_map_controls, 2), LV_EVENT_CLICKED, nullptr);
    assert(s_preview_view.zoom == fit_zoom && !s_preview_view.pan_x && !s_preview_view.pan_y);
    lv_obj_add_event_cb(host, on_route_preview_key, LV_EVENT_KEY, nullptr);
    uint32_t key = LV_KEY_RIGHT;
    lv_obj_send_event(host, LV_EVENT_KEY, &key);
    assert(s_preview_view.pan_x == -40);
    key = '+';
    lv_obj_send_event(host, LV_EVENT_KEY, &key);
    assert(s_preview_view.zoom == fit_zoom + 1 && s_preview_view.pan_x == -80);
    key = 'f';
    lv_obj_send_event(host, LV_EVENT_KEY, &key);
    assert(s_preview_view.zoom == fit_zoom && !s_preview_view.pan_x);
    // Pager sends letters, not LV_KEY arrows. Both cases must work directly.
    const auto press = [host](uint32_t input)
    { lv_obj_send_event(host, LV_EVENT_KEY, &input); };
    press('w');
    press('A');
    assert(s_preview_view.pan_x == 40 && s_preview_view.pan_y == 40);
    press('d');
    press('S');
    assert(!s_preview_view.pan_x && !s_preview_view.pan_y);
    press('W');
    press('a');
    press('D');
    press('s');
    assert(!s_preview_view.pan_x && !s_preview_view.pan_y);
    press('e');
    assert(s_preview_view.zoom == fit_zoom + 1);
    press('Q');
    assert(s_preview_view.zoom == fit_zoom);
    press('E');
    press('q');
    assert(s_preview_view.zoom == fit_zoom);
    press('w');
    press('e');
    press('F');
    assert(s_preview_view.zoom == fit_zoom && !s_preview_view.pan_x && !s_preview_view.pan_y);
    for (unsigned i = 0; i < 30; ++i) s_preview_view.zoomBy(1);
    refresh_route_preview_map();
    assert(s_preview_view.zoom == 18 && lv_obj_has_state(lv_obj_get_child(s_preview_map_controls, 0), LV_STATE_DISABLED));
    lv_obj_update_layout(host);
    assert(lv_obj_get_width(s_preview_map_controls) == 122 && lv_obj_get_height(s_preview_map_controls) == 36);
    assert(lv_obj_get_content_width(lv_obj_get_child(s_preview_map_controls, 2)) >= 30);
    lv_obj_delete(s_preview_map_controls);
    s_preview_map_controls = nullptr;
    test_profile.dense = true;
    create_preview_map_controls();
    lv_obj_update_layout(host);
    assert(lv_obj_get_width(s_preview_map_controls) == 90 && lv_obj_get_height(s_preview_map_controls) == 20);
    for (unsigned index = 0; index < 3; ++index)
    {
        auto* control = lv_obj_get_child(s_preview_map_controls, index);
        assert(lv_obj_get_width(control) == (index == 2 ? 36 : 24) && lv_obj_get_height(control) == 20);
        auto* caption = lv_obj_get_child(control, 0);
        lv_point_t text_size{};
        lv_text_get_size(&text_size, lv_label_get_text(caption), test_profile.tiny_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        assert(text_size.x <= lv_obj_get_content_width(control));
    }
    s_preview_view.reset();
    s_preview_images = {{true, 0, 0}, {true, 0.001, 0.001}};
    refresh_route_preview_map();
    lv_obj_update_layout(host);
    lv_area_t origin;
    lv_obj_get_coords(ui::widgets::map::test_widgets.overlay_layer, &origin);
    lv_point_t pin;
    ui::widgets::map::project_point(s_preview_map_runtime, {true, 0.001, 0.001}, pin);
    ui::widgets::map::GestureEvent tap;
    tap.phase = Phase::Tapped;
    tap.point = {origin.x1 + pin.x, origin.y1 + pin.y - 6};
    callback(tap, nullptr);
    assert(s_preview_selected_image == 1);
    lv_obj_delete(host);
    ui::widgets::map::test_widgets = {};
    lv_display_delete(display);
}
