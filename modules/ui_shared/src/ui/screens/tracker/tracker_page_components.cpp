#include "ui/screens/tracker/tracker_page_components.h"
#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/route_storage.h"
#include "platform/ui/tracker_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/screens/tracker/tracker_page_input.h"
#include "ui/screens/tracker/tracker_page_layout.h"
#include "ui/screens/tracker/tracker_state.h"
#include "ui/ui_common.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace tracker
{
namespace ui
{
namespace components
{

namespace
{
constexpr int kBottomBarButtonWidth = 70;
constexpr uintptr_t kListUserDataOffset = 1;
static constexpr intptr_t kBackListItemUserData = -2;
std::vector<std::string> s_route_names;
std::vector<std::string> s_record_names;
std::string s_record_empty_text = "No tracks yet";
std::string s_route_empty_text = "No KML routes";

constexpr uint32_t kPanelBtnBg = 0xFAF0D8;
constexpr uint32_t kPanelBtnBorder = 0xE7C98F;
constexpr uint32_t kPanelBtnFocused = 0xEBA341;
constexpr uint32_t kPanelBtnText = 0x6B4A1E;
constexpr uint32_t kPanelTextMuted = 0x8A6A3A;

bool s_btn_styles_inited = false;
lv_style_t s_btn_main;
lv_style_t s_btn_focused;
lv_style_t s_btn_disabled;
lv_style_t s_btn_label;

enum class ActionMenuCommand : uintptr_t
{
    Load = 1,
    Unload,
    Delete,
    Cancel,
};

lv_group_t* tracker_group();
const ::ui::page_profile::PageLayoutProfile& page_profile();
lv_coord_t filter_panel_width();
lv_coord_t filter_button_height();
lv_coord_t list_item_height();
lv_coord_t bottom_bar_button_width();
lv_coord_t modal_button_width();
lv_coord_t action_menu_button_height();
lv_coord_t action_menu_row_gap();
void update_record_status();
void update_start_stop_button();
void update_record_page();
void update_route_status();
void update_route_page();
bool can_delete_selected_item();
bool can_load_selected_route();
bool can_unload_active_route();
bool is_any_modal_open();
void on_route_load_clicked(lv_event_t* e);
void on_route_unload_clicked(lv_event_t* e);
void open_delete_confirm_modal();
void on_list_item_clicked(lv_event_t* e);
void on_list_item_focused(lv_event_t* e);
void on_list_item_defocused(lv_event_t* e);
void on_backspace_key(lv_event_t* e);
lv_obj_t* create_action_menu_button(lv_obj_t* parent, const char* text);
void on_action_menu_key(lv_event_t* e);
void on_action_menu_item_clicked(lv_event_t* e);
void open_action_menu_modal();
void clear_list_items();
lv_obj_t* create_list_item_button(const std::string& text, intptr_t user_data, bool checked, bool disabled);
void append_back_list_item();

void on_back(void*)
{
    ui_request_exit_to_menu();
}

const ::ui::page_profile::PageLayoutProfile& page_profile()
{
    return ::ui::page_profile::current();
}

lv_coord_t filter_panel_width()
{
    return page_profile().filter_panel_width;
}

lv_coord_t filter_button_height()
{
    return page_profile().filter_button_height;
}

lv_coord_t list_item_height()
{
    return page_profile().list_item_height;
}

lv_coord_t bottom_bar_button_width()
{
    return page_profile().large_touch_hitbox ? 96 : kBottomBarButtonWidth;
}

lv_coord_t modal_button_width()
{
    return ::ui::page_profile::resolve_control_button_min_width();
}

lv_coord_t action_menu_button_height()
{
    return page_profile().filter_button_height;
}

lv_coord_t action_menu_row_gap()
{
    return page_profile().large_touch_hitbox ? 6 : 4;
}

void on_backspace_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_BACKSPACE)
    {
        return;
    }
    if (g_tracker_state.top_bar.back_btn)
    {
        lv_obj_send_event(g_tracker_state.top_bar.back_btn, LV_EVENT_CLICKED, nullptr);
        return;
    }
    on_back(nullptr);
}

void modal_prepare_group()
{
    auto& state = g_tracker_state;
    if (!state.modal_group)
    {
        state.modal_group = lv_group_create();
    }
    lv_group_remove_all_objs(state.modal_group);
    state.prev_group = lv_group_get_default();
    lv_group_t* group = tracker_group();
    if (group && state.prev_group != group)
    {
        state.prev_group = group;
    }
    set_default_group(state.modal_group);
}

void modal_restore_group()
{
    auto& state = g_tracker_state;
    lv_group_t* restore = state.prev_group ? state.prev_group : tracker_group();
    if (restore)
    {
        set_default_group(restore);
    }
    state.prev_group = nullptr;
}

lv_obj_t* create_modal_root(int width, int height)
{
    lv_obj_t* screen = lv_screen_active();
    lv_coord_t screen_w = lv_obj_get_width(screen);
    lv_coord_t screen_h = lv_obj_get_height(screen);

    lv_obj_t* bg = lv_obj_create(screen);
    lv_obj_set_size(bg, screen_w, screen_h);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x3A2A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    const auto modal_size = ::ui::page_profile::resolve_modal_size(width, height, screen);
    lv_obj_t* win = lv_obj_create(bg);
    lv_obj_set_size(win, modal_size.width, modal_size.height);
    lv_obj_center(win);
    lv_obj_set_style_bg_color(win, lv_color_hex(0xFFF7E9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_hex(0xD9B06A), LV_PART_MAIN);
    lv_obj_set_style_radius(win, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, ::ui::page_profile::resolve_modal_pad(), LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    return bg;
}

void modal_close(lv_obj_t*& modal_obj)
{
    if (modal_obj)
    {
        lv_obj_del(modal_obj);
        modal_obj = nullptr;
    }
    modal_restore_group();
}

bool is_any_modal_open()
{
    return g_tracker_state.del_confirm_modal != nullptr ||
           g_tracker_state.action_menu_modal != nullptr;
}

void init_button_styles()
{
    if (s_btn_styles_inited)
    {
        return;
    }
    lv_style_init(&s_btn_main);
    lv_style_set_bg_color(&s_btn_main, lv_color_hex(kPanelBtnBg));
    lv_style_set_bg_opa(&s_btn_main, LV_OPA_COVER);
    lv_style_set_border_width(&s_btn_main, 1);
    lv_style_set_border_color(&s_btn_main, lv_color_hex(kPanelBtnBorder));
    lv_style_set_radius(&s_btn_main, 6);
    lv_style_set_text_color(&s_btn_main, lv_color_hex(kPanelBtnText));

    lv_style_init(&s_btn_focused);
    lv_style_set_bg_color(&s_btn_focused, lv_color_hex(kPanelBtnFocused));
    lv_style_set_bg_opa(&s_btn_focused, LV_OPA_COVER);
    lv_style_set_outline_width(&s_btn_focused, 0);

    lv_style_init(&s_btn_disabled);
    lv_style_set_bg_opa(&s_btn_disabled, LV_OPA_50);

    lv_style_init(&s_btn_label);
    lv_style_set_text_color(&s_btn_label, lv_color_hex(kPanelBtnText));
    lv_style_set_text_font(&s_btn_label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()));

    s_btn_styles_inited = true;
}

void apply_action_button(lv_obj_t* btn, lv_obj_t* label)
{
    if (!btn)
    {
        return;
    }
    init_button_styles();
    lv_obj_add_style(btn, &s_btn_main, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_btn_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_style(btn, &s_btn_disabled, LV_PART_MAIN | LV_STATE_DISABLED);
    if (label)
    {
        lv_obj_add_style(label, &s_btn_label, LV_PART_MAIN);
    }
}

void apply_list_button(lv_obj_t* btn)
{
    if (!btn)
    {
        return;
    }
    init_button_styles();
    lv_obj_add_style(btn, &s_btn_main, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_btn_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_style(btn, &s_btn_focused, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_add_style(btn, &s_btn_disabled, LV_PART_MAIN | LV_STATE_DISABLED);
    if (lv_obj_t* label = lv_obj_get_child(btn, -1))
    {
        lv_obj_add_style(label, &s_btn_label, LV_PART_MAIN);
    }
}

void style_mode_button(lv_obj_t* btn, lv_obj_t* label, bool active)
{
    if (!btn)
    {
        return;
    }
    lv_color_t bg = active ? lv_color_hex(0xEBA341) : lv_color_hex(0xFAF0D8);
    lv_color_t fg = lv_color_hex(0x6B4A1E);
    lv_obj_set_style_bg_color(btn, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xE7C98F), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
    if (label)
    {
        lv_obj_set_style_text_color(label, fg, LV_PART_MAIN);
    }
}

void update_mode_buttons()
{
    auto& state = g_tracker_state;
    bool record_active = (state.mode == TrackerPageState::Mode::Record);
    style_mode_button(state.mode_record_btn, state.mode_record_label, record_active);
    style_mode_button(state.mode_route_btn, state.mode_route_label, !record_active);
}

std::string path_basename(const std::string& path)
{
    const char* base = strrchr(path.c_str(), '/');
    if (base && base[1] != '\0')
    {
        return std::string(base + 1);
    }
    return path;
}

bool can_delete_selected_item()
{
    auto& state = g_tracker_state;
    bool can_delete = false;
    if (state.mode == TrackerPageState::Mode::Record)
    {
        if (state.selected_record_idx >= 0 &&
            state.selected_record_idx < static_cast<int>(s_record_names.size()))
        {
            can_delete = true;
            if (platform::ui::tracker::is_recording())
            {
                std::string current;
                platform::ui::tracker::current_path(current);
                std::string base = path_basename(current);
                if (base == s_record_names[state.selected_record_idx])
                {
                    can_delete = false;
                }
            }
        }
    }
    else
    {
        if (state.selected_route_idx >= 0 &&
            state.selected_route_idx < static_cast<int>(s_route_names.size()))
        {
            can_delete = true;
        }
    }
    return can_delete;
}

bool can_load_selected_route()
{
    auto& state = g_tracker_state;
    return state.mode == TrackerPageState::Mode::Route &&
           state.active_route.empty() &&
           state.selected_route_idx >= 0 &&
           !state.selected_route.empty();
}

bool can_unload_active_route()
{
    auto& state = g_tracker_state;
    return state.mode == TrackerPageState::Mode::Route &&
           !state.active_route.empty();
}

lv_group_t* tracker_group()
{
    if (lv_group_t* group = tracker::ui::input::tracker_input_get_group())
    {
        return group;
    }
    return ::app_g;
}

void clear_list_items()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }
    while (lv_obj_get_child_count(state.list_container) > 0)
    {
        lv_obj_del(lv_obj_get_child(state.list_container, 0));
    }
}

lv_obj_t* create_list_item_button(const std::string& text, intptr_t user_data, bool checked, bool disabled)
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return nullptr;
    }

    lv_obj_t* btn = lv_btn_create(state.list_container);
    lv_obj_set_size(btn, LV_PCT(100), list_item_height());
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    apply_list_button(btn);

    lv_obj_t* label = lv_label_create(btn);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
    lv_label_set_text(label, text.c_str());
    lv_obj_add_style(label, &s_btn_label, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, LV_PCT(100));

    lv_obj_set_user_data(btn, reinterpret_cast<void*>(user_data));
    if (checked)
    {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(btn, LV_STATE_CHECKED);
    }
    if (disabled)
    {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_clear_state(btn, LV_STATE_DISABLED);
    }

    lv_obj_add_event_cb(btn, on_list_item_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btn, on_list_item_focused, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(btn, on_list_item_defocused, LV_EVENT_DEFOCUSED, nullptr);
    return btn;
}

void append_back_list_item()
{
    create_list_item_button(::ui::i18n::tr("Back"), kBackListItemUserData, false, false);
}

void sync_list_item_checked_states()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }

    const int selected_idx =
        (state.mode == TrackerPageState::Mode::Record) ? state.selected_record_idx : state.selected_route_idx;
    const uint32_t child_count = lv_obj_get_child_count(state.list_container);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* btn = lv_obj_get_child(state.list_container, index);
        if (!btn)
        {
            continue;
        }
        const intptr_t raw = reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn));
        const bool checked = raw >= static_cast<intptr_t>(kListUserDataOffset) &&
                             static_cast<int>(raw - static_cast<intptr_t>(kListUserDataOffset)) == selected_idx;
        if (checked)
        {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_clear_state(btn, LV_STATE_CHECKED);
        }
    }
}

