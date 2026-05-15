#include "ui_ascii_runtime/ascii_runtime_screen_graph_presenter.h"

namespace trailmate::linux_sim
{

bool AsciiRuntimeScreenGraphPresenter::load(
    const product_composition::PresentationBundle& presentation)
{
    if (!product_composition::hasUxMenu(presentation) ||
        !product_composition::hasScreenBindings(presentation))
    {
        return false;
    }

    return bridge_.load(presentation);
}

std::size_t AsciiRuntimeScreenGraphPresenter::menuCount() const
{
    return bridge_.menuCount();
}

std::size_t AsciiRuntimeScreenGraphPresenter::screenCount() const
{
    return bridge_.screenCount();
}

const AsciiMenuLine* AsciiRuntimeScreenGraphPresenter::menuLines() const
{
    return bridge_.menuItems();
}

const AsciiScreenDescriptor*
AsciiRuntimeScreenGraphPresenter::screens() const
{
    return bridge_.screenItems();
}

} // namespace trailmate::linux_sim
