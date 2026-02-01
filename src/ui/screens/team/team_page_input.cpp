/**
 * @file team_page_input.cpp
 * @brief Team page input handling
 */

#include <Arduino.h>
#include "team_page_input.h"
#include "team_state.h"
#include "../../ui_common.h"
#include "../../../app/app_context.h"

extern lv_group_t* app_g;

namespace team
{
namespace ui
{
namespace
{
lv_group_t* s_group = nullptr;

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
    }
}
} // namespace

void init_team_input()
{
    extern lv_group_t* app_g;
    if (!::app_g)
    {
        return;
    }

    s_group = ::app_g;
    set_default_group(s_group);
    refresh_team_input();
}

void refresh_team_input()
{
    if (!s_group)
    {
        return;
    }
    group_clear_all(s_group);

    if (g_team_state.top_bar_widget.back_btn)
    {
        lv_group_add_obj(s_group, g_team_state.top_bar_widget.back_btn);
    }

    for (lv_obj_t* obj : g_team_state.focusables)
    {
        add_if(obj);
    }

    if (g_team_state.default_focus)
    {
        lv_group_focus_obj(g_team_state.default_focus);
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
}

} // namespace ui
} // namespace team

void init_team_input()
{
    team::ui::init_team_input();
}

void refresh_team_input()
{
    team::ui::refresh_team_input();
}

void cleanup_team_input()
{
    team::ui::cleanup_team_input();
}
