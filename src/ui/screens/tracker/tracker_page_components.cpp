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
constexpr int kModePanelWidth = 64;
constexpr int kModeButtonHeight = 28;
constexpr int kPrimaryButtonHeight = 28;
constexpr int kSecondaryButtonHeight = 28;
constexpr int kListItemHeight = 28;
constexpr int kListItemGap = 4;
constexpr int kListPageSize = 4;
constexpr int kListHeight = kListItemHeight * kListPageSize + kListItemGap * (kListPageSize - 1);
constexpr int kListLabelWidth = 390;
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

void on_back(void*)
{
    ui_request_exit_to_menu();
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

lv_group_t* tracker_group()
{
    return ::app_g;
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

lv_obj_t* first_visible_record_item()
{
    for (auto* btn : g_tracker_state.record_item_btns)
    {
        if (btn && !lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN) &&
            !lv_obj_has_state(btn, LV_STATE_DISABLED))
        {
            return btn;
        }
    }
    return g_tracker_state.start_stop_btn;
}

lv_obj_t* first_visible_route_item()
{
    for (auto* btn : g_tracker_state.route_item_btns)
    {
        if (btn && !lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN) &&
            !lv_obj_has_state(btn, LV_STATE_DISABLED))
        {
            return btn;
        }
    }
    if (g_tracker_state.load_btn && !lv_obj_has_state(g_tracker_state.load_btn, LV_STATE_DISABLED))
    {
        return g_tracker_state.load_btn;
    }
    return g_tracker_state.route_back_btn;
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

    if (g_tracker_state.mode == TrackerPageState::Mode::Record)
    {
        group_add_if(group, g_tracker_state.record_back_btn);
        for (auto* btn : g_tracker_state.record_item_btns)
        {
            group_add_if(group, btn);
        }
        group_add_if(group, g_tracker_state.record_prev_btn);
        group_add_if(group, g_tracker_state.record_next_btn);
        group_add_if(group, g_tracker_state.start_stop_btn);
    }
    else
    {
        group_add_if(group, g_tracker_state.route_back_btn);
        for (auto* btn : g_tracker_state.route_item_btns)
        {
            group_add_if(group, btn);
        }
        group_add_if(group, g_tracker_state.route_prev_btn);
        group_add_if(group, g_tracker_state.route_next_btn);
        group_add_if(group, g_tracker_state.load_btn);
        group_add_if(group, g_tracker_state.unload_btn);
    }

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
    if (g_tracker_state.mode == TrackerPageState::Mode::Record)
    {
        if (lv_obj_t* target = first_visible_record_item())
        {
            lv_group_focus_obj(target);
        }
    }
    else
    {
        if (lv_obj_t* target = first_visible_route_item())
        {
            lv_group_focus_obj(target);
        }
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
    if (state.record_panel)
    {
        if (mode == TrackerPageState::Mode::Record)
        {
            lv_obj_clear_flag(state.record_panel, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(state.record_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (state.route_panel)
    {
        if (mode == TrackerPageState::Mode::Route)
        {
            lv_obj_clear_flag(state.route_panel, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(state.route_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    update_mode_buttons();
    if (state.focus_col == TrackerPageState::FocusColumn::Main)
    {
        refresh_focus_group();
    }
}

void update_record_status()
{
    auto& state = g_tracker_state;
    if (!state.record_status_label)
    {
        return;
    }
    const bool recording = gps::TrackRecorder::getInstance().isRecording();
    lv_label_set_text(state.record_status_label, recording ? "Recording" : "Stopped");
}

void update_start_stop_button()
{
    auto& state = g_tracker_state;
    const bool recording = gps::TrackRecorder::getInstance().isRecording();
    if (state.start_stop_label)
    {
        lv_label_set_text(state.start_stop_label, recording ? "Stop Recording" : "Start New Track");
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
        set_placeholder_row(state.record_item_btns[0], state.record_item_labels[0], "No tracks yet");
        for (int i = 1; i < kListPageSize; ++i)
        {
            if (state.record_item_btns[i])
            {
                lv_obj_add_flag(state.record_item_btns[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    else
    {
    for (int i = 0; i < kListPageSize; ++i)
    {
        lv_obj_t* btn = state.record_item_btns[i];
        lv_obj_t* label = state.record_item_labels[i];
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
                lv_obj_clear_state(btn, LV_STATE_CHECKED);
                lv_obj_set_user_data(btn, reinterpret_cast<void*>((uintptr_t)idx));
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
    if (state.record_prev_btn)
    {
        if (show_pager)
        {
            lv_obj_clear_flag(state.record_prev_btn, LV_OBJ_FLAG_HIDDEN);
            if (state.record_page > 0)
            {
                lv_obj_clear_state(state.record_prev_btn, LV_STATE_DISABLED);
            }
            else
            {
                lv_obj_add_state(state.record_prev_btn, LV_STATE_DISABLED);
            }
        }
        else
        {
            lv_obj_add_flag(state.record_prev_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (state.record_next_btn)
    {
        if (show_pager)
        {
            lv_obj_clear_flag(state.record_next_btn, LV_OBJ_FLAG_HIDDEN);
            if (state.record_page < max_page)
            {
                lv_obj_clear_state(state.record_next_btn, LV_STATE_DISABLED);
            }
            else
            {
                lv_obj_add_state(state.record_next_btn, LV_STATE_DISABLED);
            }
        }
        else
        {
            lv_obj_add_flag(state.record_next_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void refresh_record_list()
{
    auto& state = g_tracker_state;
    if (!state.record_list)
    {
        return;
    }
    s_record_names.clear();

    if (SD.cardType() == CARD_NONE)
    {
        set_placeholder_row(state.record_item_btns[0], state.record_item_labels[0], "No SD Card");
        for (int i = 1; i < kListPageSize; ++i)
        {
            if (state.record_item_btns[i])
            {
                lv_obj_add_flag(state.record_item_btns[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (state.record_prev_btn) lv_obj_add_flag(state.record_prev_btn, LV_OBJ_FLAG_HIDDEN);
        if (state.record_next_btn) lv_obj_add_flag(state.record_next_btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    constexpr size_t kMaxTracks = 32;
    String names[kMaxTracks];
    const size_t count = gps::TrackRecorder::getInstance().listTracks(names, kMaxTracks);

    for (size_t i = 0; i < count; ++i)
    {
        s_record_names.push_back(names[i]);
    }
    update_record_page();
}

void update_route_status()
{
    auto& state = g_tracker_state;
    if (!state.route_status_label)
    {
        return;
    }
    lv_obj_set_style_text_font(state.route_status_label, &lv_font_noto_cjk_16_2bpp, 0);
    if (!state.active_route.empty())
    {
        lv_label_set_text_fmt(state.route_status_label, "Active: %s", state.active_route.c_str());
    }
    else if (!state.selected_route.empty())
    {
        lv_label_set_text_fmt(state.route_status_label, "Selected: %s", state.selected_route.c_str());
    }
    else
    {
        lv_label_set_text(state.route_status_label, "No route selected");
    }
    if (state.load_btn)
    {
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
    if (state.unload_btn)
    {
        const bool can_unload = !state.active_route.empty();
        if (can_unload)
        {
            lv_obj_clear_state(state.unload_btn, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_add_state(state.unload_btn, LV_STATE_DISABLED);
        }
    }
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
        set_placeholder_row(state.route_item_btns[0], state.route_item_labels[0], "No KML routes");
        for (int i = 1; i < kListPageSize; ++i)
        {
            if (state.route_item_btns[i])
            {
                lv_obj_add_flag(state.route_item_btns[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    else
    {
    for (int i = 0; i < kListPageSize; ++i)
    {
        lv_obj_t* btn = state.route_item_btns[i];
        lv_obj_t* label = state.route_item_labels[i];
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
                lv_obj_set_user_data(btn, reinterpret_cast<void*>((uintptr_t)idx));
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
    if (state.route_prev_btn)
    {
        if (show_pager)
        {
            lv_obj_clear_flag(state.route_prev_btn, LV_OBJ_FLAG_HIDDEN);
            if (state.route_page > 0)
            {
                lv_obj_clear_state(state.route_prev_btn, LV_STATE_DISABLED);
            }
            else
            {
                lv_obj_add_state(state.route_prev_btn, LV_STATE_DISABLED);
            }
        }
        else
        {
            lv_obj_add_flag(state.route_prev_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (state.route_next_btn)
    {
        if (show_pager)
        {
            lv_obj_clear_flag(state.route_next_btn, LV_OBJ_FLAG_HIDDEN);
            if (state.route_page < max_page)
            {
                lv_obj_clear_state(state.route_next_btn, LV_STATE_DISABLED);
            }
            else
            {
                lv_obj_add_state(state.route_next_btn, LV_STATE_DISABLED);
            }
        }
        else
        {
            lv_obj_add_flag(state.route_next_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void refresh_route_list()
{
    auto& state = g_tracker_state;
    if (!state.route_list)
    {
        return;
    }
    s_route_names.clear();
    state.selected_route_idx = -1;
    state.selected_route.clear();

    if (SD.cardType() == CARD_NONE)
    {
        set_placeholder_row(state.route_item_btns[0], state.route_item_labels[0], "No SD Card");
        for (int i = 1; i < kListPageSize; ++i)
        {
            if (state.route_item_btns[i])
            {
                lv_obj_add_flag(state.route_item_btns[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (state.route_prev_btn) lv_obj_add_flag(state.route_prev_btn, LV_OBJ_FLAG_HIDDEN);
        if (state.route_next_btn) lv_obj_add_flag(state.route_next_btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    File dir = SD.open(kRouteDir);
    if (!dir || !dir.isDirectory())
    {
        set_placeholder_row(state.route_item_btns[0], state.route_item_labels[0], "No routes folder");
        for (int i = 1; i < kListPageSize; ++i)
        {
            if (state.route_item_btns[i])
            {
                lv_obj_add_flag(state.route_item_btns[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (state.route_prev_btn) lv_obj_add_flag(state.route_prev_btn, LV_OBJ_FLAG_HIDDEN);
        if (state.route_next_btn) lv_obj_add_flag(state.route_next_btn, LV_OBJ_FLAG_HIDDEN);
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
        update_route_page();
        return;
    }

    std::sort(s_route_names.begin(), s_route_names.end());
    update_route_page();
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

void on_record_prev_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    if (state.record_page > 0)
    {
        state.record_page--;
        update_record_page();
        focus_main_panel();
    }
}

void on_record_next_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    state.record_page++;
    update_record_page();
    focus_main_panel();
}

void on_route_prev_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    if (state.route_page > 0)
    {
        state.route_page--;
        update_route_page();
        focus_main_panel();
    }
}

void on_record_back_clicked(lv_event_t*)
{
    focus_mode_panel();
}

void on_route_back_clicked(lv_event_t*)
{
    focus_mode_panel();
}

void on_record_back_key(lv_event_t* e)
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

void on_route_back_key(lv_event_t* e)
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

void on_route_next_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    state.route_page++;
    update_route_page();
    focus_main_panel();
}

void on_route_item_clicked(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    auto& state = g_tracker_state;
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    uintptr_t idx = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target));
    if (idx >= s_route_names.size())
    {
        return;
    }
    state.selected_route_idx = static_cast<int>(idx);
    state.selected_route = s_route_names[idx].c_str();
    update_route_status();
    update_route_page();
}

void on_record_item_focused(lv_event_t* e)
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
    const size_t idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(ud));
    if (idx >= s_record_names.size())
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(target, -1);
    if (!label)
    {
        return;
    }
    lv_label_set_text(label, s_record_names[idx].c_str());
    lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
}

void on_record_item_defocused(lv_event_t* e)
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
    const size_t idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(ud));
    if (idx >= s_record_names.size())
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(target, -1);
    if (!label)
    {
        return;
    }
    String display_name = format_list_name(s_record_names[idx]);
    lv_label_set_text(label, display_name.c_str());
    lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

void on_route_item_focused(lv_event_t* e)
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
    const size_t idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(ud));
    if (idx >= s_route_names.size())
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(target, -1);
    if (!label)
    {
        return;
    }
    lv_label_set_text(label, s_route_names[idx].c_str());
    lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
}

void on_route_item_defocused(lv_event_t* e)
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
    const size_t idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(ud));
    if (idx >= s_route_names.size())
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(target, -1);
    if (!label)
    {
        return;
    }
    String display_name = format_list_name(s_route_names[idx]);
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
        lv_label_set_text(state.route_status_label, "Select a route");
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
} // namespace

void refresh_page()
{
    sync_active_route_from_config();
    update_record_status();
    update_start_stop_button();
    refresh_record_list();
    refresh_route_list();
    update_route_status();
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
    state.body = layout::create_body(state.root);
    state.mode_panel = layout::create_mode_panel(state.body, kModePanelWidth);
    state.main_panel = layout::create_main_panel(state.body);

    state.mode_record_btn = lv_btn_create(state.mode_panel);
    lv_obj_set_width(state.mode_record_btn, LV_PCT(100));
    lv_obj_set_height(state.mode_record_btn, kModeButtonHeight);
    state.mode_record_label = lv_label_create(state.mode_record_btn);
    lv_label_set_text(state.mode_record_label, "Record");
    lv_obj_center(state.mode_record_label);

    state.mode_route_btn = lv_btn_create(state.mode_panel);
    lv_obj_set_width(state.mode_route_btn, LV_PCT(100));
    lv_obj_set_height(state.mode_route_btn, kModeButtonHeight);
    state.mode_route_label = lv_label_create(state.mode_route_btn);
    lv_label_set_text(state.mode_route_label, "Route");
    lv_obj_center(state.mode_route_label);

    state.record_panel = layout::create_section(state.main_panel);
    lv_obj_set_style_pad_all(state.record_panel, 2, 0);
    lv_obj_set_style_pad_row(state.record_panel, 4, 0);
    state.record_status_label = lv_label_create(state.record_panel);
    lv_label_set_text(state.record_status_label, "Stopped");
    lv_obj_set_style_text_font(state.record_status_label, &lv_font_noto_cjk_16_2bpp, 0);

    state.record_list = lv_obj_create(state.record_panel);
    lv_obj_set_width(state.record_list, LV_PCT(100));
    lv_obj_set_height(state.record_list, kListHeight);
    lv_obj_set_flex_flow(state.record_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state.record_list, kListItemGap, 0);
    lv_obj_set_style_pad_all(state.record_list, 0, 0);
    lv_obj_clear_flag(state.record_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(state.record_list, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < kListPageSize; ++i)
    {
        lv_obj_t* btn = lv_btn_create(state.record_list);
        lv_obj_set_size(btn, LV_PCT(100), kListItemHeight);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* label = lv_label_create(btn);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, kListLabelWidth);
        apply_list_button(btn);
        state.record_item_btns[i] = btn;
        state.record_item_labels[i] = label;
        lv_obj_add_event_cb(btn, [](lv_event_t*) {}, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(btn, on_record_item_focused, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(btn, on_record_item_defocused, LV_EVENT_DEFOCUSED, nullptr);
    }

    lv_obj_t* record_pager = lv_obj_create(state.record_panel);
    lv_obj_set_width(record_pager, LV_PCT(100));
    lv_obj_set_height(record_pager, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(record_pager, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(record_pager, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(record_pager, 0, 0);
    lv_obj_set_style_bg_opa(record_pager, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(record_pager, LV_OBJ_FLAG_SCROLLABLE);

    state.record_back_btn = lv_btn_create(record_pager);
    lv_obj_set_size(state.record_back_btn, 80, kListItemHeight);
    state.record_back_label = lv_label_create(state.record_back_btn);
    lv_label_set_text(state.record_back_label, "Back");
    lv_obj_center(state.record_back_label);
    apply_action_button(state.record_back_btn, state.record_back_label);

    state.record_prev_btn = lv_btn_create(record_pager);
    lv_obj_set_size(state.record_prev_btn, 96, kListItemHeight);
    state.record_prev_label = lv_label_create(state.record_prev_btn);
    lv_label_set_text(state.record_prev_label, "Prev");
    lv_obj_center(state.record_prev_label);
    apply_action_button(state.record_prev_btn, state.record_prev_label);

    state.record_next_btn = lv_btn_create(record_pager);
    lv_obj_set_size(state.record_next_btn, 96, kListItemHeight);
    state.record_next_label = lv_label_create(state.record_next_btn);
    lv_label_set_text(state.record_next_label, "Next");
    lv_obj_center(state.record_next_label);
    apply_action_button(state.record_next_btn, state.record_next_label);

    lv_obj_t* record_spacer = lv_obj_create(state.record_panel);
    lv_obj_set_width(record_spacer, LV_PCT(100));
    lv_obj_set_height(record_spacer, 0);
    lv_obj_set_flex_grow(record_spacer, 1);
    lv_obj_set_style_border_width(record_spacer, 0, 0);
    lv_obj_set_style_pad_all(record_spacer, 0, 0);
    lv_obj_set_style_bg_opa(record_spacer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(record_spacer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* record_action_row = lv_obj_create(state.record_panel);
    lv_obj_set_width(record_action_row, LV_PCT(100));
    lv_obj_set_height(record_action_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(record_action_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(record_action_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(record_action_row, 0, 0);
    lv_obj_set_style_bg_opa(record_action_row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(record_action_row, LV_OBJ_FLAG_SCROLLABLE);

    state.start_stop_btn = lv_btn_create(record_action_row);
    lv_obj_set_height(state.start_stop_btn, kPrimaryButtonHeight);
    state.start_stop_label = lv_label_create(state.start_stop_btn);
    lv_obj_center(state.start_stop_label);
    apply_action_button(state.start_stop_btn, state.start_stop_label);

    state.route_panel = layout::create_section(state.main_panel);
    lv_obj_set_style_pad_all(state.route_panel, 2, 0);
    lv_obj_set_style_pad_row(state.route_panel, 4, 0);
    state.route_status_label = lv_label_create(state.route_panel);
    lv_label_set_text(state.route_status_label, "No route selected");
    lv_obj_set_style_text_font(state.route_status_label, &lv_font_noto_cjk_16_2bpp, 0);

    state.route_list = lv_obj_create(state.route_panel);
    lv_obj_set_width(state.route_list, LV_PCT(100));
    lv_obj_set_height(state.route_list, kListHeight);
    lv_obj_set_flex_flow(state.route_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state.route_list, kListItemGap, 0);
    lv_obj_set_style_pad_all(state.route_list, 0, 0);
    lv_obj_clear_flag(state.route_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(state.route_list, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < kListPageSize; ++i)
    {
        lv_obj_t* btn = lv_btn_create(state.route_list);
        lv_obj_set_size(btn, LV_PCT(100), kListItemHeight);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* label = lv_label_create(btn);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, kListLabelWidth);
        apply_list_button(btn);
        state.route_item_btns[i] = btn;
        state.route_item_labels[i] = label;
        lv_obj_add_event_cb(btn, on_route_item_clicked, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(btn, on_route_item_focused, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(btn, on_route_item_defocused, LV_EVENT_DEFOCUSED, nullptr);
    }

    lv_obj_t* route_pager = lv_obj_create(state.route_panel);
    lv_obj_set_width(route_pager, LV_PCT(100));
    lv_obj_set_height(route_pager, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(route_pager, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(route_pager, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(route_pager, 0, 0);
    lv_obj_set_style_bg_opa(route_pager, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(route_pager, LV_OBJ_FLAG_SCROLLABLE);

    state.route_back_btn = lv_btn_create(route_pager);
    lv_obj_set_size(state.route_back_btn, 80, kListItemHeight);
    state.route_back_label = lv_label_create(state.route_back_btn);
    lv_label_set_text(state.route_back_label, "Back");
    lv_obj_center(state.route_back_label);
    apply_action_button(state.route_back_btn, state.route_back_label);

    state.route_prev_btn = lv_btn_create(route_pager);
    lv_obj_set_size(state.route_prev_btn, 96, kListItemHeight);
    state.route_prev_label = lv_label_create(state.route_prev_btn);
    lv_label_set_text(state.route_prev_label, "Prev");
    lv_obj_center(state.route_prev_label);
    apply_action_button(state.route_prev_btn, state.route_prev_label);

    state.route_next_btn = lv_btn_create(route_pager);
    lv_obj_set_size(state.route_next_btn, 96, kListItemHeight);
    state.route_next_label = lv_label_create(state.route_next_btn);
    lv_label_set_text(state.route_next_label, "Next");
    lv_obj_center(state.route_next_label);
    apply_action_button(state.route_next_btn, state.route_next_label);

    lv_obj_t* route_spacer = lv_obj_create(state.route_panel);
    lv_obj_set_width(route_spacer, LV_PCT(100));
    lv_obj_set_height(route_spacer, 0);
    lv_obj_set_flex_grow(route_spacer, 1);
    lv_obj_set_style_border_width(route_spacer, 0, 0);
    lv_obj_set_style_pad_all(route_spacer, 0, 0);
    lv_obj_set_style_bg_opa(route_spacer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(route_spacer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* route_action_row = lv_obj_create(state.route_panel);
    lv_obj_set_width(route_action_row, LV_PCT(100));
    lv_obj_set_height(route_action_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(route_action_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(route_action_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(route_action_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(route_action_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(route_action_row, LV_OBJ_FLAG_SCROLLABLE);

    state.load_btn = lv_btn_create(route_action_row);
    lv_obj_set_size(state.load_btn, 90, kSecondaryButtonHeight);
    state.load_label = lv_label_create(state.load_btn);
    lv_label_set_text(state.load_label, "Load");
    lv_obj_center(state.load_label);
    apply_action_button(state.load_btn, state.load_label);

    state.unload_btn = lv_btn_create(route_action_row);
    lv_obj_set_size(state.unload_btn, 90, kSecondaryButtonHeight);
    state.unload_label = lv_label_create(state.unload_btn);
    lv_label_set_text(state.unload_label, "Disable");
    lv_obj_center(state.unload_label);
    apply_action_button(state.unload_btn, state.unload_label);

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
    lv_obj_add_event_cb(state.record_back_btn, on_record_back_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.record_back_btn, on_record_back_key, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(state.record_prev_btn, on_record_prev_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.record_next_btn, on_record_next_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.route_back_btn, on_route_back_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.route_back_btn, on_route_back_key, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(state.route_prev_btn, on_route_prev_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.route_next_btn, on_route_next_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.start_stop_btn, on_start_stop_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.load_btn, on_route_load_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.unload_btn, on_route_unload_clicked, LV_EVENT_CLICKED, nullptr);

    set_mode(TrackerPageState::Mode::Record);
    refresh_page();
    focus_mode_panel();
    state.initialized = true;
}

void cleanup_page()
{
    auto& state = g_tracker_state;
    if (state.root)
    {
        lv_obj_del(state.root);
    }
    state = TrackerPageState{};
}

} // namespace components
} // namespace ui
} // namespace tracker
