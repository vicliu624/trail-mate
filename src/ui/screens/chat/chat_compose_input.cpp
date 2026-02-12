#if !defined(ARDUINO_T_WATCH_S3)
#include "chat_compose_input.h"

namespace chat::ui::compose::input
{

void setup_default_group_focus(const layout::Widgets& w)
{
    if (lv_group_t* g = lv_group_get_default())
    {
        lv_group_add_obj(g, w.textarea);
        lv_group_add_obj(g, w.send_btn);
        if (w.position_btn && !lv_obj_has_flag(w.position_btn, LV_OBJ_FLAG_HIDDEN))
        {
            lv_group_add_obj(g, w.position_btn);
        }
        lv_group_add_obj(g, w.cancel_btn);
        lv_group_focus_obj(w.textarea);
    }
    lv_obj_add_state(w.textarea, LV_STATE_FOCUSED);
}

void bind_textarea_events(const layout::Widgets& w, void* user_data,
                          lv_event_cb_t key_cb, lv_event_cb_t text_cb)
{
    lv_obj_add_event_cb(w.textarea, text_cb, LV_EVENT_VALUE_CHANGED, user_data);
    lv_obj_add_event_cb(w.textarea, key_cb, LV_EVENT_KEY, user_data);
}

} // namespace chat::ui::compose::input

#endif

