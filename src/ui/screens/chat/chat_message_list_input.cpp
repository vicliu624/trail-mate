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

#include <Arduino.h>  // keep Arduino first if your build requires it
#include "chat_message_list_input.h"
#include "chat_message_list_components.h"
#include "../ui_common.h"

#define CHAT_INPUT_DEBUG 1
#if CHAT_INPUT_DEBUG
#define CHAT_INPUT_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CHAT_INPUT_LOG(...)
#endif

namespace chat::ui::message_list::input {

namespace {

enum class FocusColumn {
    Filter = 0,
    List = 1
};

static ChatMessageListScreen* s_screen = nullptr;
static lv_group_t* s_group = nullptr;
static lv_group_t* s_prev_group = nullptr;
static FocusColumn s_col = FocusColumn::Filter;

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

static void group_add_if_valid(lv_obj_t* obj)
{
    if (!s_group || !obj || !lv_obj_is_valid(obj)) return;
    lv_group_add_obj(s_group, obj);
}

static void clear_list_focus_states()
{
    if (!s_screen) return;
    lv_state_t clear_mask = (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    size_t count = s_screen->getItemCount();
    for (size_t i = 0; i < count; ++i) {
        if (lv_obj_t* btn = s_screen->getItemButton(i)) {
            if (lv_obj_is_valid(btn)) {
                lv_obj_clear_state(btn, clear_mask);
            }
        }
    }
    if (lv_obj_t* back = s_screen->getListBackButton()) {
        if (lv_obj_is_valid(back)) {
            lv_obj_clear_state(back, clear_mask);
        }
    }
}

static void bind_filter_column()
{
    if (!s_group || !s_screen) return;
    lv_group_focus_freeze(s_group, true);
    group_clear_all(s_group);
    clear_list_focus_states();

    if (lv_obj_t* back = s_screen->getBackButton()) {
        group_add_if_valid(back);
    }
    if (lv_obj_t* direct = s_screen->getDirectButton()) {
        group_add_if_valid(direct);
    }
    if (lv_obj_t* broadcast = s_screen->getBroadcastButton()) {
        group_add_if_valid(broadcast);
    }

    lv_group_focus_freeze(s_group, false);

    if (lv_obj_t* direct = s_screen->getDirectButton()) {
        if (lv_obj_has_state(direct, LV_STATE_CHECKED)) {
            focus_first_valid(direct);
            return;
        }
    }
    if (lv_obj_t* broadcast = s_screen->getBroadcastButton()) {
        if (lv_obj_has_state(broadcast, LV_STATE_CHECKED)) {
            focus_first_valid(broadcast);
            return;
        }
    }

    if (lv_obj_t* direct = s_screen->getDirectButton()) {
        focus_first_valid(direct);
    } else if (lv_obj_t* broadcast = s_screen->getBroadcastButton()) {
        focus_first_valid(broadcast);
    } else if (lv_obj_t* back = s_screen->getBackButton()) {
        focus_first_valid(back);
    }
}

static void bind_list_column()
{
    if (!s_group || !s_screen) return;
    lv_group_focus_freeze(s_group, true);
    group_clear_all(s_group);

    size_t count = s_screen->getItemCount();
    for (size_t i = 0; i < count; ++i) {
        if (lv_obj_t* btn = s_screen->getItemButton(i)) {
            group_add_if_valid(btn);
        }
    }
    if (lv_obj_t* back = s_screen->getListBackButton()) {
        group_add_if_valid(back);
    }

    lv_group_focus_freeze(s_group, false);

    if (count > 0) {
        int selected = s_screen->getSelectedIndex();
        if (selected >= 0 && static_cast<size_t>(selected) < count) {
            focus_first_valid(s_screen->getItemButton(static_cast<size_t>(selected)));
        } else {
            focus_first_valid(s_screen->getItemButton(0));
        }
    } else if (lv_obj_t* back = s_screen->getListBackButton()) {
        focus_first_valid(back);
    } else {
        s_col = FocusColumn::Filter;
        bind_filter_column();
    }
}

static void rebind_by_column()
{
    if (s_col == FocusColumn::Filter) {
        bind_filter_column();
    } else {
        bind_list_column();
    }
}

static void root_key_event_cb(lv_event_t* e)
{
    if (!is_encoder_active()) return;

    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) {
        s_col = FocusColumn::Filter;
        rebind_by_column();
        return;
    }

    if (key != LV_KEY_ENTER) return;

    lv_obj_t* focused = s_group ? lv_group_get_focused(s_group) : nullptr;
    if (!focused || !s_screen) return;

    if (s_col == FocusColumn::Filter) {
        if (focused == s_screen->getBackButton()) {
            lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
            return;
        }
        s_col = FocusColumn::List;
        rebind_by_column();
        return;
    }

    if (s_col == FocusColumn::List) {
        if (focused == s_screen->getListBackButton()) {
            s_col = FocusColumn::Filter;
            rebind_by_column();
            return;
        }
    }
}

} // namespace

void init(ChatMessageListScreen* screen)
{
    s_screen = screen;
    if (s_group) {
        cleanup();
    }
    s_group = lv_group_create();
    s_prev_group = lv_group_get_default();
    set_default_group(nullptr);
    s_col = FocusColumn::Filter;
    rebind_by_column();
    set_default_group(s_group);

    if (s_screen && s_screen->getObj()) {
        lv_obj_add_event_cb(s_screen->getObj(), root_key_event_cb, LV_EVENT_KEY, nullptr);
    }

    CHAT_INPUT_LOG("[ChatMessageListInput] init\n");
}

void cleanup()
{
    s_screen = nullptr;
    if (s_group) {
        set_default_group(nullptr);
        lv_group_del(s_group);
        s_group = nullptr;
    }
    if (s_prev_group) {
        set_default_group(s_prev_group);
    }
    s_prev_group = nullptr;
    CHAT_INPUT_LOG("[ChatMessageListInput] cleanup\n");
}

void on_ui_refreshed()
{
    if (!s_group) return;
    rebind_by_column();
}

void focus_filter()
{
    if (!s_group) return;
    s_col = FocusColumn::Filter;
    rebind_by_column();
}

void focus_list()
{
    if (!s_group) return;
    s_col = FocusColumn::List;
    rebind_by_column();
}

} // namespace chat::ui::input
