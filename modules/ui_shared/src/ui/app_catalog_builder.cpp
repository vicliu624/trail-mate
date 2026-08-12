#include "ui/app_catalog_builder.h"

#include <cstdio>

#if defined(ESP_PLATFORM)
#include "board/BoardBase.h"
#endif
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/assets/images.h"
#include "ui/callback_app_screen.h"
#include "ui/page/page_host.h"
#if defined(ARDUINO_T_DECK_PRO)
#include "ui/tdeck_pro/text_app_adapter.h"
#endif
#if defined(ESP_PLATFORM)
#include "ui/runtime/ui_feedback.h"
#endif

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_20) || !LV_FONT_MONTSERRAT_20
#define lv_font_montserrat_20 lv_font_montserrat_16
#endif

#include "ui/screens/chat/chat_page_shell.h"
#include "ui/screens/contacts/contacts_page_shell.h"
#include "ui/screens/energy_sweep/energy_sweep_page_shell.h"
#include "ui/screens/extensions/extensions_page_shell.h"
#include "ui/screens/gnss/gnss_skyplot_page_shell.h"
#include "ui/screens/gps/gps_page_shell.h"
#include "ui/screens/network/network_page_shell.h"
#include "ui/screens/settings/settings_page_shell.h"
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
#include "ui/screens/sstv/sstv_page_runtime.h"
#include "ui/screens/sstv/sstv_page_shell.h"
#endif
#if !defined(GAT562_NO_TEAM) || !GAT562_NO_TEAM
#include "ui/screens/team/team_page_shell.h"
#endif
#include "ui/screens/tracker/tracker_page_shell.h"
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
#include "ui/screens/usb/usb_page_shell.h"
#endif
#include "ui/screens/walkie_talkie/walkie_talkie_page_shell.h"

