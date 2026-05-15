#include "product_composition/presentation_bundle.h"
#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"
#include "ui_lvgl_ux_packs/runtime/lvgl_runtime_adoption_probe.h"
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

    ui_lvgl_ux::LvglRuntimeAdoptionProbe probe;
    assert(probe.load(presentation));
    assert(probe.ready());
    assert(!probe.fallbackUsed());
    assert(probe.menuCount() > 0);
    assert(probe.screenCount() > 0);
    const ui_lvgl_ux::LvglRuntimeEntryAdoption& adoption = probe.adoption();
    assert(adoption.presenter().menuEntries()[0].route.valid);

    product_composition::PresentationBundle empty;
    assert(!probe.load(empty));
    assert(!probe.ready());
    assert(probe.fallbackUsed());
    assert(probe.menuCount() == 0);
    assert(probe.screenCount() == 0);
    return 0;
}
