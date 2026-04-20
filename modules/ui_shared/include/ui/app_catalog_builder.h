#pragma once

#include "ui/app_catalog.h"

namespace ui::app_catalog_builder
{

enum class CatalogProfile : uint8_t
{
    PioDefault = 0,
    IdfDefault,
};

struct FeatureFlags
{
    CatalogProfile profile = CatalogProfile::PioDefault;
    bool include_chat = true;
    bool include_gps_map = true;
    bool include_gnss_skyplot = true;
    bool include_contacts = true;
    bool include_energy_sweep = true;
    bool include_team = true;
    bool include_tracker = true;
    bool include_pc_link = true;
    bool include_sstv = true;
    bool include_usb = false;
    bool include_settings = true;
    bool include_extensions = true;
    bool include_walkie_talkie = false;
};

AppCatalog build(const FeatureFlags& flags);

} // namespace ui::app_catalog_builder
