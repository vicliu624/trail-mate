/**
 * @file settings_page_input.cpp
 * @brief Settings input handling implementation
 */

#include "settings_page_input.h"
#include "settings_state.h"
#include "../../ui_common.h"
#include <Arduino.h>

namespace settings::ui::input
{

namespace
{

enum class FocusColumn
{
    Filter = 0,
    List = 1,
};

static lv_group_t* s_group = nullptr;
static lv_group_t* s_prev_group = nullptr;
static lv_indev_t* s_encoder = nullptr;
static FocusColumn s_col = FocusColumn::Filter;

static lv_indev_t* find_encoder_indev()
{
    lv_indev_t* indev = nullptr;
    while ((indev = lv_indev_get_next(indev)) != nullptr)
    {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER)
        {
            return indev;
        }
    }
    return nullptr;
}

static bool is_encoder_active()
{
    lv_indev_t* indev = lv_indev_get_act();
    return indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER;
}

static void group_clear_all(lv_group_t* g)
{
    if (!g) return;
    lv_group_remove_all_objs(g);
}

static void focus_first_valid(lv_obj_t* obj)
{
    if (!s_group || !obj || !lv_obj_is_valid(obj)) return;
    lv_group_focus_obj(obj);
}

static void root_key_event_cb(lv_event_t* e);

static void attach_key_handler(lv_obj_t* obj)
{
    if (!obj || !lv_obj_is_valid(obj)) return;
    lv_obj_remove_event_cb(obj, root_key_event_cb);
    lv_obj_add_event_cb(obj, root_key_event_cb, LV_EVENT_KEY, nullptr);
}

static void bind_filter_column()
{
    if (!s_group) return;
    group_clear_all(s_group);

    if (g_state.top_bar.back_btn)
    {
        if (lv_obj_is_valid(g_state.top_bar.back_btn))
        {
            lv_group_add_obj(s_group, g_state.top_bar.back_btn);
            attach_key_handler(g_state.top_bar.back_btn);
        }
    }

    for (size_t i = 0; i < g_state.filter_count; ++i)
    {
        if (g_state.filter_buttons[i] && lv_obj_is_valid(g_state.filter_buttons[i]))
        {
            lv_group_add_obj(s_group, g_state.filter_buttons[i]);
            attach_key_handler(g_state.filter_buttons[i]);
        }
    }

    if (g_state.filter_buttons[g_state.current_category])
    {
        focus_first_valid(g_state.filter_buttons[g_state.current_category]);
    }
    else if (g_state.top_bar.back_btn)
    {
        focus_first_valid(g_state.top_bar.back_btn);
    }
}

static void bind_list_column()
{
    if (!s_group) return;
    group_clear_all(s_group);

    for (size_t i = 0; i < g_state.item_count; ++i)
    {
        if (g_state.item_widgets[i].btn && lv_obj_is_valid(g_state.item_widgets[i].btn))
        {
            lv_group_add_obj(s_group, g_state.item_widgets[i].btn);
            attach_key_handler(g_state.item_widgets[i].btn);
        }
    }
    if (g_state.list_back_btn && lv_obj_is_valid(g_state.list_back_btn))
    {
        lv_group_add_obj(s_group, g_state.list_back_btn);
        attach_key_handler(g_state.list_back_btn);
    }

    if (g_state.item_count > 0 && g_state.item_widgets[0].btn)
    {
        focus_first_valid(g_state.item_widgets[0].btn);
    }
    else if (g_state.list_back_btn)
    {
        focus_first_valid(g_state.list_back_btn);
    }
    else if (g_state.top_bar.back_btn)
    {
        focus_first_valid(g_state.top_bar.back_btn);
    }
}

static void rebind_by_column()
{
    if (s_col == FocusColumn::Filter)
    {
        bind_filter_column();
    }
    else
    {
        bind_list_column();
    }
}

static void root_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_BACKSPACE)
    {
        if (g_state.top_bar.back_btn)
        {
            lv_obj_send_event(g_state.top_bar.back_btn, LV_EVENT_CLICKED, nullptr);
        }
        return;
    }

    if (!is_encoder_active()) return;
    if (key == LV_KEY_ESC)
    {
        s_col = FocusColumn::Filter;
        rebind_by_column();
        return;
    }
    if (key != LV_KEY_ENTER) return;

    if (s_col == FocusColumn::Filter)
    {
        s_col = FocusColumn::List;
        rebind_by_column();
        return;
    }

    if (s_col == FocusColumn::List)
    {
        lv_obj_t* focused = s_group ? lv_group_get_focused(s_group) : nullptr;
        if (focused == g_state.list_back_btn)
        {
            s_col = FocusColumn::Filter;
            rebind_by_column();
            return;
        }
    }
}

} // namespace

void init()
{
    if (s_group)
    {
        cleanup();
    }
    s_group = lv_group_create();
    s_prev_group = lv_group_get_default();
    set_default_group(nullptr);
    set_default_group(s_group);
    s_encoder = find_encoder_indev();
    if (s_encoder)
    {
        lv_indev_set_group(s_encoder, s_group);
    }
    s_col = FocusColumn::Filter;
    rebind_by_column();

    lv_obj_t* key_target = g_state.root ? g_state.root : g_state.list_panel;
    if (key_target && lv_obj_is_valid(key_target))
    {
        lv_obj_add_event_cb(key_target, root_key_event_cb, LV_EVENT_KEY, nullptr);
    }
}

void cleanup()
{
    if (s_group)
    {
        if (s_encoder && lv_indev_get_group(s_encoder) == s_group)
        {
            lv_indev_set_group(s_encoder, nullptr);
        }
        set_default_group(nullptr);
        lv_group_del(s_group);
        s_group = nullptr;
    }
    s_encoder = nullptr;
    if (s_prev_group)
    {
        set_default_group(s_prev_group);
        s_prev_group = nullptr;
    }
}

void on_ui_refreshed()
{
    if (!s_group) return;
    rebind_by_column();
}

void focus_to_filter()
{
    if (!s_group) return;
    s_col = FocusColumn::Filter;
    rebind_by_column();
}

void focus_to_list()
{
    if (!s_group) return;
    s_col = FocusColumn::List;
    rebind_by_column();
}

lv_group_t* get_group()
{
    return s_group;
}

} // namespace settings::ui::input
