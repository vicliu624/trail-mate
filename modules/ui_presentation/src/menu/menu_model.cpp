#include "ui_presentation/menu/menu_model.h"

namespace ui
{
namespace menu
{

void MenuModel::clear()
{
    size_ = 0;
}

bool MenuModel::add(const MenuItem& item)
{
    if (size_ >= kMaxItems)
    {
        return false;
    }

    items_[size_] = item;
    ++size_;
    return true;
}

std::size_t MenuModel::size() const
{
    return size_;
}

const MenuItem* MenuModel::items() const
{
    return items_;
}

} // namespace menu
} // namespace ui
