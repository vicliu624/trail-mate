#pragma once

#include "product_composition/presentation_bundle.h"
#include "ui_lvgl_ux_packs/runtime/lvgl_runtime_entry_adoption.h"

#include <cstddef>

namespace ui_lvgl_ux
{

class LvglRuntimeAdoptionProbe
{
  public:
    bool load(const product_composition::PresentationBundle& presentation);

    bool ready() const;
    bool fallbackUsed() const;
    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const LvglRuntimeEntryAdoption& adoption() const;

  private:
    LvglRuntimeEntryAdoption adoption_{};
    bool ready_ = false;
    bool fallback_ = true;
};

} // namespace ui_lvgl_ux