lv_obj_t* first_visible_list_item()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return state.start_stop_btn;
    }

    const uint32_t child_count = lv_obj_get_child_count(state.list_container);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* btn = lv_obj_get_child(state.list_container, index);
        if (btn && lv_obj_has_state(btn, LV_STATE_CHECKED) &&
            !lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN) &&
            !lv_obj_has_state(btn, LV_STATE_DISABLED))
        {
            return btn;
        }
    }
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* btn = lv_obj_get_child(state.list_container, index);
        if (btn && !lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN) &&
            !lv_obj_has_state(btn, LV_STATE_DISABLED))
        {
            return btn;
        }
    }
    return state.start_stop_btn;
}

void focus_mode_panel()
{
    tracker::ui::input::tracker_focus_to_filter();
}

void focus_main_panel()
{
    tracker::ui::input::tracker_focus_to_list();
}

void set_mode(TrackerPageState::Mode mode)
{
    auto& state = g_tracker_state;
    state.mode = mode;
    update_mode_buttons();
    if (state.bottom_bar)
    {
        if (mode == TrackerPageState::Mode::Record)
        {
            lv_obj_clear_flag(state.bottom_bar, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(state.bottom_bar, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (state.start_stop_btn)
    {
        if (mode == TrackerPageState::Mode::Record)
        {
            lv_obj_clear_flag(state.start_stop_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(state.start_stop_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (mode == TrackerPageState::Mode::Record)
    {
        update_record_status();
        update_start_stop_button();
        update_record_page();
    }
    else
    {
        update_route_status();
        update_route_page();
    }
    if (state.focus_col == TrackerPageState::FocusColumn::Main)
    {
        tracker::ui::input::tracker_input_on_ui_refreshed();
    }
}

void update_record_status()
{
    auto& state = g_tracker_state;
    if (!state.status_label)
    {
        return;
    }
    const bool recording = platform::ui::tracker::is_recording();
    if (state.mode == TrackerPageState::Mode::Record)
    {
        ::ui::i18n::set_label_text(state.status_label, recording ? "Recording" : "Stopped");
    }
}

void update_start_stop_button()
{
    auto& state = g_tracker_state;
    const bool recording = platform::ui::tracker::is_recording();
    if (state.start_stop_label)
    {
        ::ui::i18n::set_label_text(state.start_stop_label, recording ? "Stop" : "New");
    }
}

size_t utf8_count_chars(const std::string& text)
{
    size_t count = 0;
    const size_t len = text.length();
    size_t i = 0;
    while (i < len)
    {
        uint8_t c = static_cast<uint8_t>(text[i]);
        size_t advance = 1;
        if ((c & 0x80) == 0x00)
        {
            advance = 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            advance = 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            advance = 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            advance = 4;
        }
        i += advance;
        ++count;
    }
    return count;
}

std::string utf8_truncate(const std::string& text, size_t max_chars)
{
    if (max_chars == 0)
    {
        return std::string("...");
    }
    size_t count = 0;
    const size_t len = text.length();
    size_t i = 0;
    while (i < len)
    {
        uint8_t c = static_cast<uint8_t>(text[i]);
        size_t advance = 1;
        if ((c & 0x80) == 0x00)
        {
            advance = 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            advance = 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            advance = 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            advance = 4;
        }
        if (count + 1 > max_chars)
        {
            break;
        }
        i += advance;
        ++count;
    }
    if (i >= len)
    {
        return text;
    }
    std::string out = text.substr(0, i);
    out += "...";
    return out;
}

std::string format_list_name(const std::string& name)
{
    if (utf8_count_chars(name) <= 20)
    {
        return name;
    }
    return utf8_truncate(name, 20);
}

void update_record_page()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }

    const lv_coord_t scroll_y = lv_obj_get_scroll_y(state.list_container);
    clear_list_items();

    if (s_record_names.empty())
    {
        create_list_item_button(s_record_empty_text.c_str(), 0, false, true);
    }
    else
    {
        for (size_t index = 0; index < s_record_names.size(); ++index)
        {
            create_list_item_button(format_list_name(s_record_names[index]),
                                    static_cast<intptr_t>(index) + kListUserDataOffset,
                                    static_cast<int>(index) == state.selected_record_idx,
                                    false);
        }
    }
    append_back_list_item();

    lv_obj_add_flag(state.list_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(state.list_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state.list_container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_scroll_to_y(state.list_container, scroll_y, LV_ANIM_OFF);
    if (state.focus_col == TrackerPageState::FocusColumn::Main && !is_any_modal_open())
    {
        tracker::ui::input::tracker_input_on_ui_refreshed();
    }
}

void refresh_record_list()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }
    s_record_names.clear();
    s_record_empty_text = ::ui::i18n::tr("No tracks yet");

    if (!platform::ui::device::sd_ready())
    {
        s_record_empty_text = ::ui::i18n::tr("No SD Card");
        if (state.mode == TrackerPageState::Mode::Record)
        {
            update_record_page();
        }
        return;
    }

    constexpr size_t kMaxTracks = 32;
    platform::ui::tracker::list_tracks(s_record_names, kMaxTracks);
    if (state.selected_record_idx >= static_cast<int>(s_record_names.size()))
    {
        state.selected_record_idx = -1;
        state.selected_record.clear();
    }
    if (state.mode == TrackerPageState::Mode::Record)
    {
        update_record_page();
    }
}

void update_route_status()
{
    auto& state = g_tracker_state;
    if (!state.status_label)
    {
        return;
    }
    lv_obj_set_style_text_font(
        state.status_label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    if (state.mode == TrackerPageState::Mode::Route)
    {
        if (!state.active_route.empty())
        {
            const std::string text = ::ui::i18n::format("Active: %s", state.active_route.c_str());
            ::ui::i18n::set_label_text_raw(state.status_label, text.c_str());
        }
        else if (!state.selected_route.empty())
        {
            const std::string text = ::ui::i18n::format("Selected: %s", state.selected_route.c_str());
            ::ui::i18n::set_label_text_raw(state.status_label, text.c_str());
        }
        else
        {
            ::ui::i18n::set_label_text(state.status_label, "No route selected");
        }
    }
}

void update_route_page()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }

    const lv_coord_t scroll_y = lv_obj_get_scroll_y(state.list_container);
    clear_list_items();

    if (s_route_names.empty())
    {
        create_list_item_button(s_route_empty_text.c_str(), 0, false, true);
    }
    else
    {
        for (size_t index = 0; index < s_route_names.size(); ++index)
        {
            create_list_item_button(format_list_name(s_route_names[index]),
                                    static_cast<intptr_t>(index) + kListUserDataOffset,
                                    static_cast<int>(index) == state.selected_route_idx,
                                    false);
        }
    }
    append_back_list_item();

    lv_obj_add_flag(state.list_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(state.list_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state.list_container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_scroll_to_y(state.list_container, scroll_y, LV_ANIM_OFF);
    if (state.focus_col == TrackerPageState::FocusColumn::Main && !is_any_modal_open())
    {
        tracker::ui::input::tracker_input_on_ui_refreshed();
    }
}

void refresh_route_list()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }
    s_route_names.clear();
    state.selected_route_idx = -1;
    state.selected_route.clear();
    s_route_empty_text = ::ui::i18n::tr("No KML routes");

    if (!platform::ui::device::sd_ready())
    {
        s_route_empty_text = ::ui::i18n::tr("No SD Card");
        if (state.mode == TrackerPageState::Mode::Route)
        {
            update_route_page();
        }
        return;
    }

    const bool route_dir_ready = platform::ui::route_storage::list_routes(s_route_names, 64);
    if (!route_dir_ready)
    {
        s_route_empty_text = ::ui::i18n::tr("No routes folder");
        if (state.mode == TrackerPageState::Mode::Route)
        {
            update_route_page();
        }
        return;
    }

    if (s_route_names.empty())
    {
        if (state.mode == TrackerPageState::Mode::Route)
        {
            update_route_page();
        }
        return;
    }

    std::sort(s_route_names.begin(), s_route_names.end());
    if (state.mode == TrackerPageState::Mode::Route)
    {
        update_route_page();
    }
}

void sync_active_route_from_config()
{
    auto& state = g_tracker_state;
    app::IAppFacade& app_ctx = app::appFacade();
    const auto& cfg = app_ctx.getConfig();
    if (cfg.route_enabled && cfg.route_path[0] != '\0')
    {
        const char* base = strrchr(cfg.route_path, '/');
        if (base && base[1] != '\0')
        {
            state.active_route = base + 1;
        }
        else
        {
            state.active_route = cfg.route_path;
        }
    }
    else
    {
        state.active_route.clear();
    }
}

void on_start_stop_clicked(lv_event_t*)
{
    if (platform::ui::tracker::is_recording())
    {
        platform::ui::tracker::stop_recording();
    }
    else
    {
        platform::ui::tracker::start_recording();
    }
    update_record_status();
    update_start_stop_button();
    refresh_record_list();
}

void on_mode_record_clicked(lv_event_t*)
{
    set_mode(TrackerPageState::Mode::Record);
    focus_main_panel();
}

void on_mode_route_clicked(lv_event_t*)
{
    set_mode(TrackerPageState::Mode::Route);
    focus_main_panel();
}

void on_list_item_clicked(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    auto& state = g_tracker_state;
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    void* ud = lv_obj_get_user_data(target);
    if (!ud)
    {
        return;
    }
    const intptr_t raw = reinterpret_cast<intptr_t>(ud);
    if (raw == kBackListItemUserData)
    {
        if (state.mode == TrackerPageState::Mode::Record)
        {
            state.selected_record_idx = -1;
            state.selected_record.clear();
            update_record_status();
        }
        else
        {
            state.selected_route_idx = -1;
            state.selected_route.clear();
            update_route_status();
        }
        sync_list_item_checked_states();
        focus_mode_panel();
        return;
    }
    if (raw < static_cast<intptr_t>(kListUserDataOffset))
    {
        return;
    }
    const size_t idx = static_cast<size_t>(raw - static_cast<intptr_t>(kListUserDataOffset));

    if (state.mode == TrackerPageState::Mode::Record)
    {
        if (idx >= s_record_names.size())
        {
            return;
        }
        state.selected_record_idx = static_cast<int>(idx);
        state.selected_record = s_record_names[idx].c_str();
        update_record_status();
    }
    else
    {
        if (idx >= s_route_names.size())
        {
            return;
        }
        state.selected_route_idx = static_cast<int>(idx);
        state.selected_route = s_route_names[idx].c_str();
        update_route_status();
    }
    sync_list_item_checked_states();
    open_action_menu_modal();
}

lv_obj_t* create_action_menu_button(lv_obj_t* parent, const char* text)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), action_menu_button_height());

    lv_obj_t* label = lv_label_create(btn);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_center(label);
    apply_action_button(btn, label);
    return btn;
}

void on_action_menu_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        modal_close(g_tracker_state.action_menu_modal);
        focus_main_panel();
    }
}

