#include "ui/menu/menu_layout.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/menu/menu_profile.h"
#include "ui/menu/menu_runtime.h"
#include "ui/presentation/menu_dashboard_layout.h"
#include "ui/presentation/presentation_registry.h"
#include "ui/theme/theme_component_style.h"
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
    std::string icon_path;
};

struct BottomBarChipUi
{
    lv_obj_t* container = nullptr;
    lv_obj_t* label = nullptr;
};

void apply_component_profile(lv_obj_t* obj, ::ui::theme::ComponentSlot slot)
{
    ::ui::theme::ComponentProfile profile{};
    if (::ui::theme::resolve_component_profile(slot, profile))
    {
        ::ui::theme::apply_component_profile_to_obj(obj, profile);
    }
}

lv_obj_t* s_menu_panel = nullptr;
lv_obj_t* s_app_panel = nullptr;
lv_obj_t* s_grid_panel = nullptr;
lv_obj_t* s_desc_label = nullptr;
lv_obj_t* s_bottom_bar = nullptr;
lv_obj_t* s_bottom_bar_left = nullptr;
lv_obj_t* s_bottom_bar_right = nullptr;
BottomBarChipUi s_bottom_node_chip{};
BottomBarChipUi s_bottom_ram_chip{};
BottomBarChipUi s_bottom_psram_chip{};
MenuAppUi s_menu_apps[kMaxMenuApps];
InitOptions s_init_options{};

bool usesSimpleListMenuLayout();

#if LVGL_VERSION_MAJOR == 9
uint32_t s_name_change_id = 0;
#endif
AppScreen* s_pending_app_launch = nullptr;

bool asset_slot_for_menu_app(const AppScreen* app, ::ui::theme::AssetSlot& out_slot)
{
    if (app == nullptr || app->stable_id() == nullptr)
    {
        return false;
    }

    const char* stable_id = app->stable_id();
    if (std::strcmp(stable_id, "chat") == 0)
    {
        out_slot = ::ui::theme::AssetSlot::MenuAppChat;
        return true;
    }
    if (std::strcmp(stable_id, "map") == 0)
    {
        out_slot = ::ui::theme::AssetSlot::MenuAppMap;
        return true;
    }
    if (std::strcmp(stable_id, "sky_plot") == 0)
    {
        out_slot = ::ui::theme::AssetSlot::MenuAppSkyPlot;
        return true;
    }
    if (std::strcmp(stable_id, "contacts") == 0)
    {
        out_slot = ::ui::theme::AssetSlot::MenuAppContacts;
        return true;
    }
    if (std::strcmp(stable_id, "energy_sweep") == 0)
    {
        out_slot = ::ui::theme::AssetSlot::MenuAppEnergySweep;
        return true;
    }
    if (std::strcmp(stable_id, "team") == 0)
    {
        out_slot = ::ui::theme::AssetSlot::MenuAppTeam;
        return true;
    }
    if (std::strcmp(stable_id, "tracker") == 0)
    {
        out_slot = ::ui::theme::AssetSlot::MenuAppTracker;
        return true;
    }
    if (std::strcmp(stable_id, "pc_link") == 0)
    {
        out_slot = ::ui::theme::AssetSlot::MenuAppPcLink;
        return true;
    }
    if (std::strcmp(stable_id, "sstv") == 0)
    {
        out_slot = ::ui::theme::AssetSlot::MenuAppSstv;
        return true;
    }
    if (std::strcmp(stable_id, "settings") == 0)
    {
        out_slot = ::ui::theme::AssetSlot::MenuAppSettings;
        return true;
    }
    if (std::strcmp(stable_id, "extensions") == 0)
    {
        out_slot = ::ui::theme::AssetSlot::MenuAppExtensions;
        return true;
    }
    if (std::strcmp(stable_id, "usb_mass_storage") == 0)
    {
        out_slot = ::ui::theme::AssetSlot::MenuAppUsb;
        return true;
    }
    if (std::strcmp(stable_id, "walkie_talkie") == 0)
    {
        out_slot = ::ui::theme::AssetSlot::MenuAppWalkieTalkie;
        return true;
    }
    return false;
}

