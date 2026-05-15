#pragma once

#include "product_composition/presentation_bundle.h"
#include "ui_lvgl_ux_packs/runtime/lvgl_runtime_entry_adoption.h"

#include <cstddef>

namespace ui_lvgl_ux
{

enum class LvglScreenGraphRuntimeSource
{
    ScreenGraphAdoption,
    HardcodedFallback,
};

class LvglPrimaryScreenGraphRuntime
{
  public:
    bool load(const product_composition::PresentationBundle& presentation);

    bool ready() const noexcept;
    bool usingPrimaryScreenGraph() const noexcept;
    bool fallbackUsed() const noexcept;
    LvglScreenGraphRuntimeSource runtimeSource() const noexcept;

    std::size_t menuCount() const noexcept;
    std::size_t screenCount() const noexcept;

    const LvglRuntimeEntryAdoption& adoption() const noexcept;

  private:
    bool loadFallback(const product_composition::PresentationBundle& presentation);

  private:
    LvglRuntimeEntryAdoption adoption_{};
    LvglScreenGraphRuntimeSource source_ =
        LvglScreenGraphRuntimeSource::HardcodedFallback;
    bool ready_ = false;
    bool fallback_ = true;
};

} // namespace ui_lvgl_ux
