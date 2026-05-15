#include "linux_sim_composition_root.h"

#include "product_composition/presentation_bundle.h"
#include "ui_ascii_runtime/ascii_runtime_screen_graph_presenter.h"

#include <cassert>
#include <cstddef>
#include <cstring>

namespace
{

bool containsScreen(const trailmate::linux_sim::AsciiRuntimeScreenGraphPresenter& presenter,
                    ui::menu::MenuScreenId screen_id,
                    const char* binding_id)
{
    for (std::size_t index = 0; index < presenter.screenCount(); ++index)
    {
        const trailmate::linux_sim::AsciiScreenDescriptor& screen =
            presenter.screens()[index];
        if (screen.screen_id == screen_id && screen.available &&
            screen.binding_id != nullptr &&
            std::strcmp(screen.binding_id, binding_id) == 0)
        {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    trailmate::linux_sim::LinuxSimCompositionRoot root;
    assert(root.initialize());
    assert(product_composition::hasUxMenu(root.presentation()));
    assert(product_composition::hasScreenBindings(root.presentation()));

    trailmate::linux_sim::AsciiRuntimeScreenGraphPresenter presenter;
    assert(presenter.load(root.presentation()));
    assert(presenter.menuCount() > 0);
    assert(presenter.screenCount() > 0);
    assert(presenter.menuLines()[0].route.valid);

    assert(containsScreen(presenter, ui::menu::MenuScreenId::Chat, "chat"));
    assert(containsScreen(presenter, ui::menu::MenuScreenId::Map, "map"));
    assert(containsScreen(presenter, ui::menu::MenuScreenId::Settings, "settings"));

    product_composition::PresentationBundle empty;
    assert(!presenter.load(empty));
    return 0;
}
