#include "board/BoardBase.h"
#include "ui/app_catalog_builder.h"
#include "ui/callback_app_screen.h"
#include "ui/screens/gps/gps_page_shell.h"

#include <cassert>
#include <cstring>

CatalogTestBoard board;
lv_group_t* menu_g = nullptr;
void ui_request_exit_to_menu() {}
void ui_set_overlay_active(bool) {}
namespace ui::feedback
{
bool show_notice(const char*, uint32_t) { return true; }
} // namespace ui::feedback
// Unrelated apps are descriptor-only stubs. The catalog under test is the
// production implementation, compiled with all optional Pager apps enabled.
#define STUB_PAGE(ns)               \
    namespace ns                    \
    {                               \
    void enter(void*, lv_obj_t*) {} \
    void exit(void*, lv_obj_t*) {}  \
    }
STUB_PAGE(chat::ui::shell)
STUB_PAGE(calculator::ui::shell)
STUB_PAGE(geocaching::ui::shell)
STUB_PAGE(gps::ui::shell)
namespace gps::ui::shell
{
void enter_route(const RouteSpec*, lv_obj_t*) {}
void exit_route(const RouteSpec*, lv_obj_t*) {}
} // namespace gps::ui::shell
STUB_PAGE(gnss::ui::shell)
STUB_PAGE(contacts::ui::shell)
STUB_PAGE(energy_sweep::ui::shell)
STUB_PAGE(team::ui::shell)
STUB_PAGE(tracker::ui::shell)
STUB_PAGE(sstv_page::ui::shell)
STUB_PAGE(usb_storage::ui::shell)
STUB_PAGE(settings::ui::shell)
STUB_PAGE(network::ui::shell)
STUB_PAGE(extensions::ui::shell)
STUB_PAGE(walkie_page::ui::shell)
#undef STUB_PAGE
namespace sstv_page::ui::runtime
{
bool is_available() { return true; }
} // namespace sstv_page::ui::runtime
extern "C"
{
#define STUB_ICON(name) extern const lv_image_dsc_t name = {}
    STUB_ICON(Chat);
    STUB_ICON(calc);
    STUB_ICON(gps_icon);
    STUB_ICON(Satellite);
    STUB_ICON(contact);
    STUB_ICON(radar);
    STUB_ICON(team_icon);
    STUB_ICON(tracker_icon);
    STUB_ICON(sstv);
    STUB_ICON(Setting);
    STUB_ICON(nomad);
    STUB_ICON(ext);
    STUB_ICON(img_usb);
    STUB_ICON(walkie_talkie);
    STUB_ICON(shutdown);
#undef STUB_ICON
}

namespace catalog_icons
{
extern "C" const lv_image_dsc_t geocaching = {};
}

int main()
{
    ui::CallbackAppScreen calendar("calendar", "Calendar", nullptr,
                                   static_cast<ui::CallbackAppScreen::SimpleCallback>(nullptr), nullptr);
    ui::app_catalog_builder::FeatureFlags flags;
    flags.include_usb = flags.include_network = flags.include_walkie_talkie = flags.include_power_off = true;
    auto catalog = ui::app_catalog_builder::build(flags);
    assert(ui::catalogCount(catalog) == 16);
    flags.calendar_app = &calendar;
    catalog = ui::app_catalog_builder::build(flags);
    assert(ui::catalogCount(catalog) == 17);
    assert(ui::catalogAt(catalog, 0) == &calendar);
    bool settings = false, shutdown = false, geocaching = false;
    for (std::size_t i = 0; i < ui::catalogCount(catalog); ++i)
    {
        auto* app = ui::catalogAt(catalog, i);
        assert(app);
        settings |= std::strcmp(app->stable_id(), "settings") == 0;
        shutdown |= std::strcmp(app->stable_id(), "shutdown") == 0;
        geocaching |= std::strcmp(app->stable_id(), "geocaching") == 0;
        for (std::size_t j = i + 1; j < ui::catalogCount(catalog); ++j)
            assert(std::strcmp(app->stable_id(), ui::catalogAt(catalog, j)->stable_id()) != 0);
    }
    assert(settings && shutdown && geocaching && !ui::catalogAt(catalog, 17));
    // The IDF catalog and callers that do not inject Agenda retain their shape.
    flags.calendar_app = nullptr;
    flags.profile = ui::app_catalog_builder::CatalogProfile::IdfDefault;
    catalog = ui::app_catalog_builder::build(flags);
    assert(ui::catalogCount(catalog) == 16);
    for (std::size_t i = 0; i < ui::catalogCount(catalog); ++i)
        assert(std::strcmp(ui::catalogAt(catalog, i)->stable_id(), "calendar") != 0);
}
