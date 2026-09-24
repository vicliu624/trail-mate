#include "product_composition/agenda_target.h"
#include "product_composition/target_profile.h"
#include "product_composition/target_ux_binding.h"
#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"
#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"
#include "ui_lvgl_ux_packs/ux/ux_screen_menu_adapter.h"
#include "ui_presentation/page/page_manifest.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
    static_assert(ui_lvgl_ux::ScreenRegistry::kMaxScreens == 16);
    static_assert(ui::screen::ScreenBindingRegistry::kMaxBindings == 16);
    std::size_t count = 0;
    const auto* targets = product_composition::allTargetProfiles(&count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& target = targets[i];
        const bool expected = std::strcmp(target.target_id, "wio_tracker_l2") == 0 ||
                              std::strcmp(target.target_id, "tdeck") == 0 ||
                              std::strcmp(target.target_id, "tlora_pager") == 0;
        const auto* manifest = ui::presentation::findPageManifest(target.page_manifest_id);
        bool declared = false;
        if (manifest)
            for (std::size_t j = 0; j < manifest->item_count; ++j)
            {
                const auto& item = manifest->items[j];
                if (item.page_id == ui::presentation::PageId::Calendar)
                {
                    assert(item.enabled && item.visible_in_menu);
                    assert(std::strcmp(item.binding_id, "calendar") == 0);
                    declared = true;
                }
            }
        assert(declared == expected);
        assert(product_composition::targetHasAgenda(&target) == expected);
        const auto* binding = product_composition::findTargetUxBinding(target.target_id);
        assert(binding);
        const auto* pack = ui_lvgl_ux::findUxPackById(binding->active_ux_pack_id);
        assert(pack);
        ui_lvgl_ux::ScreenRegistry screens;
        pack->buildScreens(screens);
        assert(screens.size() <= screens.kMaxScreens);
        for (std::size_t a = 0; a < screens.size(); ++a)
            for (std::size_t b = a + 1; b < screens.size(); ++b)
                assert(screens.items()[a].id != screens.items()[b].id);
        ui::menu::MenuModel menu;
        ui_lvgl_ux::UxScreenMenuAdapter{}.buildMenu(screens, menu);
        assert(menu.size() == screens.size());
        ui::screen::ScreenBindingRegistry bindings;
        ui_lvgl_ux::CompatibilityScreenFactory{}.buildBindingsForMenu(menu, bindings);
        assert(bindings.size() == menu.size());
        const auto* calendar = bindings.find(ui::menu::MenuScreenId::Calendar);
        assert((calendar != nullptr) == expected);
        if (calendar)
        {
            assert(calendar->available && std::strcmp(calendar->binding_id, "calendar") == 0);
            const bool touch_only = std::strcmp(target.target_id, "wio_tracker_l2") == 0;
            assert(screens.size() == (touch_only ? 10 : 11));
            assert(target.renderer == product_composition::TargetRenderer::Lvgl);
            std::printf("%s: manifest=%zu screens=%zu bindings=%zu\n",
                        target.target_id, manifest->item_count, screens.size(), bindings.size());
        }
    }
}
