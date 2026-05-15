#include "product_composition/presentation_bundle.h"
#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"
#include "ui_lvgl_ux_packs/runtime/lvgl_primary_screen_graph_runtime.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

#include <cassert>

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

    ui_lvgl_ux::LvglPrimaryScreenGraphRuntime runtime;
    assert(runtime.load(presentation));
    assert(runtime.ready());
    assert(runtime.usingPrimaryScreenGraph());
    assert(runtime.runtimeSource() ==
           ui_lvgl_ux::LvglScreenGraphRuntimeSource::ScreenGraphAdoption);
    assert(!runtime.fallbackUsed());
    assert(runtime.menuCount() > 0);
    assert(runtime.screenCount() > 0);
    assert(runtime.adoption().presenter().menuEntries()[0].route.valid);

    product_composition::PresentationBundle empty;
    assert(!runtime.load(empty));
    assert(!runtime.ready());
    assert(!runtime.usingPrimaryScreenGraph());
    assert(runtime.runtimeSource() ==
           ui_lvgl_ux::LvglScreenGraphRuntimeSource::HardcodedFallback);
    assert(runtime.fallbackUsed());
    assert(runtime.menuCount() == 0);
    assert(runtime.screenCount() == 0);
    return 0;
}
