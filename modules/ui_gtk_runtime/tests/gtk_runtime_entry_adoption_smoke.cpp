#include "product_composition/presentation_bundle.h"
#include "ui_gtk_runtime/gtk_runtime_entry_adoption.h"
#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

#include <cassert>

int main()
{
    ui::menu::MenuModel menu;
    assert(ui_lvgl_ux::buildMenuForUxPack("uconsole_desktop", menu));

    ui::screen::ScreenBindingRegistry bindings;
    ui_lvgl_ux::CompatibilityScreenFactory factory;
    factory.buildBindingsForMenu(menu, bindings);

    product_composition::PresentationBundle presentation;
    presentation.ux_menu = &menu;
    presentation.screen_bindings = &bindings;

    trailmate::uconsole::gtk::GtkRuntimeEntryAdoption adoption;
    assert(adoption.load(presentation));
    assert(adoption.ready());
    assert(adoption.menuCount() > 0);
    assert(adoption.screenCount() > 0);
    assert(adoption.presenter().menuDescriptors()[0].route.valid);
    return 0;
}
