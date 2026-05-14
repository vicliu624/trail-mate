#include "ui_lvgl_ux_packs/ux/screen_registry.h"

namespace ui_lvgl_ux
{

void ScreenRegistry::clear()
{
    for (std::size_t i = 0; i < kMaxScreens; ++i)
    {
        items_[i] = ScreenDescriptor{};
    }
    size_ = 0;
}

bool ScreenRegistry::add(const ScreenDescriptor& descriptor)
{
    if (size_ >= kMaxScreens)
    {
        return false;
    }

    items_[size_] = descriptor;
    ++size_;
    return true;
}

std::size_t ScreenRegistry::size() const
{
    return size_;
}

const ScreenDescriptor* ScreenRegistry::items() const
{
    return items_;
}

} // namespace ui_lvgl_ux
