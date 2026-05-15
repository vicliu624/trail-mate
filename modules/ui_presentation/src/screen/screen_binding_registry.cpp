#include "ui_presentation/screen/screen_binding_registry.h"

namespace ui
{
namespace screen
{

void ScreenBindingRegistry::clear()
{
    size_ = 0;
}

bool ScreenBindingRegistry::add(const ScreenBinding& binding)
{
    if (size_ >= kMaxBindings)
    {
        return false;
    }

    items_[size_] = binding;
    ++size_;
    return true;
}

const ScreenBinding* ScreenBindingRegistry::find(
    ui::menu::MenuScreenId screen_id) const
{
    for (std::size_t index = 0; index < size_; ++index)
    {
        if (items_[index].screen_id == screen_id)
        {
            return &items_[index];
        }
    }

    return nullptr;
}

std::size_t ScreenBindingRegistry::size() const
{
    return size_;
}

const ScreenBinding* ScreenBindingRegistry::items() const
{
    return items_;
}

} // namespace screen
} // namespace ui
