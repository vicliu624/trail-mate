#include "esp32_lvgl_arduino_agenda.h"
#include "ui/app_registry.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/lora_runtime.h"
#include "platform/ui/route_storage.h"
#include "platform/ui/sstv_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/usb_support_runtime.h"
#include "platform/ui/walkie_runtime.h"
#include "ui/app_catalog_builder.h"

#include <cstdio>

namespace
{

#define APP_REG_LOG(...) std::printf("[UI][Registry] " __VA_ARGS__)

ui::app_catalog_builder::FeatureFlags buildFeatureFlags()
{
    ui::app_catalog_builder::FeatureFlags flags{};
    flags.calendar_app = trailmate::apps::esp32_lvgl::arduino_agenda::application();
    flags.map_markers = trailmate::apps::esp32_lvgl::arduino_agenda::mapMarkers();
    flags.profile = ui::app_catalog_builder::CatalogProfile::PioDefault;
    flags.include_gps_map = platform::ui::device::gps_supported();
    flags.include_gnss_skyplot = platform::ui::device::gps_supported();
    flags.include_tracker = platform::ui::route_storage::is_supported() || platform::ui::tracker::is_supported();
    flags.include_energy_sweep = platform::ui::lora::is_supported();
    flags.include_sstv = platform::ui::sstv::is_supported();
    flags.include_usb = platform::ui::usb_support::is_supported() && platform::ui::device::sd_ready();
    flags.include_extensions = true;
    flags.include_network = app::hasAppFacade() &&
                            chat::infra::isReticulumMeshProtocol(
                                app::configFacade().readConfig().mesh_protocol);
    flags.include_walkie_talkie = platform::ui::walkie::is_supported();
    flags.include_power_off =
#if defined(ARDUINO_T_LORA_PAGER)
        true;
#else
        false;
#endif
    APP_REG_LOG(
        "flags gps_map=%d skyplot=%d tracker=%d chat=%d sweep=%d sstv=%d usb=%d network=%d walkie=%d power_off=%d gps_supported=%d gps_ready=%d sd_ready=%d\n",
        flags.include_gps_map ? 1 : 0,
        flags.include_gnss_skyplot ? 1 : 0,
        flags.include_tracker ? 1 : 0,
        flags.include_chat ? 1 : 0,
        flags.include_energy_sweep ? 1 : 0,
        flags.include_sstv ? 1 : 0,
        flags.include_usb ? 1 : 0,
        flags.include_network ? 1 : 0,
        flags.include_walkie_talkie ? 1 : 0,
        flags.include_power_off ? 1 : 0,
        platform::ui::device::gps_supported() ? 1 : 0,
        platform::ui::device::gps_ready() ? 1 : 0,
        platform::ui::device::sd_ready() ? 1 : 0);
    return flags;
}

ui::AppCatalog buildCatalog()
{
    return ui::app_catalog_builder::build(buildFeatureFlags());
}

} // namespace

namespace ui
{

AppCatalog appCatalog()
{
    return buildCatalog();
}

} // namespace ui
