#include "ui_lvgl_ux_packs/runtime/lvgl_runtime_entry_adoption.h"

namespace ui_lvgl_ux
{

bool LvglRuntimeEntryAdoption::load(
    const product_composition::PresentationBundle& presentation)
{
    ready_ = presenter_.load(presentation);
    return ready_;
}

bool LvglRuntimeEntryAdoption::ready() const
{
    return ready_;
}

std::size_t LvglRuntimeEntryAdoption::menuCount() const
{
    return ready_ ? presenter_.menuCount() : 0;
}

std::size_t LvglRuntimeEntryAdoption::screenCount() const
{
    return ready_ ? presenter_.screenCount() : 0;
}

const LvglRuntimeScreenGraphPresenter&
LvglRuntimeEntryAdoption::presenter() const
{
    return presenter_;
}

} // namespace ui_lvgl_ux
