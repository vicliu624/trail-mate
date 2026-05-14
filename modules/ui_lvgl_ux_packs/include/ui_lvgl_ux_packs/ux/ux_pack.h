#pragma once

#include "ui_lvgl_ux_packs/ux/device_ux_profile.h"
#include "ui_lvgl_ux_packs/ux/input_binding_set.h"
#include "ui_lvgl_ux_packs/ux/screen_registry.h"
#include "ui_lvgl_ux_packs/ux/ux_feature_set.h"

namespace ui_lvgl_ux
{

class IUxPack
{
  public:
    virtual ~IUxPack() = default;

    virtual const char* id() const = 0;
    virtual const DeviceUxProfile& profile() const = 0;
    virtual const UxFeatureSet& features() const = 0;

    virtual void buildScreens(ScreenRegistry& out) const = 0;
    virtual void buildInputBindings(InputBindingSet& out) const = 0;
};

} // namespace ui_lvgl_ux