void on_action_menu_item_clicked(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    const auto cmd = static_cast<ActionMenuCommand>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));

    modal_close(g_tracker_state.action_menu_modal);

    switch (cmd)
    {
    case ActionMenuCommand::Load:
        on_route_load_clicked(nullptr);
        focus_main_panel();
        break;
    case ActionMenuCommand::Unload:
        on_route_unload_clicked(nullptr);
        focus_main_panel();
        break;
    case ActionMenuCommand::Delete:
        open_delete_confirm_modal();
        break;
    case ActionMenuCommand::Cancel:
    default:
        focus_main_panel();
        break;
    }
}

void open_action_menu_modal()
{
    auto& state = g_tracker_state;
    if (is_any_modal_open())
    {
        return;
    }

    bool has_item = false;
    int action_count = 1; // Cancel
    if (state.mode == TrackerPageState::Mode::Record)
    {
        has_item = state.selected_record_idx >= 0 &&
                   state.selected_record_idx < static_cast<int>(s_record_names.size());
        if (can_delete_selected_item())
        {
            ++action_count;
        }
    }
    else
    {
        has_item = state.selected_route_idx >= 0 &&
                   state.selected_route_idx < static_cast<int>(s_route_names.size());
        if (can_load_selected_route())
        {
            ++action_count;
        }
        if (can_unload_active_route())
        {
            ++action_count;
        }
        if (can_delete_selected_item())
        {
            ++action_count;
        }
    }

    if (!has_item)
    {
        return;
    }

    modal_prepare_group();
    const int max_modal_h = page_profile().large_touch_hitbox ? 320 : 220;
    const int modal_content_h = 56 + static_cast<int>(action_count) *
                                         static_cast<int>(action_menu_button_height() + action_menu_row_gap());
    const int modal_h = std::min<int>(max_modal_h, modal_content_h);
    state.action_menu_modal = create_modal_root(190, modal_h);
    lv_obj_t* win = lv_obj_get_child(state.action_menu_modal, 0);
    if (!win)
    {
        modal_close(state.action_menu_modal);
        return;
    }

    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(win, 4, LV_PART_MAIN);

    lv_obj_t* title_label = lv_label_create(win);
    const char* title = (state.mode == TrackerPageState::Mode::Record) ? "Track Actions" : "Route Actions";
    ::ui::i18n::set_label_text(title_label, title);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* list = lv_obj_create(win);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, 0);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, action_menu_row_gap(), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* first_focus = nullptr;
    auto add_action = [&](ActionMenuCommand cmd, const char* text)
    {
        lv_obj_t* btn = create_action_menu_button(list, text);
        lv_obj_add_event_cb(btn,
                            on_action_menu_item_clicked,
                            LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<uintptr_t>(cmd)));
        lv_obj_add_event_cb(btn, on_action_menu_key, LV_EVENT_KEY, nullptr);
        lv_group_add_obj(state.modal_group, btn);
        if (!first_focus)
        {
            first_focus = btn;
        }
    };

    if (state.mode == TrackerPageState::Mode::Route)
    {
        if (can_load_selected_route())
        {
            add_action(ActionMenuCommand::Load, "Load");
        }
        if (can_unload_active_route())
        {
            add_action(ActionMenuCommand::Unload, "Off");
        }
    }
    if (can_delete_selected_item())
    {
        add_action(ActionMenuCommand::Delete, "Delete");
    }
    add_action(ActionMenuCommand::Cancel, "Cancel");

    if (first_focus)
    {
        lv_group_focus_obj(first_focus);
    }
}

