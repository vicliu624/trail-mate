#include "ui/screens/agenda/agenda_page_input.h"
#include "ui/app_runtime.h"
#include "ui/screens/agenda/agenda_page_runtime.h"

namespace ui::agenda::page::input
{
namespace
{
void key(lv_event_t* event)
{
    auto* s = state();
    if (!s || !s->group) return;
    const auto code = lv_event_get_key(event);
    auto* object = static_cast<lv_obj_t*>(lv_event_get_target(event));
    const bool text = lv_obj_check_type(object, &lv_textarea_class);
    const bool editor = text || lv_obj_check_type(object, &lv_spinbox_class) || lv_obj_check_type(object, &lv_buttonmatrix_class);
    // Text backspace belongs to the editor, not to page navigation. Editing
    // widgets retain their own arrows; Tab/encoder focus traversal can leave.
    if (text && code == LV_KEY_BACKSPACE) return;
    if (editor && lv_group_get_editing(s->group) && code != LV_KEY_ESC &&
        code != LV_KEY_NEXT && code != LV_KEY_PREV) return;
    if (code == LV_KEY_ESC || code == LV_KEY_BACKSPACE) runtime::request(Action::Back);
    else if (code == LV_KEY_UP || code == LV_KEY_LEFT || code == LV_KEY_PREV || code == 19)
        lv_group_focus_prev(s->group);
    else if (code == LV_KEY_DOWN || code == LV_KEY_RIGHT || code == LV_KEY_NEXT || code == 20)
        lv_group_focus_next(s->group);
    else return;
    lv_group_set_editing(s->group, false);
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    // KEY carries a uint32_t*, not an input-device pointer. Synthetic group
    // events may have no active device at all.
    if (auto* device = lv_indev_active()) lv_indev_stop_processing(device);
}
} // namespace

void begin()
{
    auto* s = state();
    s->previous_group = lv_group_get_default();
    s->group = lv_group_create();
    lv_group_set_wrap(s->group, true);
    set_default_group(s->group);
}
void bind(lv_obj_t* object)
{
    if (!object || !state()) return;
    lv_group_add_obj(state()->group, object);
    lv_obj_remove_event_cb(object, key);
    lv_obj_add_event_cb(object, key, LV_EVENT_KEY, nullptr);
}
void activate(lv_event_t* event)
{
    const auto encoded = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    runtime::request(static_cast<Action>(encoded & 0xff), static_cast<uint8_t>(encoded >> 8));
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    if (auto* device = lv_event_get_indev(event)) lv_indev_stop_processing(device);
}
void end()
{
    auto* s = state();
    if (!s) return;
    set_default_group(s->previous_group);
    if (s->group) lv_group_delete(s->group);
    s->group = nullptr;
}
} // namespace ui::agenda::page::input
