/**
 * @file top_bar.cpp
 * @brief Shared top bar widget (bright header reused across pages)
 */

#include "ui/widgets/top_bar.h"
#if defined(ARDUINO_T_LORA_PAGER)
#include "platform/ui/psram_ui_lease.h"
#endif

#include <algorithm>
#include <cstring>

#include "ui/assets/fonts/font_utils.h"
#include "ui/app_runtime.h"
#include "ui/input/pager_navigation.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/ui_theme.h"
#include "ui/widgets/top_bar_power_presenter.h"

#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_18) || !LV_FONT_MONTSERRAT_18
#define lv_font_montserrat_18 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_20) || !LV_FONT_MONTSERRAT_20
#define lv_font_montserrat_20 lv_font_montserrat_18
#endif

namespace ui
{
namespace widgets
{

static void back_event_cb(lv_event_t* e)
{
    TopBar* bar = static_cast<TopBar*>(lv_event_get_user_data(e));
    if (bar != nullptr && bar->back_cb != nullptr)
    {
        bar->back_cb(bar->back_user_data);
    }
}

static void help_event_cb(lv_event_t* event)
{
    lv_group_t* group = nullptr;
    for (auto* device = lv_indev_get_next(nullptr); device; device = lv_indev_get_next(device))
        if (lv_indev_get_type(device) == LV_INDEV_TYPE_KEYPAD && lv_indev_get_group(device))
        {
            group = lv_indev_get_group(device);
            break;
        }
    if (!group) group = lv_group_get_default();
    if (!group) group = app_g;
    (void)::ui::input::pager::open_help(static_cast<lv_obj_t*>(lv_event_get_user_data(event)), group);
}

static lv_coord_t resolve_top_bar_height(const TopBarConfig& config)
{
    if (config.height > 0)
    {
        return config.height;
    }

    const lv_coord_t profile_height = ::ui::page_profile::current().top_bar_height;
    return profile_height > 0 ? profile_height : static_cast<lv_coord_t>(kTopBarHeight);
}

static const lv_font_t* resolve_top_bar_font(lv_coord_t height)
{
    if (height <= ::ui::kUnifiedTopBarHeight)
        return &lv_font_montserrat_14;
#if defined(ARDUINO_T_DECK_PRO)
    (void)height;
    return ::ui::page_profile::resolve_title_font();
#else
    if (height >= 60)
    {
        return &lv_font_montserrat_20;
    }
    if (height >= 48)
    {
        return &lv_font_montserrat_18;
    }
    if (height >= 40)
    {
        return &lv_font_montserrat_16;
    }
    if (height <= 24)
    {
        return ::ui::page_profile::resolve_title_font();
    }
    return &lv_font_montserrat_14;
#endif
}

void top_bar_init(TopBar& bar, lv_obj_t* parent, const TopBarConfig& config)
{
#if defined(ARDUINO_T_LORA_PAGER)
    platform::ui::PsramUiLease memory;
#endif
    const auto& profile = ::ui::page_profile::current();
    const lv_coord_t resolved_height = resolve_top_bar_height(config);
    const bool large_touch = resolved_height > ::ui::kUnifiedTopBarHeight &&
                             (profile.large_touch_hitbox || resolved_height >= 56);
    const bool dense = !large_touch && resolved_height <= 24;
    const bool narrow = dense && lv_display_get_horizontal_resolution(nullptr) <= 320;
    const lv_coord_t side_pad = large_touch ? 18 : (dense ? 4 : (resolved_height >= 40 ? 14 : 10));
    const lv_coord_t vertical_pad = large_touch ? 10 : (dense ? 1 : (resolved_height >= 40 ? 8 : 6));
    const lv_coord_t back_btn_height = std::max<lv_coord_t>(
        large_touch ? 44 : (dense ? 16 : 20),
        resolved_height - (vertical_pad * 2));
    const lv_coord_t back_btn_width = std::max<lv_coord_t>(
#if defined(ARDUINO_T_DECK_PRO)
        52, // Keep the four-letter BACK label readable in the compact bar.
#else
        large_touch ? 68 : (dense ? 28 : 44),
#endif
        back_btn_height + (large_touch ? 24 : (dense ? 10 : (resolved_height >= 40 ? 16 : 10))));
#if defined(ARDUINO_T_DECK_PRO)
    const lv_coord_t back_btn_radius = 0;
#else
    const lv_coord_t back_btn_radius = std::max<lv_coord_t>(large_touch ? 16 : 10, back_btn_height / 2);
#endif
    const lv_coord_t right_label_width = large_touch ? 156 : (narrow ? 56 : (dense ? 72 : (resolved_height >= 40 ? 120 : 90)));
    const lv_font_t* text_font = resolve_top_bar_font(resolved_height);

    bar.container = lv_obj_create(parent);
    lv_obj_set_size(bar.container, LV_PCT(100), resolved_height);
    lv_obj_set_style_bg_color(bar.container, ui::theme::accent(), 0);
    lv_obj_set_style_bg_opa(bar.container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar.container, 0, 0);
    lv_obj_set_style_pad_left(bar.container, side_pad, 0);
    lv_obj_set_style_pad_right(bar.container, side_pad, 0);
    lv_obj_set_style_pad_top(bar.container, vertical_pad, 0);
    lv_obj_set_style_pad_bottom(bar.container, vertical_pad, 0);
    lv_obj_set_style_radius(bar.container, 0, 0);
    lv_obj_clear_flag(bar.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bar.container, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_flex_flow(bar.container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar.container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar.container, large_touch ? 12 : 6, 0);

    if (config.back_btn_override != nullptr)
    {
        bar.back_btn = config.back_btn_override;
    }
    else if (config.create_back)
    {
        bar.back_btn = lv_btn_create(bar.container);
        lv_obj_set_size(bar.back_btn, back_btn_width, back_btn_height);
        lv_obj_set_style_bg_color(bar.back_btn, ui::theme::surface(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar.back_btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(bar.back_btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(bar.back_btn, ui::theme::border(), LV_PART_MAIN);
        lv_obj_set_style_radius(bar.back_btn, back_btn_radius, LV_PART_MAIN);
        if (dense) lv_obj_set_style_pad_all(bar.back_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar.back_btn, ui::theme::accent(), LV_STATE_FOCUSED);
        lv_obj_set_style_outline_width(bar.back_btn, 0, LV_STATE_FOCUSED);
        lv_obj_align(bar.back_btn, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_event_cb(bar.back_btn, back_event_cb, LV_EVENT_CLICKED, &bar);
        lv_obj_t* back_label = lv_label_create(bar.back_btn);
        lv_label_set_text(back_label,
#if defined(ARDUINO_T_DECK_PRO)
                          "BACK"
#else
                          LV_SYMBOL_LEFT
#endif
        );
        lv_obj_center(back_label);
        lv_obj_set_style_text_color(back_label,
#if defined(ARDUINO_T_DECK_PRO)
                                    lv_color_black(),
#else
                                    ui::theme::text(),
#endif
                                    0);
        lv_obj_set_style_text_font(back_label, text_font, 0);
    }

    if (config.title_label_override != nullptr)
    {
        bar.title_label = config.title_label_override;
    }
    else
    {
        bar.title_label = lv_label_create(bar.container);
        lv_label_set_text(bar.title_label, "");
        lv_label_set_long_mode(bar.title_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(bar.title_label,
#if defined(ARDUINO_T_DECK_PRO)
                                    lv_color_white(),
#else
                                    ui::theme::text(),
#endif
                                    0);
        lv_obj_set_style_bg_opa(bar.title_label, LV_OPA_TRANSP, 0);
    }
    lv_obj_set_style_text_font(bar.title_label, text_font, 0);
#if defined(ARDUINO_T_DECK_PRO)
    lv_obj_set_style_text_color(bar.title_label, lv_color_white(), 0);
#endif
    lv_obj_set_flex_grow(bar.title_label, 1);
    lv_obj_set_width(bar.title_label, 0);
    lv_obj_set_style_min_width(bar.title_label, 0, 0);
    lv_obj_set_style_text_align(bar.title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_max_width(bar.title_label, LV_PCT(100), 0);

    bar.right_label = lv_label_create(bar.container);
    lv_label_set_text(bar.right_label, "");
    lv_obj_set_width(bar.right_label, right_label_width);
    lv_obj_set_height(bar.right_label, lv_font_get_line_height(text_font));
    lv_label_set_long_mode(bar.right_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(bar.right_label,
#if defined(ARDUINO_T_DECK_PRO)
                                lv_color_white(),
#else
                                ui::theme::text_muted(),
#endif
                                0);
    lv_obj_set_style_text_align(bar.right_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(bar.right_label, text_font, 0);
    lv_obj_set_style_bg_opa(bar.right_label, LV_OPA_TRANSP, 0);

    if (config.power_indicator)
    {
        top_bar_power::bind(bar);
    }
#if defined(ARDUINO_T_LORA_PAGER) || defined(TRAIL_MATE_PLATFORM_LINUX)
    if (config.page_help)
    {
        // Dialog headers own their shortcuts. Registering them as pages would
        // make H open another Help instead of closing the current dialog.
        auto* help_hint = lv_label_create(bar.container);
        lv_label_set_text(help_hint, narrow ? "H ?" : "H Help");
        lv_obj_set_style_text_font(help_hint, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(help_hint, ui::theme::text(), 0);
        lv_obj_add_flag(help_hint, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(help_hint, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_set_ext_click_area(help_hint, dense ? 4 : 0);
        if (large_touch)
        {
            lv_obj_set_style_min_height(help_hint, 44, 0);
            lv_obj_set_style_min_width(help_hint, 72, 0);
            lv_obj_set_style_pad_all(help_hint, 8, 0);
        }
        lv_obj_add_event_cb(help_hint, help_event_cb, LV_EVENT_CLICKED, parent);
        lv_obj_move_to_index(help_hint, lv_obj_get_index(bar.right_label));
        ui::input::pager::install_help(parent, bar.title_label);
    }
#endif
}

void top_bar_set_title(TopBar& bar, const char* title)
{
    if (bar.title_label == nullptr || title == nullptr)
    {
        return;
    }

    const char* current = lv_label_get_text(bar.title_label);
    if (current != nullptr && std::strcmp(current, title) == 0)
    {
        return;
    }

    lv_label_set_text(bar.title_label, title);
    ::ui::fonts::apply_content_font(
        bar.title_label, title, resolve_top_bar_font(lv_obj_get_height(bar.container)));
}

void top_bar_set_right_text(TopBar& bar, const char* text)
{
    if (bar.right_label == nullptr || text == nullptr)
    {
        return;
    }

    top_bar_power::unbind(bar);

    const char* current = lv_label_get_text(bar.right_label);
    if (current != nullptr && std::strcmp(current, text) == 0)
    {
        return;
    }

    lv_label_set_text(bar.right_label, text);
    ::ui::fonts::apply_content_font(
        bar.right_label, text, resolve_top_bar_font(lv_obj_get_height(bar.container)));
}

void top_bar_set_right_text_ascii(TopBar& bar, const char* text)
{
    if (bar.right_label == nullptr || text == nullptr)
    {
        return;
    }

    const char* current = lv_label_get_text(bar.right_label);
    if (current != nullptr && std::strcmp(current, text) == 0)
    {
        return;
    }

    lv_label_set_text(bar.right_label, text);
    ::ui::fonts::apply_font(
        bar.right_label, resolve_top_bar_font(lv_obj_get_height(bar.container)));
}

void top_bar_set_back_callback(TopBar& bar, void (*cb)(void*), void* user_data)
{
    bar.back_cb = cb;
    bar.back_user_data = user_data;
}

lv_obj_t* top_bar_create_dialog_header(lv_obj_t* parent, const char* title,
                                       lv_event_cb_t on_close, void* context, lv_obj_t** close_button)
{
#if defined(ARDUINO_T_LORA_PAGER)
    platform::ui::PsramUiLease memory;
#endif
    TopBar bar{};
    TopBarConfig config{};
    // Neither a back callback nor the power presenter retains this local view.
    config.create_back = false;
    config.power_indicator = false;
    config.page_help = false;
    top_bar_init(bar, parent, config);
    lv_obj_del(bar.right_label);
    top_bar_set_title(bar, ::ui::i18n::tr(title));
    // A header is a leaf widget. Keep its close control independent of the
    // taskbar/menu implementation so standalone pages can link it directly.
    auto* close = lv_button_create(bar.container);
    lv_obj_set_style_bg_color(close, lv_color_hex(0xF8E6C3), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(close, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(close, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(close, lv_color_hex(0x8A6E43), LV_PART_MAIN);
    lv_obj_set_style_radius(close, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(close, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(close, lv_color_hex(0x25170D), LV_PART_MAIN);
    lv_obj_set_style_text_font(close, resolve_top_bar_font(resolve_top_bar_height(config)), LV_PART_MAIN);
    lv_obj_clear_flag(close, LV_OBJ_FLAG_SCROLLABLE);
    auto* caption = lv_label_create(close);
    lv_label_set_text(caption, LV_SYMBOL_CLOSE);
    lv_obj_center(caption);
    if (on_close) lv_obj_add_event_cb(close, on_close, LV_EVENT_CLICKED, context);
    const auto size = std::max<lv_coord_t>(16, resolve_top_bar_height(config) - 4);
    lv_obj_set_size(close, size, size);
    lv_obj_set_style_pad_all(close, 0, LV_PART_MAIN);
    if (close_button) *close_button = close;
    return bar.container;
}

} // namespace widgets
} // namespace ui