void on_list_item_focused(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    void* ud = lv_obj_get_user_data(target);
    if (!ud)
    {
        return;
    }
    lv_obj_scroll_to_view(target, LV_ANIM_OFF);
    const intptr_t raw = reinterpret_cast<intptr_t>(ud);
    if (raw == kBackListItemUserData)
    {
        if (lv_obj_t* label = lv_obj_get_child(target, -1))
        {
            ::ui::i18n::set_label_text(label, "Back");
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        }
        return;
    }
    if (raw < static_cast<intptr_t>(kListUserDataOffset))
    {
        return;
    }
    const size_t idx = static_cast<size_t>(raw - static_cast<intptr_t>(kListUserDataOffset));
    const auto& names =
        (g_tracker_state.mode == TrackerPageState::Mode::Record) ? s_record_names : s_route_names;
    if (idx >= names.size())
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(target, -1);
    if (!label)
    {
        return;
    }
    lv_label_set_text(label, names[idx].c_str());
    lv_obj_set_style_text_font(label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
}

void on_list_item_defocused(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    void* ud = lv_obj_get_user_data(target);
    if (!ud)
    {
        return;
    }
    const intptr_t raw = reinterpret_cast<intptr_t>(ud);
    if (raw == kBackListItemUserData)
    {
        if (lv_obj_t* label = lv_obj_get_child(target, -1))
        {
            lv_label_set_text(label, ::ui::i18n::tr("Back"));
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        }
        return;
    }
    if (raw < static_cast<intptr_t>(kListUserDataOffset))
    {
        return;
    }
    const size_t idx = static_cast<size_t>(raw - static_cast<intptr_t>(kListUserDataOffset));
    const auto& names =
        (g_tracker_state.mode == TrackerPageState::Mode::Record) ? s_record_names : s_route_names;
    if (idx >= names.size())
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(target, -1);
    if (!label)
    {
        return;
    }
    std::string display_name = format_list_name(names[idx]);
    lv_label_set_text(label, display_name.c_str());
    lv_obj_add_style(label, &s_btn_label, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

void on_mode_record_focused(lv_event_t*)
{
    set_mode(TrackerPageState::Mode::Record);
}

void on_mode_route_focused(lv_event_t*)
{
    set_mode(TrackerPageState::Mode::Route);
}

void on_mode_record_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_ENTER)
    {
        return;
    }
    on_mode_record_clicked(e);
}

void on_mode_route_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_ENTER)
    {
        return;
    }
    on_mode_route_clicked(e);
}

