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
constexpr int kPrimaryButtonHeight = 44;
constexpr int kSecondaryButtonHeight = 34;
constexpr const char* kRouteDir = "/routes";
std::vector<String> s_route_names;

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

void update_focus_group_for_mode()
{
    lv_group_t* group = ::app_g;
    if (!group)
    {
        return;
    }
    auto remove_if = [](lv_obj_t* obj)
    {
        if (obj)
        {
            lv_group_remove_obj(obj);
        }
    };
    auto add_if = [group](lv_obj_t* obj)
    {
        if (obj)
        {
            lv_group_add_obj(group, obj);
        }
    };

    remove_if(g_tracker_state.start_stop_btn);
    remove_if(g_tracker_state.record_list);
    remove_if(g_tracker_state.route_list);
    remove_if(g_tracker_state.load_btn);
    remove_if(g_tracker_state.unload_btn);

    if (g_tracker_state.mode == TrackerPageState::Mode::Record)
    {
        add_if(g_tracker_state.start_stop_btn);
        add_if(g_tracker_state.record_list);
    }
    else
    {
        add_if(g_tracker_state.route_list);
        add_if(g_tracker_state.load_btn);
        add_if(g_tracker_state.unload_btn);
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
    update_focus_group_for_mode();
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

void refresh_record_list()
{
    auto& state = g_tracker_state;
    if (!state.record_list)
    {
        return;
    }
    lv_obj_clean(state.record_list);

    if (SD.cardType() == CARD_NONE)
    {
        lv_obj_t* label = lv_label_create(state.record_list);
        lv_label_set_text(label, "No SD Card");
        lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
        return;
    }

    constexpr size_t kMaxTracks = 32;
    String names[kMaxTracks];
    const size_t count = gps::TrackRecorder::getInstance().listTracks(names, kMaxTracks);

    if (count == 0)
    {
        lv_obj_t* label = lv_label_create(state.record_list);
        lv_label_set_text(label, "No tracks yet");
        lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
        return;
    }

    for (size_t i = 0; i < count; ++i)
    {
        lv_obj_t* btn = lv_list_add_btn(state.record_list, LV_SYMBOL_FILE, names[i].c_str());
        apply_list_button(btn);
        if (lv_obj_t* label = lv_obj_get_child(btn, -1))
        {
            lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
        }
        lv_obj_add_event_cb(btn, [](lv_event_t*) {}, LV_EVENT_CLICKED, nullptr);
    }
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

void refresh_route_list()
{
    auto& state = g_tracker_state;
    if (!state.route_list)
    {
        return;
    }
    lv_obj_clean(state.route_list);
    s_route_names.clear();
    state.selected_route_idx = -1;
    state.selected_route.clear();

    if (SD.cardType() == CARD_NONE)
    {
        lv_obj_t* label = lv_label_create(state.route_list);
        lv_label_set_text(label, "No SD Card");
        lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
        return;
    }

    File dir = SD.open(kRouteDir);
    if (!dir || !dir.isDirectory())
    {
        lv_obj_t* label = lv_label_create(state.route_list);
        lv_label_set_text(label, "No routes folder");
        lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
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
        lv_obj_t* label = lv_label_create(state.route_list);
        lv_label_set_text(label, "No KML routes");
        lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
        return;
    }

    std::sort(s_route_names.begin(), s_route_names.end());
    for (size_t i = 0; i < s_route_names.size(); ++i)
    {
        lv_obj_t* btn = lv_list_add_btn(state.route_list, LV_SYMBOL_FILE, s_route_names[i].c_str());
        apply_list_button(btn);
        if (lv_obj_t* label = lv_obj_get_child(btn, -1))
        {
            lv_obj_set_style_text_font(label, &lv_font_noto_cjk_16_2bpp, 0);
        }
        lv_obj_add_event_cb(
            btn,
            [](lv_event_t* e)
            {
                auto& s = g_tracker_state;
                const uintptr_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
                if (idx >= s_route_names.size())
                {
                    return;
                }
                s.selected_route_idx = static_cast<int>(idx);
                s.selected_route = s_route_names[idx].c_str();
                update_route_status();
            },
            LV_EVENT_CLICKED,
            reinterpret_cast<void*>(i));
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
    if (g_tracker_state.start_stop_btn)
    {
        lv_group_focus_obj(g_tracker_state.start_stop_btn);
    }
    else if (g_tracker_state.record_list)
    {
        lv_group_focus_obj(g_tracker_state.record_list);
    }
}

void on_mode_route_clicked(lv_event_t*)
{
    if (g_tracker_state.route_list)
    {
        lv_group_focus_obj(g_tracker_state.route_list);
    }
    else if (g_tracker_state.load_btn)
    {
        lv_group_focus_obj(g_tracker_state.load_btn);
    }
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
    state.record_status_label = lv_label_create(state.record_panel);
    lv_label_set_text(state.record_status_label, "Stopped");
    state.start_stop_btn = lv_btn_create(state.record_panel);
    lv_obj_set_height(state.start_stop_btn, kPrimaryButtonHeight);
    state.start_stop_label = lv_label_create(state.start_stop_btn);
    lv_obj_center(state.start_stop_label);
    apply_action_button(state.start_stop_btn, state.start_stop_label);
    state.record_list = lv_list_create(state.record_panel);
    lv_obj_set_flex_grow(state.record_list, 1);

    state.route_panel = layout::create_section(state.main_panel);
    state.route_status_label = lv_label_create(state.route_panel);
    lv_label_set_text(state.route_status_label, "No route selected");
    state.route_list = lv_list_create(state.route_panel);
    lv_obj_set_flex_grow(state.route_list, 1);

    lv_obj_t* route_btn_row = lv_obj_create(state.route_panel);
    lv_obj_set_width(route_btn_row, LV_PCT(100));
    lv_obj_set_height(route_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(route_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(route_btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(route_btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(route_btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(route_btn_row, LV_OBJ_FLAG_SCROLLABLE);

    state.load_btn = lv_btn_create(route_btn_row);
    lv_obj_set_size(state.load_btn, 90, kSecondaryButtonHeight);
    state.load_label = lv_label_create(state.load_btn);
    lv_label_set_text(state.load_label, "Load");
    lv_obj_center(state.load_label);
    apply_action_button(state.load_btn, state.load_label);

    state.unload_btn = lv_btn_create(route_btn_row);
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
    lv_obj_add_event_cb(state.start_stop_btn, on_start_stop_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.load_btn, on_route_load_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.unload_btn, on_route_unload_clicked, LV_EVENT_CLICKED, nullptr);

    input::bind_focus(app_g);
    set_mode(TrackerPageState::Mode::Record);
    refresh_page();
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