namespace
{

constexpr size_t kMaxMenuApps = 16;

#define APP_CATALOG_LOG(...) std::printf("[UI][Catalog] " __VA_ARGS__)

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
constexpr bool kTab5SkipSkyPlot = true;
#else
constexpr bool kTab5SkipSkyPlot = false;
#endif

#if !defined(ARDUINO_T_DECK_PRO)
extern "C"
{
    extern const lv_image_dsc_t Chat;
    extern const lv_image_dsc_t gps_icon;
    extern const lv_image_dsc_t Satellite;
    extern const lv_image_dsc_t contact;
    extern const lv_image_dsc_t radar;
#if !defined(GAT562_NO_TEAM) || !GAT562_NO_TEAM
    extern const lv_image_dsc_t team_icon;
#endif
    extern const lv_image_dsc_t tracker_icon;
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
    extern const lv_image_dsc_t sstv;
#endif
    extern const lv_image_dsc_t Setting;
    extern const lv_image_dsc_t nomad;
    extern const lv_image_dsc_t ext;
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
    extern const lv_image_dsc_t img_usb;
#endif
    extern const lv_image_dsc_t walkie_talkie;
    extern const lv_image_dsc_t shutdown;
}
#define CATALOG_ICON(symbol) (&symbol)
#else
// The T-Deck Pro's text shell does not render application images.  Keeping
// the catalog icon-free also prevents the visual adapter from keeping the
// legacy image catalogue alive through AppScreen pointers.
#define CATALOG_ICON(symbol) nullptr
#endif

void request_menu_exit(void*)
{
    ui_request_exit_to_menu();
}

ui::page::Host make_menu_host()
{
    ui::page::Host host{};
    host.request_exit = request_menu_exit;
    return host;
}

ui::page::Host s_menu_host = make_menu_host();

#if defined(ARDUINO_T_DECK_PRO)
// These descriptors use the same catalogue identities as other targets, but
// never hold legacy shell callbacks. The text adapter calls typed sources and
// action ports directly, so the retired visual page has no Pro link root.
ui::tdeck_pro::TextAppAdapter s_pro_map_app("map", "Map", ui::tdeck_pro::TextAppPageKind::Map);
ui::tdeck_pro::TextAppAdapter s_pro_chat_app("chat", "Chat", ui::tdeck_pro::TextAppPageKind::Chat);
ui::tdeck_pro::TextAppAdapter s_pro_team_app("team", "Team", ui::tdeck_pro::TextAppPageKind::Team);
ui::tdeck_pro::TextAppAdapter s_pro_contacts_app("contacts", "Contacts", ui::tdeck_pro::TextAppPageKind::Contacts);
ui::tdeck_pro::TextAppAdapter s_pro_sky_plot_app("sky_plot", "Sky Plot", ui::tdeck_pro::TextAppPageKind::SkyPlot);
ui::tdeck_pro::TextAppAdapter s_pro_network_app("network", "Network", ui::tdeck_pro::TextAppPageKind::Network);
ui::tdeck_pro::TextAppAdapter s_pro_settings_app("settings", "Setting", ui::tdeck_pro::TextAppPageKind::Settings);
ui::tdeck_pro::TextAppAdapter s_pro_tracker_app("tracker", "Tracker", ui::tdeck_pro::TextAppPageKind::Tracker);
ui::tdeck_pro::TextAppAdapter s_pro_walkie_app("walkie_talkie", "Walkie Talkie", ui::tdeck_pro::TextAppPageKind::Walkie);
ui::tdeck_pro::TextAppAdapter s_pro_extensions_app("extensions", "Extensions", ui::tdeck_pro::TextAppPageKind::Extensions);
ui::tdeck_pro::TextAppAdapter s_pro_protocol_probe_app(
    "energy_sweep", "Protocol Probe", ui::tdeck_pro::TextAppPageKind::ProtocolProbe);
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
ui::tdeck_pro::TextAppAdapter s_pro_sstv_app("sstv", "SSTV", ui::tdeck_pro::TextAppPageKind::Sstv);
#endif
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
ui::tdeck_pro::TextAppAdapter s_pro_usb_app("usb_mass_storage", "USB Disk", ui::tdeck_pro::TextAppPageKind::UsbStorage);
#endif
// Preserve the shared catalogue assembly order without keeping a callback to
// any legacy page. These aliases deliberately name the Pro descriptors.
AppScreen& s_chat_app = s_pro_chat_app;
AppScreen& s_gps_app = s_pro_map_app;
AppScreen& s_skyplot_app = s_pro_sky_plot_app;
AppScreen& s_contacts_app = s_pro_contacts_app;
AppScreen& s_team_app = s_pro_team_app;
AppScreen& s_tracker_app = s_pro_tracker_app;
AppScreen& s_setting_app = s_pro_settings_app;
AppScreen& s_network_app = s_pro_network_app;
AppScreen& s_extensions_app = s_pro_extensions_app;
AppScreen& s_walkie_app = s_pro_walkie_app;
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
AppScreen& s_sstv_app = s_pro_sstv_app;
#endif
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
AppScreen& s_usb_app = s_pro_usb_app;
#endif
#else
ui::CallbackAppScreen s_chat_app("chat", "Chat", CATALOG_ICON(Chat),
                                 chat::ui::shell::enter,
                                 chat::ui::shell::exit,
                                 &s_menu_host);
ui::CallbackAppScreen s_gps_app("map", "Map", CATALOG_ICON(gps_icon),
                                gps::ui::shell::enter,
                                gps::ui::shell::exit,
                                &s_menu_host);
ui::CallbackAppScreen s_skyplot_app("sky_plot", "Sky Plot", CATALOG_ICON(Satellite),
                                    gnss::ui::shell::enter,
                                    gnss::ui::shell::exit,
                                    &s_menu_host);
ui::CallbackAppScreen s_contacts_app("contacts", "Contacts", CATALOG_ICON(contact),
                                     contacts::ui::shell::enter,
                                     contacts::ui::shell::exit,
                                     &s_menu_host);
ui::CallbackAppScreen s_energy_sweep_app("energy_sweep", "Protocol Probe", CATALOG_ICON(radar),
                                         energy_sweep::ui::shell::enter,
                                         energy_sweep::ui::shell::exit,
                                         &s_menu_host);
#if !defined(GAT562_NO_TEAM) || !GAT562_NO_TEAM
ui::CallbackAppScreen s_team_app("team", "Team", CATALOG_ICON(team_icon),
                                 team::ui::shell::enter,
                                 team::ui::shell::exit,
                                 &s_menu_host);
#endif
ui::CallbackAppScreen s_tracker_app("tracker", "Tracker", CATALOG_ICON(tracker_icon),
                                    tracker::ui::shell::enter,
                                    tracker::ui::shell::exit,
                                    &s_menu_host);
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
ui::CallbackAppScreen s_sstv_app("sstv", "SSTV", CATALOG_ICON(sstv),
                                 sstv_page::ui::shell::enter,
                                 sstv_page::ui::shell::exit,
                                 &s_menu_host);
#endif
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
ui::CallbackAppScreen s_usb_app("usb_mass_storage", "USB Disk", CATALOG_ICON(img_usb),
                                usb_storage::ui::shell::enter,
                                usb_storage::ui::shell::exit,
                                &s_menu_host);
#endif
ui::CallbackAppScreen s_setting_app("settings", "Setting", CATALOG_ICON(Setting),
                                    settings::ui::shell::enter,
                                    settings::ui::shell::exit,
                                    &s_menu_host);
ui::CallbackAppScreen s_network_app("network", "Network", CATALOG_ICON(nomad),
                                    network::ui::shell::enter,
                                    network::ui::shell::exit,
                                    &s_menu_host);
ui::CallbackAppScreen s_extensions_app("extensions", "Extensions", CATALOG_ICON(ext),
                                       extensions::ui::shell::enter,
                                       extensions::ui::shell::exit,
                                       &s_menu_host);
ui::CallbackAppScreen s_walkie_app("walkie_talkie", "Walkie Talkie", CATALOG_ICON(walkie_talkie),
                                   walkie_page::ui::shell::enter,
                                   walkie_page::ui::shell::exit,
                                   &s_menu_host);
#endif
#if defined(ESP_PLATFORM) && !defined(ARDUINO_T_DECK_PRO)
constexpr uint32_t kPowerOffAmber = 0xEBA341;
constexpr uint32_t kPowerOffAmberDark = 0xC98118;
constexpr uint32_t kPowerOffWarmBg = 0xF6E6C6;
constexpr uint32_t kPowerOffPanelBg = 0xFAF0D8;
constexpr uint32_t kPowerOffLine = 0xE7C98F;
constexpr uint32_t kPowerOffText = 0x6B4A1E;
constexpr uint32_t kPowerOffTextDim = 0x8A6A3A;
constexpr uint32_t kPowerOffWarn = 0xB94A2C;

lv_obj_t* s_power_off_modal = nullptr;
lv_group_t* s_power_off_group = nullptr;
lv_group_t* s_power_off_prev_group = nullptr;

void power_off_restore_group()
{
    set_default_group(s_power_off_prev_group != nullptr ? s_power_off_prev_group : menu_g);
    s_power_off_prev_group = nullptr;
    ui_set_overlay_active(false);
}

void power_off_close_modal()
{
    if (s_power_off_modal != nullptr)
    {
        lv_obj_del(s_power_off_modal);
        s_power_off_modal = nullptr;
    }
    power_off_restore_group();
}

void power_off_confirm_cb(lv_event_t* e)
{
    (void)e;
    power_off_close_modal();
    ::ui::feedback::show_notice(::ui::i18n::tr("Powering off..."), 1000);
    ::board.softwareShutdown();
}

void power_off_cancel_cb(lv_event_t* e)
{
    (void)e;
    power_off_close_modal();
}

void set_power_off_label_text(lv_obj_t* label,
                              const char* text,
                              uint32_t color,
                              const lv_font_t* font)
{
    const char* localized = ::ui::i18n::tr(text);
    lv_label_set_text(label, localized);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    ::ui::fonts::apply_localized_font(label, localized, font);
}

lv_obj_t* create_power_off_button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, bool primary)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 132, 32);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(primary ? kPowerOffAmber : kPowerOffWarmBg), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(primary ? kPowerOffAmberDark : kPowerOffLine), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(primary ? kPowerOffAmberDark : kPowerOffLine), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* label = lv_label_create(btn);
    set_power_off_label_text(label,
                             text,
                             primary ? kPowerOffWarmBg : kPowerOffText,
                             &lv_font_montserrat_16);
    lv_obj_center(label);

    if (s_power_off_group != nullptr)
    {
        lv_group_add_obj(s_power_off_group, btn);
    }
    return btn;
}

