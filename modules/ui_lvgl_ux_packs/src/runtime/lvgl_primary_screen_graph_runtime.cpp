#include "ui_lvgl_ux_packs/runtime/lvgl_primary_screen_graph_runtime.h"

namespace ui_lvgl_ux
{

bool LvglPrimaryScreenGraphRuntime::load(
    const product_composition::PresentationBundle& presentation)
{
    ready_ = adoption_.load(presentation);
    if (ready_)
    {
        source_ = LvglScreenGraphRuntimeSource::ScreenGraphAdoption;
        fallback_ = false;
        return true;
    }

    source_ = LvglScreenGraphRuntimeSource::HardcodedFallback;
    fallback_ = true;
    return loadFallback(presentation);
}

bool LvglPrimaryScreenGraphRuntime::loadFallback(
    const product_composition::PresentationBundle&)
{
    // Phase 10 fallback containment: real LVGL menu/page creation remains
    // available until descriptor consumption becomes the renderer input.
    return false;
}

bool LvglPrimaryScreenGraphRuntime::ready() const noexcept
{
    return ready_;
}

bool LvglPrimaryScreenGraphRuntime::usingPrimaryScreenGraph() const noexcept
{
    return source_ == LvglScreenGraphRuntimeSource::ScreenGraphAdoption &&
           ready_ && !fallback_;
}

bool LvglPrimaryScreenGraphRuntime::fallbackUsed() const noexcept
{
    return fallback_;
}

LvglScreenGraphRuntimeSource
LvglPrimaryScreenGraphRuntime::runtimeSource() const noexcept
{
    return source_;
}

std::size_t LvglPrimaryScreenGraphRuntime::menuCount() const noexcept
{
    return ready_ ? adoption_.menuCount() : 0;
}

std::size_t LvglPrimaryScreenGraphRuntime::screenCount() const noexcept
{
    return ready_ ? adoption_.screenCount() : 0;
}

const LvglRuntimeEntryAdoption& LvglPrimaryScreenGraphRuntime::adoption()
    const noexcept
{
    return adoption_;
}

} // namespace ui_lvgl_ux
