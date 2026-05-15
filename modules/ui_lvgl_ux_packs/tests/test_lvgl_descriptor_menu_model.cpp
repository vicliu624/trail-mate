#include "product_composition/presentation_bundle.h"
#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"
#include "ui_lvgl_ux_packs/runtime/lvgl_descriptor_menu_model.h"
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
    assert(runtime.usingPrimaryScreenGraph());

    ui_lvgl_ux::LvglDescriptorMenuModel model;
    assert(model.load(runtime));
    assert(model.itemCount() > 0);
    assert(model.items()[0].binding_id != nullptr);

    ui_lvgl_ux::LvglPrimaryScreenGraphRuntime fallback_runtime;
    product_composition::PresentationBundle empty;
    assert(!fallback_runtime.load(empty));
    assert(!model.load(fallback_runtime));
    assert(model.itemCount() == 0);
    return 0;
}
