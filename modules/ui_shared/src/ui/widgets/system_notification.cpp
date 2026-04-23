/**
 * @file system_notification.cpp
 * @brief System-level notification toast component implementation
 */

#include "ui/widgets/system_notification.h"

#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/theme/theme_component_style.h"
#include "ui/theme/theme_registry.h"
#include "ui/ui_theme.h"

#include <cstring>

#if !defined(LV_FONT_MONTSERRAT_20) || !LV_FONT_MONTSERRAT_20
#define lv_font_montserrat_20 lv_font_montserrat_18
#endif

namespace ui
{

lv_obj_t* SystemNotification::container_ = nullptr;
lv_obj_t* SystemNotification::icon_ = nullptr;
lv_obj_t* SystemNotification::icon_image_ = nullptr;
lv_obj_t* SystemNotification::icon_fallback_label_ = nullptr;
lv_obj_t* SystemNotification::label_ = nullptr;
lv_timer_t* SystemNotification::hide_timer_ = nullptr;
bool SystemNotification::visible_ = false;
std::string SystemNotification::icon_path_;

void SystemNotification::refreshIcon()
{
    if (!icon_ || !icon_image_ || !icon_fallback_label_)
    {
        return;
    }

    icon_path_.clear();
    const bool has_external =
        ::ui::theme::resolve_asset_path(::ui::theme::asset_slot_id(::ui::theme::AssetSlot::NotificationAlert),
                                        icon_path_);
    if (has_external)
    {
        lv_image_set_src(icon_image_, icon_path_.c_str());
        lv_obj_clear_flag(icon_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(icon_fallback_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(icon_image_);
        return;
    }

    if (const lv_image_dsc_t* builtin = ::ui::theme::builtin_asset(::ui::theme::AssetSlot::NotificationAlert))
    {
        lv_image_set_src(icon_image_, builtin);
        lv_obj_clear_flag(icon_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(icon_fallback_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(icon_image_);
        return;
    }

    lv_label_set_text(icon_fallback_label_, "!");
    lv_obj_set_style_text_color(icon_fallback_label_,
                                ::ui::theme::color(::ui::theme::ColorSlot::StateWarn),
                                0);
    lv_obj_add_flag(icon_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(icon_fallback_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(icon_fallback_label_);
}

void SystemNotification::init()
{
    if (container_)
    {
        return; // Already initialized
    }

    // Get the active screen (top layer)
    lv_obj_t* top_layer = lv_layer_top();

    // Create container - with 30px margins on left and right
    container_ = lv_obj_create(top_layer);
    ::ui::theme::ComponentProfile banner_profile{};
    (void)::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::NotificationBanner,
                                                 banner_profile);
    // Get screen width and calculate container width (screen width - 60px for 30px margins on each side)
    lv_coord_t screen_width = lv_display_get_physical_horizontal_resolution(NULL);
    const auto& profile = ::ui::page_profile::current();
    lv_coord_t margin = profile.large_touch_hitbox ? 40 : 30;
    lv_coord_t container_width = screen_width - (margin * 2);
    lv_obj_set_size(container_, container_width, profile.large_touch_hitbox ? 72 : 50);
    lv_obj_set_pos(container_, margin, profile.large_touch_hitbox ? -84 : -60);
    lv_obj_set_style_bg_color(container_, ::ui::theme::surface_alt(), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    // Set radius for bottom corners only
    // Since LVGL doesn't support per-corner radius, we'll set a larger radius (20)
    // and position the container at y=0 so top corners are effectively square (clipped by screen edge)
    lv_obj_set_style_radius(container_, 20, 0); // Larger radius for bottom corners (increased from 8 to 20)
    lv_obj_set_style_pad_all(container_, 8, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_shadow_width(container_, 10, 0);
    lv_obj_set_style_shadow_color(container_, ::ui::theme::accent_strong(), 0);
    lv_obj_set_style_shadow_opa(container_, LV_OPA_50, 0);
    ::ui::theme::apply_component_profile_to_obj(container_, banner_profile);

    // Create flex layout
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(container_, 8, 0);

    // Create icon
    icon_ = lv_obj_create(container_);
    lv_obj_set_size(icon_, 24, 24);
    lv_obj_set_style_bg_opa(icon_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_, 0, 0);
    lv_obj_set_style_pad_all(icon_, 0, 0);
    lv_obj_set_style_radius(icon_, 0, 0);
    lv_obj_set_style_shadow_width(icon_, 0, 0);
    lv_obj_clear_flag(icon_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(icon_, LV_OBJ_FLAG_CLICKABLE);

    icon_image_ = lv_image_create(icon_);
    lv_obj_add_flag(icon_image_, LV_OBJ_FLAG_HIDDEN);

    icon_fallback_label_ = lv_label_create(icon_);
    lv_label_set_text(icon_fallback_label_, "");
    lv_obj_set_style_text_font(icon_fallback_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon_fallback_label_, ::ui::theme::color(::ui::theme::ColorSlot::StateWarn), 0);
    lv_obj_add_flag(icon_fallback_label_, LV_OBJ_FLAG_HIDDEN);
    refreshIcon();

    // Create label with larger font
    label_ = lv_label_create(container_);
    lv_label_set_text(label_, "");
    lv_obj_set_style_text_color(label_,
                                banner_profile.text_color.present
                                    ? banner_profile.text_color.value
                                    : ::ui::theme::text(),
                                0);
    lv_obj_set_style_text_font(label_, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_obj_set_flex_grow(label_, 1);

    // Initially hidden
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

void SystemNotification::show(const char* text, uint32_t duration_ms)
{
    if (!container_)
    {
        init();
    }

    const char* localized = ::ui::i18n::tr(text ? text : "");
    const bool has_non_ascii = ::ui::fonts::utf8_has_non_ascii(localized);

    // Keep the historical short-toast behavior for ASCII text, but avoid
    // byte-splitting UTF-8 strings.
    char truncated[96];
    size_t len = strlen(localized);
    const size_t max_chars = ::ui::page_profile::current().large_touch_hitbox ? 30 : 15;
    if (!has_non_ascii && len > max_chars)
    {
        strncpy(truncated, localized, max_chars);
        truncated[max_chars] = '\0';
    }
    else
    {
        strncpy(truncated, localized, sizeof(truncated) - 1);
        truncated[sizeof(truncated) - 1] = '\0';
    }

    lv_label_set_text(label_, truncated);
    ::ui::fonts::apply_localized_font(label_, truncated, ::ui::fonts::ui_chrome_font());
    refreshIcon();

    // Cancel existing timer if any
    if (hide_timer_)
    {
        lv_timer_del(hide_timer_);
        hide_timer_ = nullptr;
    }

    // If already visible, hide first
    if (visible_)
    {
        hide();
    }

    // Set initial position (above screen)
    lv_obj_set_y(container_, ::ui::page_profile::current().large_touch_hitbox ? -84 : -60);

    // Show container
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    visible_ = true;

    // Animate from top (y = -60) to visible position (y = 0, tight to top edge)
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, container_);
    lv_anim_set_values(&anim, ::ui::page_profile::current().large_touch_hitbox ? -84 : -60, 0);
    lv_anim_set_time(&anim, 300);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_ready_cb(&anim, animReadyCallback);
    lv_anim_start(&anim);

    // Set up auto-hide timer
    hide_timer_ = lv_timer_create(hideTimerCallback, duration_ms, nullptr);
    lv_timer_set_repeat_count(hide_timer_, 1);
}

void SystemNotification::hide()
{
    if (!container_ || !visible_)
    {
        return;
    }

    // Cancel timer
    if (hide_timer_)
    {
        lv_timer_del(hide_timer_);
        hide_timer_ = nullptr;
    }

    // Animate out (move to y = -60)
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, container_);
    lv_anim_set_values(&anim, lv_obj_get_y(container_), ::ui::page_profile::current().large_touch_hitbox ? -84 : -60);
    lv_anim_set_time(&anim, 300);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_ready_cb(&anim, [](lv_anim_t* a)
                         {
        (void)a;
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        visible_ = false; });
    lv_anim_start(&anim);
}

bool SystemNotification::isVisible()
{
    return visible_ && container_ && !lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

void SystemNotification::hideTimerCallback(lv_timer_t* timer)
{
    (void)timer;
    hide();
}

void SystemNotification::animReadyCallback(lv_anim_t* anim)
{
    (void)anim;
    // Animation completed, notification is now visible
}

} // namespace ui
