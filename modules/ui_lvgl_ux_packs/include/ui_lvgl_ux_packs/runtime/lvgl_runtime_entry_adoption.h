#pragma once

#include "product_composition/presentation_bundle.h"
#include "ui_lvgl_ux_packs/runtime/lvgl_runtime_screen_graph_presenter.h"

#include <cstddef>

namespace ui_lvgl_ux
{

class LvglRuntimeEntryAdoption
{
  public:
    bool load(const product_composition::PresentationBundle& presentation);

    bool ready() const;
    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const LvglRuntimeScreenGraphPresenter& presenter() const;

  private:
    LvglRuntimeScreenGraphPresenter presenter_{};
    bool ready_ = false;
};

} // namespace ui_lvgl_ux
