#include "ui/shared_ui_shell.h"

#include "ui/app_catalog.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"
#include "ui/localization.h"
#include "ui/screens/chat/chat_page_shell.h"
#include "ui/screens/contacts/contacts_page_shell.h"
#include "ui/screens/energy_sweep/energy_sweep_page_shell.h"
#include "ui/screens/extensions/extensions_page_shell.h"
#include "ui/screens/gnss/gnss_skyplot_page_shell.h"
#include "ui/screens/gps/gps_page_shell.h"
#include "ui/screens/pc_link/pc_link_page_shell.h"
#include "ui/screens/settings/settings_page_shell.h"
#include "ui/screens/sstv/sstv_page_shell.h"
#include "ui/screens/team/team_page_shell.h"
#include "ui/screens/tracker/tracker_page_shell.h"
#include "ui/screens/walkie_talkie/walkie_talkie_page_shell.h"
#include "ui/startup_ui_shell.h"
#include "ui/ui_boot.h"
#include "ui/ui_theme.h"

namespace trailmate::cardputer_zero::linux_ui {
namespace {

extern "C" {
extern const lv_image_dsc_t Chat;
extern const lv_image_dsc_t contact;
extern const lv_image_dsc_t gps_icon;
extern const lv_image_dsc_t Satellite;
extern const lv_image_dsc_t Setting;
extern const lv_image_dsc_t Spectrum;
extern const lv_image_dsc_t rf;
extern const lv_image_dsc_t sstv;
extern const lv_image_dsc_t team_icon;
extern const lv_image_dsc_t tracker_icon;
extern const lv_image_dsc_t ext;
extern const lv_image_dsc_t walkie_talkie;
}

struct PlaceholderPageSpec {
    const char* stable_id = nullptr;
    const char* title = nullptr;
    const char* body = nullptr;
    const char* chip = nullptr;
    const lv_image_dsc_t* icon = nullptr;
    uint32_t chip_color = 0xEBA341;
};

void requestExit()
{
    ui_request_exit_to_menu();
}

void backButtonEventCb(lv_event_t* event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED) {
        requestExit();
        return;
    }

    if (code != LV_EVENT_KEY) {
        return;
    }

    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE || key == LV_KEY_LEFT) {
        requestExit();
    }
}

lv_obj_t* createChip(lv_obj_t* parent, const PlaceholderPageSpec& spec)
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

