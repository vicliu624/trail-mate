#include "product_composition/presentation_bundle.h"
#include "ui_ascii_runtime/ascii_descriptor_renderer.h"
#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

#include <cassert>

int main()
{
    ui::menu::MenuModel menu;
    assert(ui_lvgl_ux::buildMenuForUxPack("simulator_full", menu));

    ui::screen::ScreenBindingRegistry bindings;
    ui_lvgl_ux::CompatibilityScreenFactory factory;
    factory.buildBindingsForMenu(menu, bindings);

    product_composition::PresentationBundle presentation;
    presentation.ux_menu = &menu;
    presentation.screen_bindings = &bindings;

    trailmate::linux_sim::AsciiRuntimeEntryAdoption adoption;
    assert(adoption.load(presentation));

    trailmate::linux_sim::AsciiDescriptorRenderer renderer;
    assert(renderer.render(adoption));
    assert(renderer.lineCount() > 0);
    assert(renderer.lines() != nullptr);
    assert(renderer.lines()[0].text != nullptr);
    assert(renderer.line(0) != nullptr);

    trailmate::linux_sim::AsciiRuntimeEntryAdoption empty;
    assert(!renderer.render(empty));
    assert(renderer.lineCount() == 0);
    return 0;
}
