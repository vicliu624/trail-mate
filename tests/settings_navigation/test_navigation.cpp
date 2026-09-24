#include "lvgl.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <memory>
#include <vector>

namespace ui::page_profile
{
struct Profile
{
    lv_coord_t top_bar_height = 40;
};
struct Size
{
    lv_coord_t width, height;
};
const Profile& current()
{
    static Profile profile;
    return profile;
}
Size resolve_modal_size(lv_coord_t w, lv_coord_t h, lv_obj_t*) { return {w, h}; }
lv_coord_t resolve_modal_pad() { return 4; }
} // namespace ui::page_profile
namespace ui::widgets
{
constexpr int kTopBarHeight = 40;
}

namespace
{
namespace style
{
void apply_modal_bg(lv_obj_t*) {}
void apply_modal_panel(lv_obj_t*) {}
} // namespace style
struct State
{
    lv_obj_t* root = nullptr;
    struct
    {
        lv_obj_t* back_btn = nullptr;
    } top_bar;
    lv_obj_t* modal_root = nullptr;
    lv_group_t* modal_group = nullptr;
    lv_obj_t* modal_textarea = nullptr;
    lv_obj_t* modal_error = nullptr;
    void* editing_item = nullptr;
    void* editing_widget = nullptr;
} g_state;
struct ImeFixture
{
    void detach() {}
};
std::unique_ptr<ImeFixture> s_text_modal_ime;
lv_group_t* s_modal_prev_group = nullptr;
lv_group_t* page_group = nullptr;
lv_obj_t* page_root = nullptr;
lv_obj_t* page_item = nullptr;
lv_obj_t* s_gps_diagnostics_label = nullptr;
lv_obj_t* s_spi_diagnostics_label = nullptr;
std::array<lv_obj_t*, 6> s_manual_time_rollers{};
std::array<lv_obj_t*, 8> s_manual_datetime_focus_order{};
unsigned s_manual_datetime_focus_count = 0, s_option_click_count = 0, s_ime_toggle_count = 0;
unsigned refresh_count = 0, exit_count = 0;

void set_default_group(lv_group_t* group)
{
    for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
        lv_indev_set_group(input, group);
    lv_group_set_default(group);
}
namespace settings::ui::input
{
lv_group_t* get_group() { return page_group; }
void on_ui_refreshed()
{
    ++refresh_count;
    lv_group_remove_all_objs(page_group);
    lv_group_add_obj(page_group, g_state.top_bar.back_btn);
    lv_group_add_obj(page_group, page_item);
    lv_group_focus_obj(page_item);
}
} // namespace settings::ui::input
void ui_request_exit_to_menu() { ++exit_count; }
#include "settings_lifecycle_actual.inc"

void tick()
{
    lv_tick_inc(20);
    lv_timer_handler();
}
void open_modal(bool text)
{
    modal_prepare_group();
    assert(lv_obj_get_group(g_state.top_bar.back_btn) == g_state.modal_group);
    g_state.modal_root = create_modal_root(300, 240);
    auto* panel = lv_obj_get_child(g_state.modal_root, 0);
    auto* field = text ? lv_textarea_create(panel) : lv_button_create(panel);
    lv_group_add_obj(g_state.modal_group, field);
    lv_obj_add_event_cb(field, on_modal_key, LV_EVENT_KEY, nullptr);
    lv_group_focus_obj(field);
    if (text)
    {
        g_state.modal_textarea = field;
        s_text_modal_ime = std::make_unique<ImeFixture>();
    }
    lv_group_set_editing(g_state.modal_group, false);
    lv_obj_update_layout(page_root);
    assert(lv_obj_get_y(g_state.modal_root) == 40);
    assert(lv_obj_get_height(g_state.modal_root) == 200);
    assert(lv_obj_get_height(panel) <= 200);
    assert(lv_obj_get_height(page_item) == 200);
}
} // namespace

int main()
{
    lv_init();
    auto* display = lv_display_create(320, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    std::vector<uint16_t> pixels(320 * 240);
    lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(d); });
    page_root = lv_obj_create(lv_screen_active());
    g_state.root = page_root;
    lv_obj_remove_style_all(page_root);
    lv_obj_set_size(page_root, 320, 240);
    lv_obj_set_flex_flow(page_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(page_root, LV_OBJ_FLAG_SCROLLABLE);
    page_group = lv_group_create();
    set_default_group(page_group);
    auto* header = lv_obj_create(page_root);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, 320, 40);
    g_state.top_bar.back_btn = lv_button_create(header);
    lv_obj_set_pos(g_state.top_bar.back_btn, 0, 0);
    lv_obj_set_size(g_state.top_bar.back_btn, 60, 32);
    lv_obj_add_event_cb(
        g_state.top_bar.back_btn, [](lv_event_t*)
        { settings_back_cb(nullptr); },
        LV_EVENT_CLICKED, nullptr);
    page_item = lv_button_create(page_root);
    lv_obj_set_size(page_item, 320, 0);
    lv_obj_set_flex_grow(page_item, 1);

    lv_indev_data_t encoder_sample{}, pointer_sample{};
    encoder_sample.key = LV_KEY_ENTER;
    auto* encoder = lv_indev_create();
    lv_indev_set_type(encoder, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_user_data(encoder, &encoder_sample);
    auto* pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(pointer, &pointer_sample);
    for (auto* input : {encoder, pointer})
    {
        lv_indev_set_display(input, display);
        lv_indev_set_read_cb(input, [](lv_indev_t* device, lv_indev_data_t* data)
                             { *data = *static_cast<lv_indev_data_t*>(lv_indev_get_user_data(device)); });
    }
    auto read = [](lv_indev_t* input)
    { lv_tick_inc(20); lv_indev_read(input); };
    for (int cycle = 0; cycle < 100; ++cycle)
    {
        open_modal(cycle % 2 == 0);
        for (unsigned step = 0; step < 8 && lv_group_get_focused(g_state.modal_group) != g_state.top_bar.back_btn; ++step)
        {
            encoder_sample.enc_diff = 1;
            read(encoder);
        }
        encoder_sample.enc_diff = 0;
        assert(lv_group_get_focused(g_state.modal_group) == g_state.top_bar.back_btn);
        encoder_sample.state = LV_INDEV_STATE_PRESSED;
        read(encoder);
        encoder_sample.state = LV_INDEV_STATE_RELEASED;
        read(encoder);
        assert(!g_state.modal_root && !g_state.modal_group && !s_text_modal_ime);
        assert(exit_count == 0);
        assert(lv_indev_get_group(encoder) == page_group);
        assert(lv_obj_get_group(g_state.top_bar.back_btn) == page_group);
        tick();

        open_modal(cycle % 2 != 0);
        pointer_sample.point = {20, 15};
        pointer_sample.state = LV_INDEV_STATE_PRESSED;
        read(pointer);
        pointer_sample.state = LV_INDEV_STATE_RELEASED;
        read(pointer);
        assert(!g_state.modal_root && exit_count == 0);
        tick();
    }
    settings_back_cb(nullptr);
    assert(exit_count == 1);

    // Destroying the page must not rebuild its focus group along the way.
    open_modal(true);
    const auto before = refresh_count;
    modal_close(false);
    assert(refresh_count == before && !s_modal_prev_group);
    set_default_group(nullptr);
    lv_group_delete(page_group);
    lv_obj_delete(page_root);
    tick(); // Pending child deletion must be cancelled by parent deletion.
    lv_indev_delete(pointer);
    lv_indev_delete(encoder);
    lv_display_delete(display);
    lv_deinit();
}
