#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
#include "ui/input/cardputer_keyboard.h"
#include "../screens/music/music_state.h"
#include "ui/app_runtime.h"
#include "ui/input/cardputer_softkeys.h"
#include "ui/input/keyboard_shortcuts.h"
#include "ui/input/pager_navigation.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/menu/menu_layout.h"
#include "ui/menu/menu_runtime.h"
#include "ui/screens/calculator/calculator_page_runtime.h"
#include "ui/screens/tracker/tracker_page_input.h"
#include "ui/screens/tracker/tracker_state.h"
#include "ui/ui_common.h"
#include "ui/widgets/ime/ime_widget.h"
#include <cstring>
#include <new>

namespace ui::input::cardputer
{
namespace
{
bool editor(lv_obj_t* focused)
{
    if (::ui::widgets::ime_owns_text_focus(focused)) return true;
    for (auto* object = focused; object; object = lv_obj_get_parent(object))
        if (lv_obj_check_type(object, &lv_textarea_class) || lv_obj_check_type(object, &lv_spinbox_class) ||
            (lv_obj_check_type(object, &lv_dropdown_class) && lv_dropdown_is_open(object))) return true;
    return false;
}
bool click(lv_obj_t* object)
{
    if (!object || !lv_obj_is_valid(object) || lv_obj_has_state(object, LV_STATE_DISABLED)) return false;
    if (lv_obj_get_group(object)) lv_group_focus_obj(object);
    lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
    return true;
}
bool tracker_action(uint32_t key)
{
    auto& page = ::tracker::ui::g_tracker_state;
    if (page.action_menu_modal || page.del_confirm_modal || page.route_preview_page || page.route_preview_help_modal) return false;
    if (key == '4' || key == '5' || key == '6')
    {
        ::tracker::ui::input::tracker_focus_to_filter();
        click(key == '6' ? page.mode_route_btn : page.mode_record_btn);
        ::tracker::ui::input::tracker_focus_to_list();
        if (key == '4') click(page.start_stop_btn);
        return true;
    }
    if (key == '7')
    {
        ::tracker::ui::input::tracker_focus_to_list();
        return click(page.card_actions);
    }
    if (key == '8')
    {
        if (page.filter_panel && !lv_obj_has_flag(page.filter_panel, LV_OBJ_FLAG_HIDDEN)) ::tracker::ui::input::tracker_focus_to_list();
        else ::tracker::ui::input::tracker_focus_to_filter();
        return true;
    }
    return false;
}
bool music_action(uint32_t key)
{
    auto* page = ::ui::music_page::state();
    if (!page || page->view == ::ui::music_page::View::NewPlaylist || page->view == ::ui::music_page::View::ConfirmDelete || page->menu.is_open()) return false;
    if (key == '4') return ::ui::music_page::command(::music::Action::Previous);
    if (key == '5') return ::ui::music_page::command(::music::Action::TogglePause);
    if (key == '6') return ::ui::music_page::command(::music::Action::Next);
    if (key == '7')
    {
        page->view = ::ui::music_page::View::Library;
        ::ui::music_page::browse(::music::Browse::Songs);
        return true;
    }
    if (key == '8')
    {
        page->menu.open();
        return true;
    }
    return false;
}
} // namespace
namespace softkeys
{
namespace
{
struct Row
{
    lv_obj_t* bar = nullptr;
    lv_obj_t* buttons[5]{};
    Row* next = nullptr;
};
Row* rows = nullptr;

Row* find_row(lv_obj_t* bar)
{
    for (auto* row = rows; row; row = row->next)
        if (row->bar == bar) return row;
    return nullptr;
}

void layout(Row& row)
{
    const auto width = lv_obj_get_content_width(row.bar);
    const auto height = lv_obj_get_content_height(row.bar);
    if (width <= 0 || height <= 0) return;
    for (int slot = 0; slot < 5; ++slot)
    {
        auto* button = row.buttons[slot];
        if (!button) continue;
        lv_obj_set_size(button, width * 56 / 320, 20);
        lv_obj_set_pos(button, width * (20 + 60 * slot) / 320, (height - 20) / 2);
    }
}

void button_deleted(lv_event_t* event)
{
    auto* row = static_cast<Row*>(lv_event_get_user_data(event));
    for (auto*& button : row->buttons)
        if (button == lv_event_get_target_obj(event)) button = nullptr;
}

void row_event(lv_event_t* event)
{
    auto* row = static_cast<Row*>(lv_event_get_user_data(event));
    if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED)
    {
        layout(*row);
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_DELETE) return;
    for (auto* button : row->buttons)
        if (button) lv_obj_remove_event_cb_with_user_data(button, button_deleted, row);
    auto** link = &rows;
    while (*link && *link != row) link = &(*link)->next;
    if (*link) *link = row->next;
    delete row;
}
} // namespace

void install_row(lv_obj_t* bar)
{
    if (!bar || find_row(bar)) return;
    auto* row = new (std::nothrow) Row;
    if (!row) return;
    row->bar = bar;
    row->next = rows;
    rows = row;
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(bar, row_event, LV_EVENT_ALL, row);
}

void bind_button(lv_obj_t* bar, lv_obj_t* button, int slot)
{
    auto* row = find_row(bar);
    if (!row || !button) return;
    if (slot < 0)
    {
        for (slot = 0; slot < 5 && row->buttons[slot]; ++slot)
        {
        }
    }
    if (slot < 0 || slot >= 5 || (row->buttons[slot] && row->buttons[slot] != button)) return;
    if (row->buttons[slot] == button) return;
    row->buttons[slot] = button;
    lv_obj_add_flag(button, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_flex_grow(button, 0);
    lv_obj_set_style_pad_all(button, 1, LV_PART_MAIN);
    if (auto* label = lv_obj_get_child(button, 0))
    {
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_center(label);
    }
    lv_obj_add_event_cb(button, button_deleted, LV_EVENT_DELETE, row);
    layout(*row);
}

bool activate(uint32_t key, lv_group_t* group)
{
    if (key < '4' || key > '8' || !group || ui_is_overlay_active()) return false;
    auto* focused = lv_group_get_focused(group);
    for (auto* row = rows; row; row = row->next)
    {
        if (!lv_obj_is_visible(row->bar)) continue;
        auto* root = lv_obj_get_parent(row->bar);
        bool owns_focus = false;
        for (auto* object = focused; object; object = lv_obj_get_parent(object))
            if (object == root) owns_focus = true;
        // A page's Back button can be a sibling of its content viewport. Its
        // visible footer still belongs to the same active input group.
        if (!owns_focus)
            for (auto* candidate : row->buttons)
                if (candidate && lv_obj_get_group(candidate) == group) owns_focus = true;
        auto* button = row->buttons[key - '4'];
        if (owns_focus && button && lv_obj_is_visible(button))
        {
            (void)click(button);
            return true;
        }
    }
    return false;
}
} // namespace softkeys

bool handle_key(uint32_t& key, bool pressed, lv_group_t* group, State& state)
{
    if (!pressed)
    {
        key = state.translated;
        return state.consumed;
    }
    state.translated = key;
    state.consumed = false;
    if (!group) return false;
    if (::ui::menu_runtime::currentScene() == ::ui::menu_runtime::Scene::Sleeping) return false;
    auto* focused = group ? lv_group_get_focused(group) : nullptr;
    auto* app = ui_get_active_app();
    const char* id = app ? app->stable_id() : "";
    const bool calculator = !std::strcmp(id, "calculator");
    if (!calculator || !state.calculator_active) state.calculator_controls = false;
    state.calculator_active = calculator;
    if (calculator)
    {
        if (key == LV_KEY_NEXT || key == LV_KEY_PREV) state.calculator_controls = true;
        else if ((key >= '0' && key <= '9') || key == '+' || key == '-' || key == '*' || key == '/' ||
                 key == '=' || key == '.' || key == '(' || key == ')' || key == LV_KEY_BACKSPACE)
            state.calculator_controls = false;
        if (state.calculator_controls && key == LV_KEY_ENTER)
        {
            state.consumed = ::calculator::ui::runtime::activate_control(focused);
            return state.consumed;
        }
    }
    const bool typing = editor(focused) || (calculator && !state.calculator_controls);
    if (typing) return false;
    if (::ui::input::pager::help_open()) return false;
    if (softkeys::activate(key, group))
    {
        state.consumed = true;
        return true;
    }
    if (key == LV_KEY_HOME || key == LV_KEY_END)
    {
        const auto count = lv_group_get_obj_count(group);
        for (uint32_t step = 0; step < count; ++step)
        {
            auto* object = lv_group_get_obj_by_index(group, key == LV_KEY_HOME ? step : count - step - 1);
            if (!lv_obj_is_visible(object) || lv_obj_has_state(object, LV_STATE_DISABLED)) continue;
            lv_group_focus_obj(object);
            lv_obj_scroll_to_view(object, LV_ANIM_OFF);
            break;
        }
        state.consumed = true;
        return true;
    }
    const bool launcher = !app && ::ui::menu_runtime::currentScene() == ::ui::menu_runtime::Scene::Menu &&
                          group == menu_g && !::ui::menu::dashboard::isFeedOpen() && !ui_is_overlay_active();
    if (launcher && key == kHelpKey) key = 'h';
    if (launcher && key >= '4' && key <= '8')
    {
        const char* applications[] = {"chat", "map", "tracker", "contacts", "settings"};
        state.consumed = ::ui::menu_layout::launchAppByStableId(applications[key - '4']);
    }
    else if (key >= '4' && key <= '8' && !std::strcmp(id, "music")) state.consumed = music_action(key);
    else if (key >= '4' && key <= '8' && !std::strcmp(id, "tracker"))
    {
        auto& page = ::tracker::ui::g_tracker_state;
        if (page.route_preview_page && !page.route_preview_help_modal && !page.action_menu_modal && !page.del_confirm_modal)
        {
            const uint32_t actions[] = {'q', 'e', 'f', 'l', 'h'};
            key = actions[key - '4'];
        }
        else state.consumed = tracker_action(key);
    }
    else if (key >= '4' && key <= '8' && !std::strcmp(id, "map") && ::ui::input::pager::page_owns_focus(group))
    {
        const uint32_t actions[] = {'q', 'e', 'c', 'l', 'm'};
        key = actions[key - '4'];
    }
    else if (!calculator || !lv_group_get_editing(group))
    {
        if (key == 'f' || key == 'F') key = LV_KEY_UP;
        else if (key == 'z' || key == 'Z') key = LV_KEY_LEFT;
        else if (key == 'x' || key == 'X') key = LV_KEY_DOWN;
        else if (key == 'c' || key == 'C') key = LV_KEY_RIGHT;
    }
    if (state.consumed)
    {
        state.translated = key;
        return true;
    }
    const bool arrow = key == LV_KEY_UP || key == LV_KEY_DOWN || key == LV_KEY_LEFT || key == LV_KEY_RIGHT;
    const bool button = focused && (lv_obj_check_type(focused, &lv_button_class) || lv_obj_check_type(focused, &lv_checkbox_class));
    if (app && !state.consumed && arrow && button &&
        (!::ui::input::pager::page_owns_focus(group) || !std::strcmp(id, "music") || (calculator && !lv_group_get_editing(group))))
    {
        if (key == LV_KEY_UP || key == LV_KEY_LEFT) lv_group_focus_prev(group);
        else lv_group_focus_next(group);
        if (auto* next = lv_group_get_focused(group)) lv_obj_scroll_to_view(next, LV_ANIM_OFF);
        state.consumed = true;
    }
    state.translated = key;
    return state.consumed;
}
} // namespace ui::input::cardputer
#endif
