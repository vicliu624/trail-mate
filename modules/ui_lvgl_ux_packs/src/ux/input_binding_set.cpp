#include "ui_lvgl_ux_packs/ux/input_binding_set.h"

namespace ui_lvgl_ux
{

void InputBindingSet::clear()
{
    for (std::size_t i = 0; i < kMaxBindings; ++i)
    {
        items_[i] = InputBinding{};
    }
    size_ = 0;
}

bool InputBindingSet::add(const InputBinding& binding)
{
    if (size_ >= kMaxBindings)
    {
        return false;
    }

    items_[size_] = binding;
    ++size_;
    return true;
}

std::size_t InputBindingSet::size() const
{
    return size_;
}

const InputBinding* InputBindingSet::items() const
{
    return items_;
}

} // namespace ui_lvgl_ux