void placeholderEnter(void* user_data, lv_obj_t* parent)
{
    auto* spec = static_cast<PlaceholderPageSpec*>(user_data);
    if (spec == nullptr || parent == nullptr) {
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
    lv_obj_add_event_cb(back_btn, backButtonEventCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(back_btn, backButtonEventCb, LV_EVENT_KEY, nullptr);

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
    ::ui::i18n::set_label_text(subtitle, "Shared UI shell inside the Linux shell");

    createChip(header, *spec);

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

    lv_obj_t* footer = lv_label_create(page);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_style_text_color(footer, ui::theme::text_muted(), 0);
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
    ::ui::i18n::set_label_text(
        footer,
        "Press Esc / Backspace or use the back button to return to the menu.");
}

void placeholderExit(void* user_data, lv_obj_t* parent)
{
    (void)user_data;
    if (parent != nullptr) {
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
};

PlaceholderPageSpec s_gps_spec{
    .stable_id = "map",
    .title = "Map",
    .body =
        "GPS runtime is still a Linux stub on Cardputer Zero. "
        "Bringing this page up next will mean replacing this placeholder with the shared GPS layout plus Linux adapters.",
    .chip = "Stubbed",
    .icon = &gps_icon,
    .chip_color = 0xCFE4FF,
};

PlaceholderPageSpec s_contacts_spec{
    .stable_id = "contacts",
    .title = "Contacts",
    .body =
        "Contacts is part of the shared product shape, but the Linux-side local data store is still being brought up. "
        "This placeholder keeps the menu structure aligned while that seam is being implemented.",
    .chip = "Planned",
    .icon = &contact,
    .chip_color = 0xF8D6B5,
};

PlaceholderPageSpec s_sky_plot_spec{
    .stable_id = "sky_plot",
    .title = "Sky Plot",
    .body =
        "Sky Plot belongs in the Linux product shape, but the shared GNSS skyplot page still needs Linux-side data plumbing. "
        "This placeholder keeps the menu structure aligned until that adapter is brought up.",
    .chip = "Planned",
    .icon = &Satellite,
    .chip_color = 0xD8ECFF,
};

PlaceholderPageSpec s_team_spec{
    .stable_id = "team",
    .title = "Team",
    .body =
        "Team mode is a real product entry and should appear in the Linux menu. "
        "The page is still waiting on Linux-side team runtime and storage integration, so it stays as a placeholder for now.",
    .chip = "Planned",
    .icon = &team_icon,
    .chip_color = 0xD4F0D2,
};

PlaceholderPageSpec s_tracker_spec{
    .stable_id = "tracker",
    .title = "Tracker",
    .body =
        "Tracker is part of the full Cardputer Zero menu. "
        "The Linux storage/runtime seam for routes and tracks is still being completed, so this entry is placeholder-backed for now.",
    .chip = "Planned",
    .icon = &tracker_icon,
    .chip_color = 0xD9F1C9,
};

PlaceholderPageSpec s_pc_link_spec{
    .stable_id = "pc_link",
    .title = "PC Link",
    .body =
        "PC Link is reserved here as a real product entry. "
        "The Linux-side host link runtime is still a stub, so this page is currently a placeholder.",
    .chip = "Stubbed",
    .icon = &rf,
    .chip_color = 0xD9E7FF,
};

PlaceholderPageSpec s_sstv_spec{
    .stable_id = "sstv",
    .title = "SSTV",
    .body =
        "SSTV is included in the full menu shape even though Cardputer Zero does not have the runtime adapter yet. "
        "This placeholder will be replaced once the Linux-side feature seam is ready.",
    .chip = "Planned",
    .icon = &sstv,
    .chip_color = 0xFFE4B8,
};

PlaceholderPageSpec s_energy_sweep_spec{
    .stable_id = "energy_sweep",
    .title = "Energy Sweep",
    .body =
        "Energy Sweep belongs to the shared product menu, but Linux still needs a real RF/runtime implementation before this page can become functional.",
    .chip = "Stubbed",
    .icon = &Spectrum,
    .chip_color = 0xF8D6B5,
};

PlaceholderPageSpec s_extensions_spec{
    .stable_id = "extensions",
    .title = "Extensions",
    .body =
        "Extensions will eventually host Linux-side package and add-on workflows. "
        "The entry belongs in the menu now even though the actual page logic has not been migrated yet.",
    .chip = "Planned",
    .icon = &ext,
    .chip_color = 0xF3D8FF,
};

PlaceholderPageSpec s_walkie_spec{
    .stable_id = "walkie_talkie",
    .title = "Walkie Talkie",
    .body =
        "Walkie Talkie is part of the 12-entry product menu. "
        "The Linux audio/radio runtime is not connected yet, so this stays as a placeholder until that seam is implemented.",
    .chip = "Planned",
    .icon = &walkie_talkie,
    .chip_color = 0xFFD8E4,
};

ui::CallbackAppScreen s_chat_app{
    s_chat_spec.stable_id,
    s_chat_spec.title,
    s_chat_spec.icon,
    chat::ui::shell::enter,
    chat::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_gps_app{
    s_gps_spec.stable_id,
    s_gps_spec.title,
    s_gps_spec.icon,
    gps::ui::shell::enter,
    gps::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_contacts_app{
    s_contacts_spec.stable_id,
    s_contacts_spec.title,
    s_contacts_spec.icon,
    contacts::ui::shell::enter,
    contacts::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_sky_plot_app{
    s_sky_plot_spec.stable_id,
    s_sky_plot_spec.title,
    s_sky_plot_spec.icon,
    gnss::ui::shell::enter,
    gnss::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_team_app{
    s_team_spec.stable_id,
    s_team_spec.title,
    s_team_spec.icon,
    team::ui::shell::enter,
    team::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_tracker_app{
    s_tracker_spec.stable_id,
    s_tracker_spec.title,
    s_tracker_spec.icon,
    tracker::ui::shell::enter,
    tracker::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_pc_link_app{
    s_pc_link_spec.stable_id,
    s_pc_link_spec.title,
    s_pc_link_spec.icon,
    pc_link::ui::shell::enter,
    pc_link::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_sstv_app{
    s_sstv_spec.stable_id,
    s_sstv_spec.title,
    s_sstv_spec.icon,
    sstv_page::ui::shell::enter,
    sstv_page::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_energy_sweep_app{
    s_energy_sweep_spec.stable_id,
    s_energy_sweep_spec.title,
    s_energy_sweep_spec.icon,
    energy_sweep::ui::shell::enter,
    energy_sweep::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_extensions_app{
    s_extensions_spec.stable_id,
    s_extensions_spec.title,
    s_extensions_spec.icon,
    extensions::ui::shell::enter,
    extensions::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_walkie_app{
    s_walkie_spec.stable_id,
    s_walkie_spec.title,
    s_walkie_spec.icon,
    walkie_page::ui::shell::enter,
    walkie_page::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_settings_app{
    "settings",
    "Settings",
    &Setting,
    settings::ui::shell::enter,
    settings::ui::shell::exit,
    nullptr,
};

AppScreen* s_apps[] = {
    &s_chat_app,
    &s_gps_app,
    &s_sky_plot_app,
    &s_contacts_app,
    &s_team_app,
    &s_tracker_app,
    &s_pc_link_app,
    &s_sstv_app,
    &s_energy_sweep_app,
    &s_walkie_app,
    &s_extensions_app,
    &s_settings_app,
};

ui::StaticAppCatalogState& catalogState()
{
    static ui::StaticAppCatalogState state = ui::makeStaticAppCatalogState(s_apps);
    return state;
}

[[nodiscard]] ui::startup_ui_shell::Hooks buildHooks()
{
    ui::startup_ui_shell::Hooks hooks{};
    hooks.apps = ui::makeStaticAppCatalog(&catalogState());
    return hooks;
}

constexpr unsigned int kMenuBuildDelayMs = 140U;
constexpr unsigned int kThemeReadyDelayMs = 280U;
constexpr unsigned int kFinalizeDelayMs = 420U;

} // namespace

bool SharedUiShellStartup::begin()
{
    if (phase_ != Phase::Idle) {
        return true;
    }

    started_at_ms_ = lv_tick_get();
    if (!ui::startup_ui_shell::prepareBootUi(buildHooks(), false)) {
        return false;
    }

    phase_ = Phase::BootVisible;
    return true;
}

bool SharedUiShellStartup::runPhase(Phase next_phase, const char* log_line)
{
    if (log_line != nullptr && log_line[0] != '\0') {
        ui::boot::set_log_line(log_line);
    }
    phase_ = next_phase;
    return true;
}

bool SharedUiShellStartup::tick()
{
    if (phase_ == Phase::Idle) {
        return begin();
    }

    if (phase_ == Phase::Finalized) {
        return true;
    }

    const unsigned int elapsed = lv_tick_elaps(started_at_ms_);
    if (phase_ == Phase::BootVisible && elapsed >= kMenuBuildDelayMs) {
        ui::boot::set_log_line("Building menu shell...");
        if (!ui::startup_ui_shell::initializeMenuSkeleton(buildHooks())) {
            return false;
        }
        return runPhase(Phase::MenuBuilt, nullptr);
    }

    if (phase_ == Phase::MenuBuilt && elapsed >= kThemeReadyDelayMs) {
        return runPhase(Phase::ThemeReady, "Preparing navigation and apps...");
    }

    if (phase_ == Phase::ThemeReady && elapsed >= kFinalizeDelayMs) {
        ui::boot::set_log_line("Starting Trail Mate...");
        if (!ui::startup_ui_shell::finalizeStartup(buildHooks(), false)) {
            return false;
        }
        return runPhase(Phase::Finalized, nullptr);
    }

    return true;
}

bool SharedUiShellStartup::ready() const noexcept
{
    return phase_ == Phase::Finalized;
}

} // namespace trailmate::cardputer_zero::linux_ui
