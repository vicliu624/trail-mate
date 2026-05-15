#include "ui_lvgl_ux_packs/runtime/lvgl_runtime_screen_graph_presenter.h"

namespace ui_lvgl_ux
{

bool LvglRuntimeScreenGraphPresenter::load(
    const product_composition::PresentationBundle& presentation)
{
    if (!product_composition::hasUxMenu(presentation) ||
        !product_composition::hasScreenBindings(presentation))
    {
        return false;
    }

    return bridge_.load(presentation);
}

std::size_t LvglRuntimeScreenGraphPresenter::menuCount() const
{
    return bridge_.menuCount();
}

std::size_t LvglRuntimeScreenGraphPresenter::screenCount() const
{
    return bridge_.screenCount();
}

const LvglMenuEntry* LvglRuntimeScreenGraphPresenter::menuEntries() const
{
    return bridge_.menuItems();
}

const LvglScreenHostEntry* LvglRuntimeScreenGraphPresenter::screenEntries() const
{
    return bridge_.screenItems();
}

} // namespace ui_lvgl_ux
