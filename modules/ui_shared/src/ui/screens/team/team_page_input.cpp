/**
 * @file team_page_input.cpp
 * @brief Team page input handling
 */

#include "ui/screens/team/team_page_input.h"
#include "ui/app_runtime.h"
#include "ui/ui_common.h"

namespace team
{
namespace ui
{
namespace
{
lv_group_t* s_group = nullptr;
TeamPageInputContext s_context{};

lv_obj_t* back_button()
{
    return s_context.top_bar ? s_context.top_bar->back_btn : nullptr;
}

void root_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (lv_event_get_target_obj(e) == s_context.body &&
        (key == 'w' || key == 'W' || key == 's' || key == 'S'))
    {
        const lv_coord_t step = lv_obj_get_content_height(s_context.body) / 3;
        lv_obj_scroll_by(s_context.body, 0,
                         key == 'w' || key == 'W' ? step : -step, LV_ANIM_OFF);
        lv_event_stop_processing(e);
        return;
    }
    if (key != LV_KEY_BACKSPACE && key != LV_KEY_ESC)
    {
        return;
    }
    if (back_button())
    {
        lv_obj_send_event(back_button(), LV_EVENT_CLICKED, nullptr);
    }
}

void body_focus_event_cb(lv_event_t* e)
{
    // Keep the shared keyboard router from turning reading arrows into footer
    // selection. Tab still leaves this viewport for ordinary action controls.
    if (s_group && lv_event_get_code(e) == LV_EVENT_FOCUSED)
        lv_group_set_editing(s_group, lv_event_get_target_obj(e) == s_context.body);
}

void group_clear_all(lv_group_t* group)
{
    if (!group)
    {
        return;
    }
    lv_group_remove_all_objs(group);
}

void add_if(lv_obj_t* obj)
{
    if (obj && s_group)
    {
        lv_group_add_obj(s_group, obj);
        lv_obj_remove_event_cb(obj, root_key_event_cb);
        lv_obj_add_event_cb(obj, root_key_event_cb, LV_EVENT_KEY, nullptr);
        lv_obj_remove_event_cb(obj, body_focus_event_cb);
        lv_obj_add_event_cb(obj, body_focus_event_cb, LV_EVENT_FOCUSED, nullptr);
    }
}
} // namespace

void init_team_input(const TeamPageInputContext& context)
{
    s_context = context;
    if (!::app_g)
    {
        return;
    }

    s_group = ::app_g;
    set_default_group(s_group);
    refresh_team_input(s_context);

    if (s_context.root)
    {
        lv_obj_add_event_cb(s_context.root, root_key_event_cb, LV_EVENT_KEY, nullptr);
    }
}

void refresh_team_input(const TeamPageInputContext& context)
{
    s_context = context;
    if (!s_group)
    {
        return;
    }
    group_clear_all(s_group);

    if (back_button())
    {
        add_if(back_button());
    }

    if (s_context.root) lv_obj_update_layout(s_context.root);
    const bool scrollable_body = s_context.body &&
                                 (lv_obj_get_scroll_bottom(s_context.body) > 0 ||
                                  lv_obj_get_scroll_y(s_context.body) > 0);
    if (scrollable_body)
    {
        add_if(s_context.body);
    }

    if (s_context.focusables)
    {
        for (lv_obj_t* obj : *s_context.focusables)
        {
            add_if(obj);
        }
    }

    if (scrollable_body)
    {
        lv_group_focus_obj(s_context.body);
    }
    else if (s_context.default_focus && *s_context.default_focus)
    {
        lv_group_focus_obj(*s_context.default_focus);
    }
}

void cleanup_team_input()
{
    if (!s_group)
    {
        return;
    }
    group_clear_all(s_group);
    s_group = nullptr;
    s_context = TeamPageInputContext{};
}

} // namespace ui
} // namespace team
