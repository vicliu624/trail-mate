#include "ui_shell/shared_ui_shell.h"

#include <cstdio>
#include <string>

#include "platform/ui/device_runtime.h"
#include "ui/app_catalog.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"
#include "ui/localization.h"
#include "ui/startup_ui_shell.h"
#include "ui/ui_theme.h"

namespace trailmate::cardputer_zero::simulator::ui_shell
{
namespace
{

extern "C"
{
    extern const lv_image_dsc_t Chat;
    extern const lv_image_dsc_t gps_icon;
    extern const lv_image_dsc_t Setting;
    extern const lv_image_dsc_t ext;
}

struct PlaceholderPageSpec
{
    const char* stable_id = nullptr;
    const char* title = nullptr;
    const char* body = nullptr;
    const char* chip = nullptr;
    const lv_image_dsc_t* icon = nullptr;
    uint32_t chip_color = 0xEBA341;
    bool show_runtime_snapshot = false;
};

std::string format_bytes_mib(std::size_t bytes)
{
    char buffer[32];
    const unsigned long mib_whole = static_cast<unsigned long>(bytes / (1024U * 1024U));
    const unsigned long mib_tenth =
        static_cast<unsigned long>((bytes % (1024U * 1024U)) * 10U / (1024U * 1024U));
    if (mib_tenth == 0U)
    {
        std::snprintf(buffer, sizeof(buffer), "%lu MiB", mib_whole);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%lu.%lu MiB", mib_whole, mib_tenth);
    }
    return buffer;
}

std::string build_runtime_snapshot()
{
    const auto battery = ::platform::ui::device::battery_info();
    const auto memory = ::platform::ui::device::memory_stats();

    char buffer[512];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "Firmware: %s\nBattery: %s\nGPS capability: %s\nGPS ready: %s\nSD ready: %s\nRAM total: %s\nPSRAM: %s",
                  ::platform::ui::device::firmware_version(),
                  battery.level >= 0 ? (battery.charging ? "charging" : "available") : "unknown",
                  ::platform::ui::device::gps_supported() ? "yes" : "no",
                  ::platform::ui::device::gps_ready() ? "yes" : "no",
                  ::platform::ui::device::sd_ready() ? "yes" : "no",
                  format_bytes_mib(memory.ram_total_bytes).c_str(),
                  memory.psram_available ? format_bytes_mib(memory.psram_total_bytes).c_str() : "not present");
    return buffer;
}

void request_exit()
{
    ui_request_exit_to_menu();
}

void back_button_event_cb(lv_event_t* event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED)
    {
        request_exit();
        return;
    }

    if (code != LV_EVENT_KEY)
    {
        return;
    }

    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE || key == LV_KEY_LEFT)
    {
        request_exit();
    }
}

