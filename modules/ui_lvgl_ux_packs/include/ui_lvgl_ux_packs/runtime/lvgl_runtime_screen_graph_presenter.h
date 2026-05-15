#pragma once

#include "product_composition/presentation_bundle.h"
#include "ui_lvgl_ux_packs/runtime/lvgl_screen_graph_bridge.h"

#include <cstddef>

namespace ui_lvgl_ux
{

class LvglRuntimeScreenGraphPresenter
{
  public:
    bool load(const product_composition::PresentationBundle& presentation);

    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const LvglMenuEntry* menuEntries() const;
    const LvglScreenHostEntry* screenEntries() const;

  private:
    LvglScreenGraphBridge bridge_{};
};

} // namespace ui_lvgl_ux
