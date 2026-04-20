#include "ui/app_catalog_builder.h"

#include <cstdio>

#include "ui/app_runtime.h"
#include "ui/assets/images.h"
#include "ui/callback_app_screen.h"
#include "ui/page/page_host.h"

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

#include "ui/screens/chat/chat_page_shell.h"
#include "ui/screens/contacts/contacts_page_shell.h"
#include "ui/screens/energy_sweep/energy_sweep_page_shell.h"
#include "ui/screens/extensions/extensions_page_shell.h"
#include "ui/screens/gnss/gnss_skyplot_page_shell.h"
#include "ui/screens/gps/gps_page_shell.h"
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
#include "ui/screens/pc_link/pc_link_page_shell.h"
#endif
#include "ui/screens/settings/settings_page_shell.h"
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
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

extern "C"
{
    extern const lv_image_dsc_t Chat;
    extern const lv_image_dsc_t gps_icon;
    extern const lv_image_dsc_t Satellite;
    extern const lv_image_dsc_t contact;
    extern const lv_image_dsc_t Spectrum;
#if !defined(GAT562_NO_TEAM) || !GAT562_NO_TEAM
    extern const lv_image_dsc_t team_icon;
#endif
    extern const lv_image_dsc_t tracker_icon;
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
    extern const lv_image_dsc_t rf;
#endif
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
    extern const lv_image_dsc_t sstv;
#endif
    extern const lv_image_dsc_t Setting;
    extern const lv_image_dsc_t ext;
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
    extern const lv_image_dsc_t img_usb;
#endif
    extern const lv_image_dsc_t walkie_talkie;
}

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

ui::CallbackAppScreen s_chat_app("chat", "Chat", &Chat,
                                 chat::ui::shell::enter,
                                 chat::ui::shell::exit,
                                 &s_menu_host);
ui::CallbackAppScreen s_gps_app("map", "Map", &gps_icon,
                                gps::ui::shell::enter,
                                gps::ui::shell::exit,
                                &s_menu_host);
ui::CallbackAppScreen s_skyplot_app("sky_plot", "Sky Plot", &Satellite,
                                    gnss::ui::shell::enter,
                                    gnss::ui::shell::exit,
                                    &s_menu_host);
ui::CallbackAppScreen s_contacts_app("contacts", "Contacts", &contact,
                                     contacts::ui::shell::enter,
                                     contacts::ui::shell::exit,
                                     &s_menu_host);
ui::CallbackAppScreen s_energy_sweep_app("energy_sweep", "Energy Sweep", &Spectrum,
                                         energy_sweep::ui::shell::enter,
                                         energy_sweep::ui::shell::exit,
                                         &s_menu_host);
#if !defined(GAT562_NO_TEAM) || !GAT562_NO_TEAM
ui::CallbackAppScreen s_team_app("team", "Team", &team_icon,
                                 team::ui::shell::enter,
                                 team::ui::shell::exit,
                                 &s_menu_host);
#endif
ui::CallbackAppScreen s_tracker_app("tracker", "Tracker", &tracker_icon,
                                    tracker::ui::shell::enter,
                                    tracker::ui::shell::exit,
                                    &s_menu_host);
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
ui::CallbackAppScreen s_pc_link_app("pc_link", "PC Link", &rf,
                                    pc_link::ui::shell::enter,
                                    pc_link::ui::shell::exit,
                                    &s_menu_host);
#endif
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
ui::CallbackAppScreen s_sstv_app("sstv", "SSTV", &sstv,
                                 sstv_page::ui::shell::enter,
                                 sstv_page::ui::shell::exit,
                                 &s_menu_host);
#endif
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
ui::CallbackAppScreen s_usb_app("usb_mass_storage", "USB Disk", &img_usb,
                                usb_storage::ui::shell::enter,
                                usb_storage::ui::shell::exit,
                                &s_menu_host);
#endif
ui::CallbackAppScreen s_setting_app("settings", "Setting", &Setting,
                                    settings::ui::shell::enter,
                                    settings::ui::shell::exit,
                                    &s_menu_host);
ui::CallbackAppScreen s_extensions_app("extensions", "Extensions", &ext,
                                       extensions::ui::shell::enter,
                                       extensions::ui::shell::exit,
                                       &s_menu_host);
ui::CallbackAppScreen s_walkie_app("walkie_talkie", "Walkie Talkie", &walkie_talkie,
                                   walkie_page::ui::shell::enter,
                                   walkie_page::ui::shell::exit,
                                   &s_menu_host);

AppScreen* s_apps[kMaxMenuApps] = {};
ui::StaticAppCatalogState s_catalog_state = ui::makeStaticAppCatalogState(s_apps);
ui::AppCatalog s_catalog = ui::makeStaticAppCatalog(&s_catalog_state);

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
        if (flags.include_pc_link)
        {
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
            add(&s_pc_link_app);
#endif
        }
        if (flags.profile == CatalogProfile::PioDefault && flags.include_sstv)
        {
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
            add(&s_sstv_app);
#endif
        }
        if (flags.include_energy_sweep)
        {
            add(&s_energy_sweep_app);
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
        if (flags.profile == CatalogProfile::IdfDefault && flags.include_sstv)
        {
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
            add(&s_sstv_app);
#endif
        }
        if (flags.include_settings)
        {
            add(&s_setting_app);
        }
        if (flags.include_extensions)
        {
            add(&s_extensions_app);
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