const char* fallback_text_for_menu_app(const AppScreen* app)
{
    if (app == nullptr || app->stable_id() == nullptr)
    {
        return "?";
    }

    const char* stable_id = app->stable_id();
    if (std::strcmp(stable_id, "chat") == 0) return "CH";
    if (std::strcmp(stable_id, "map") == 0) return "MP";
    if (std::strcmp(stable_id, "sky_plot") == 0) return "GN";
    if (std::strcmp(stable_id, "contacts") == 0) return "CT";
    if (std::strcmp(stable_id, "energy_sweep") == 0) return "ES";
    if (std::strcmp(stable_id, "team") == 0) return "TM";
    if (std::strcmp(stable_id, "tracker") == 0) return "TR";
    if (std::strcmp(stable_id, "pc_link") == 0) return "PC";
    if (std::strcmp(stable_id, "sstv") == 0) return "SV";
    if (std::strcmp(stable_id, "settings") == 0) return "ST";
    if (std::strcmp(stable_id, "extensions") == 0) return "EX";
    if (std::strcmp(stable_id, "usb_mass_storage") == 0) return "US";
    if (std::strcmp(stable_id, "walkie_talkie") == 0) return "WT";
    return "AP";
}

lv_obj_t* menu_icon_image(lv_obj_t* holder)
{
    return holder ? lv_obj_get_child(holder, 0) : nullptr;
}

lv_obj_t* menu_icon_fallback_label(lv_obj_t* holder)
{
    return holder ? lv_obj_get_child(holder, 1) : nullptr;
}

void center_menu_icon_child(lv_obj_t* child)
{
    if (child != nullptr)
    {
        lv_obj_center(child);
    }
}

