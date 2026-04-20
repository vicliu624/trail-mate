#include "ui/menu/menu_layout.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/menu/menu_profile.h"
#include "ui/menu/menu_runtime.h"
#include "ui/ui_common.h"
#include "ui/ui_status.h"
#include "ui/ui_theme.h"

namespace ui
{
namespace menu_layout
{
namespace
{

#if defined(ESP_PLATFORM)
constexpr const char* kTag = "ui-menu-layout";
#endif

#define MENU_LAYOUT_DIAG(...) std::printf("[UI][Menu] " __VA_ARGS__)

constexpr size_t kMaxMenuApps = 16;

struct MenuAppUi
{
    AppScreen* app = nullptr;
    const char* name = nullptr;
    lv_obj_t* icon = nullptr;
    lv_obj_t* button = nullptr;
    lv_obj_t* label = nullptr;
};

struct BottomBarChipUi
{
    lv_obj_t* container = nullptr;
    lv_obj_t* label = nullptr;
};

lv_obj_t* s_menu_panel = nullptr;
lv_obj_t* s_app_panel = nullptr;
lv_obj_t* s_grid_panel = nullptr;
lv_obj_t* s_desc_label = nullptr;
lv_obj_t* s_bottom_bar = nullptr;
BottomBarChipUi s_bottom_node_chip{};
BottomBarChipUi s_bottom_ram_chip{};
BottomBarChipUi s_bottom_psram_chip{};
MenuAppUi s_menu_apps[kMaxMenuApps];
InitOptions s_init_options{};

#if LVGL_VERSION_MAJOR == 9
uint32_t s_name_change_id = 0;
#endif
AppScreen* s_pending_app_launch = nullptr;

lv_obj_t* firstMenuButton()
{
    for (const auto& item : s_menu_apps)
    {
        if (item.button != nullptr && !lv_obj_has_flag(item.button, LV_OBJ_FLAG_HIDDEN))
        {
            return item.button;
        }
    }
    return nullptr;
}

bool usesDirectionalMenuKeys()
{
    return ui::menu_profile::current().directional_key_nav;
}

int findMenuButtonIndex(lv_obj_t* obj)
{
    if (obj == nullptr)
    {
        return -1;
    }
    for (size_t i = 0; i < kMaxMenuApps; ++i)
    {
        if (s_menu_apps[i].button == obj)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

lv_obj_t* findMenuNeighbor(lv_obj_t* current, uint32_t key)
{
    if (current == nullptr)
    {
        return firstMenuButton();
    }

    const int current_x = lv_obj_get_x(current) + lv_obj_get_width(current) / 2;
    const int current_y = lv_obj_get_y(current) + lv_obj_get_height(current) / 2;
    lv_obj_t* best = nullptr;
    int32_t best_score = INT32_MAX;

    for (const auto& item : s_menu_apps)
    {
        lv_obj_t* candidate = item.button;
        if (candidate == nullptr || candidate == current || lv_obj_has_flag(candidate, LV_OBJ_FLAG_HIDDEN))
        {
            continue;
        }

        const int dx = lv_obj_get_x(candidate) + lv_obj_get_width(candidate) / 2 - current_x;
        const int dy = lv_obj_get_y(candidate) + lv_obj_get_height(candidate) / 2 - current_y;

        bool valid = false;
        int32_t score = INT32_MAX;
        switch (key)
        {
        case LV_KEY_LEFT:
            valid = dx < 0;
            score = static_cast<int32_t>(-dx) + static_cast<int32_t>(std::abs(dy)) * 1000;
            break;
        case LV_KEY_RIGHT:
            valid = dx > 0;
            score = static_cast<int32_t>(dx) + static_cast<int32_t>(std::abs(dy)) * 1000;
            break;
        case LV_KEY_UP:
            valid = dy < 0;
            score = static_cast<int32_t>(-dy) + static_cast<int32_t>(std::abs(dx)) * 1000;
            break;
        case LV_KEY_DOWN:
            valid = dy > 0;
            score = static_cast<int32_t>(dy) + static_cast<int32_t>(std::abs(dx)) * 1000;
            break;
        default:
            break;
        }

        if (valid && score < best_score)
        {
            best = candidate;
            best_score = score;
        }
    }

    return best;
}

void ensureMenuFocus()
{
    if (menu_g == nullptr)
    {
        return;
    }

    lv_obj_t* focused = lv_group_get_focused(menu_g);
    if (findMenuButtonIndex(focused) >= 0 && !lv_obj_has_flag(focused, LV_OBJ_FLAG_HIDDEN))
    {
        return;
    }

    if (lv_obj_t* first = firstMenuButton())
    {
        lv_group_focus_obj(first);
    }
}

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
#if defined(ESP_PLATFORM)
        ESP_LOGW(kTag,
                 "enterPendingApp skipped target=%s pending=%s app_panel=%d",
                 target_app ? target_app->name() : "(null)",
                 s_pending_app_launch ? s_pending_app_launch->name() : "(null)",
                 s_app_panel ? 1 : 0);
#endif
        return;
    }

#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "enterPendingApp target=%s", target_app->name());
#endif
    s_pending_app_launch = nullptr;
    ui_switch_to_app(target_app, s_app_panel);
}

void menuHidden()
{
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "menuHidden -> tile(0,1)");
#endif
    setMenuVisible(false);
    lv_tileview_set_tile_by_index(main_screen, 0, 1, transition_anim());
}

void menuNameLabelEventCallback(lv_event_t* e)
{
#if LVGL_VERSION_MAJOR == 9
    const char* value = static_cast<const char*>(lv_event_get_param(e));
    if (value)
    {
        lv_label_set_text(lv_event_get_target_obj(e), value);
        ::ui::fonts::apply_localized_font(
            lv_event_get_target_obj(e), value, ui::menu_profile::current().desc_font);
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

void menuButtonKeyCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY || menu_g == nullptr || !usesDirectionalMenuKeys())
    {
        return;
    }

    const uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_LEFT && key != LV_KEY_RIGHT && key != LV_KEY_UP && key != LV_KEY_DOWN)
    {
        return;
    }

