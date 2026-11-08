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
    if (key != LV_KEY_BACKSPACE && key != LV_KEY_ESC)
    {
        return;
    }
    if (back_button())
    {
        lv_obj_send_event(back_button(), LV_EVENT_CLICKED, nullptr);
    }
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

    if (s_context.focusables)
    {
        for (lv_obj_t* obj : *s_context.focusables)
        {
            add_if(obj);
        }
    }

    if (s_context.default_focus && *s_context.default_focus)
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
