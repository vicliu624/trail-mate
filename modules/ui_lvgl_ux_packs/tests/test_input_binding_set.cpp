#include "ui_lvgl_ux_packs/ux/input_binding_set.h"

#include <cassert>

int main()
{
    ui_lvgl_ux::InputBindingSet bindings;
    assert(bindings.size() == 0);

    for (std::size_t i = 0; i < ui_lvgl_ux::InputBindingSet::kMaxBindings; ++i)
    {
        assert(bindings.add({ui_lvgl_ux::InputAction::Select, "Select"}));
    }
    assert(bindings.size() == ui_lvgl_ux::InputBindingSet::kMaxBindings);
    assert(!bindings.add({ui_lvgl_ux::InputAction::Back, "Overflow"}));

    bindings.clear();
    assert(bindings.size() == 0);
    assert(bindings.add({ui_lvgl_ux::InputAction::Back, "Back"}));
    assert(bindings.items()[0].action == ui_lvgl_ux::InputAction::Back);
    return 0;
}