void on_route_load_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    if (state.selected_route_idx < 0 || state.selected_route.empty())
    {
        if (state.status_label)
        {
            ::ui::i18n::set_label_text(state.status_label, "Select a route");
        }
        return;
    }
    state.active_route = state.selected_route;
    {
        app::IAppFacade& app_ctx = app::appFacade();
        auto& cfg = app_ctx.getConfig();
        cfg.route_enabled = true;
        char path[96];
        snprintf(path, sizeof(path), "%s/%s", platform::ui::route_storage::route_dir(), state.active_route.c_str());
        strncpy(cfg.route_path, path, sizeof(cfg.route_path) - 1);
        cfg.route_path[sizeof(cfg.route_path) - 1] = '\0';
        app_ctx.saveConfig();
    }
    update_route_status();
}

void on_route_unload_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    if (state.active_route.empty())
    {
        update_route_status();
        return;
    }
    state.active_route.clear();
    {
        app::IAppFacade& app_ctx = app::appFacade();
        auto& cfg = app_ctx.getConfig();
        cfg.route_enabled = false;
        cfg.route_path[0] = '\0';
        app_ctx.saveConfig();
    }
    update_route_status();
}

void on_del_confirm_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    bool ok = false;
    if (state.pending_delete_path.empty())
    {
        modal_close(state.del_confirm_modal);
        return;
    }
    std::string path = state.pending_delete_path;

    if (state.pending_delete_mode == TrackerPageState::Mode::Record)
    {
        if (platform::ui::tracker::is_recording())
        {
            std::string current;
            platform::ui::tracker::current_path(current);
            if (!current.empty() && path_basename(current) == state.pending_delete_name)
            {
                if (state.status_label)
                {
                    ::ui::i18n::set_label_text(state.status_label, "Stop recording first");
                }
                modal_close(state.del_confirm_modal);
                return;
            }
        }
        ok = platform::ui::tracker::remove_track(path);
        if (ok)
        {
            state.selected_record_idx = -1;
            state.selected_record.clear();
            refresh_record_list();
            if (state.mode == TrackerPageState::Mode::Record)
            {
                update_record_page();
            }
            update_record_status();
        }
    }
    else
    {
        if (!state.active_route.empty() && state.active_route == state.pending_delete_name)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            auto& cfg = app_ctx.getConfig();
            cfg.route_enabled = false;
            cfg.route_path[0] = '\0';
            app_ctx.saveConfig();
            state.active_route.clear();
        }
        ok = platform::ui::route_storage::remove_route(path);
        if (ok)
        {
            state.selected_route_idx = -1;
            state.selected_route.clear();
            refresh_route_list();
            if (state.mode == TrackerPageState::Mode::Route)
            {
                update_route_page();
            }
            update_route_status();
        }
    }

    if (!ok && state.status_label)
    {
        ::ui::i18n::set_label_text(state.status_label, "Delete failed");
    }
    modal_close(state.del_confirm_modal);
}