lv_obj_t* create_chip(lv_obj_t* parent, const PlaceholderPageSpec& spec)
{
    lv_obj_t* chip = lv_obj_create(parent);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(chip, lv_color_hex(spec.chip_color), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(chip, 0, 0);
    lv_obj_set_style_pad_left(chip, 10, 0);
    lv_obj_set_style_pad_right(chip, 10, 0);
    lv_obj_set_style_pad_top(chip, 4, 0);
    lv_obj_set_style_pad_bottom(chip, 4, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(chip);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, ui::theme::text(), 0);
    ::ui::i18n::set_label_text(label, spec.chip ? spec.chip : "Preview");
    lv_obj_center(label);
    return chip;
}

void placeholder_enter(void* user_data, lv_obj_t* parent)
{
    auto* spec = static_cast<PlaceholderPageSpec*>(user_data);
    if (spec == nullptr || parent == nullptr)
    {
        return;
    }

    lv_obj_clean(parent);
    set_default_group(app_g);

    lv_obj_t* page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(page, ui::theme::page_bg(), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_shadow_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 12, 0);
    lv_obj_set_style_pad_row(page, 10, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = lv_obj_create(page);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_shadow_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_column(header, 8, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 40, 32);
    lv_obj_set_style_bg_color(back_btn, ui::theme::surface_alt(), 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_radius(back_btn, 10, 0);
    lv_obj_add_event_cb(back_btn, back_button_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(back_btn, back_button_event_cb, LV_EVENT_KEY, nullptr);

    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_obj_set_style_text_color(back_label, ui::theme::text(), 0);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_18, 0);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);

    lv_group_add_obj(app_g, back_btn);
    lv_group_focus_obj(back_btn);

    lv_obj_t* title_wrap = lv_obj_create(header);
    lv_obj_set_flex_grow(title_wrap, 1);
    lv_obj_set_height(title_wrap, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_wrap, 0, 0);
    lv_obj_set_style_radius(title_wrap, 0, 0);
    lv_obj_set_style_shadow_width(title_wrap, 0, 0);
    lv_obj_set_style_pad_all(title_wrap, 0, 0);
    lv_obj_set_flex_flow(title_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(title_wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(title_wrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(title_wrap);
    lv_obj_set_style_text_color(title, ui::theme::text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    ::ui::i18n::set_label_text(title, spec->title);

    lv_obj_t* subtitle = lv_label_create(title_wrap);
    lv_obj_set_style_text_color(subtitle, ui::theme::text_muted(), 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
    ::ui::i18n::set_label_text(subtitle, "Shared UI shell inside the device simulator");

    create_chip(header, *spec);

    lv_obj_t* body_card = lv_obj_create(page);
    lv_obj_set_width(body_card, LV_PCT(100));
    lv_obj_set_flex_grow(body_card, 1);
    lv_obj_set_style_bg_color(body_card, ui::theme::surface(), 0);
    lv_obj_set_style_bg_opa(body_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(body_card, ui::theme::border(), 0);
    lv_obj_set_style_border_width(body_card, 1, 0);
    lv_obj_set_style_radius(body_card, 14, 0);
    lv_obj_set_style_shadow_width(body_card, 0, 0);
    lv_obj_set_style_pad_all(body_card, 12, 0);
    lv_obj_set_style_pad_row(body_card, 10, 0);
    lv_obj_set_flex_flow(body_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(body_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* body = lv_label_create(body_card);
    lv_obj_set_width(body, LV_PCT(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(body, ui::theme::text(), 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_14, 0);
    ::ui::i18n::set_content_label_text(body, spec->body);

    if (spec->show_runtime_snapshot)
    {
        lv_obj_t* snapshot = lv_label_create(body_card);
        lv_obj_set_width(snapshot, LV_PCT(100));
        lv_label_set_long_mode(snapshot, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(snapshot, ui::theme::text_muted(), 0);
        lv_obj_set_style_text_font(snapshot, &lv_font_montserrat_12, 0);
        const std::string snapshot_text = build_runtime_snapshot();
        ::ui::i18n::set_content_label_text_raw(snapshot, snapshot_text.c_str());
    }

    lv_obj_t* footer = lv_label_create(page);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_style_text_color(footer, ui::theme::text_muted(), 0);
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
    ::ui::i18n::set_label_text(
        footer,
        "Press Esc / Backspace or use the back button to return to the menu.");
}

void placeholder_exit(void* user_data, lv_obj_t* parent)
{
    (void)user_data;
    if (parent != nullptr)
    {
        lv_obj_clean(parent);
    }
    set_default_group(menu_g);
}

PlaceholderPageSpec s_chat_spec{
    .stable_id = "chat",
    .title = "Chat",
    .body =
        "This placeholder is standing in for the future Linux chat slice. "
        "The menu shell, tile navigation and app handoff are already real ui_shared code.",
    .chip = "Phase 1",
    .icon = &Chat,
    .chip_color = 0xF1B75A,
    .show_runtime_snapshot = false,
};

PlaceholderPageSpec s_gps_spec{
    .stable_id = "gps",
    .title = "GPS",
    .body =
        "GPS runtime is still a Linux stub on Cardputer Zero. "
        "Bringing this page up next will mean replacing this placeholder with the shared GPS layout plus Linux adapters.",
    .chip = "Stubbed",
    .icon = &gps_icon,
    .chip_color = 0xCFE4FF,
    .show_runtime_snapshot = false,
};

PlaceholderPageSpec s_settings_spec{
    .stable_id = "settings",
    .title = "Settings",
    .body =
        "File-backed settings_store is already live on Linux. "
        "This page is the next good candidate for replacing the placeholder with a real shared screen.",
    .chip = "Next",
    .icon = &Setting,
    .chip_color = 0xD4F0D2,
    .show_runtime_snapshot = false,
};

PlaceholderPageSpec s_system_spec{
    .stable_id = "system",
    .title = "System",
    .body =
        "This preview app is fed by the Linux runtime layer rather than hardcoded UI state. "
        "It is useful for validating that the simulator and future Pi OS shell see the same platform contract.",
    .chip = "Runtime",
    .icon = &ext,
    .chip_color = 0xF8D6B5,
    .show_runtime_snapshot = true,
};

ui::CallbackAppScreen s_chat_app{
    s_chat_spec.stable_id,
    s_chat_spec.title,
    s_chat_spec.icon,
    placeholder_enter,
    placeholder_exit,
    &s_chat_spec,
};

ui::CallbackAppScreen s_gps_app{
    s_gps_spec.stable_id,
    s_gps_spec.title,
    s_gps_spec.icon,
    placeholder_enter,
    placeholder_exit,
    &s_gps_spec,
};

ui::CallbackAppScreen s_settings_app{
    s_settings_spec.stable_id,
    s_settings_spec.title,
    s_settings_spec.icon,
    placeholder_enter,
    placeholder_exit,
    &s_settings_spec,
};

ui::CallbackAppScreen s_system_app{
    s_system_spec.stable_id,
    s_system_spec.title,
    s_system_spec.icon,
    placeholder_enter,
    placeholder_exit,
    &s_system_spec,
};

AppScreen* s_apps[] = {
    &s_chat_app,
    &s_gps_app,
    &s_settings_app,
    &s_system_app,
};

ui::StaticAppCatalogState& catalog_state()
{
    static ui::StaticAppCatalogState state = ui::makeStaticAppCatalogState(s_apps);
    return state;
}

} // namespace

bool initialize()
{
    ui::startup_ui_shell::Hooks hooks{};
    hooks.apps = ui::makeStaticAppCatalog(&catalog_state());

    if (!ui::startup_ui_shell::prepareBootUi(hooks, true))
    {
        return false;
    }

    if (!ui::startup_ui_shell::initializeMenuSkeleton(hooks))
    {
        return false;
    }

    return ui::startup_ui_shell::finalizeStartup(hooks, true);
}

} // namespace trailmate::cardputer_zero::simulator::ui_shell
