#include "ui/widgets/map/map_viewport.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#define MAP_VIEWPORT_LOG(...) ((void)0)
#define MAP_DIAG(...) ((void)0)

namespace ui::widgets::map
{
// Minimal host fixture for the fields used by the actual production callbacks.
struct RuntimeImpl
{
    Widgets widgets{};
    bool alive = true;
    bool gesture_enabled = true;
    bool gesture_pressed = false;
    bool gesture_dragging = false;
    lv_point_t gesture_start{};
    lv_point_t gesture_last{};
    GestureCallback gesture_callback = nullptr;
    void* gesture_user_data = nullptr;
};
#include "map_gesture_actual.inc"
} // namespace ui::widgets::map

int main()
{
    using namespace ui::widgets::map;
    lv_init();
    auto* display = lv_display_create(320, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    std::vector<uint16_t> pixels(320 * 240);
    lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(d); });
    auto* surface = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(surface);
    lv_obj_set_size(surface, 320, 240);
    lv_obj_clear_flag(surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(surface, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_update_layout(surface);

    RuntimeImpl runtime;
    runtime.widgets.root = runtime.widgets.tile_layer = runtime.widgets.gesture_surface = surface;
    std::vector<GestureEvent> events;
    runtime.gesture_user_data = &events;
    runtime.gesture_callback = [](const GestureEvent& event, void* data)
    {
        static_cast<std::vector<GestureEvent>*>(data)->push_back(event);
    };
    lv_obj_add_event_cb(surface, gesture_surface_event_cb, LV_EVENT_ALL, &runtime);

    lv_indev_data_t sample{};
    auto* input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input, display);
    lv_indev_set_user_data(input, &sample);
    lv_indev_set_read_cb(input, [](lv_indev_t* device, lv_indev_data_t* data)
                         { *data = *static_cast<lv_indev_data_t*>(lv_indev_get_user_data(device)); });
    auto feed = [&](int x, int y, lv_indev_state_t state)
    {
        sample.point = {x, y};
        sample.state = state;
        lv_tick_inc(20);
        lv_indev_read(input);
    };

    // A tap must not generate a drag or leave the loader's pressed gate set.
    feed(100, 100, LV_INDEV_STATE_PRESSED);
    feed(100, 100, LV_INDEV_STATE_RELEASED);
    assert(events.size() == 2 && events[0].phase == GesturePhase::Pressed && events[1].phase == GesturePhase::Tapped);
    assert(events[1].point.x == 100 && events[1].point.y == 100);
    assert(!runtime.gesture_pressed && !runtime.gesture_dragging);

    // Real LVGL pointer dispatch, including repeated drags and moving outside
    // the surface. No direct invocation of the RELEASED callback can hide the bug.
    for (int cycle = 0; cycle < 100; ++cycle)
    {
        events.clear();
        feed(100, 100, LV_INDEV_STATE_PRESSED);
        feed(120, 110, LV_INDEV_STATE_PRESSED);
        feed(cycle % 2 ? 400 : 180, 140, LV_INDEV_STATE_PRESSED);
        feed(cycle % 2 ? 400 : 180, 140, LV_INDEV_STATE_RELEASED);
        unsigned ended = 0;
        for (const auto& event : events)
        {
            assert(event.phase != GesturePhase::Tapped);
            ended += event.phase == GesturePhase::DragEnd || event.phase == GesturePhase::Cancel;
        }
        if (runtime.gesture_pressed || runtime.gesture_dragging || ended != 1)
        {
            std::fprintf(stderr, "release lost: cycle=%d pressed=%d dragging=%d ended=%u\n",
                         cycle, runtime.gesture_pressed, runtime.gesture_dragging, ended);
            return 1;
        }
    }
    lv_indev_delete(input);
    lv_obj_delete(surface);
    lv_display_delete(display);
    lv_deinit();
    std::puts("real LVGL: tap and 100 drag/release cycles passed");
}