void on_del_cancel_clicked(lv_event_t*)
{
    modal_close(g_tracker_state.del_confirm_modal);
}

void open_delete_confirm_modal()
{
    auto& state = g_tracker_state;
    if (state.del_confirm_modal)
    {
        return;
    }

    if (state.mode == TrackerPageState::Mode::Record)
    {
        if (state.selected_record_idx < 0 ||
            state.selected_record_idx >= static_cast<int>(s_record_names.size()))
        {
            if (state.status_label)
            {
                ::ui::i18n::set_label_text(state.status_label, "Select a track");
            }
            return;
        }
        state.pending_delete_mode = TrackerPageState::Mode::Record;
        state.pending_delete_name = s_record_names[state.selected_record_idx].c_str();
        std::string path =
            std::string(platform::ui::tracker::track_dir()) + "/" + s_record_names[state.selected_record_idx];
        state.pending_delete_path = path;
    }
    else
    {
        if (state.selected_route_idx < 0 ||
            state.selected_route_idx >= static_cast<int>(s_route_names.size()))
        {
            if (state.status_label)
            {
                ::ui::i18n::set_label_text(state.status_label, "Select a route");
            }
            return;
        }
        state.pending_delete_mode = TrackerPageState::Mode::Route;
        state.pending_delete_name = s_route_names[state.selected_route_idx].c_str();
        std::string path = std::string(platform::ui::route_storage::route_dir()) + "/" + s_route_names[state.selected_route_idx];
        state.pending_delete_path = path;
    }

    modal_prepare_group();
    state.del_confirm_modal = create_modal_root(280, 140);
    lv_obj_t* win = lv_obj_get_child(state.del_confirm_modal, 0);

    const std::string msg = ::ui::i18n::format("Delete %s?", state.pending_delete_name.c_str());
    lv_obj_t* label = lv_label_create(win);
    ::ui::i18n::set_label_text_raw(label, msg.c_str());
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t* btn_row = lv_obj_create(win);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* confirm_btn = lv_btn_create(btn_row);
    lv_obj_set_size(confirm_btn, modal_button_width(), action_menu_button_height());
    lv_obj_t* confirm_label = lv_label_create(confirm_btn);
    ::ui::i18n::set_label_text(confirm_label, "Confirm");
    lv_obj_center(confirm_label);
    apply_action_button(confirm_btn, confirm_label);
    lv_obj_add_event_cb(confirm_btn, on_del_confirm_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, modal_button_width(), action_menu_button_height());
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    ::ui::i18n::set_label_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    apply_action_button(cancel_btn, cancel_label);
    lv_obj_add_event_cb(cancel_btn, on_del_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(state.modal_group, confirm_btn);
    lv_group_add_obj(state.modal_group, cancel_btn);
    lv_group_focus_obj(cancel_btn);
}

} // namespace

