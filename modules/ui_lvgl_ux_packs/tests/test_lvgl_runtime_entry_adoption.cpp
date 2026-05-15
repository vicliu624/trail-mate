#include "product_composition/presentation_bundle.h"
#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"
#include "ui_lvgl_ux_packs/runtime/lvgl_runtime_entry_adoption.h"
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

    ui_lvgl_ux::LvglRuntimeEntryAdoption adoption;
    assert(adoption.load(presentation));
    assert(adoption.ready());
    assert(adoption.menuCount() > 0);
    assert(adoption.screenCount() > 0);
    assert(adoption.presenter().menuEntries()[0].route.valid);

    product_composition::PresentationBundle empty;
    assert(!adoption.load(empty));
    assert(!adoption.ready());
    assert(adoption.menuCount() == 0);
    assert(adoption.screenCount() == 0);
    return 0;
}
