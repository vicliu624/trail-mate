#include "product_composition/presentation_bundle.h"
#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"
#include "ui_lvgl_ux_packs/runtime/lvgl_descriptor_renderer_probe.h"
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

    ui_lvgl_ux::LvglDescriptorRendererProbe probe;
    assert(probe.render(runtime));
    assert(probe.ready());
    assert(probe.usingPrimaryScreenGraph());
    assert(probe.usedPrimaryScreenGraph());
    assert(!probe.fallbackUsed());
    assert(!probe.usedFallback());
    assert(probe.itemCount() > 0);
    assert(probe.items() != nullptr);

    ui_lvgl_ux::LvglPrimaryScreenGraphRuntime fallback_runtime;
    product_composition::PresentationBundle empty;
    assert(!fallback_runtime.load(empty));
    assert(!probe.render(fallback_runtime));
    assert(probe.fallbackUsed());
    assert(probe.usedFallback());
    return 0;
}
