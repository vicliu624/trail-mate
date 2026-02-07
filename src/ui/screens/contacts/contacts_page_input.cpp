/**
 * @file contacts_page_input.cpp
 * @brief Contacts page input handling implementation
 */

#include "contacts_page_input.h"
#include "contacts_state.h"
#include "../../ui_common.h"
#include <Arduino.h>     // Must be first for library compilation
#include <Preferences.h> // Required for contacts_state.h -> contact_service.h dependency chain

#define CONTACTS_DEBUG 1
#if CONTACTS_DEBUG
#define CONTACTS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CONTACTS_LOG(...)
#endif

using namespace contacts::ui;

namespace
{

enum class FocusColumn
{
    Filter = 0,
    List = 1,
    Action = 2,
};

static lv_group_t* s_group = nullptr;
static lv_group_t* s_prev_group = nullptr;
static FocusColumn s_col = FocusColumn::Filter;
static lv_indev_t* s_encoder = nullptr;

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

static void clear_action_focus_states()
{
    lv_state_t clear_mask = (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY | LV_STATE_EDITED);
    if (g_contacts_state.action_back_btn && lv_obj_is_valid(g_contacts_state.action_back_btn))
        lv_obj_clear_state(g_contacts_state.action_back_btn, clear_mask);
    if (g_contacts_state.chat_btn && lv_obj_is_valid(g_contacts_state.chat_btn))
        lv_obj_clear_state(g_contacts_state.chat_btn, clear_mask);
    if (g_contacts_state.position_btn && lv_obj_is_valid(g_contacts_state.position_btn))
        lv_obj_clear_state(g_contacts_state.position_btn, clear_mask);
    if (g_contacts_state.edit_btn && lv_obj_is_valid(g_contacts_state.edit_btn))
        lv_obj_clear_state(g_contacts_state.edit_btn, clear_mask);
    if (g_contacts_state.del_btn && lv_obj_is_valid(g_contacts_state.del_btn))
        lv_obj_clear_state(g_contacts_state.del_btn, clear_mask);
    if (g_contacts_state.add_btn && lv_obj_is_valid(g_contacts_state.add_btn))
        lv_obj_clear_state(g_contacts_state.add_btn, clear_mask);
    if (g_contacts_state.info_btn && lv_obj_is_valid(g_contacts_state.info_btn))
        lv_obj_clear_state(g_contacts_state.info_btn, clear_mask);
}

static bool is_valid(lv_obj_t* obj)
{
    return obj && lv_obj_is_valid(obj);
}

static bool is_visible(lv_obj_t* obj)
{
    return is_valid(obj) && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void root_key_event_cb(lv_event_t* e);

static void attach_key_handler(lv_obj_t* obj)
{
    if (!obj || !lv_obj_is_valid(obj)) return;
    lv_obj_remove_event_cb(obj, root_key_event_cb);
    lv_obj_add_event_cb(obj, root_key_event_cb, LV_EVENT_KEY, nullptr);
}

static void focus_first_valid(lv_obj_t* obj)
{
    if (!s_group || !is_valid(obj)) return;
    lv_group_focus_obj(obj);
}

static void bind_filter_column(bool keep_mode_focus)
{
    if (!s_group) return;
    group_clear_all(s_group);
    clear_action_focus_states();

    // ① TopBar Back —— 第一列“出口”，必须可达
    if (is_valid(g_contacts_state.top_bar.back_btn))
    {
        lv_group_add_obj(s_group, g_contacts_state.top_bar.back_btn);
        attach_key_handler(g_contacts_state.top_bar.back_btn);
    }

    // ② Filter buttons
    if (is_visible(g_contacts_state.contacts_btn))
    {
        lv_group_add_obj(s_group, g_contacts_state.contacts_btn);
        attach_key_handler(g_contacts_state.contacts_btn);
    }
    if (is_visible(g_contacts_state.nearby_btn))
    {
        lv_group_add_obj(s_group, g_contacts_state.nearby_btn);
        attach_key_handler(g_contacts_state.nearby_btn);
    }
    if (is_visible(g_contacts_state.broadcast_btn))
    {
        lv_group_add_obj(s_group, g_contacts_state.broadcast_btn);
        attach_key_handler(g_contacts_state.broadcast_btn);
    }
    if (is_visible(g_contacts_state.team_btn))
    {
        lv_group_add_obj(s_group, g_contacts_state.team_btn);
        attach_key_handler(g_contacts_state.team_btn);
    }

    // Focus preference:
    // - keep_mode_focus: focus current mode button (better for rotate-to-switch-mode UX)
    // - else: focus current mode button; fallback to back
    if (keep_mode_focus)
    {
        if (g_contacts_state.current_mode == ContactsMode::Contacts && g_contacts_state.contacts_btn)
        {
            focus_first_valid(g_contacts_state.contacts_btn);
        }
        else if (g_contacts_state.current_mode == ContactsMode::Nearby && g_contacts_state.nearby_btn)
        {
            focus_first_valid(g_contacts_state.nearby_btn);
        }
        else if (g_contacts_state.current_mode == ContactsMode::Broadcast && is_visible(g_contacts_state.broadcast_btn))
        {
            focus_first_valid(g_contacts_state.broadcast_btn);
        }
        else if (g_contacts_state.current_mode == ContactsMode::Team && is_visible(g_contacts_state.team_btn))
        {
            focus_first_valid(g_contacts_state.team_btn);
        }
        else if (g_contacts_state.top_bar.back_btn)
        {
            focus_first_valid(g_contacts_state.top_bar.back_btn);
        }
        else if (is_visible(g_contacts_state.contacts_btn))
        {
            focus_first_valid(g_contacts_state.contacts_btn);
        }
        else if (is_visible(g_contacts_state.nearby_btn))
        {
            focus_first_valid(g_contacts_state.nearby_btn);
        }
        else if (is_visible(g_contacts_state.broadcast_btn))
        {
            focus_first_valid(g_contacts_state.broadcast_btn);
        }
        else if (is_visible(g_contacts_state.team_btn))
        {
            focus_first_valid(g_contacts_state.team_btn);
        }
    }
    else
    {
        // first entry: prefer current mode button
        if (g_contacts_state.current_mode == ContactsMode::Contacts && is_visible(g_contacts_state.contacts_btn))
        {
            focus_first_valid(g_contacts_state.contacts_btn);
        }
        else if (g_contacts_state.current_mode == ContactsMode::Nearby && is_visible(g_contacts_state.nearby_btn))
        {
            focus_first_valid(g_contacts_state.nearby_btn);
        }
        else if (g_contacts_state.current_mode == ContactsMode::Broadcast && is_visible(g_contacts_state.broadcast_btn))
        {
            focus_first_valid(g_contacts_state.broadcast_btn);
        }
        else if (g_contacts_state.current_mode == ContactsMode::Team && is_visible(g_contacts_state.team_btn))
        {
            focus_first_valid(g_contacts_state.team_btn);
        }
        else if (is_visible(g_contacts_state.contacts_btn))
        {
            focus_first_valid(g_contacts_state.contacts_btn);
        }
        else if (is_visible(g_contacts_state.nearby_btn))
        {
            focus_first_valid(g_contacts_state.nearby_btn);
        }
        else if (is_visible(g_contacts_state.broadcast_btn))
        {
            focus_first_valid(g_contacts_state.broadcast_btn);
        }
        else if (is_visible(g_contacts_state.team_btn))
        {
            focus_first_valid(g_contacts_state.team_btn);
        }
        else if (g_contacts_state.top_bar.back_btn)
        {
            focus_first_valid(g_contacts_state.top_bar.back_btn);
        }
    }
}

static void bind_list_column()
{
    if (!s_group) return;
    group_clear_all(s_group);
    clear_action_focus_states();

    for (auto* item : g_contacts_state.list_items)
    {
        if (is_visible(item))
        {
            lv_group_add_obj(s_group, item);
            attach_key_handler(item);
        }
    }

    if (is_valid(g_contacts_state.prev_btn))
    {
        lv_group_add_obj(s_group, g_contacts_state.prev_btn);
        attach_key_handler(g_contacts_state.prev_btn);
    }
    if (is_valid(g_contacts_state.next_btn))
    {
        lv_group_add_obj(s_group, g_contacts_state.next_btn);
        attach_key_handler(g_contacts_state.next_btn);
    }
    if (is_valid(g_contacts_state.back_btn))
    {
        lv_group_add_obj(s_group, g_contacts_state.back_btn);
        attach_key_handler(g_contacts_state.back_btn);
    }

    if (!g_contacts_state.list_items.empty() && is_valid(g_contacts_state.list_items[0]))
    {
        focus_first_valid(g_contacts_state.list_items[0]);
    }
    else if (is_valid(g_contacts_state.back_btn))
    {
        focus_first_valid(g_contacts_state.back_btn);
    }
    else if (is_valid(g_contacts_state.prev_btn))
    {
        focus_first_valid(g_contacts_state.prev_btn);
    }
    else if (is_valid(g_contacts_state.next_btn))
    {
        focus_first_valid(g_contacts_state.next_btn);
    }

    CONTACTS_LOG("[Contacts][Input] bind_list_column (items=%u)\n",
                 (unsigned)g_contacts_state.list_items.size());
}

static void bind_action_column()
{
    if (!s_group) return;
    group_clear_all(s_group);

    bool any = false;

    // ① Actions
    if (is_visible(g_contacts_state.chat_btn))
    {
        lv_group_add_obj(s_group, g_contacts_state.chat_btn);
        attach_key_handler(g_contacts_state.chat_btn);
        any = true;
    }

    if (g_contacts_state.current_mode == ContactsMode::Contacts)
    {
        if (is_visible(g_contacts_state.edit_btn))
        {
            lv_group_add_obj(s_group, g_contacts_state.edit_btn);
            attach_key_handler(g_contacts_state.edit_btn);
            any = true;
        }
        if (is_visible(g_contacts_state.del_btn))
        {
            lv_group_add_obj(s_group, g_contacts_state.del_btn);
            attach_key_handler(g_contacts_state.del_btn);
            any = true;
        }
    }
    else if (g_contacts_state.current_mode == ContactsMode::Nearby)
    {
        if (is_visible(g_contacts_state.add_btn))
        {
            lv_group_add_obj(s_group, g_contacts_state.add_btn);
            attach_key_handler(g_contacts_state.add_btn);
            any = true;
        }
    }
    else if (g_contacts_state.current_mode == ContactsMode::Team)
    {
        if (is_visible(g_contacts_state.position_btn))
        {
            lv_group_add_obj(s_group, g_contacts_state.position_btn);
            attach_key_handler(g_contacts_state.position_btn);
            any = true;
        }
    }

    if (g_contacts_state.current_mode != ContactsMode::Broadcast &&
        g_contacts_state.current_mode != ContactsMode::Team)
    {
        if (is_visible(g_contacts_state.info_btn))
        {
            lv_group_add_obj(s_group, g_contacts_state.info_btn);
            attach_key_handler(g_contacts_state.info_btn);
            any = true;
        }
    }

    // ② Back (added last, not the default focus)
    if (is_visible(g_contacts_state.action_back_btn))
    {
        lv_group_add_obj(s_group, g_contacts_state.action_back_btn);
        attach_key_handler(g_contacts_state.action_back_btn);
        any = true;
    }

    if (!any)
    {
        s_col = FocusColumn::List;
        bind_list_column();
        return;
    }

    // Only allow action focus when a list item is selected
    if (g_contacts_state.selected_index < 0)
    {
        clear_action_focus_states();
        s_col = FocusColumn::List;
        bind_list_column();
        return;
    }

    // Default focus to the first action button (Chat) instead of Back
    if (is_valid(g_contacts_state.chat_btn))
    {
        focus_first_valid(g_contacts_state.chat_btn);
    }
    else if (is_valid(g_contacts_state.position_btn))
    {
        focus_first_valid(g_contacts_state.position_btn);
    }
    else if (is_valid(g_contacts_state.info_btn))
    {
        focus_first_valid(g_contacts_state.info_btn);
    }
    else if (is_valid(g_contacts_state.action_back_btn))
    {
        focus_first_valid(g_contacts_state.action_back_btn);
    }

    CONTACTS_LOG("[Contacts][Input] bind_action_column\n");
}

static void rebind_by_column()
{
    switch (s_col)
    {
    case FocusColumn::Filter:
        bind_filter_column(true);
        break;
    case FocusColumn::List:
        bind_list_column();
        break;
    case FocusColumn::Action:
        bind_action_column();
        break;
    }
}

/**
 * Encoder key handler (SPEC):
 * - Filter: ENTER -> List
 * - List:   ENTER on item -> Action
 *           ENTER on Prev/Next -> click (stay List)
 *           ENTER on Back -> Filter
 * - Action: ENTER -> click focused action button
 * - ESC/BACKSPACE: Action -> List -> Filter
 */
static void root_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);

