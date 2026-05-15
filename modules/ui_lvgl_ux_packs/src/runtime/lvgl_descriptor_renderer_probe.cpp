#include "ui_lvgl_ux_packs/runtime/lvgl_descriptor_renderer_probe.h"

namespace ui_lvgl_ux
{

bool LvglDescriptorRendererProbe::render(
    const LvglPrimaryScreenGraphRuntime& runtime)
{
    return load(runtime);
}

bool LvglDescriptorRendererProbe::load(
    const LvglPrimaryScreenGraphRuntime& runtime)
{
    ready_ = false;
    primary_ = false;
    fallback_ = true;

    if (runtime.usingPrimaryScreenGraph())
    {
        ready_ = model_.load(runtime);
        primary_ = ready_;
        fallback_ = !ready_;
        return ready_;
    }

    if (runtime.fallbackUsed())
    {
        return loadFallback(runtime);
    }

    return false;
}

bool LvglDescriptorRendererProbe::loadFallback(
    const LvglPrimaryScreenGraphRuntime&)
{
    // Phase 11 fallback: hardcoded LVGL menu/page rendering remains available
    // only when descriptor model consumption is unavailable.
    ready_ = false;
    primary_ = false;
    fallback_ = true;
    return false;
}

bool LvglDescriptorRendererProbe::ready() const noexcept
{
    return ready_;
}

bool LvglDescriptorRendererProbe::usingPrimaryScreenGraph() const noexcept
{
    return primary_;
}

bool LvglDescriptorRendererProbe::usedPrimaryScreenGraph() const noexcept
{
    return primary_;
}

bool LvglDescriptorRendererProbe::fallbackUsed() const noexcept
{
    return fallback_;
}

bool LvglDescriptorRendererProbe::usedFallback() const noexcept
{
    return fallback_;
}

std::size_t LvglDescriptorRendererProbe::itemCount() const noexcept
{
    return ready_ ? model_.itemCount() : 0;
}

const LvglDescriptorMenuItem* LvglDescriptorRendererProbe::items()
    const noexcept
{
    return ready_ ? model_.items() : nullptr;
}

} // namespace ui_lvgl_ux
