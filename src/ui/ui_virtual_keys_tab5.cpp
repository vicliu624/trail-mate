#include "ui_virtual_keys_tab5.h"

#if defined(ARDUINO_M5STACK_TAB5)

#include "LV_Helper.h"

namespace ui::tab5
{

namespace
{

void send_key(lv_key_t key)
{
    lv_indev_t* indev = lv_get_keyboard_indev();
    if (!indev)
    {
        // If there is no keyboard indev, try encoder as a fallback.
        indev = lv_get_encoder_indev();
    }
    if (!indev)
    {
        return;
    }
    lv_group_t* group = lv_group_get_default();
    if (!group)
    {
        return;
    }

    // Simulate a keyboard press+release sequence on the focused object.
    lv_obj_t* focused = lv_group_get_focused(group);
    if (!focused)
    {
        return;
    }

    lv_event_send(focused, LV_EVENT_KEY, &key);
}

void btn_event_back(lv_event_t* /*e*/)
{
    const lv_key_t key = LV_KEY_ESC;
    send_key(key);
}

void btn_event_up(lv_event_t* /*e*/)
{
    const lv_key_t key = LV_KEY_UP;
    send_key(key);
}

void btn_event_down(lv_event_t* /*e*/)
{
    const lv_key_t key = LV_KEY_DOWN;
    send_key(key);
}

void btn_event_ok(lv_event_t* /*e*/)
{
    const lv_key_t key = LV_KEY_ENTER;
    send_key(key);
}

} // namespace

void init_virtual_keys(lv_obj_t* root)
{
    if (!root)
    {
        return;
    }

    lv_obj_t* bar = lv_obj_create(root);
    lv_obj_set_size(bar, LV_PCT(100), 40);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_60, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_set_style_pad_column(bar, 6, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto make_btn = [&](const char* text, lv_event_cb_t cb)
    {
        lv_obj_t* btn = lv_btn_create(bar);
        lv_obj_set_size(btn, 60, LV_PCT(100));
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);
    };

    make_btn("Back", btn_event_back);
    make_btn("Up", btn_event_up);
    make_btn("Down", btn_event_down);
    make_btn("OK", btn_event_ok);
}

} // namespace ui::tab5

// C-style shim for use from main.cpp without including this header there.
extern "C" void ui_tab5_init_virtual_keys(lv_obj_t* root)
{
    ui::tab5::init_virtual_keys(root);
}

#endif // ARDUINO_M5STACK_TAB5