    if (key == LV_KEY_BACKSPACE)
    {
        if (is_valid(g_contacts_state.top_bar.back_btn))
        {
            lv_obj_send_event(g_contacts_state.top_bar.back_btn, LV_EVENT_CLICKED, nullptr);
        }
        return;
    }

    if (!is_encoder_active()) return;

    // Back one column
    if (key == LV_KEY_ESC)
    {
        if (s_col == FocusColumn::Action) s_col = FocusColumn::List;
        else if (s_col == FocusColumn::List) s_col = FocusColumn::Filter;
        else s_col = FocusColumn::Filter;
        rebind_by_column();
        return;
    }

    if (key != LV_KEY_ENTER) return;

    lv_obj_t* focused = s_group ? lv_group_get_focused(s_group) : nullptr;

    if (s_col == FocusColumn::Filter)
    {
        s_col = FocusColumn::List;
        rebind_by_column();
        return;
    }

    if (s_col == FocusColumn::List)
    {
        if (focused == g_contacts_state.back_btn && is_valid(g_contacts_state.back_btn))
        {
            s_col = FocusColumn::Filter;
            rebind_by_column();
            return;
        }

        if (focused == g_contacts_state.prev_btn && is_valid(g_contacts_state.prev_btn))
        {
            if (g_contacts_state.prev_btn) lv_obj_send_event(g_contacts_state.prev_btn, LV_EVENT_CLICKED, nullptr);
            if (g_contacts_state.prev_btn) focus_first_valid(g_contacts_state.prev_btn);
            return;
        }
        if (focused == g_contacts_state.next_btn && is_valid(g_contacts_state.next_btn))
        {
            if (g_contacts_state.next_btn) lv_obj_send_event(g_contacts_state.next_btn, LV_EVENT_CLICKED, nullptr);
            if (g_contacts_state.next_btn) focus_first_valid(g_contacts_state.next_btn);
            return;
        }

        for (size_t i = 0; i < g_contacts_state.list_items.size(); ++i)
        {
            if (focused == g_contacts_state.list_items[i])
            {
                // selected_index stored in user_data by refresh_ui()
                g_contacts_state.selected_index = (int)(intptr_t)lv_obj_get_user_data(focused);
                s_col = FocusColumn::Action;
                rebind_by_column();
                return;
            }
        }
        return;
    }

