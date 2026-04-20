#include "ui/app_registry.h"
#include "ui/app_catalog_builder.h"

#include "platform/ui/device_runtime.h"
#include "platform/ui/hostlink_runtime.h"
#include "platform/ui/lora_runtime.h"
#include "platform/ui/route_storage.h"
#include "platform/ui/sstv_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/usb_support_runtime.h"
#include "platform/ui/walkie_runtime.h"

namespace
{

ui::app_catalog_builder::FeatureFlags buildFeatureFlags()
{
    ui::app_catalog_builder::FeatureFlags flags{};
    flags.profile = ui::app_catalog_builder::CatalogProfile::IdfDefault;
    flags.include_gps_map = platform::ui::device::gps_supported();
    flags.include_gnss_skyplot = platform::ui::device::gps_supported();
    flags.include_energy_sweep = platform::ui::lora::is_supported();
    flags.include_tracker = platform::ui::route_storage::is_supported() || platform::ui::tracker::is_supported();
    flags.include_pc_link = platform::ui::hostlink::is_supported();
    flags.include_sstv = platform::ui::sstv::is_supported();
    flags.include_usb = platform::ui::usb_support::is_supported();
    flags.include_extensions = true;
    flags.include_walkie_talkie = platform::ui::walkie::is_supported();
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