void apply_menu_icon_source(lv_obj_t* icon, MenuAppUi& item)
{
    if (icon == nullptr || item.app == nullptr)
    {
        return;
    }

    lv_obj_t* image_obj = menu_icon_image(icon);
    lv_obj_t* fallback_label = menu_icon_fallback_label(icon);
    if (image_obj == nullptr || fallback_label == nullptr)
    {
        return;
    }
    const bool list_layout = usesSimpleListMenuLayout();
    const lv_coord_t list_icon_box = 20;

    ::ui::theme::AssetSlot slot = ::ui::theme::AssetSlot::MenuAppChat;
    if (asset_slot_for_menu_app(item.app, slot))
    {
        item.icon_path.clear();
        if (::ui::theme::resolve_asset_path(::ui::theme::asset_slot_id(slot), item.icon_path))
        {
            if (list_layout)
            {
                lv_obj_set_size(icon, list_icon_box, list_icon_box);
                lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
            }
            lv_image_set_src(image_obj, item.icon_path.c_str());
            const auto& profile = ui::menu_profile::current();
            if (profile.icon_scale != 256)
            {
                const lv_coord_t icon_width = lv_image_get_src_width(image_obj);
                const lv_coord_t icon_height = lv_image_get_src_height(image_obj);
                lv_obj_set_style_transform_pivot_x(image_obj, icon_width / 2, 0);
                lv_obj_set_style_transform_pivot_y(image_obj, icon_height / 2, 0);
                lv_obj_set_style_transform_scale(image_obj, profile.icon_scale, 0);
            }
            lv_obj_clear_flag(image_obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(fallback_label, LV_OBJ_FLAG_HIDDEN);
            center_menu_icon_child(image_obj);
            return;
        }
    }

    if (const lv_image_dsc_t* builtin = item.app->icon())
    {
        if (list_layout)
        {
            lv_obj_set_size(icon, list_icon_box, list_icon_box);
            lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
        }
        lv_image_set_src(image_obj, builtin);
        const auto& profile = ui::menu_profile::current();
        if (profile.icon_scale != 256)
        {
            const lv_coord_t icon_width = lv_image_get_src_width(image_obj);
            const lv_coord_t icon_height = lv_image_get_src_height(image_obj);
            lv_obj_set_style_transform_pivot_x(image_obj, icon_width / 2, 0);
            lv_obj_set_style_transform_pivot_y(image_obj, icon_height / 2, 0);
            lv_obj_set_style_transform_scale(image_obj, profile.icon_scale, 0);
        }
        lv_obj_clear_flag(image_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(fallback_label, LV_OBJ_FLAG_HIDDEN);
        center_menu_icon_child(image_obj);
        return;
    }

    if (list_layout)
    {
        lv_obj_add_flag(image_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(fallback_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(icon, 0, 0);
        return;
    }

    lv_label_set_text(fallback_label, fallback_text_for_menu_app(item.app));
    lv_obj_set_style_text_color(fallback_label, ui::theme::text(), 0);
    lv_obj_add_flag(image_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(fallback_label, LV_OBJ_FLAG_HIDDEN);
    center_menu_icon_child(fallback_label);
}

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

bool usesSimpleListMenuLayout()
{
    return ::ui::presentation::active_menu_dashboard_layout() ==
           ::ui::presentation::MenuDashboardLayout::SimpleList;
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
        ::ui::i18n::set_label_text(lv_event_get_target_obj(e), value);
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
    const bool list_layout = usesSimpleListMenuLayout();
    const char* name = app ? app->name() : "";
    ::ui::theme::ComponentProfile button_profile{};
    ::ui::theme::ComponentProfile label_profile{};
    (void)::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::MenuAppButton,
                                                 button_profile);
    (void)::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::MenuAppLabel,
                                                 label_profile);

    lv_obj_t* btn = lv_btn_create(parent);
    const lv_coord_t width = list_layout ? LV_PCT(100) : profile.card_width;
    const lv_coord_t height = list_layout ? 36 : profile.card_height;

    lv_obj_set_size(btn, width, height);
    lv_obj_set_style_bg_opa(btn, list_layout ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, list_layout ? ui::theme::page_bg() : ui::theme::surface(), 0);
    lv_obj_set_style_border_width(btn, list_layout ? 1 : profile.card_border_width, 0);
    lv_obj_set_style_border_color(btn, list_layout ? ui::theme::separator() : ui::theme::border(), 0);
    lv_obj_set_style_border_side(btn, list_layout ? LV_BORDER_SIDE_BOTTOM : LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_radius(btn, list_layout ? 0 : profile.card_radius, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn,
                              list_layout
                                  ? ui::theme::surface_alt()
                                  : (button_profile.accent_color.present
                                         ? button_profile.accent_color.value
                                         : ui::theme::accent()),
                              LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(btn, list_layout ? ui::theme::accent() : ui::theme::border(), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn,
                              list_layout
                                  ? ui::theme::surface_alt()
                                  : (button_profile.accent_color.present
                                         ? button_profile.accent_color.value
                                         : ui::theme::accent()),
                              LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(btn, list_layout ? ui::theme::accent() : ui::theme::border(), LV_STATE_FOCUS_KEY);

    if (profile.transparent_cards)
    {
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    }

    if (list_layout)
    {
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(btn, 6, 0);
        lv_obj_set_style_pad_right(btn, 8, 0);
        lv_obj_set_style_pad_top(btn, 2, 0);
        lv_obj_set_style_pad_bottom(btn, 2, 0);
        lv_obj_set_style_pad_row(btn, 0, 0);
        lv_obj_set_style_pad_column(btn, 8, 0);
    }
    else if (profile.show_card_label)
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

    if (!list_layout)
    {
        ::ui::theme::apply_component_profile_to_obj(btn, button_profile);
    }

    const lv_coord_t icon_box = std::max<lv_coord_t>(
        20,
        std::min<lv_coord_t>(list_layout ? 20 : width - 12,
                             (list_layout || profile.show_card_label) ? height - 16 : height - 8));
    lv_obj_t* icon = lv_obj_create(btn);
    lv_obj_set_size(icon, icon_box, icon_box);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_set_style_pad_all(icon, 0, 0);
    lv_obj_set_style_radius(icon, 0, 0);
    lv_obj_set_style_shadow_width(icon, 0, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    if (list_layout)
    {
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    }
    else if (profile.show_card_label)
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

    lv_obj_t* icon_image = lv_image_create(icon);
    lv_obj_add_flag(icon_image, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* icon_fallback = lv_label_create(icon);
    lv_label_set_text(icon_fallback, "");
    lv_obj_set_style_text_align(icon_fallback, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(icon_fallback, profile.card_label_font, 0);
    lv_obj_set_style_text_color(icon_fallback, ui::theme::text(), 0);
    lv_obj_add_flag(icon_fallback, LV_OBJ_FLAG_HIDDEN);

    if ((list_layout || profile.show_card_label) && name && name[0] != '\0')
    {
        lv_obj_t* label = lv_label_create(btn);
        lv_obj_set_width(label,
                         list_layout
                             ? 0
                             : width - (profile.variant == ui::menu_profile::LayoutVariant::LargeTouchGrid
                                            ? 16
                                            : 4));
        if (list_layout)
        {
            lv_obj_set_flex_grow(label, 1);
        }
        lv_obj_set_style_text_align(label, list_layout ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(label, profile.card_label_font, 0);
        lv_obj_set_style_text_color(label, ui::theme::text(), 0);
        lv_obj_set_style_text_color(label,
                                    list_layout ? ui::theme::accent_strong() : ui::theme::text(),
                                    LV_STATE_FOCUSED);
        if (!list_layout)
        {
            ::ui::theme::apply_component_profile_to_obj(label, label_profile);
        }
        lv_label_set_long_mode(label, list_layout ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);
        ::ui::i18n::set_label_text(label, name);
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

    apply_menu_icon_source(icon, s_menu_apps[idx]);

    if (app != nullptr && app->stable_id() != nullptr &&
        std::strcmp(app->stable_id(), "chat") == 0)
    {
        lv_obj_t* badge = lv_obj_create(btn);
        lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(badge, ui::theme::error(), 0);
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
                                    ::ui::theme::ComponentSlot slot,
                                    lv_color_t bg_color,
                                    const char* text)
{
    BottomBarChipUi chip{};
    ::ui::theme::ComponentProfile chip_profile{};
    (void)::ui::theme::resolve_component_profile(slot, chip_profile);
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
    ::ui::theme::apply_component_profile_to_obj(chip.container, chip_profile);

    chip.label = lv_label_create(chip.container);
    lv_obj_set_width(chip.label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(chip.label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chip.label,
                                chip_profile.text_color.present
                                    ? chip_profile.text_color.value
                                    : ui::theme::text(),
                                0);
    lv_obj_set_style_bg_opa(chip.label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(chip.label, 0, 0);
    lv_obj_set_style_text_font(chip.label, profile.node_id_font, 0);
    lv_label_set_long_mode(chip.label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(chip.label, text ? text : "");
    lv_obj_center(chip.label);
    return chip;
}

lv_obj_t* createBottomBarGroup(lv_obj_t* parent)
{
    lv_obj_t* group = lv_obj_create(parent);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(group, 0, 0);
    lv_obj_set_style_radius(group, 0, 0);
    lv_obj_set_style_shadow_width(group, 0, 0);
    lv_obj_set_style_pad_all(group, 0, 0);
    lv_obj_set_style_pad_column(group, 6, 0);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_CLICKABLE);
    return group;
}

lv_obj_t* createBottomBarSpacer(lv_obj_t* parent)
{
    lv_obj_t* spacer = lv_obj_create(parent);
    lv_obj_set_size(spacer, 0, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_radius(spacer, 0, 0);
    lv_obj_set_style_shadow_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
    return spacer;
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
        lv_obj_set_style_bg_color(s_app_panel, ui::theme::white(), 0);
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
    const bool list_layout = usesSimpleListMenuLayout();
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

    if (list_layout)
    {
        ui::presentation::menu_dashboard_layout::AppListSpec list_spec{};
        list_spec.top_offset = profile.top_bar_height + 2;
        list_spec.pad_left = 0;
        list_spec.pad_right = 0;
        list_spec.pad_top = 0;
        list_spec.pad_bottom = 0;
        list_spec.pad_row = 0;
        s_grid_panel =
            ui::presentation::menu_dashboard_layout::create_app_list_region(s_menu_panel, list_spec);
    }
    else
    {
        ui::presentation::menu_dashboard_layout::AppGridSpec grid_spec{};
        lv_coord_t panel_width = lv_pct(100);
        if (profile.max_columns > 0)
        {
            panel_width = static_cast<lv_coord_t>(
                profile.grid_pad_left +
                profile.grid_pad_right +
                (profile.card_width * profile.max_columns) +
                (profile.grid_pad_column * (profile.max_columns - 1)));
        }
        grid_spec.width = panel_width;
        grid_spec.height_pct = profile.grid_height_pct;
        grid_spec.top_offset = profile.grid_top_offset;
        grid_spec.pad_row = profile.grid_pad_row;
        grid_spec.pad_column = profile.grid_pad_column;
        grid_spec.align = profile.grid_anchor_top_left ? LV_ALIGN_TOP_LEFT : LV_ALIGN_TOP_MID;
        grid_spec.scroll_dir = profile.vertical_scroll ? LV_DIR_VER : LV_DIR_HOR;
        grid_spec.flow = profile.wrap_grid ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW;
        grid_spec.snap_x = profile.snap_center ? LV_SCROLL_SNAP_CENTER : LV_SCROLL_SNAP_NONE;
        if (profile.variant != ui::menu_profile::LayoutVariant::CompactGrid)
        {
            grid_spec.pad_left = profile.grid_pad_left;
            grid_spec.pad_right = profile.grid_pad_right;
        }
        if (profile.wrap_grid)
        {
            if (profile.grid_anchor_top_left ||
                profile.variant == ui::menu_profile::LayoutVariant::CompactGrid)
            {
                grid_spec.main_align = LV_FLEX_ALIGN_START;
                grid_spec.cross_align = LV_FLEX_ALIGN_START;
                grid_spec.track_align = LV_FLEX_ALIGN_START;
            }
            else
            {
                grid_spec.main_align = LV_FLEX_ALIGN_CENTER;
                grid_spec.cross_align = LV_FLEX_ALIGN_START;
                grid_spec.track_align = LV_FLEX_ALIGN_CENTER;
            }
        }
        else
        {
            grid_spec.main_align = LV_FLEX_ALIGN_START;
            grid_spec.cross_align = LV_FLEX_ALIGN_START;
            grid_spec.track_align = LV_FLEX_ALIGN_START;
        }

        lv_obj_t* panel =
            ui::presentation::menu_dashboard_layout::create_app_grid_region(s_menu_panel, grid_spec);
        s_grid_panel = panel;
        lv_obj_set_scroll_dir(panel, profile.vertical_scroll ? LV_DIR_VER : LV_DIR_HOR);
        lv_obj_add_style(panel, &frameless_style, 0);
    }

    const size_t app_count = ui::catalogCount(s_init_options.apps);
    MENU_LAYOUT_DIAG("createAppGrid app_count=%u\n", static_cast<unsigned>(app_count));
    for (size_t index = 0; index < app_count && index < kMaxMenuApps; ++index)
    {
        AppScreen* app = ui::catalogAt(s_init_options.apps, index);
        createAppButton(s_grid_panel, app, index);
        lv_group_add_obj(menu_g, lv_obj_get_child(s_grid_panel, static_cast<int32_t>(index)));
    }

    s_desc_label = lv_label_create(s_menu_panel);
    if (!profile.show_desc_label || list_layout)
    {
        lv_obj_add_flag(s_desc_label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_width(s_desc_label, LV_PCT(100));
    const lv_coord_t desc_offset = profile.desc_offset - (profile.show_node_id ? profile.top_bar_height : 0);
    lv_obj_align(s_desc_label, LV_ALIGN_BOTTOM_MID, 0, desc_offset);
    lv_obj_set_style_text_align(s_desc_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_desc_label, ui::theme::text(), 0);
    lv_obj_set_style_text_font(s_desc_label, profile.desc_font, 0);

#if !defined(ARDUINO_T_WATCH_S3)
    if (!list_layout)
    {
        ui::presentation::menu_dashboard_layout::BottomChipsSpec bottom_chips_spec{};
        bottom_chips_spec.height = profile.top_bar_height;
        bottom_chips_spec.pad_left = profile.top_bar_side_inset;
        bottom_chips_spec.pad_right = profile.top_bar_side_inset;
        s_bottom_bar = ui::presentation::menu_dashboard_layout::create_bottom_chips_region(
            s_menu_panel, bottom_chips_spec);
        lv_obj_set_style_bg_color(s_bottom_bar, ui::theme::surface_alt(), 0);
        lv_obj_set_style_bg_opa(s_bottom_bar, LV_OPA_COVER, 0);
        if (!profile.show_node_id)
        {
            lv_obj_add_flag(s_bottom_bar, LV_OBJ_FLAG_HIDDEN);
        }

        s_bottom_bar_left = createBottomBarGroup(s_bottom_bar);
        createBottomBarSpacer(s_bottom_bar);
        s_bottom_bar_right = createBottomBarGroup(s_bottom_bar);

        s_bottom_node_chip = createBottomBarChip(s_bottom_bar_left,
                                                 profile,
                                                 ::ui::theme::ComponentSlot::MenuBottomChipNode,
                                                 ui::theme::accent(),
                                                 "-");
        s_bottom_ram_chip = createBottomBarChip(s_bottom_bar_right,
                                                profile,
                                                ::ui::theme::ComponentSlot::MenuBottomChipRam,
                                                ui::theme::status_blue(),
                                                "--/--");
        s_bottom_psram_chip = createBottomBarChip(s_bottom_bar_right,
                                                  profile,
                                                  ::ui::theme::ComponentSlot::MenuBottomChipPsram,
                                                  ui::theme::battery_green(),
                                                  "--/--");

        char node_id_buf[24];
        const uint32_t self_id =
            s_init_options.messaging ? s_init_options.messaging->getSelfNodeId() : 0;
        if (self_id != 0)
        {
            std::snprintf(node_id_buf,
                          sizeof(node_id_buf),
                          "!%08lX",
                          static_cast<unsigned long>(self_id));
        }
        else
        {
            std::snprintf(node_id_buf, sizeof(node_id_buf), "-");
        }
        setBottomBarChipText(s_bottom_node_chip, node_id_buf);
    }
#endif

    lv_label_set_long_mode(s_desc_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

#if LVGL_VERSION_MAJOR == 9
    s_name_change_id = lv_event_register_id();
    lv_obj_add_event_cb(s_desc_label, menuNameLabelEventCallback, static_cast<lv_event_code_t>(s_name_change_id), nullptr);
#endif

    ensureMenuFocus();
    if (!list_layout)
    {
        lv_obj_update_snap(s_grid_panel, LV_ANIM_ON);
    }
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
    if (!usesSimpleListMenuLayout())
    {
        ui::menu::dashboard::init(s_menu_panel, s_grid_panel, s_init_options);
    }
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
    for (auto& item : s_menu_apps)
    {
        if (item.app == nullptr)
        {
            continue;
        }

        item.name = item.app->name();
        if (item.label != nullptr)
        {
            ::ui::i18n::set_label_text(item.label, item.name);
        }
    }

    if (s_desc_label != nullptr && menu_g != nullptr)
    {
        lv_obj_t* focused = lv_group_get_focused(menu_g);
        const int index = findMenuButtonIndex(focused);
        if (index >= 0 && static_cast<size_t>(index) < kMaxMenuApps)
        {
            ::ui::i18n::set_label_text(s_desc_label, s_menu_apps[index].name);
        }
    }

    ui::menu::dashboard::refresh_localized_text();
}

void refresh_theme()
{
    if (main_screen != nullptr)
    {
        lv_obj_set_style_bg_color(main_screen, ui::theme::page_bg(), 0);
    }
    if (s_menu_panel != nullptr)
    {
        lv_obj_set_style_bg_color(s_menu_panel, ui::theme::page_bg(), 0);
    }
    if (s_app_panel != nullptr)
    {
        lv_obj_set_style_bg_color(s_app_panel, ui::theme::white(), 0);
    }
    if (s_desc_label != nullptr)
    {
        lv_obj_set_style_text_color(s_desc_label, ui::theme::text(), 0);
    }
    if (s_bottom_bar != nullptr)
    {
        lv_obj_set_style_bg_color(s_bottom_bar, ui::theme::surface_alt(), 0);
        apply_component_profile(s_bottom_bar, ::ui::theme::ComponentSlot::MenuDashboardBottomChips);
    }
    if (s_bottom_node_chip.label != nullptr)
    {
        apply_component_profile(s_bottom_node_chip.container,
                                ::ui::theme::ComponentSlot::MenuBottomChipNode);
        apply_component_profile(s_bottom_node_chip.label,
                                ::ui::theme::ComponentSlot::MenuBottomChipNode);
    }
    if (s_bottom_ram_chip.label != nullptr)
    {
        apply_component_profile(s_bottom_ram_chip.container,
                                ::ui::theme::ComponentSlot::MenuBottomChipRam);
        apply_component_profile(s_bottom_ram_chip.label,
                                ::ui::theme::ComponentSlot::MenuBottomChipRam);
    }
    if (s_bottom_psram_chip.label != nullptr)
    {
        apply_component_profile(s_bottom_psram_chip.container,
                                ::ui::theme::ComponentSlot::MenuBottomChipPsram);
        apply_component_profile(s_bottom_psram_chip.label,
                                ::ui::theme::ComponentSlot::MenuBottomChipPsram);
    }

    for (auto& item : s_menu_apps)
    {
        if (item.button != nullptr)
        {
            lv_obj_set_style_bg_color(item.button, ui::theme::surface(), 0);
            lv_obj_set_style_border_color(item.button, ui::theme::border(), 0);
            lv_obj_set_style_bg_color(item.button, ui::theme::accent(), LV_STATE_FOCUSED);
            lv_obj_set_style_border_color(item.button, ui::theme::border(), LV_STATE_FOCUSED);
            lv_obj_set_style_bg_color(item.button, ui::theme::accent(), LV_STATE_FOCUS_KEY);
            lv_obj_set_style_border_color(item.button, ui::theme::border(), LV_STATE_FOCUS_KEY);
            apply_component_profile(item.button, ::ui::theme::ComponentSlot::MenuAppButton);
        }
        if (item.label != nullptr)
        {
            lv_obj_set_style_text_color(item.label, ui::theme::text(), 0);
            lv_obj_set_style_text_color(item.label, ui::theme::text(), LV_STATE_FOCUSED);
            apply_component_profile(item.label, ::ui::theme::ComponentSlot::MenuAppLabel);
        }
        if (item.icon != nullptr && item.app != nullptr)
        {
            apply_menu_icon_source(item.icon, item);
        }
    }
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