    lv_obj_t* current = lv_event_get_target_obj(e);
    if (lv_obj_t* next = findMenuNeighbor(current, key))
    {
        lv_group_focus_obj(next);
        lv_obj_scroll_to_view(next, LV_ANIM_OFF);
    }
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
#if defined(ESP_PLATFORM)
        ESP_LOGW(kTag, "click ignored because main_screen hidden target=%s", target_app ? target_app->name() : "(null)");
#endif
        return;
    }

#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag,
             "menu click target=%s touch_primary=%d",
             target_app ? target_app->name() : "(null)",
             ui::menu_profile::current().input_mode == ui::menu_profile::InputMode::TouchPrimary ? 1 : 0);
#endif
    set_default_group(nullptr);
    if (ui::menu_profile::current().input_mode == ui::menu_profile::InputMode::TouchPrimary)
    {
        s_pending_app_launch = target_app;
#if defined(ESP_PLATFORM)
        ESP_LOGI(kTag, "queue async app enter target=%s", target_app ? target_app->name() : "(null)");
#endif
        menuHidden();
        lv_async_call(enterPendingApp, target_app);
        return;
    }

#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "direct app enter target=%s", target_app ? target_app->name() : "(null)");
#endif
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

        MENU_LAYOUT_DIAG("createAppButton idx=%u name=%s btn=%p icon=%p image=%p src=%p w=%d h=%d\n",
                         static_cast<unsigned>(idx),
                         name ? name : "(null)",
                         btn,
                         icon,
                         image,
                         lv_image_get_src(icon),
                         static_cast<int>(lv_image_get_src_width(icon)),
                         static_cast<int>(lv_image_get_src_height(icon)));

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
    else
    {
        MENU_LAYOUT_DIAG("createAppButton idx=%u name=%s btn=%p icon=(null) image=(null)\n",
                         static_cast<unsigned>(idx),
                         name ? name : "(null)",
                         btn);
    }

    if (profile.show_card_label && name && name[0] != '\0')
    {
        lv_obj_t* label = lv_label_create(btn);
        ::ui::i18n::set_label_text_raw(label, name);
        lv_obj_set_width(label, width - (profile.variant == ui::menu_profile::LayoutVariant::LargeTouchGrid ? 16 : 4));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(label, profile.card_label_font, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x6B4A1E), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x6B4A1E), LV_STATE_FOCUSED);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        ::ui::fonts::apply_localized_font(label, name, profile.card_label_font);
        s_menu_apps[idx].label = label;
    }

    if (idx < kMaxMenuApps)
    {
        s_menu_apps[idx].app = app;
        s_menu_apps[idx].name = name;
        s_menu_apps[idx].icon = icon;
        s_menu_apps[idx].button = btn;
        lv_obj_set_user_data(btn, &s_menu_apps[idx]);
    }

    if (icon != nullptr && app != nullptr && app->stable_id() != nullptr &&
        std::strcmp(app->stable_id(), "chat") == 0)
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
            lv_obj_align_to(badge, icon, LV_ALIGN_TOP_RIGHT, profile.badge_offset_x, profile.badge_offset_y);
        }
        else
        {
            lv_obj_align_to(badge, icon, LV_ALIGN_TOP_LEFT, profile.badge_offset_x, profile.badge_offset_y);
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
    if (usesDirectionalMenuKeys())
    {
        lv_obj_add_event_cb(btn, menuButtonKeyCallback, LV_EVENT_KEY, nullptr);
    }
    lv_obj_add_event_cb(btn, menuButtonClickCallback, LV_EVENT_CLICKED, app);
}

