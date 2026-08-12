#if defined(ARDUINO_T_DECK_PRO)

#include "ui/tdeck_pro/text_shell.h"

#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/tdeck_pro/text_font.h"

#include <cstdio>
#include <cstring>

namespace ui::tdeck_pro::text_shell
{
namespace
{

constexpr size_t kMaxApps = 16;
constexpr lv_coord_t kScreenWidth = 240;
constexpr lv_coord_t kScreenHeight = 320;
constexpr lv_coord_t kMargin = 8;
constexpr lv_coord_t kContentWidth = kScreenWidth - (kMargin * 2);
constexpr lv_coord_t kHeaderHeight = 40;
constexpr lv_coord_t kListTop = 56;
constexpr lv_coord_t kListHeight = 208;
constexpr lv_coord_t kRowHeight = 18;
constexpr lv_coord_t kDetailTop = 272;
constexpr lv_coord_t kCommandTop = 296;

struct AppRow
{
    AppScreen* app = nullptr;
    lv_obj_t* button = nullptr;
    lv_obj_t* label = nullptr;
};

struct State
{
    InitOptions options{};
    lv_obj_t* menu_panel = nullptr;
    lv_obj_t* app_panel = nullptr;
    lv_obj_t* list = nullptr;
    lv_obj_t* time_label = nullptr;
    lv_obj_t* battery_label = nullptr;
    lv_obj_t* node_label = nullptr;
    lv_obj_t* detail_label = nullptr;
    lv_obj_t* command_label = nullptr;
    AppRow rows[kMaxApps]{};
    size_t row_count = 0;
    char node_text[32] = "NODE --";
    bool walkie_recording = false;
};

State s_state;

bool valid(const lv_obj_t* object)
{
    return object != nullptr && lv_obj_is_valid(const_cast<lv_obj_t*>(object));
}

void set_label_text_if_changed(lv_obj_t* label, const char* text)
{
    if (!valid(label))
    {
        return;
    }

    const char* safe = text ? text : "";
    const char* current = lv_label_get_text(label);
    if (current != nullptr && std::strcmp(current, safe) == 0)
    {
        return;
    }

    lv_label_set_text(label, safe);
    ::ui::fonts::apply_localized_font(label, safe, text_font());
}

lv_obj_t* make_label(lv_obj_t* parent, lv_coord_t width, lv_text_align_t alignment)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, text_font(), 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_text_align(label, alignment, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

void style_paper_panel(lv_obj_t* object)
{
    lv_obj_set_style_bg_color(object, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

const char* description_for(const AppScreen* app)
{
    const char* id = app ? app->stable_id() : nullptr;
    if (id == nullptr)
    {
        return "APPLICATION";
    }
    if (std::strcmp(id, "chat") == 0)
    {
        return "MESSAGES AND CONVERSATIONS";
    }
    if (std::strcmp(id, "map") == 0)
    {
        return "MAP AND GPS POSITION";
    }
    if (std::strcmp(id, "sky_plot") == 0)
    {
        return "GPS SATELLITES";
    }
    if (std::strcmp(id, "contacts") == 0)
    {
        return "PEERS AND CONTACTS";
    }
    if (std::strcmp(id, "team") == 0)
    {
        return "TEAM STATUS";
    }
    if (std::strcmp(id, "tracker") == 0)
    {
        return "TRACK RECORDING";
    }
    if (std::strcmp(id, "sstv") == 0)
    {
        return "IMAGE RADIO";
    }
    if (std::strcmp(id, "energy_sweep") == 0)
    {
        return "RADIO SCAN";
    }
    if (std::strcmp(id, "walkie_talkie") == 0)
    {
        return "VOICE RADIO";
    }
    if (std::strcmp(id, "usb_mass_storage") == 0)
    {
        return "USB STORAGE";
    }
    if (std::strcmp(id, "extensions") == 0)
    {
        return "EXTENSIONS";
    }
    if (std::strcmp(id, "network") == 0)
    {
        return "NETWORK";
    }
    if (std::strcmp(id, "settings") == 0)
    {
        return "DEVICE SETTINGS";
    }
    if (std::strcmp(id, "shutdown") == 0)
    {
        return "POWER OFF";
    }
    return "APPLICATION";
}

int row_index_for(const lv_obj_t* button)
{
    for (size_t index = 0; index < s_state.row_count; ++index)
    {
        if (s_state.rows[index].button == button)
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void update_detail_for(size_t index)
{
    if (index >= s_state.row_count || !valid(s_state.detail_label))
    {
        return;
    }

    const AppScreen* app = s_state.rows[index].app;
    char text[64] = {};
    std::snprintf(text,
                  sizeof(text),
                  "%02u/%02u %s",
                  static_cast<unsigned>(index + 1U),
                  static_cast<unsigned>(s_state.row_count),
                  description_for(app));
    set_label_text_if_changed(s_state.detail_label, ::ui::i18n::tr(text));
}

void set_row_selected(AppRow& row, bool selected)
{
    if (!valid(row.button) || !valid(row.label))
    {
        return;
    }

    lv_obj_set_style_bg_color(row.button, selected ? lv_color_black() : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(row.button, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(row.label, selected ? lv_color_white() : lv_color_black(), 0);
}

void on_row_focus(lv_event_t* event)
{
    if (lv_event_get_code(event) != LV_EVENT_FOCUSED)
    {
        return;
    }

    lv_obj_t* button = lv_event_get_target_obj(event);
    const int index = row_index_for(button);
    if (index < 0)
    {
        return;
    }

    for (size_t row = 0; row < s_state.row_count; ++row)
    {
        set_row_selected(s_state.rows[row], static_cast<int>(row) == index);
    }
    update_detail_for(static_cast<size_t>(index));
    lv_obj_scroll_to_view(button, LV_ANIM_OFF);
}

void on_row_defocus(lv_event_t* event)
{
    if (lv_event_get_code(event) != LV_EVENT_DEFOCUSED)
    {
        return;
    }

    const int index = row_index_for(lv_event_get_target_obj(event));
    if (index >= 0)
    {
        set_row_selected(s_state.rows[static_cast<size_t>(index)], false);
    }
}

void show_app(AppScreen* app)
{
    if (app == nullptr || !valid(s_state.app_panel))
    {
        return;
    }

    if (app->launch_mode() == ::ui::AppLaunchMode::MenuOverlay)
    {
        app->enter(lv_screen_active());
        return;
    }

    set_default_group(nullptr);
    ui_switch_to_app(app, s_state.app_panel);
    if (s_state.options.set_menu_visible != nullptr)
    {
        s_state.options.set_menu_visible(false);
    }
    else
    {
        set_visible(false);
    }
    if (main_screen != nullptr)
    {
        lv_tileview_set_tile_by_index(main_screen, 0, 1, LV_ANIM_OFF);
    }
}

void on_row_click(lv_event_t* event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }

    show_app(static_cast<AppScreen*>(lv_event_get_user_data(event)));
}

void create_panels()
{
    main_screen = lv_tileview_create(lv_screen_active());
    lv_obj_set_size(main_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(main_screen, LV_ALIGN_TOP_LEFT, 0, 0);
    style_paper_panel(main_screen);
    lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_state.menu_panel = lv_tileview_add_tile(main_screen, 0, 0, LV_DIR_HOR);
    style_paper_panel(s_state.menu_panel);

    s_state.app_panel = lv_tileview_add_tile(main_screen, 0, 1, LV_DIR_HOR);
    style_paper_panel(s_state.app_panel);
}

void create_header()
{
    lv_obj_t* header = lv_obj_create(s_state.menu_panel);
    lv_obj_set_pos(header, kMargin, kMargin);
    lv_obj_set_size(header, kContentWidth, kHeaderHeight);
    style_paper_panel(header);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_black(), 0);

    lv_obj_t* title = make_label(header, 112, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(title, 0, 0);
    set_label_text_if_changed(title, "TRAIL MATE");

    s_state.time_label = make_label(header, 96, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(s_state.time_label, 128, 0);
    set_label_text_if_changed(s_state.time_label, "--:--");

    s_state.node_label = make_label(header, 144, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_state.node_label, 0, 18);
    set_label_text_if_changed(s_state.node_label, s_state.node_text);

    s_state.battery_label = make_label(header, 80, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(s_state.battery_label, 144, 18);
    set_label_text_if_changed(s_state.battery_label, "BAT --");
}

void create_footer()
{
    s_state.detail_label = make_label(s_state.menu_panel, kContentWidth, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_state.detail_label, kMargin, kDetailTop);
    set_label_text_if_changed(s_state.detail_label, "00/00 APPLICATION");

    lv_obj_t* command_rule = lv_obj_create(s_state.menu_panel);
    lv_obj_set_pos(command_rule, kMargin, kCommandTop - 1);
    lv_obj_set_size(command_rule, kContentWidth, 1);
    lv_obj_set_style_bg_color(command_rule, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(command_rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(command_rule, 0, 0);
    lv_obj_clear_flag(command_rule, LV_OBJ_FLAG_SCROLLABLE);

    s_state.command_label = make_label(s_state.menu_panel, kContentWidth, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_state.command_label, kMargin, kCommandTop);
    set_label_text_if_changed(s_state.command_label, "UP/DN MOVE ENT OPEN H HELP");
}

void create_rows()
{
    s_state.list = lv_obj_create(s_state.menu_panel);
    lv_obj_set_pos(s_state.list, kMargin, kListTop);
    lv_obj_set_size(s_state.list, kContentWidth, kListHeight);
    style_paper_panel(s_state.list);
    lv_obj_set_scroll_dir(s_state.list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_state.list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(s_state.list, 0, 0);
    lv_obj_set_style_pad_row(s_state.list, 0, 0);
    lv_obj_set_flex_flow(s_state.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_state.list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    const size_t app_count = ui::catalogCount(s_state.options.apps);
    for (size_t index = 0; index < app_count && s_state.row_count < kMaxApps; ++index)
    {
        AppScreen* app = ui::catalogAt(s_state.options.apps, index);
        if (app == nullptr)
        {
            continue;
        }

        AppRow& row = s_state.rows[s_state.row_count];
        row.app = app;
        row.button = lv_btn_create(s_state.list);
        lv_obj_set_size(row.button, kContentWidth, kRowHeight);
        lv_obj_set_style_bg_color(row.button, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(row.button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row.button, 0, 0);
        lv_obj_set_style_radius(row.button, 0, 0);
        lv_obj_set_style_shadow_width(row.button, 0, 0);
        lv_obj_set_style_pad_left(row.button, 4, 0);
        lv_obj_set_style_pad_right(row.button, 4, 0);
        lv_obj_set_style_pad_top(row.button, 0, 0);
        lv_obj_set_style_pad_bottom(row.button, 0, 0);
        lv_obj_clear_flag(row.button, LV_OBJ_FLAG_SCROLLABLE);

        row.label = make_label(row.button, kContentWidth - 8, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(row.label, LV_ALIGN_LEFT_MID, 0, 0);
        ::ui::i18n::set_label_text(row.label, app->name());
        ::ui::fonts::apply_localized_font(row.label, app->name(), text_font());

        lv_obj_add_event_cb(row.button, on_row_focus, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(row.button, on_row_defocus, LV_EVENT_DEFOCUSED, nullptr);
        lv_obj_add_event_cb(row.button, on_row_click, LV_EVENT_CLICKED, app);
        lv_group_add_obj(menu_g, row.button);
        ++s_state.row_count;
    }

    if (s_state.row_count > 0)
    {
        lv_group_focus_obj(s_state.rows[0].button);
    }
}

AppScreen* find_app(const char* stable_id)
{
    if (stable_id == nullptr || stable_id[0] == '\0')
    {
        return nullptr;
    }

    for (size_t index = 0; index < s_state.row_count; ++index)
    {
        AppScreen* app = s_state.rows[index].app;
        if (app != nullptr && app->stable_id() != nullptr &&
            std::strcmp(app->stable_id(), stable_id) == 0)
        {
            return app;
        }
    }
    return nullptr;
}

} // namespace

void init(const InitOptions& options)
{
    s_state = State();
    s_state.options = options;

    menu_g = lv_group_create();
    app_g = lv_group_create();
    set_default_group(menu_g);

    create_panels();
    create_header();
    create_rows();
    create_footer();
}

lv_obj_t* menu_panel()
{
    return s_state.menu_panel;
}

bool launch_app_by_stable_id(const char* stable_id)
{
    AppScreen* app = find_app(stable_id);
    if (app == nullptr || app->launch_mode() != ::ui::AppLaunchMode::Screen)
    {
        return false;
    }
    show_app(app);
    return ui_get_active_app() == app;
}

void refresh_localized_text()
{
    for (size_t index = 0; index < s_state.row_count; ++index)
    {
        AppRow& row = s_state.rows[index];
        if (row.app == nullptr || !valid(row.label))
        {
            continue;
        }
        ::ui::i18n::set_label_text(row.label, row.app->name());
        ::ui::fonts::apply_localized_font(row.label, row.app->name(), text_font());
    }

    if (menu_g != nullptr)
    {
        const int index = row_index_for(lv_group_get_focused(menu_g));
        if (index >= 0)
        {
            update_detail_for(static_cast<size_t>(index));
        }
    }
}

void set_visible(bool visible)
{
    if (valid(s_state.menu_panel))
    {
        if (visible)
        {
            lv_obj_clear_flag(s_state.menu_panel, LV_OBJ_FLAG_HIDDEN);
            if (menu_g != nullptr && lv_group_get_focused(menu_g) == nullptr && s_state.row_count > 0)
            {
                lv_group_focus_obj(s_state.rows[0].button);
            }
        }
        else
        {
            lv_obj_add_flag(s_state.menu_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void bring_content_to_front()
{
    if (valid(s_state.list))
    {
        lv_obj_move_foreground(s_state.list);
    }
    if (valid(s_state.detail_label))
    {
        lv_obj_move_foreground(s_state.detail_label);
    }
    if (valid(s_state.command_label))
    {
        lv_obj_move_foreground(s_state.command_label);
    }
}

void set_time_text(const char* text)
{
    set_label_text_if_changed(s_state.time_label, text);
}

void set_battery_text(const char* text)
{
    set_label_text_if_changed(s_state.battery_label, text);
}

void set_walkie_recording(bool recording)
{
    if (s_state.walkie_recording == recording)
    {
        return;
    }
    s_state.walkie_recording = recording;
    set_label_text_if_changed(
        s_state.command_label,
        recording ? "PTT TRANSMIT  RELEASE STOP" : "UP/DN MOVE ENT OPEN H HELP");
}

void set_node_text(const char* text)
{
    std::snprintf(s_state.node_text, sizeof(s_state.node_text), "NODE %s", text ? text : "--");
    set_label_text_if_changed(s_state.node_label, s_state.node_text);
}

void set_help_text(const char* text)
{
    (void)text;
}

} // namespace ui::tdeck_pro::text_shell

#endif // defined(ARDUINO_T_DECK_PRO)