void power_off_enter(void*, lv_obj_t* parent)
{
    if (parent == nullptr)
    {
        return;
    }
    if (s_power_off_modal != nullptr)
    {
        return;
    }

    s_power_off_prev_group = lv_group_get_default();
    if (s_power_off_group == nullptr)
    {
        s_power_off_group = lv_group_create();
    }
    lv_group_remove_all_objs(s_power_off_group);
    set_default_group(s_power_off_group);
    ui_set_overlay_active(true);

    s_power_off_modal = lv_obj_create(parent);
    lv_obj_set_size(s_power_off_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_power_off_modal, 0, 0);
    lv_obj_set_style_bg_color(s_power_off_modal, lv_color_hex(kPowerOffText), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_power_off_modal, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_power_off_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_power_off_modal, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_power_off_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_power_off_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(s_power_off_modal);

    lv_obj_t* dialog = lv_obj_create(s_power_off_modal);
    lv_obj_set_size(dialog, 320, 136);
    lv_obj_center(dialog);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(kPowerOffPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dialog, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(dialog, lv_color_hex(kPowerOffAmber), LV_PART_MAIN);
    lv_obj_set_style_radius(dialog, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dialog, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dialog, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* title = lv_label_create(dialog);
    set_power_off_label_text(title, "Shutdown", kPowerOffText, &lv_font_montserrat_20);
    lv_obj_set_size(title, 292, 26);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 10);

    lv_obj_t* body = lv_label_create(dialog);
    set_power_off_label_text(body, "Power off device now?", kPowerOffTextDim, &lv_font_montserrat_16);
    lv_obj_set_size(body, 292, 22);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 14, 43);

    lv_obj_t* hint = lv_label_create(dialog);
    set_power_off_label_text(hint,
                             "USB power keeps the device awake.",
                             kPowerOffWarn,
                             &lv_font_montserrat_14);
    lv_obj_set_size(hint, 292, 18);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 14, 66);

    lv_obj_t* row = lv_obj_create(dialog);
    lv_obj_set_size(row, 292, 34);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* cancel_btn = create_power_off_button(row, "Cancel", power_off_cancel_cb, false);
    (void)create_power_off_button(row, "Power Off", power_off_confirm_cb, true);
    lv_group_focus_obj(cancel_btn);
}