BottomBarChipUi createBottomBarChip(lv_obj_t* parent,
                                    const ui::menu_profile::MenuLayoutProfile& profile,
                                    lv_color_t bg_color,
                                    const char* text)
{
    BottomBarChipUi chip{};
    chip.container = lv_obj_create(parent);
    lv_obj_set_size(chip.container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(chip.container, bg_color, 0);
    lv_obj_set_style_bg_opa(chip.container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chip.container, 0, 0);
    lv_obj_set_style_radius(chip.container, 10, 0);
    lv_obj_set_style_shadow_width(chip.container, 0, 0);
    lv_obj_set_style_pad_left(chip.container, 8, 0);
    lv_obj_set_style_pad_right(chip.container, 8, 0);
    lv_obj_set_style_pad_top(chip.container, 3, 0);
    lv_obj_set_style_pad_bottom(chip.container, 3, 0);
    lv_obj_set_style_min_height(chip.container, profile.top_bar_height - 6, 0);
    lv_obj_clear_flag(chip.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chip.container, LV_OBJ_FLAG_CLICKABLE);

    chip.label = lv_label_create(chip.container);
    lv_obj_set_width(chip.label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(chip.label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chip.label, ui::theme::text(), 0);
    lv_obj_set_style_bg_opa(chip.label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(chip.label, 0, 0);
    lv_obj_set_style_text_font(chip.label, profile.node_id_font, 0);
    lv_label_set_long_mode(chip.label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(chip.label, text ? text : "");
    lv_obj_center(chip.label);
    return chip;
}

void setBottomBarChipText(const BottomBarChipUi& chip, const char* text)
{
    if (chip.label == nullptr)
    {
        return;
    }
    lv_label_set_text(chip.label, text ? text : "");
}

void setBottomBarChipVisible(const BottomBarChipUi& chip, bool visible)
{
    if (chip.container == nullptr)
    {
        return;
    }

    if (visible)
    {
        lv_obj_clear_flag(chip.container, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(chip.container, LV_OBJ_FLAG_HIDDEN);
    }
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
    s_grid_panel = panel;
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_coord_t panel_width = lv_pct(100);
    if (profile.max_columns > 0)
    {
        panel_width = static_cast<lv_coord_t>(
            profile.grid_pad_left +
            profile.grid_pad_right +
            (profile.card_width * profile.max_columns) +
            (profile.grid_pad_column * (profile.max_columns - 1)));
    }
    lv_obj_set_size(panel, panel_width, LV_PCT(profile.grid_height_pct));
    lv_obj_set_style_pad_row(panel, profile.grid_pad_row, 0);
    lv_obj_set_style_pad_column(panel, profile.grid_pad_column, 0);
    if (profile.variant != ui::menu_profile::LayoutVariant::CompactGrid)
    {
        lv_obj_set_style_pad_top(panel, 0, 0);
        lv_obj_set_style_pad_bottom(panel, 0, 0);
        lv_obj_set_style_pad_left(panel, profile.grid_pad_left, 0);
        lv_obj_set_style_pad_right(panel, profile.grid_pad_right, 0);
    }
    lv_obj_set_scroll_dir(panel, profile.vertical_scroll ? LV_DIR_VER : LV_DIR_HOR);
    if (profile.wrap_grid)
    {
        if (profile.grid_anchor_top_left || profile.variant == ui::menu_profile::LayoutVariant::CompactGrid)
        {
            lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        }
        else
        {
            lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        }
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
    if (profile.grid_anchor_top_left)
    {
        lv_obj_align(panel, LV_ALIGN_TOP_LEFT, 0, profile.grid_top_offset);
    }
    else
    {
        lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, profile.grid_top_offset);
    }
    lv_obj_add_style(panel, &frameless_style, 0);

    const size_t app_count = ui::catalogCount(s_init_options.apps);
    MENU_LAYOUT_DIAG("createAppGrid app_count=%u\n", static_cast<unsigned>(app_count));
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
    s_bottom_bar = lv_obj_create(s_menu_panel);
    lv_obj_set_size(s_bottom_bar, LV_PCT(100), profile.top_bar_height);
    lv_obj_set_style_bg_color(s_bottom_bar, ui::theme::surface_alt(), 0);
    lv_obj_set_style_bg_opa(s_bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_bottom_bar, 0, 0);
    lv_obj_set_style_radius(s_bottom_bar, 0, 0);
    lv_obj_set_style_shadow_width(s_bottom_bar, 0, 0);
    lv_obj_set_style_pad_left(s_bottom_bar, profile.top_bar_side_inset, 0);
    lv_obj_set_style_pad_right(s_bottom_bar, profile.top_bar_side_inset, 0);
    lv_obj_set_style_pad_top(s_bottom_bar, 0, 0);
    lv_obj_set_style_pad_bottom(s_bottom_bar, 0, 0);
    lv_obj_set_style_pad_column(s_bottom_bar, 6, 0);
    lv_obj_set_flex_flow(s_bottom_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_bottom_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(s_bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_bottom_bar, LV_OBJ_FLAG_CLICKABLE);
    if (!profile.show_node_id)
    {
        lv_obj_add_flag(s_bottom_bar, LV_OBJ_FLAG_HIDDEN);
    }

    s_bottom_node_chip = createBottomBarChip(s_bottom_bar, profile, lv_color_hex(0xF1B75A), "-");
    s_bottom_ram_chip = createBottomBarChip(s_bottom_bar, profile, lv_color_hex(0xCFE4FF), "--/--");
    s_bottom_psram_chip = createBottomBarChip(s_bottom_bar, profile, lv_color_hex(0xD4F0D2), "--/--");

    char node_id_buf[24];
    const uint32_t self_id = s_init_options.messaging ? s_init_options.messaging->getSelfNodeId() : 0;
    if (self_id != 0)
    {
        std::snprintf(node_id_buf, sizeof(node_id_buf), "!%08lX", static_cast<unsigned long>(self_id));
    }
    else
    {
        std::snprintf(node_id_buf, sizeof(node_id_buf), "-");
    }
    setBottomBarChipText(s_bottom_node_chip, node_id_buf);
#endif

    lv_label_set_long_mode(s_desc_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

#if LVGL_VERSION_MAJOR == 9
    s_name_change_id = lv_event_register_id();
    lv_obj_add_event_cb(s_desc_label, menuNameLabelEventCallback, static_cast<lv_event_code_t>(s_name_change_id), nullptr);
#endif

    ensureMenuFocus();
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
    ui::menu::dashboard::init(s_menu_panel, s_grid_panel, s_init_options);
}

lv_obj_t* menuPanel()
{
    return s_menu_panel;
}

void bringContentToFront()
{
    if (s_grid_panel != nullptr)
    {
        lv_obj_move_foreground(s_grid_panel);
    }
    if (s_desc_label != nullptr)
    {
        lv_obj_move_foreground(s_desc_label);
    }
    if (s_bottom_bar != nullptr)
    {
        lv_obj_move_foreground(s_bottom_bar);
    }
    ui::menu::dashboard::bringToFront();
}

void refresh_localized_text()
{
    const auto& profile = ui::menu_profile::current();
    for (auto& item : s_menu_apps)
    {
        if (item.app == nullptr)
        {
            continue;
        }

        item.name = item.app->name();
        if (item.label != nullptr)
        {
            ::ui::i18n::set_label_text_raw(item.label, item.name);
            ::ui::fonts::apply_localized_font(item.label, item.name, profile.card_label_font);
        }
    }

    if (s_desc_label != nullptr && menu_g != nullptr)
    {
        lv_obj_t* focused = lv_group_get_focused(menu_g);
        const int index = findMenuButtonIndex(focused);
        if (index >= 0 && static_cast<size_t>(index) < kMaxMenuApps)
        {
            ::ui::i18n::set_label_text_raw(s_desc_label, s_menu_apps[index].name);
            ::ui::fonts::apply_localized_font(s_desc_label, s_menu_apps[index].name, profile.desc_font);
        }
    }

    ui::menu::dashboard::refresh_localized_text();
}

void set_bottom_bar_node_text(const char* text)
{
    setBottomBarChipText(s_bottom_node_chip, text);
}

void set_bottom_bar_ram_text(const char* text)
{
    setBottomBarChipText(s_bottom_ram_chip, text);
}

void set_bottom_bar_psram_text(const char* text)
{
    setBottomBarChipText(s_bottom_psram_chip, text);
}

void set_bottom_bar_psram_visible(bool visible)
{
    setBottomBarChipVisible(s_bottom_psram_chip, visible);
}

void setMenuVisible(bool visible)
{
    if (s_menu_panel != nullptr)
    {
        if (visible)
        {
            lv_obj_clear_flag(s_menu_panel, LV_OBJ_FLAG_HIDDEN);
            ensureMenuFocus();
        }
        else
        {
            lv_obj_add_flag(s_menu_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    ui::menu_runtime::setMenuActive(visible);
    ui::status::set_menu_active(visible);
    ui::menu::dashboard::setActive(visible);
}

} // namespace menu_layout
} // namespace ui
