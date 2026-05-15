#include "product_composition/presentation_bundle.h"
#include "ui_gtk_runtime/gtk_descriptor_page_registry.h"
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

    trailmate::uconsole::GtkDescriptorPageRegistry registry;
    assert(registry.load(adoption));
    assert(registry.pageCount() > 0);
    assert(registry.pages()[0].binding_id != nullptr);
    assert(registry.pages()[0].label != nullptr);

    trailmate::uconsole::gtk::GtkRuntimeEntryAdoption empty;
    assert(!registry.load(empty));
    assert(registry.pageCount() == 0);
    return 0;
}
