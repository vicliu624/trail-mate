/**
 * @file chat_message_list_input.cpp
 * @brief Input handling for ChatMessageListScreen (explicit layer)
 *
 * Current policy:
 * - Rotary focuses filter buttons (Direct/Broadcast) or list items.
 * - Press in filter column moves to list (or back button exits).
 * - Press in list column returns to filter when Back is focused.
 * - Back/ESC returns to filter column.
 */

#include "chat_message_list_input.h"
#include "../ui_common.h"
#include "chat_message_list_components.h"
#include <Arduino.h> // keep Arduino first if your build requires it

#define CHAT_INPUT_DEBUG 1
#if CHAT_INPUT_DEBUG
#define CHAT_INPUT_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CHAT_INPUT_LOG(...)
#endif

namespace chat::ui::message_list::input
{

namespace
{

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

static bool screen_alive(const Binding* binding)
{
    return binding && binding->screen && binding->screen->isAlive();
}

static void focus_first_valid(Binding* binding, lv_obj_t* obj)
{
    if (!binding || !binding->group || !obj || !lv_obj_is_valid(obj)) return;
    lv_group_focus_obj(obj);
}

static void root_key_event_cb(lv_event_t* e);

static void group_add_if_valid(Binding* binding, lv_obj_t* obj)
{
    if (!binding || !binding->group || !obj || !lv_obj_is_valid(obj)) return;
    lv_group_add_obj(binding->group, obj);
    lv_obj_remove_event_cb(obj, root_key_event_cb);
    lv_obj_add_event_cb(obj, root_key_event_cb, LV_EVENT_KEY, binding);
}

static bool is_visible(lv_obj_t* obj)
{
    return obj && lv_obj_is_valid(obj) && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void clear_list_focus_states(Binding* binding)
{
    if (!binding || !binding->screen) return;
    lv_state_t clear_mask = (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    size_t count = binding->screen->getItemCount();
    for (size_t i = 0; i < count; ++i)
    {
        if (lv_obj_t* btn = binding->screen->getItemButton(i))
        {
            if (lv_obj_is_valid(btn))
            {
                lv_obj_clear_state(btn, clear_mask);
            }
        }
    }
    if (lv_obj_t* back = binding->screen->getListBackButton())
    {
        if (lv_obj_is_valid(back))
        {
            lv_obj_clear_state(back, clear_mask);
        }
    }
}

static void bind_filter_column(Binding* binding)
{
    if (!binding || !binding->group || !binding->screen) return;
    lv_group_focus_freeze(binding->group, true);
    group_clear_all(binding->group);
    clear_list_focus_states(binding);

    if (lv_obj_t* back = binding->screen->getBackButton())
    {
        group_add_if_valid(binding, back);
    }
    if (lv_obj_t* direct = binding->screen->getDirectButton())
    {
        group_add_if_valid(binding, direct);
    }
    if (lv_obj_t* broadcast = binding->screen->getBroadcastButton())
    {
        group_add_if_valid(binding, broadcast);
    }
    if (lv_obj_t* team = binding->screen->getTeamButton())
    {
        if (is_visible(team))
        {
            group_add_if_valid(binding, team);
        }
    }

    lv_group_focus_freeze(binding->group, false);

    if (lv_obj_t* direct = binding->screen->getDirectButton())
    {
        if (lv_obj_has_state(direct, LV_STATE_CHECKED))
        {
            focus_first_valid(binding, direct);
            return;
        }
    }
    if (lv_obj_t* broadcast = binding->screen->getBroadcastButton())
    {
        if (lv_obj_has_state(broadcast, LV_STATE_CHECKED))
        {
            focus_first_valid(binding, broadcast);
            return;
        }
    }
    if (lv_obj_t* team = binding->screen->getTeamButton())
    {
        if (is_visible(team) && lv_obj_has_state(team, LV_STATE_CHECKED))
        {
            focus_first_valid(binding, team);
            return;
        }
    }

    if (lv_obj_t* direct = binding->screen->getDirectButton())
    {
        focus_first_valid(binding, direct);
    }
    else if (lv_obj_t* broadcast = binding->screen->getBroadcastButton())
    {
        focus_first_valid(binding, broadcast);
    }
    else if (lv_obj_t* team = binding->screen->getTeamButton())
    {
        if (is_visible(team))
        {
            focus_first_valid(binding, team);
        }
    }
    else if (lv_obj_t* back = binding->screen->getBackButton())
    {
        focus_first_valid(binding, back);
    }
}

static void bind_list_column(Binding* binding)
{
    if (!binding || !binding->group || !binding->screen) return;
    lv_group_focus_freeze(binding->group, true);
    group_clear_all(binding->group);

    size_t count = binding->screen->getItemCount();
    for (size_t i = 0; i < count; ++i)
    {
        if (lv_obj_t* btn = binding->screen->getItemButton(i))
        {
            group_add_if_valid(binding, btn);
        }
    }
    if (lv_obj_t* back = binding->screen->getListBackButton())
    {
        group_add_if_valid(binding, back);
    }

    lv_group_focus_freeze(binding->group, false);

    if (count > 0)
    {
        int selected = binding->screen->getSelectedIndex();
        if (selected >= 0 && static_cast<size_t>(selected) < count)
        {
            focus_first_valid(binding, binding->screen->getItemButton(static_cast<size_t>(selected)));
        }
        else
        {
            focus_first_valid(binding, binding->screen->getItemButton(0));
        }
    }
    else if (lv_obj_t* back = binding->screen->getListBackButton())
    {
        focus_first_valid(binding, back);
    }
    else
    {
        binding->col = FocusColumn::Filter;
        bind_filter_column(binding);
    }
}

static void rebind_by_column(Binding* binding)
{
    if (!binding) return;
    if (binding->col == FocusColumn::Filter)
    {
        bind_filter_column(binding);
    }
    else
    {
        bind_list_column(binding);
    }
}

static void root_key_event_cb(lv_event_t* e)
{
    auto* binding = static_cast<Binding*>(lv_event_get_user_data(e));
    if (!screen_alive(binding)) return;

    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_BACKSPACE)
    {
        if (binding->screen && binding->screen->getBackButton())
        {
            lv_obj_send_event(binding->screen->getBackButton(), LV_EVENT_CLICKED, nullptr);
        }
        return;
    }
    if (!is_encoder_active()) return;
    if (key == LV_KEY_ESC)
    {
        binding->col = FocusColumn::Filter;
        rebind_by_column(binding);
        return;
    }

    if (key != LV_KEY_ENTER) return;

    lv_obj_t* focused = binding->group ? lv_group_get_focused(binding->group) : nullptr;
    if (!focused || !binding->screen) return;

    if (binding->col == FocusColumn::Filter)
    {
        if (focused == binding->screen->getBackButton())
        {
            lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
            return;
        }
        binding->col = FocusColumn::List;
        rebind_by_column(binding);
        return;
    }

    if (binding->col == FocusColumn::List)
    {
        if (focused == binding->screen->getListBackButton())
        {
            binding->col = FocusColumn::Filter;
            rebind_by_column(binding);
            return;
        }
    }
}

} // namespace

void init(ChatMessageListScreen* screen, Binding* binding)
{
    if (!binding)
    {
        return;
    }
    if (binding->bound)
    {
        cleanup(binding);
    }

    binding->screen = screen;
    binding->group = lv_group_create();
    binding->prev_group = lv_group_get_default();
    set_default_group(nullptr);
    binding->col = FocusColumn::Filter;
    rebind_by_column(binding);
    set_default_group(binding->group);

    if (binding->screen && binding->screen->getObj())
    {
        lv_obj_add_event_cb(binding->screen->getObj(), root_key_event_cb, LV_EVENT_KEY, binding);
    }
    binding->bound = true;

    CHAT_INPUT_LOG("[ChatMessageListInput] init\n");
}

void cleanup(Binding* binding)
{
    if (!binding || !binding->bound)
    {
        return;
    }
    binding->screen = nullptr;
    if (binding->group)
    {
        set_default_group(nullptr);
        lv_group_del(binding->group);
        binding->group = nullptr;
    }
    if (binding->prev_group)
    {
        set_default_group(binding->prev_group);
    }
    binding->prev_group = nullptr;
    binding->bound = false;
    CHAT_INPUT_LOG("[ChatMessageListInput] cleanup\n");
}

void on_ui_refreshed(Binding* binding)
{
    if (!binding || !binding->group || !screen_alive(binding)) return;
    rebind_by_column(binding);
}

void focus_filter(Binding* binding)
{
    if (!binding || !binding->group || !screen_alive(binding)) return;
    binding->col = FocusColumn::Filter;
    rebind_by_column(binding);
}

void focus_list(Binding* binding)
{
    if (!binding || !binding->group || !screen_alive(binding)) return;
    binding->col = FocusColumn::List;
    rebind_by_column(binding);
}

} // namespace chat::ui::message_list::input