void power_off_exit(void*, lv_obj_t* parent)
{
    (void)parent;
}

ui::CallbackAppScreen s_power_off_app("shutdown", "Shutdown", CATALOG_ICON(shutdown),
                                      power_off_enter,
                                      power_off_exit,
                                      nullptr,
                                      ui::AppLaunchMode::MenuOverlay);
#endif

AppScreen* s_apps[kMaxMenuApps] = {};
ui::StaticAppCatalogState s_catalog_state = ui::makeStaticAppCatalogState(s_apps);
ui::AppCatalog s_catalog = ui::makeStaticAppCatalog(&s_catalog_state);

#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
bool sstv_available()
{
    return sstv_page::ui::runtime::is_available();
}
#else
bool sstv_available()
{
    return false;
}
#endif

} // namespace

namespace ui::app_catalog_builder
{

AppCatalog build(const FeatureFlags& flags)
{
    size_t count = 0;
    auto add = [&](AppScreen* app)
    {
        if (app != nullptr && count < kMaxMenuApps)
        {
            s_apps[count++] = app;
            APP_CATALOG_LOG("add index=%u app=%s\n",
                            static_cast<unsigned>(count - 1),
                            app->name());
        }
    };

    const auto add_common_tail = [&]()
    {
        if (flags.include_contacts)
        {
            add(&s_contacts_app);
        }
        if (flags.include_team)
        {
#if !defined(GAT562_NO_TEAM) || !GAT562_NO_TEAM
            add(&s_team_app);
#endif
        }
        if (flags.profile == CatalogProfile::IdfDefault && flags.include_tracker)
        {
            add(&s_tracker_app);
        }
        if (flags.profile == CatalogProfile::PioDefault && flags.include_sstv && sstv_available())
        {
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
            add(&s_sstv_app);
#endif
        }
        if (flags.include_energy_sweep)
        {
#if defined(ARDUINO_T_DECK_PRO)
            add(&s_pro_protocol_probe_app);
#else
            add(&s_energy_sweep_app);
#endif
        }
        if (flags.include_walkie_talkie)
        {
            add(&s_walkie_app);
        }
        if (flags.include_usb)
        {
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
            add(&s_usb_app);
#endif
        }
        if (flags.profile == CatalogProfile::IdfDefault && flags.include_sstv && sstv_available())
        {
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
            add(&s_sstv_app);
#endif
        }
        if (flags.include_extensions)
        {
            add(&s_extensions_app);
        }
        if (flags.include_network)
        {
            add(&s_network_app);
        }
        if (flags.include_power_off)
        {
#if defined(ESP_PLATFORM) && !defined(ARDUINO_T_DECK_PRO)
            add(&s_power_off_app);
#endif
        }
        if (flags.include_settings)
        {
            add(&s_setting_app);
        }
    };

    if (flags.profile == CatalogProfile::IdfDefault)
    {
        if (flags.include_chat)
        {
            add(&s_chat_app);
        }
        if (flags.include_gps_map)
        {
            add(&s_gps_app);
        }
        if (flags.include_gnss_skyplot)
        {
            if (!kTab5SkipSkyPlot)
            {
                add(&s_skyplot_app);
            }
        }
        add_common_tail();
    }
    else
    {
        if (flags.include_gps_map)
        {
            add(&s_gps_app);
        }
        if (flags.include_gnss_skyplot)
        {
            if (!kTab5SkipSkyPlot)
            {
                add(&s_skyplot_app);
            }
        }
        if (flags.include_tracker)
        {
            add(&s_tracker_app);
        }
        if (flags.include_chat)
        {
            add(&s_chat_app);
        }
        add_common_tail();
    }

    for (size_t index = count; index < kMaxMenuApps; ++index)
    {
        s_apps[index] = nullptr;
    }
    s_catalog_state.count = count;
    APP_CATALOG_LOG("build complete count=%u\n", static_cast<unsigned>(count));
    return s_catalog;
}

} // namespace ui::app_catalog_builder
