#include "ui_lvgl_ux_packs/runtime/lvgl_runtime_adoption_probe.h"

namespace ui_lvgl_ux
{

bool LvglRuntimeAdoptionProbe::load(
    const product_composition::PresentationBundle& presentation)
{
    ready_ = adoption_.load(presentation);
    // Phase 9 fallback: hardcoded LVGL menu/page creation remains until the
    // compatibility runtime consumes screen graph descriptors as primary input.
    fallback_ = !ready_;
    return ready_;
}

bool LvglRuntimeAdoptionProbe::ready() const
{
    return ready_;
}

bool LvglRuntimeAdoptionProbe::fallbackUsed() const
{
    return fallback_;
}

std::size_t LvglRuntimeAdoptionProbe::menuCount() const
{
    return ready_ ? adoption_.menuCount() : 0;
}

std::size_t LvglRuntimeAdoptionProbe::screenCount() const
{
    return ready_ ? adoption_.screenCount() : 0;
}

const LvglRuntimeEntryAdoption& LvglRuntimeAdoptionProbe::adoption() const
{
    return adoption_;
}

} // namespace ui_lvgl_ux