void refresh_page()
{
    sync_active_route_from_config();
    update_mode_buttons();
    update_record_status();
    update_start_stop_button();
    refresh_record_list();
    refresh_route_list();
    update_route_status();
    tracker::ui::input::tracker_input_on_ui_refreshed();
    ui_update_top_bar_battery(g_tracker_state.top_bar);
}

void init_page(lv_obj_t* parent)
{
    auto& state = g_tracker_state;
    if (!parent)
    {
        return;
    }

    if (state.root)
    {
        cleanup_page();
    }

    state.root = layout::create_root(parent);
    state.header = layout::create_header(state.root);
    state.content = layout::create_content(state.root);
    state.filter_panel = layout::create_filter_panel(state.content, filter_panel_width());
    state.list_panel = layout::create_list_panel(state.content);

    state.mode_record_btn = lv_btn_create(state.filter_panel);
    lv_obj_set_width(state.mode_record_btn, LV_PCT(100));
    lv_obj_set_height(state.mode_record_btn, filter_button_height());
    state.mode_record_label = lv_label_create(state.mode_record_btn);
    lv_obj_add_style(state.mode_record_label, &s_btn_label, LV_PART_MAIN);
    ::ui::i18n::set_label_text(state.mode_record_label, "Record");
    lv_obj_center(state.mode_record_label);

    state.mode_route_btn = lv_btn_create(state.filter_panel);
    lv_obj_set_width(state.mode_route_btn, LV_PCT(100));
    lv_obj_set_height(state.mode_route_btn, filter_button_height());
    state.mode_route_label = lv_label_create(state.mode_route_btn);
    lv_obj_add_style(state.mode_route_label, &s_btn_label, LV_PART_MAIN);
    ::ui::i18n::set_label_text(state.mode_route_label, "Route");
    lv_obj_center(state.mode_route_label);

    state.status_label = lv_label_create(state.list_panel);
    ::ui::i18n::set_label_text(state.status_label, "Stopped");
    lv_obj_set_width(state.status_label, LV_PCT(100));
    lv_obj_set_style_text_font(
        state.status_label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_obj_set_style_text_color(state.status_label, lv_color_hex(kPanelTextMuted), 0);

    state.list_container = layout::create_list_container(state.list_panel);
    lv_obj_add_flag(state.list_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(state.list_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state.list_container, LV_SCROLLBAR_MODE_AUTO);

    state.bottom_bar = layout::create_bottom_bar(state.list_panel);
    state.start_stop_btn = lv_btn_create(state.bottom_bar);
    lv_obj_set_size(state.start_stop_btn, bottom_bar_button_width(), filter_button_height());
    state.start_stop_label = lv_label_create(state.start_stop_btn);
    lv_obj_set_width(state.start_stop_label, LV_PCT(100));
    lv_label_set_long_mode(state.start_stop_label, LV_LABEL_LONG_DOT);
    lv_obj_center(state.start_stop_label);
    apply_action_button(state.start_stop_btn, state.start_stop_label);

    ::ui::widgets::TopBarConfig cfg;
    cfg.height = page_profile().top_bar_height;
    ::ui::widgets::top_bar_init(state.top_bar, state.header, cfg);
    ::ui::widgets::top_bar_set_title(state.top_bar, ::ui::i18n::tr("Tracker"));
    ::ui::widgets::top_bar_set_back_callback(state.top_bar, on_back, nullptr);

    lv_obj_add_event_cb(state.mode_record_btn, on_mode_record_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.mode_route_btn, on_mode_route_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.mode_record_btn, on_mode_record_focused, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(state.mode_route_btn, on_mode_route_focused, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(state.mode_record_btn, on_mode_record_key, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(state.mode_route_btn, on_mode_route_key, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(state.start_stop_btn, on_start_stop_clicked, LV_EVENT_CLICKED, nullptr);

    set_mode(TrackerPageState::Mode::Record);
    refresh_page();
}

void cleanup_page()
{
    auto& state = g_tracker_state;
    if (state.action_menu_modal)
    {
        lv_obj_del(state.action_menu_modal);
        state.action_menu_modal = nullptr;
    }
    if (state.del_confirm_modal)
    {
        lv_obj_del(state.del_confirm_modal);
        state.del_confirm_modal = nullptr;
    }
    if (state.modal_group)
    {
        lv_group_del(state.modal_group);
        state.modal_group = nullptr;
    }
    state.prev_group = nullptr;
    if (state.root)
    {
        lv_obj_del(state.root);
    }
    state = TrackerPageState{};
}

} // namespace components
} // namespace ui
} // namespace tracker
