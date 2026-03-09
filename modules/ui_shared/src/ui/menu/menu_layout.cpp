#include "ui/menu/menu_layout.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ui/app_runtime.h"
#include "ui/menu/menu_profile.h"
#include "ui/ui_common.h"
#include "ui/ui_theme.h"
#include "ui/ui_status.h"

namespace ui
{
namespace menu_layout
{
namespace
{

constexpr size_t kMaxMenuApps = 16;

struct MenuAppUi
{
    const char* name = nullptr;
    lv_obj_t* icon = nullptr;
};

lv_obj_t* s_menu_panel = nullptr;
lv_obj_t* s_app_panel = nullptr;
lv_obj_t* s_desc_label = nullptr;
lv_obj_t* s_node_id_label = nullptr;
MenuAppUi s_menu_apps[kMaxMenuApps];
InitOptions s_init_options{};

#if LVGL_VERSION_MAJOR == 9
uint32_t s_name_change_id = 0;
#endif
AppScreen* s_pending_app_launch = nullptr;

lv_anim_enable_t transition_anim()
{
    return ui::menu_profile::current().input_mode == ui::menu_profile::InputMode::TouchPrimary
               ? LV_ANIM_OFF
               : LV_ANIM_ON;
}

void enterPendingApp(void* user_data)
{
    auto* target_app = static_cast<AppScreen*>(user_data);
    if (target_app == nullptr || target_app != s_pending_app_launch || s_app_panel == nullptr)
    {
        return;
    }

    s_pending_app_launch = nullptr;
    ui_switch_to_app(target_app, s_app_panel);
}

void menuHidden()
{
    lv_tileview_set_tile_by_index(main_screen, 0, 1, transition_anim());
}

void menuNameLabelEventCallback(lv_event_t* e)
{
#if LVGL_VERSION_MAJOR == 9
    const char* value = static_cast<const char*>(lv_event_get_param(e));
    if (value)
    {
        lv_label_set_text(lv_event_get_target_obj(e), value);
    }
#else
    (void)e;
#endif
}

void menuButtonFocusCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_FOCUSED || s_desc_label == nullptr)
    {
        return;
    }

    auto* data = static_cast<MenuAppUi*>(lv_event_get_user_data(e));
    const char* text = data ? data->name : nullptr;
#if LVGL_VERSION_MAJOR == 9
    lv_obj_send_event(s_desc_label, static_cast<lv_event_code_t>(s_name_change_id), (void*)text);
#else
    (void)text;
#endif
}

void menuButtonClickCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_app_panel == nullptr)
    {
        return;
    }

    auto* target_app = static_cast<AppScreen*>(lv_event_get_user_data(e));
    if (main_screen != nullptr && lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN))
    {
        return;
    }

    set_default_group(nullptr);
    if (ui::menu_profile::current().input_mode == ui::menu_profile::InputMode::TouchPrimary)
    {
        s_pending_app_launch = target_app;
        menuHidden();
        lv_async_call(enterPendingApp, target_app);
        return;
    }

    ui_switch_to_app(target_app, s_app_panel);
    menuHidden();
}

