#pragma once

#include "ui/app_catalog.h"

namespace ui::map
{
struct MapMarkerBinding;
}

namespace ui::app_catalog_builder
{

enum class CatalogProfile : uint8_t
{
    PioDefault = 0,
    IdfDefault,
};

struct FeatureFlags
{
    // Optional descriptor supplied by the owning target composition. The
    // catalog does not construct or locate Agenda services.
    AppScreen* calendar_app = nullptr;
    const ui::map::MapMarkerBinding* map_markers = nullptr;
    CatalogProfile profile = CatalogProfile::PioDefault;
    bool include_chat = true;
    bool include_gps_map = true;
    bool include_gnss_skyplot = true;
    bool include_contacts = true;
    bool include_energy_sweep = true;
    bool include_team = true;
    bool include_tracker = true;
    bool include_sstv = true;
    bool include_usb = false;
    bool include_settings = true;
    bool include_extensions = true;
    bool include_network = false;
    bool include_walkie_talkie = false;
    bool include_power_off = false;
};

AppCatalog build(const FeatureFlags& flags);

} // namespace ui::app_catalog_builder
