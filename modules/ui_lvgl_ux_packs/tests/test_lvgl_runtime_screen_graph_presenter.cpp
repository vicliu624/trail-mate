#include "product_composition/presentation_bundle.h"
#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"
#include "ui_lvgl_ux_packs/runtime/lvgl_runtime_screen_graph_presenter.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

#include <cassert>
#include <cstddef>
#include <cstring>

namespace
{

bool containsScreen(const ui_lvgl_ux::LvglRuntimeScreenGraphPresenter& presenter,
                    ui::menu::MenuScreenId screen_id,
                    const char* binding_id)
{
    for (std::size_t index = 0; index < presenter.screenCount(); ++index)
    {
        const ui_lvgl_ux::LvglScreenHostEntry& entry =
            presenter.screenEntries()[index];
        if (entry.screen_id == screen_id && entry.available &&
            entry.binding_id != nullptr &&
            std::strcmp(entry.binding_id, binding_id) == 0)
        {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    ui::menu::MenuModel menu;
    assert(ui_lvgl_ux::buildMenuForUxPack("compatibility", menu));

    ui::screen::ScreenBindingRegistry bindings;
    ui_lvgl_ux::CompatibilityScreenFactory factory;
    factory.buildBindingsForMenu(menu, bindings);

    product_composition::PresentationBundle presentation;
    presentation.ux_menu = &menu;
    presentation.screen_bindings = &bindings;
    assert(product_composition::hasUxMenu(presentation));
    assert(product_composition::hasScreenBindings(presentation));

    ui_lvgl_ux::LvglRuntimeScreenGraphPresenter presenter;
    assert(presenter.load(presentation));
    assert(presenter.menuCount() > 0);
    assert(presenter.screenCount() > 0);
    assert(presenter.menuEntries()[0].route.valid);
    assert(containsScreen(presenter, ui::menu::MenuScreenId::Chat, "chat"));
    assert(containsScreen(presenter, ui::menu::MenuScreenId::Map, "map"));
    assert(containsScreen(presenter, ui::menu::MenuScreenId::Settings, "settings"));

    product_composition::PresentationBundle empty;
    assert(!presenter.load(empty));
    return 0;
}
