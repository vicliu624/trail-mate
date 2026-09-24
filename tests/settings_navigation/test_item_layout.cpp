#include "ui/screens/settings/settings_item_layout.h"
#include <cassert>
#include <vector>

int main()
{
    lv_init();
    auto* display = lv_display_create(480, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    std::vector<uint16_t> pixels(480 * 240);
    lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(d); });
    auto* row = lv_button_create(lv_screen_active());
    lv_obj_set_style_pad_left(row, 6, 0);
    lv_obj_set_style_pad_right(row, 6, 0);
    auto* title = lv_label_create(row);
    auto* value = lv_label_create(row);
    lv_label_set_text(title, "Contact Alerts");
    lv_label_set_text(value, "Contacts Only - a long value that must not overlap");
    settings::ui::item_layout::apply(row, title, value, 32, true);
    assert(lv_obj_has_flag(row, LV_OBJ_FLAG_CLICKABLE));
    assert(!lv_obj_has_flag(title, LV_OBJ_FLAG_CLICKABLE));
    assert(!lv_obj_has_flag(value, LV_OBJ_FLAG_CLICKABLE));
    for (int width : {220, 360, 180, 360, 220})
    {
        lv_obj_set_width(row, width);
        lv_obj_update_layout(row);
        lv_area_t a{}, b{}, r{};
        lv_obj_get_coords(title, &a);
        lv_obj_get_coords(value, &b);
        lv_obj_get_coords(row, &r);
        assert(a.x1 >= r.x1 && a.x2 <= r.x2);
        assert(b.x1 >= r.x1 && b.x2 <= r.x2);
        assert(b.y2 <= r.y2);
        if (width < 280)
        {
            assert(a.y2 < b.y1);
            assert(lv_obj_get_style_bg_opa(title, LV_PART_MAIN) != LV_OPA_TRANSP);
        }
        else
        {
            assert(a.x2 < b.x1);
            assert(lv_obj_get_style_bg_opa(title, LV_PART_MAIN) == LV_OPA_TRANSP);
        }
        lv_tick_inc(20);
        lv_timer_handler();
    }
    // Existing full-length identity displays must keep wrapping, even on wide rows.
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_label_set_text(value, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    lv_obj_set_width(row, 360);
    lv_obj_update_layout(row);
    assert(lv_obj_get_y(value) > lv_obj_get_y(title) + lv_obj_get_height(title));
    assert(lv_obj_get_height(value) >= 2 * lv_font_get_line_height(lv_obj_get_style_text_font(value, LV_PART_MAIN)));
    lv_obj_delete(row);
    row = lv_button_create(lv_screen_active());
    lv_obj_set_width(row, 220);
    title = lv_label_create(row);
    value = lv_label_create(row);
    lv_label_set_text(title, "Reboot");
    settings::ui::item_layout::apply(row, title, value, 32, false);
    lv_obj_update_layout(row);
    assert(lv_obj_has_flag(value, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_height(row) == 32);
    lv_obj_delete(row);
    lv_display_delete(display);
    lv_deinit();
}