    if (s_col == FocusColumn::Action)
    {
        if (focused)
        {
            lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
        }
        return;
    }
}

} // namespace

void init_contacts_input()
{
    if (s_group)
    {
        cleanup_contacts_input();
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
    else
    {
        CONTACTS_LOG("[Contacts][Input] WARNING: no encoder indev found\n");
    }
    s_col = FocusColumn::Filter;
    rebind_by_column();

    lv_obj_t* key_target = g_contacts_state.root
                               ? g_contacts_state.root
                               : (g_contacts_state.list_panel ? g_contacts_state.list_panel
                                                              : g_contacts_state.filter_panel);

    if (is_valid(key_target))
    {
        lv_obj_add_event_cb(key_target, root_key_event_cb, LV_EVENT_KEY, nullptr);
    }

    CONTACTS_LOG("[Contacts][Input] initialized: start in Filter column\n");
}

void cleanup_contacts_input()
{
    if (s_group)
    {

        // 只解绑我们自己绑定过的 encoder
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

    CONTACTS_LOG("[Contacts][Input] cleaned up\n");
}

void contacts_input_on_ui_refreshed()
{
    if (!s_group) return;
    rebind_by_column();
}

void contacts_focus_to_filter()
{
    if (!s_group) return;
    clear_action_focus_states();
    s_col = FocusColumn::Filter;
    rebind_by_column();
}

void contacts_focus_to_list()
{
    if (!s_group) return;
    clear_action_focus_states();
    s_col = FocusColumn::List;
    rebind_by_column();
}

void contacts_focus_to_action()
{
    if (!s_group) return;
    s_col = FocusColumn::Action;
    rebind_by_column();
}

lv_group_t* contacts_input_get_group()
{
    return s_group;
}
