#include <Arduino.h>

#include "../../../app/app_context.h"
#include "../../../gps/usecase/track_recorder.h"
#include "../../ui_common.h"
#include "../../assets/fonts/fonts.h"
#include "tracker_page_components.h"
#include "tracker_page_input.h"
#include "tracker_page_layout.h"
#include "tracker_state.h"

#include <SD.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace tracker
{
namespace ui
{
namespace components
{

namespace
{
constexpr int kFilterPanelWidth = 80;
constexpr int kActionPanelWidth = 80;
constexpr int kModeButtonHeight = 28;
constexpr int kPrimaryButtonHeight = 28;
constexpr int kSecondaryButtonHeight = 28;
constexpr int kListItemHeight = 28;
constexpr int kBottomBarButtonWidth = 70;
constexpr int kListPageSize = 4;
constexpr uintptr_t kListUserDataOffset = 1;
constexpr const char* kRouteDir = "/routes";
std::vector<String> s_route_names;
std::vector<String> s_record_names;

constexpr uint32_t kPanelBtnBg = 0xF4C77A;
constexpr uint32_t kPanelBtnBorder = 0xEBA341;
constexpr uint32_t kPanelBtnFocused = 0xF1B65A;
constexpr uint32_t kPanelBtnText = 0x202020;

bool s_btn_styles_inited = false;
lv_style_t s_btn_main;
lv_style_t s_btn_focused;
lv_style_t s_btn_disabled;
lv_style_t s_btn_label;

lv_group_t* tracker_group();
void update_record_status();
void update_start_stop_button();
void update_record_page();
void update_route_status();
void update_route_page();
void update_del_button();
void on_action_back_clicked(lv_event_t* e);
lv_obj_t* action_focus_target();
void focus_action_panel();

void on_back(void*)
{
    ui_request_exit_to_menu();
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
    lv_obj_set_style_bg_color(bg, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* win = lv_obj_create(bg);
    lv_obj_set_size(win, width, height);
    lv_obj_center(win);
    lv_obj_set_style_bg_color(win, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_radius(win, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, 8, LV_PART_MAIN);
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

    lv_style_init(&s_btn_focused);
    lv_style_set_bg_color(&s_btn_focused, lv_color_hex(kPanelBtnFocused));
    lv_style_set_bg_opa(&s_btn_focused, LV_OPA_COVER);
    lv_style_set_outline_width(&s_btn_focused, 2);
    lv_style_set_outline_color(&s_btn_focused, lv_color_hex(kPanelBtnBorder));

    lv_style_init(&s_btn_disabled);
    lv_style_set_bg_opa(&s_btn_disabled, LV_OPA_50);

    lv_style_init(&s_btn_label);
    lv_style_set_text_color(&s_btn_label, lv_color_hex(kPanelBtnText));
    lv_style_set_text_font(&s_btn_label, &lv_font_noto_cjk_16_2bpp);

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
    lv_color_t bg = active ? lv_color_hex(0xEBA341) : lv_color_hex(0xF4C77A);
    lv_color_t fg = lv_color_hex(0x202020);
    lv_obj_set_style_bg_color(btn, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xEBA341), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
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

String path_basename(const String& path)
{
    const char* base = strrchr(path.c_str(), '/');
    if (base && base[1] != '\0')
    {
        return String(base + 1);
    }
    return path;
}

void update_del_button()
{
    auto& state = g_tracker_state;
    if (!state.del_btn)
    {
        return;
    }
    if (state.mode == TrackerPageState::Mode::Route)
    {
        if (!state.active_route.empty())
        {
            lv_obj_add_flag(state.del_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(state.del_btn, LV_STATE_DISABLED);
            return;
        }
        lv_obj_clear_flag(state.del_btn, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(state.del_btn, LV_OBJ_FLAG_HIDDEN);
    }
    bool can_delete = false;
    if (state.mode == TrackerPageState::Mode::Record)
    {
        if (state.selected_record_idx >= 0 &&
            state.selected_record_idx < static_cast<int>(s_record_names.size()))
        {
            can_delete = true;
            if (gps::TrackRecorder::getInstance().isRecording())
            {
                String current = gps::TrackRecorder::getInstance().currentPath();
                String base = path_basename(current);
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
    if (can_delete)
    {
        lv_obj_clear_state(state.del_btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(state.del_btn, LV_STATE_DISABLED);
    }
}

lv_group_t* tracker_group()
{
    return ::app_g;
}

lv_obj_t* action_focus_target()
{
    auto& state = g_tracker_state;
    auto visible = [](lv_obj_t* btn) {
        return btn && !lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN);
    };
    auto enabled = [](lv_obj_t* btn) {
        return btn && !lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN) &&
               !lv_obj_has_state(btn, LV_STATE_DISABLED);
    };

    if (state.mode == TrackerPageState::Mode::Record)
    {
        if (enabled(state.del_btn)) return state.del_btn;
        if (visible(state.action_back_btn)) return state.action_back_btn;
        if (enabled(state.start_stop_btn)) return state.start_stop_btn;
        if (visible(state.del_btn)) return state.del_btn;
        if (visible(state.start_stop_btn)) return state.start_stop_btn;
        return nullptr;
    }

    if (enabled(state.load_btn)) return state.load_btn;
    if (enabled(state.unload_btn)) return state.unload_btn;
    if (enabled(state.del_btn)) return state.del_btn;
    if (visible(state.action_back_btn)) return state.action_back_btn;
    if (visible(state.load_btn)) return state.load_btn;
    if (visible(state.unload_btn)) return state.unload_btn;
    if (visible(state.del_btn)) return state.del_btn;
    return nullptr;
}

void group_add_if(lv_group_t* group, lv_obj_t* obj)
{
    if (!group || !obj)
    {
        return;
    }
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN))
    {
        return;
    }
    if (lv_obj_has_state(obj, LV_STATE_DISABLED))
    {
        return;
    }
    lv_group_add_obj(group, obj);
}

lv_obj_t* first_visible_list_item()
{
    for (auto* btn : g_tracker_state.list_item_btns)
    {
        if (btn && !lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN) &&
            !lv_obj_has_state(btn, LV_STATE_DISABLED))
        {
            return btn;
        }
    }
    return g_tracker_state.list_back_btn;
}

void bind_mode_group()
{
    lv_group_t* group = tracker_group();
    if (!group)
    {
        return;
    }
    lv_group_focus_freeze(group, true);
    lv_group_remove_all_objs(group);

    group_add_if(group, g_tracker_state.top_bar.back_btn);
    group_add_if(group, g_tracker_state.mode_record_btn);
    group_add_if(group, g_tracker_state.mode_route_btn);

    lv_group_focus_freeze(group, false);
}

void bind_main_group()
{
    lv_group_t* group = tracker_group();
    if (!group)
    {
        return;
    }
    lv_group_focus_freeze(group, true);
    lv_group_remove_all_objs(group);

    group_add_if(group, g_tracker_state.top_bar.back_btn);
    for (auto* btn : g_tracker_state.list_item_btns)
    {
        group_add_if(group, btn);
    }
    group_add_if(group, g_tracker_state.list_prev_btn);
    group_add_if(group, g_tracker_state.list_next_btn);
    group_add_if(group, g_tracker_state.list_back_btn);

    if (g_tracker_state.mode == TrackerPageState::Mode::Record)
    {
        group_add_if(group, g_tracker_state.start_stop_btn);
    }
    else
    {
        group_add_if(group, g_tracker_state.load_btn);
        group_add_if(group, g_tracker_state.unload_btn);
    }
    group_add_if(group, g_tracker_state.del_btn);
    group_add_if(group, g_tracker_state.action_back_btn);

    lv_group_focus_freeze(group, false);
}

void focus_mode_panel_async(void*)
{
    if (!g_tracker_state.root)
    {
        return;
    }
    if (g_tracker_state.focus_col != TrackerPageState::FocusColumn::Mode)
    {
        return;
    }
    bind_mode_group();
    lv_group_t* group = tracker_group();
    if (!group)
    {
        return;
    }
    lv_obj_t* target = nullptr;
    if (g_tracker_state.mode == TrackerPageState::Mode::Record)
    {
        target = g_tracker_state.mode_record_btn;
    }
    else
    {
        target = g_tracker_state.mode_route_btn;
    }
    if (target && lv_group_get_focused(group) != target)
    {
        lv_group_focus_obj(target);
    }
}

void focus_main_panel_async(void*)
{
    if (!g_tracker_state.root)
    {
        return;
    }
    if (g_tracker_state.focus_col != TrackerPageState::FocusColumn::Main)
    {
        return;
    }
    bind_main_group();
    if (lv_obj_t* target = first_visible_list_item())
    {
        lv_group_focus_obj(target);
    }
}

void focus_action_panel_async(void*)
{
    if (!g_tracker_state.root)
    {
        return;
    }
    g_tracker_state.focus_col = TrackerPageState::FocusColumn::Main;
    bind_main_group();
    lv_group_t* group = tracker_group();
    if (!group)
    {
        return;
    }
    if (lv_obj_t* target = action_focus_target())
    {
        lv_group_focus_obj(target);
    }
}

void focus_mode_panel()
{
    g_tracker_state.focus_col = TrackerPageState::FocusColumn::Mode;
    lv_async_call(focus_mode_panel_async, nullptr);
}

void focus_main_panel()
{
    g_tracker_state.focus_col = TrackerPageState::FocusColumn::Main;
    lv_async_call(focus_main_panel_async, nullptr);
}

void focus_action_panel()
{
    g_tracker_state.focus_col = TrackerPageState::FocusColumn::Main;
    lv_async_call(focus_action_panel_async, nullptr);
}

void refresh_focus_group()
{
    if (g_tracker_state.focus_col == TrackerPageState::FocusColumn::Main)
    {
        bind_main_group();
    }
    else
    {
        bind_mode_group();
    }
}

void set_mode(TrackerPageState::Mode mode)
{
    auto& state = g_tracker_state;
    state.mode = mode;
    update_mode_buttons();
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
    if (state.load_btn)
    {
        if (mode == TrackerPageState::Mode::Route)
        {
            lv_obj_clear_flag(state.load_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(state.load_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (state.unload_btn)
    {
        if (mode == TrackerPageState::Mode::Route)
        {
            lv_obj_clear_flag(state.unload_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(state.unload_btn, LV_OBJ_FLAG_HIDDEN);
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
        refresh_focus_group();
    }
}

void update_record_status()
{
    auto& state = g_tracker_state;
    if (!state.status_label)
    {
        return;
    }
    const bool recording = gps::TrackRecorder::getInstance().isRecording();
    if (state.mode == TrackerPageState::Mode::Record)
    {
        lv_label_set_text(state.status_label, recording ? "Recording" : "Stopped");
    }
}

void update_start_stop_button()
{
    auto& state = g_tracker_state;
    const bool recording = gps::TrackRecorder::getInstance().isRecording();
    if (state.start_stop_label)
    {
        lv_label_set_text(state.start_stop_label, recording ? "Stop" : "New");
    }
}

size_t utf8_count_chars(const String& text)
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

String utf8_truncate(const String& text, size_t max_chars)
{
    if (max_chars == 0)
    {
        return String("...");
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
    String out = text.substring(0, static_cast<int>(i));
    out += "...";
    return out;
}

String format_list_name(const String& name)
{
    if (utf8_count_chars(name) <= 20)
    {
        return name;
    }
    return utf8_truncate(name, 20);
}

void set_placeholder_row(lv_obj_t* btn, lv_obj_t* label, const char* text)
{
    if (!btn || !label)
    {
        return;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(btn, LV_STATE_DISABLED);
    lv_obj_clear_state(btn, LV_STATE_CHECKED);
    lv_obj_set_user_data(btn, nullptr);
}

void update_record_page()
{
    auto& state = g_tracker_state;
    const size_t total = s_record_names.size();
    int max_page = 0;
    if (total > 0)
    {
        max_page = static_cast<int>((total - 1) / kListPageSize);
    }
    if (state.record_page < 0) state.record_page = 0;
    if (state.record_page > max_page) state.record_page = max_page;

    const int start = state.record_page * kListPageSize;
    if (total == 0)
    {
        set_placeholder_row(state.list_item_btns[0], state.list_item_labels[0], "No tracks yet");
        for (int i = 1; i < kListPageSize; ++i)
        {
            if (state.list_item_btns[i])
            {
                lv_obj_add_flag(state.list_item_btns[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    else
    {
        for (int i = 0; i < kListPageSize; ++i)
        {
            lv_obj_t* btn = state.list_item_btns[i];
            lv_obj_t* label = state.list_item_labels[i];
            if (!btn || !label)
            {
                continue;
            }
            const int idx = start + i;
            if (idx < static_cast<int>(total))
            {
                String display_name = format_list_name(s_record_names[idx]);
                lv_label_set_text(label, display_name.c_str());
                lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(btn, LV_STATE_DISABLED);
                if (idx == state.selected_record_idx)
                {
                    lv_obj_add_state(btn, LV_STATE_CHECKED);
                }
                else
                {
                    lv_obj_clear_state(btn, LV_STATE_CHECKED);
                }
                lv_obj_set_user_data(btn,
                                     reinterpret_cast<void*>((uintptr_t)idx + kListUserDataOffset));
                lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
                lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            }
            else
            {
                lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    const bool show_pager = total > static_cast<size_t>(kListPageSize);
    if (state.list_prev_btn)
    {
        if (show_pager)
        {
            lv_obj_clear_flag(state.list_prev_btn, LV_OBJ_FLAG_HIDDEN);
            if (state.record_page > 0)
            {
                lv_obj_clear_state(state.list_prev_btn, LV_STATE_DISABLED);
            }
            else
            {
                lv_obj_add_state(state.list_prev_btn, LV_STATE_DISABLED);
            }
        }
        else
        {
            lv_obj_add_flag(state.list_prev_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (state.list_next_btn)
    {
        if (show_pager)
        {
            lv_obj_clear_flag(state.list_next_btn, LV_OBJ_FLAG_HIDDEN);
            if (state.record_page < max_page)
            {
                lv_obj_clear_state(state.list_next_btn, LV_STATE_DISABLED);
            }
            else
            {
                lv_obj_add_state(state.list_next_btn, LV_STATE_DISABLED);
            }
        }
        else
        {
            lv_obj_add_flag(state.list_next_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    update_del_button();
}

void refresh_record_list()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }
    s_record_names.clear();

    if (SD.cardType() == CARD_NONE)
    {
        set_placeholder_row(state.list_item_btns[0], state.list_item_labels[0], "No SD Card");
        for (int i = 1; i < kListPageSize; ++i)
        {
            if (state.list_item_btns[i])
            {
                lv_obj_add_flag(state.list_item_btns[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (state.list_prev_btn) lv_obj_add_flag(state.list_prev_btn, LV_OBJ_FLAG_HIDDEN);
        if (state.list_next_btn) lv_obj_add_flag(state.list_next_btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    constexpr size_t kMaxTracks = 32;
    String names[kMaxTracks];
    const size_t count = gps::TrackRecorder::getInstance().listTracks(names, kMaxTracks);

    for (size_t i = 0; i < count; ++i)
    {
        s_record_names.push_back(names[i]);
    }
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
    lv_obj_set_style_text_font(state.status_label, &lv_font_noto_cjk_16_2bpp, 0);
    if (state.mode == TrackerPageState::Mode::Route)
    {
        if (!state.active_route.empty())
        {
            lv_label_set_text_fmt(state.status_label, "Active: %s", state.active_route.c_str());
        }
        else if (!state.selected_route.empty())
        {
            lv_label_set_text_fmt(state.status_label, "Selected: %s", state.selected_route.c_str());
        }
        else
        {
            lv_label_set_text(state.status_label, "No route selected");
        }
    }
    if (state.load_btn)
    {
        const bool active = !state.active_route.empty();
        if (active)
        {
            lv_obj_add_flag(state.load_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(state.load_btn, LV_OBJ_FLAG_HIDDEN);
            const bool can_load = (state.selected_route_idx >= 0) && !state.selected_route.empty();
            if (can_load)
            {
                lv_obj_clear_state(state.load_btn, LV_STATE_DISABLED);
            }
            else
            {
                lv_obj_add_state(state.load_btn, LV_STATE_DISABLED);
            }
        }
    }
    if (state.unload_btn)
    {
        const bool active = !state.active_route.empty();
        if (active)
        {
            lv_obj_clear_flag(state.unload_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_state(state.unload_btn, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_add_flag(state.unload_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(state.unload_btn, LV_STATE_DISABLED);
        }
    }
    update_del_button();
}

void update_route_page()
{
    auto& state = g_tracker_state;
    const size_t total = s_route_names.size();
    int max_page = 0;
    if (total > 0)
    {
        max_page = static_cast<int>((total - 1) / kListPageSize);
    }
    if (state.route_page < 0) state.route_page = 0;
    if (state.route_page > max_page) state.route_page = max_page;

    const int start = state.route_page * kListPageSize;
    if (total == 0)
    {
        set_placeholder_row(state.list_item_btns[0], state.list_item_labels[0], "No KML routes");
        for (int i = 1; i < kListPageSize; ++i)
        {
            if (state.list_item_btns[i])
            {
                lv_obj_add_flag(state.list_item_btns[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    else
    {
        for (int i = 0; i < kListPageSize; ++i)
        {
            lv_obj_t* btn = state.list_item_btns[i];
            lv_obj_t* label = state.list_item_labels[i];
            if (!btn || !label)
            {
                continue;
            }
            const int idx = start + i;
            if (idx < static_cast<int>(total))
            {
                String display_name = format_list_name(s_route_names[idx]);
                lv_label_set_text(label, display_name.c_str());
                lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(btn, LV_STATE_DISABLED);
                if (idx == state.selected_route_idx)
                {
                    lv_obj_add_state(btn, LV_STATE_CHECKED);
                }
                else
                {
                    lv_obj_clear_state(btn, LV_STATE_CHECKED);
                }
                lv_obj_set_user_data(btn,
                                     reinterpret_cast<void*>((uintptr_t)idx + kListUserDataOffset));
                lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
                lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            }
            else
            {
                lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    const bool show_pager = total > static_cast<size_t>(kListPageSize);
    if (state.list_prev_btn)
    {
        if (show_pager)
        {
            lv_obj_clear_flag(state.list_prev_btn, LV_OBJ_FLAG_HIDDEN);
            if (state.route_page > 0)
            {
                lv_obj_clear_state(state.list_prev_btn, LV_STATE_DISABLED);
            }
            else
            {
                lv_obj_add_state(state.list_prev_btn, LV_STATE_DISABLED);
            }
        }
        else
        {
            lv_obj_add_flag(state.list_prev_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (state.list_next_btn)
    {
        if (show_pager)
        {
            lv_obj_clear_flag(state.list_next_btn, LV_OBJ_FLAG_HIDDEN);
            if (state.route_page < max_page)
            {
                lv_obj_clear_state(state.list_next_btn, LV_STATE_DISABLED);
            }
            else
            {
                lv_obj_add_state(state.list_next_btn, LV_STATE_DISABLED);
            }
        }
        else
        {
            lv_obj_add_flag(state.list_next_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    update_del_button();
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

    if (SD.cardType() == CARD_NONE)
    {
        set_placeholder_row(state.list_item_btns[0], state.list_item_labels[0], "No SD Card");
        for (int i = 1; i < kListPageSize; ++i)
        {
            if (state.list_item_btns[i])
            {
                lv_obj_add_flag(state.list_item_btns[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (state.list_prev_btn) lv_obj_add_flag(state.list_prev_btn, LV_OBJ_FLAG_HIDDEN);
        if (state.list_next_btn) lv_obj_add_flag(state.list_next_btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    File dir = SD.open(kRouteDir);
    if (!dir || !dir.isDirectory())
    {
        set_placeholder_row(state.list_item_btns[0], state.list_item_labels[0], "No routes folder");
        for (int i = 1; i < kListPageSize; ++i)
        {
            if (state.list_item_btns[i])
            {
                lv_obj_add_flag(state.list_item_btns[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (state.list_prev_btn) lv_obj_add_flag(state.list_prev_btn, LV_OBJ_FLAG_HIDDEN);
        if (state.list_next_btn) lv_obj_add_flag(state.list_next_btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    for (File f = dir.openNextFile(); f; f = dir.openNextFile())
    {
        if (!f.isDirectory())
        {
            String name = String(f.name());
            String lower = name;
            lower.toLowerCase();
            if (lower.endsWith(".kml"))
            {
                s_route_names.push_back(name);
            }
        }
        f.close();
    }
    dir.close();

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
    app::AppContext& app_ctx = app::AppContext::getInstance();
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
    auto& recorder = gps::TrackRecorder::getInstance();
    if (recorder.isRecording())
    {
        recorder.stop();
    }
    else
    {
        recorder.start();
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

void on_list_prev_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    if (state.mode == TrackerPageState::Mode::Record)
    {
        if (state.record_page > 0)
        {
            state.record_page--;
            update_record_page();
        }
    }
    else
    {
        if (state.route_page > 0)
        {
            state.route_page--;
            update_route_page();
        }
    }
    focus_main_panel();
}

void on_list_next_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    if (state.mode == TrackerPageState::Mode::Record)
    {
        state.record_page++;
        update_record_page();
    }
    else
    {
        state.route_page++;
        update_route_page();
    }
    focus_main_panel();
}

void on_list_back_clicked(lv_event_t*)
{
    focus_mode_panel();
}

void on_list_back_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ENTER)
    {
        focus_mode_panel();
    }
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
    uintptr_t raw = reinterpret_cast<uintptr_t>(ud);
    if (raw < kListUserDataOffset)
    {
        return;
    }
    uintptr_t idx = raw - kListUserDataOffset;

    if (state.mode == TrackerPageState::Mode::Record)
    {
        if (idx >= s_record_names.size())
        {
            return;
        }
        state.selected_record_idx = static_cast<int>(idx);
        state.selected_record = s_record_names[idx].c_str();
        update_record_page();
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
        update_route_page();
    }
    update_del_button();
    focus_action_panel();
}

void on_action_back_clicked(lv_event_t*)
{
    focus_main_panel();
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
    uintptr_t raw = reinterpret_cast<uintptr_t>(ud);
    if (raw < kListUserDataOffset)
    {
        return;
    }
    const size_t idx = static_cast<size_t>(raw - kListUserDataOffset);
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
    lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
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
    uintptr_t raw = reinterpret_cast<uintptr_t>(ud);
    if (raw < kListUserDataOffset)
    {
        return;
    }
    const size_t idx = static_cast<size_t>(raw - kListUserDataOffset);
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
    String display_name = format_list_name(names[idx]);
    lv_label_set_text(label, display_name.c_str());
    lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
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
            lv_label_set_text(state.status_label, "Select a route");
        }
        return;
    }
    state.active_route = state.selected_route;
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        auto& cfg = app_ctx.getConfig();
        cfg.route_enabled = true;
        char path[96];
        snprintf(path, sizeof(path), "%s/%s", kRouteDir, state.active_route.c_str());
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
        app::AppContext& app_ctx = app::AppContext::getInstance();
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
    String path = state.pending_delete_path.c_str();

    if (state.pending_delete_mode == TrackerPageState::Mode::Record)
    {
        if (gps::TrackRecorder::getInstance().isRecording())
        {
            String current = gps::TrackRecorder::getInstance().currentPath();
            if (!current.isEmpty() && path_basename(current) == state.pending_delete_name.c_str())
            {
                if (state.status_label)
                {
                    lv_label_set_text(state.status_label, "Stop recording first");
                }
                modal_close(state.del_confirm_modal);
                return;
            }
        }
        ok = SD.remove(path);
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
            app::AppContext& app_ctx = app::AppContext::getInstance();
            auto& cfg = app_ctx.getConfig();
            cfg.route_enabled = false;
            cfg.route_path[0] = '\0';
            app_ctx.saveConfig();
            state.active_route.clear();
        }
        ok = SD.remove(path);
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
        lv_label_set_text(state.status_label, "Delete failed");
    }
    update_del_button();
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
                lv_label_set_text(state.status_label, "Select a track");
            }
            return;
        }
        state.pending_delete_mode = TrackerPageState::Mode::Record;
        state.pending_delete_idx = state.selected_record_idx;
        state.pending_delete_name = s_record_names[state.selected_record_idx].c_str();
        String path =
            String(gps::TrackRecorder::kTrackDir) + "/" + s_record_names[state.selected_record_idx];
        state.pending_delete_path = path.c_str();
    }
    else
    {
        if (state.selected_route_idx < 0 ||
            state.selected_route_idx >= static_cast<int>(s_route_names.size()))
        {
            if (state.status_label)
            {
                lv_label_set_text(state.status_label, "Select a route");
            }
            return;
        }
        state.pending_delete_mode = TrackerPageState::Mode::Route;
        state.pending_delete_idx = state.selected_route_idx;
        state.pending_delete_name = s_route_names[state.selected_route_idx].c_str();
        String path = String(kRouteDir) + "/" + s_route_names[state.selected_route_idx];
        state.pending_delete_path = path.c_str();
    }

    modal_prepare_group();
    state.del_confirm_modal = create_modal_root(280, 140);
    lv_obj_t* win = lv_obj_get_child(state.del_confirm_modal, 0);

    char msg[80];
    snprintf(msg, sizeof(msg), "Delete %s?", state.pending_delete_name.c_str());
    lv_obj_t* label = lv_label_create(win);
    lv_label_set_text(label, msg);
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
    lv_obj_set_size(confirm_btn, 90, 28);
    lv_obj_t* confirm_label = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_label, "Confirm");
    lv_obj_center(confirm_label);
    apply_action_button(confirm_btn, confirm_label);
    lv_obj_add_event_cb(confirm_btn, on_del_confirm_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 90, 28);
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    apply_action_button(cancel_btn, cancel_label);
    lv_obj_add_event_cb(cancel_btn, on_del_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(state.modal_group, confirm_btn);
    lv_group_add_obj(state.modal_group, cancel_btn);
    lv_group_focus_obj(cancel_btn);
}

void on_del_clicked(lv_event_t*)
{
    open_delete_confirm_modal();
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
    update_del_button();
    refresh_focus_group();
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
    state.filter_panel = layout::create_filter_panel(state.content, kFilterPanelWidth);
    state.list_panel = layout::create_list_panel(state.content);
    state.action_panel = layout::create_action_panel(state.content, kActionPanelWidth);

    state.mode_record_btn = lv_btn_create(state.filter_panel);
    lv_obj_set_width(state.mode_record_btn, LV_PCT(100));
    lv_obj_set_height(state.mode_record_btn, kModeButtonHeight);
    state.mode_record_label = lv_label_create(state.mode_record_btn);
    lv_label_set_text(state.mode_record_label, "Record");
    lv_obj_center(state.mode_record_label);

    state.mode_route_btn = lv_btn_create(state.filter_panel);
    lv_obj_set_width(state.mode_route_btn, LV_PCT(100));
    lv_obj_set_height(state.mode_route_btn, kModeButtonHeight);
    state.mode_route_label = lv_label_create(state.mode_route_btn);
    lv_label_set_text(state.mode_route_label, "Route");
    lv_obj_center(state.mode_route_label);

    state.status_label = lv_label_create(state.list_panel);
    lv_label_set_text(state.status_label, "Stopped");
    lv_obj_set_width(state.status_label, LV_PCT(100));
    lv_obj_set_style_text_font(state.status_label, &lv_font_noto_cjk_16_2bpp, 0);

    state.list_container = layout::create_list_container(state.list_panel);
    for (int i = 0; i < kListPageSize; ++i)
    {
        lv_obj_t* btn = lv_btn_create(state.list_container);
        lv_obj_set_size(btn, LV_PCT(100), kListItemHeight);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* label = lv_label_create(btn);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, LV_PCT(100));
        apply_list_button(btn);
        state.list_item_btns[i] = btn;
        state.list_item_labels[i] = label;
        lv_obj_add_event_cb(btn, on_list_item_clicked, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(btn, on_list_item_focused, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(btn, on_list_item_defocused, LV_EVENT_DEFOCUSED, nullptr);
    }

    state.bottom_bar = layout::create_bottom_bar(state.list_panel);
    state.list_prev_btn = lv_btn_create(state.bottom_bar);
    lv_obj_set_size(state.list_prev_btn, kBottomBarButtonWidth, kListItemHeight);
    state.list_prev_label = lv_label_create(state.list_prev_btn);
    lv_label_set_text(state.list_prev_label, "Prev");
    lv_obj_center(state.list_prev_label);
    apply_action_button(state.list_prev_btn, state.list_prev_label);

    state.list_next_btn = lv_btn_create(state.bottom_bar);
    lv_obj_set_size(state.list_next_btn, kBottomBarButtonWidth, kListItemHeight);
    state.list_next_label = lv_label_create(state.list_next_btn);
    lv_label_set_text(state.list_next_label, "Next");
    lv_obj_center(state.list_next_label);
    apply_action_button(state.list_next_btn, state.list_next_label);

    state.list_back_btn = lv_btn_create(state.bottom_bar);
    lv_obj_set_size(state.list_back_btn, kBottomBarButtonWidth, kListItemHeight);
    state.list_back_label = lv_label_create(state.list_back_btn);
    lv_label_set_text(state.list_back_label, "Back");
    lv_obj_center(state.list_back_label);
    apply_action_button(state.list_back_btn, state.list_back_label);

    state.start_stop_btn = lv_btn_create(state.bottom_bar);
    lv_obj_set_size(state.start_stop_btn, kBottomBarButtonWidth, kPrimaryButtonHeight);
    state.start_stop_label = lv_label_create(state.start_stop_btn);
    lv_obj_set_width(state.start_stop_label, LV_PCT(100));
    lv_label_set_long_mode(state.start_stop_label, LV_LABEL_LONG_DOT);
    lv_obj_center(state.start_stop_label);
    apply_action_button(state.start_stop_btn, state.start_stop_label);

    state.load_btn = lv_btn_create(state.action_panel);
    lv_obj_set_size(state.load_btn, LV_PCT(100), kSecondaryButtonHeight);
    state.load_label = lv_label_create(state.load_btn);
    lv_label_set_text(state.load_label, "Load");
    lv_obj_set_width(state.load_label, LV_PCT(100));
    lv_label_set_long_mode(state.load_label, LV_LABEL_LONG_DOT);
    lv_obj_center(state.load_label);
    apply_action_button(state.load_btn, state.load_label);

    state.unload_btn = lv_btn_create(state.action_panel);
    lv_obj_set_size(state.unload_btn, LV_PCT(100), kSecondaryButtonHeight);
    state.unload_label = lv_label_create(state.unload_btn);
    lv_label_set_text(state.unload_label, "Off");
    lv_obj_set_width(state.unload_label, LV_PCT(100));
    lv_label_set_long_mode(state.unload_label, LV_LABEL_LONG_DOT);
    lv_obj_center(state.unload_label);
    apply_action_button(state.unload_btn, state.unload_label);

    state.del_btn = lv_btn_create(state.action_panel);
    lv_obj_set_size(state.del_btn, LV_PCT(100), kSecondaryButtonHeight);
    state.del_label = lv_label_create(state.del_btn);
    lv_label_set_text(state.del_label, "Del");
    lv_obj_set_width(state.del_label, LV_PCT(100));
    lv_label_set_long_mode(state.del_label, LV_LABEL_LONG_DOT);
    lv_obj_center(state.del_label);
    apply_action_button(state.del_btn, state.del_label);

    state.action_back_btn = lv_btn_create(state.action_panel);
    lv_obj_set_size(state.action_back_btn, LV_PCT(100), kSecondaryButtonHeight);
    state.action_back_label = lv_label_create(state.action_back_btn);
    lv_label_set_text(state.action_back_label, "Back");
    lv_obj_set_width(state.action_back_label, LV_PCT(100));
    lv_label_set_long_mode(state.action_back_label, LV_LABEL_LONG_DOT);
    lv_obj_center(state.action_back_label);
    apply_action_button(state.action_back_btn, state.action_back_label);

    ::ui::widgets::TopBarConfig cfg;
    cfg.height = ::ui::widgets::kTopBarHeight;
    ::ui::widgets::top_bar_init(state.top_bar, state.header, cfg);
    ::ui::widgets::top_bar_set_title(state.top_bar, "Tracker");
    ::ui::widgets::top_bar_set_back_callback(state.top_bar, on_back, nullptr);

    lv_obj_add_event_cb(state.mode_record_btn, on_mode_record_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.mode_route_btn, on_mode_route_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.mode_record_btn, on_mode_record_focused, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(state.mode_route_btn, on_mode_route_focused, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(state.mode_record_btn, on_mode_record_key, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(state.mode_route_btn, on_mode_route_key, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(state.list_back_btn, on_list_back_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.list_back_btn, on_list_back_key, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(state.list_prev_btn, on_list_prev_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.list_next_btn, on_list_next_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.start_stop_btn, on_start_stop_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.load_btn, on_route_load_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.unload_btn, on_route_unload_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.del_btn, on_del_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.action_back_btn, on_action_back_clicked, LV_EVENT_CLICKED, nullptr);

    set_mode(TrackerPageState::Mode::Record);
    refresh_page();
    focus_mode_panel();
    state.initialized = true;
}

void cleanup_page()
{
    auto& state = g_tracker_state;
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