void createAppButton(lv_obj_t* parent, AppScreen* app, size_t idx)
{
    const auto& profile = ui::menu_profile::current();
    const char* name = app ? app->name() : "";
    const lv_image_dsc_t* image = app ? app->icon() : nullptr;

    lv_obj_t* btn = lv_btn_create(parent);
    const lv_coord_t width = profile.card_width;
    const lv_coord_t height = profile.card_height;

    lv_obj_set_size(btn, width, height);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, ui::theme::surface(), 0);
    lv_obj_set_style_border_width(btn, profile.card_border_width, 0);
    lv_obj_set_style_border_color(btn, ui::theme::border(), 0);
    lv_obj_set_style_radius(btn, profile.card_radius, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, ui::theme::accent(), LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(btn, ui::theme::border(), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn, ui::theme::accent(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(btn, ui::theme::border(), LV_STATE_FOCUS_KEY);

    if (profile.transparent_cards)
    {
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    }

    if (profile.show_card_label)
    {
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(btn, profile.card_pad_left, 0);
        lv_obj_set_style_pad_right(btn, profile.card_pad_right, 0);
        lv_obj_set_style_pad_top(btn, profile.card_pad_top, 0);
        lv_obj_set_style_pad_bottom(btn, profile.card_pad_bottom, 0);
        lv_obj_set_style_pad_row(btn, profile.card_pad_row, 0);
    }

    if (profile.transparent_cards)
    {
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
    }

    lv_obj_t* icon = nullptr;
    if (image != nullptr)
    {
        icon = lv_image_create(btn);
        lv_image_set_src(icon, image);

        if (profile.icon_scale != 256)
        {
            const lv_coord_t icon_width = lv_image_get_src_width(icon);
            const lv_coord_t icon_height = lv_image_get_src_height(icon);
            lv_obj_set_style_transform_pivot_x(icon, icon_width / 2, 0);
            lv_obj_set_style_transform_pivot_y(icon, icon_height / 2, 0);
            lv_obj_set_style_transform_scale(icon, profile.icon_scale, 0);
        }

        if (profile.show_card_label)
        {
            if (profile.variant != ui::menu_profile::LayoutVariant::LargeTouchGrid)
            {
                lv_obj_set_flex_grow(icon, 1);
            }
        }
        else
        {
            lv_obj_center(icon);
        }
    }

    if (profile.show_card_label && name && name[0] != '\0')
    {
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, name);
        lv_obj_set_width(label, width - (profile.variant == ui::menu_profile::LayoutVariant::LargeTouchGrid ? 16 : 4));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(label, profile.card_label_font, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x6B4A1E), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x6B4A1E), LV_STATE_FOCUSED);
        lv_label_set_long_mode(label,
                               profile.input_mode == ui::menu_profile::InputMode::TouchPrimary
                                   ? LV_LABEL_LONG_CLIP
                                   : LV_LABEL_LONG_SCROLL_CIRCULAR);
    }

    if (idx < kMaxMenuApps)
    {
        s_menu_apps[idx].name = name;
        s_menu_apps[idx].icon = icon;
        lv_obj_set_user_data(btn, &s_menu_apps[idx]);
    }

    if (icon != nullptr && name != nullptr && strcmp(name, "Chat") == 0)
    {
        lv_obj_t* badge = lv_obj_create(btn);
        lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(badge, lv_color_hex(0xE53935), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_left(badge, profile.badge_pad_h, 0);
        lv_obj_set_style_pad_right(badge, profile.badge_pad_h, 0);
        lv_obj_set_style_pad_top(badge, profile.badge_pad_v, 0);
        lv_obj_set_style_pad_bottom(badge, profile.badge_pad_v, 0);
        lv_obj_set_style_min_width(badge, profile.badge_min_size, 0);
        lv_obj_set_style_min_height(badge, profile.badge_min_size, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(badge, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_move_foreground(badge);
        lv_obj_update_layout(btn);
        if (profile.badge_anchor_mode == ui::menu_profile::BadgeAnchorMode::IconTopRight)
        {
            lv_obj_align_to(badge,
                            icon,
                            LV_ALIGN_TOP_RIGHT,
                            profile.badge_offset_x,
                            profile.badge_offset_y);
        }
        else
        {
            lv_obj_align_to(badge,
                            icon,
                            LV_ALIGN_TOP_LEFT,
                            profile.badge_offset_x,
                            profile.badge_offset_y);
        }

        lv_obj_t* badge_label = lv_label_create(badge);
        lv_label_set_text(badge_label, "");
        lv_obj_set_style_text_color(badge_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(badge_label, profile.badge_font, 0);
        lv_obj_center(badge_label);

        ui::status::register_chat_badge(badge, badge_label);
    }

    if (idx < kMaxMenuApps)
    {
        lv_obj_add_event_cb(btn, menuButtonFocusCallback, LV_EVENT_FOCUSED, &s_menu_apps[idx]);
    }
    lv_obj_add_event_cb(btn, menuButtonClickCallback, LV_EVENT_CLICKED, app);
}

void createPanels()
{
    main_screen = lv_tileview_create(lv_screen_active());
    lv_obj_align(main_screen, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_size(main_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(main_screen, ui::theme::page_bg(), 0);
    lv_obj_set_style_bg_opa(main_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(main_screen, 0, 0);
    lv_obj_set_style_radius(main_screen, 0, 0);
    lv_obj_set_style_shadow_width(main_screen, 0, 0);
    lv_obj_set_style_pad_all(main_screen, 0, 0);

    s_menu_panel = lv_tileview_add_tile(main_screen, 0, 0, LV_DIR_HOR);
    lv_obj_set_style_bg_color(s_menu_panel, ui::theme::page_bg(), 0);
    lv_obj_set_style_bg_opa(s_menu_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_menu_panel, 0, 0);
    lv_obj_set_style_radius(s_menu_panel, 0, 0);
    lv_obj_set_style_shadow_width(s_menu_panel, 0, 0);
    lv_obj_set_style_pad_all(s_menu_panel, 0, 0);

    s_app_panel = lv_tileview_add_tile(main_screen, 0, 1, LV_DIR_HOR);
    if (s_app_panel)
    {
        lv_obj_set_style_bg_color(s_app_panel, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(s_app_panel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_app_panel, 0, 0);
        lv_obj_set_style_radius(s_app_panel, 0, 0);
        lv_obj_set_style_shadow_width(s_app_panel, 0, 0);
        lv_obj_set_style_pad_all(s_app_panel, 0, 0);
    }

    lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);
}

void createAppGrid()
{
    const auto& profile = ui::menu_profile::current();
    static lv_style_t frameless_style;
    static bool frameless_style_initialized = false;
    if (!frameless_style_initialized)
    {
        lv_style_init(&frameless_style);
        lv_style_set_radius(&frameless_style, 0);
        lv_style_set_border_width(&frameless_style, 0);
        lv_style_set_bg_opa(&frameless_style, LV_OPA_TRANSP);
        lv_style_set_shadow_width(&frameless_style, 0);
        frameless_style_initialized = true;
    }

    lv_obj_t* panel = lv_obj_create(s_menu_panel);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(profile.grid_height_pct));
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_pad_row(panel, profile.grid_pad_row, 0);
    lv_obj_set_style_pad_column(panel, profile.grid_pad_column, 0);
    lv_obj_set_scroll_dir(panel, profile.vertical_scroll ? LV_DIR_VER : LV_DIR_HOR);
    if (profile.wrap_grid)
    {
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    }
    else
    {
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    }
    if (profile.snap_center)
    {
        lv_obj_set_scroll_snap_x(panel, LV_SCROLL_SNAP_CENTER);
    }
    lv_obj_set_flex_flow(panel, profile.wrap_grid ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, profile.grid_top_offset);
    lv_obj_add_style(panel, &frameless_style, 0);

    const size_t app_count = ui::catalogCount(s_init_options.apps);
    if (app_count > kMaxMenuApps)
    {
        // Ignore anything above the UI cap for now.
    }
    for (size_t index = 0; index < app_count && index < kMaxMenuApps; ++index)
    {
        AppScreen* app = ui::catalogAt(s_init_options.apps, index);
        createAppButton(panel, app, index);
        lv_group_add_obj(menu_g, lv_obj_get_child(panel, static_cast<int32_t>(index)));
    }

    s_desc_label = lv_label_create(s_menu_panel);
    if (!profile.show_desc_label)
    {
        lv_obj_add_flag(s_desc_label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_width(s_desc_label, LV_PCT(100));
    lv_obj_align(s_desc_label, LV_ALIGN_BOTTOM_MID, 0, profile.desc_offset);
    lv_obj_set_style_text_align(s_desc_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_desc_label, ui::theme::text(), 0);
    lv_obj_set_style_text_font(s_desc_label, profile.desc_font, 0);

#if !defined(ARDUINO_T_WATCH_S3)
    s_node_id_label = lv_label_create(s_menu_panel);
    lv_obj_set_width(s_node_id_label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(s_node_id_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(s_node_id_label, ui::theme::text_muted(), 0);
    lv_obj_set_style_text_font(s_node_id_label, profile.node_id_font, 0);
    lv_obj_align(s_node_id_label,
                 LV_ALIGN_BOTTOM_LEFT,
                 profile.node_id_offset_x,
                 profile.node_id_offset_y);
    if (!profile.show_node_id)
    {
        lv_obj_add_flag(s_node_id_label, LV_OBJ_FLAG_HIDDEN);
    }

    char node_id_buf[24];
    const uint32_t self_id = s_init_options.messaging ? s_init_options.messaging->getSelfNodeId() : 0;
    if (self_id != 0)
    {
        snprintf(node_id_buf, sizeof(node_id_buf), "ID: !%08lX", static_cast<unsigned long>(self_id));
    }
    else
    {
        snprintf(node_id_buf, sizeof(node_id_buf), "ID: -");
    }
    lv_label_set_text(s_node_id_label, node_id_buf);
#endif
    lv_label_set_long_mode(s_desc_label,
                           profile.input_mode == ui::menu_profile::InputMode::TouchPrimary
                               ? LV_LABEL_LONG_CLIP
                               : LV_LABEL_LONG_SCROLL_CIRCULAR);

#if LVGL_VERSION_MAJOR == 9
    s_name_change_id = lv_event_register_id();
    lv_obj_add_event_cb(s_desc_label, menuNameLabelEventCallback, static_cast<lv_event_code_t>(s_name_change_id), nullptr);
    lv_obj_t* first_child = lv_obj_get_child(panel, 0);
    if (first_child != nullptr)
    {
        lv_obj_send_event(first_child, LV_EVENT_FOCUSED, nullptr);
    }
#endif

    lv_obj_update_snap(panel, LV_ANIM_ON);
}

} // namespace

void init(const InitOptions& options)
{
    s_init_options = options;

    menu_g = lv_group_create();
    app_g = lv_group_create();
    set_default_group(menu_g);

    createPanels();
    createAppGrid();
}

lv_obj_t* menuPanel()
{
    return s_menu_panel;
}

} // namespace menu_layout
} // namespace ui
