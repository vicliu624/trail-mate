#include "ui/app_catalog_builder.h"

#include "ui/app_runtime.h"
#include "ui/assets/images.h"
#include "ui/callback_app_screen.h"
#include "ui/page/page_host.h"
#include "ui/screens/chat/chat_page_shell.h"
#include "ui/screens/contacts/contacts_page_shell.h"
#include "ui/screens/energy_sweep/energy_sweep_page_shell.h"
#include "ui/screens/gnss/gnss_skyplot_page_shell.h"
#include "ui/screens/gps/gps_page_shell.h"
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
#include "ui/screens/pc_link/pc_link_page_shell.h"
#endif
#include "ui/screens/settings/settings_page_shell.h"
#include "ui/screens/sstv/sstv_page_shell.h"
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
    extern const lv_image_dsc_t sstv;
    extern const lv_image_dsc_t Setting;
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

ui::CallbackAppScreen s_chat_app("Chat", &Chat,
                                 chat::ui::shell::enter,
                                 chat::ui::shell::exit,
                                 &s_menu_host);
ui::CallbackAppScreen s_gps_app("Map", &gps_icon,
                                gps::ui::shell::enter,
                                gps::ui::shell::exit,
                                &s_menu_host);
ui::CallbackAppScreen s_skyplot_app("Sky Plot", &Satellite,
                                    gnss::ui::shell::enter,
                                    gnss::ui::shell::exit,
                                    &s_menu_host);
ui::CallbackAppScreen s_contacts_app("Contacts", &contact,
                                     contacts::ui::shell::enter,
                                     contacts::ui::shell::exit,
                                     &s_menu_host);
ui::CallbackAppScreen s_energy_sweep_app("Energy Sweep", &Spectrum,
                                         energy_sweep::ui::shell::enter,
                                         energy_sweep::ui::shell::exit,
                                         &s_menu_host);
#if !defined(GAT562_NO_TEAM) || !GAT562_NO_TEAM
ui::CallbackAppScreen s_team_app("Team", &team_icon,
                                 team::ui::shell::enter,
                                 team::ui::shell::exit,
                                 &s_menu_host);
#endif
ui::CallbackAppScreen s_tracker_app("Tracker", &tracker_icon,
                                    tracker::ui::shell::enter,
                                    tracker::ui::shell::exit,
                                    &s_menu_host);
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
ui::CallbackAppScreen s_pc_link_app("Data Exchange", &rf,
                                    pc_link::ui::shell::enter,
                                    pc_link::ui::shell::exit,
                                    &s_menu_host);
#endif
ui::CallbackAppScreen s_sstv_app("SSTV", &sstv,
                                 sstv_page::ui::shell::enter,
                                 sstv_page::ui::shell::exit,
                                 &s_menu_host);
#if !defined(GAT562_NO_HOSTLINK) || !GAT562_NO_HOSTLINK
ui::CallbackAppScreen s_usb_app("USB Mass Storage", &img_usb,
                                usb_storage::ui::shell::enter,
                                usb_storage::ui::shell::exit,
                                &s_menu_host);
#endif
ui::CallbackAppScreen s_setting_app("Setting", &Setting,
                                    settings::ui::shell::enter,
                                    settings::ui::shell::exit,
                                    &s_menu_host);
ui::CallbackAppScreen s_walkie_app("Walkie Talkie", &walkie_talkie,
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
            add(&s_sstv_app);
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
            add(&s_sstv_app);
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
            add(&s_skyplot_app);
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
            add(&s_skyplot_app);
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
    return s_catalog;
}

} // namespace ui::app_catalog_builder
