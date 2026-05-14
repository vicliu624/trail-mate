#pragma once

#include "ui_lvgl_ux_packs/ux/ux_pack.h"

namespace ui_lvgl_ux
{

class SimulatorFullUxPack final : public IUxPack
{
  public:
    const char* id() const override;
    const DeviceUxProfile& profile() const override;
    const UxFeatureSet& features() const override;
    void buildScreens(ScreenRegistry& out) const override;
    void buildInputBindings(InputBindingSet& out) const override;
};

} // namespace ui_lvgl_ux
