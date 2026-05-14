#include "ui_lvgl_ux_packs/ux/ux_menu_model.h"

namespace ui_lvgl_ux
{

void UxMenuModel::clear()
{
    size_ = 0;
}

bool UxMenuModel::add(const UxMenuItem& item)
{
    if (size_ >= kMaxItems)
    {
        return false;
    }

    items_[size_] = item;
    ++size_;
    return true;
}

std::size_t UxMenuModel::size() const
{
    return size_;
}

const UxMenuItem* UxMenuModel::items() const
{
    return items_;
}

} // namespace ui_lvgl_ux
