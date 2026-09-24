#include "esp32_lvgl_runtime_config.h"
#include "product_composition/agenda_target.h"
#include "product_composition/target_ux_binding.h"
#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

#include <cassert>
#include <cstring>

int main()
{
    using namespace trailmate::apps::esp32_lvgl;
    assert(std::strcmp(esp32LvglRuntimeConfig().target_id, EXPECT_TARGET_ID) == 0);
    const auto* profile = esp32LvglRuntimeTargetProfile();
    assert(product_composition::targetHasAgenda(profile));
    const auto* binding = esp32LvglRuntimeUxBinding();
    assert(binding && std::strcmp(binding->target_id, profile->target_id) == 0);
    const auto* pack = ui_lvgl_ux::findUxPackById(binding->active_ux_pack_id);
    assert(pack);
    ui_lvgl_ux::ScreenRegistry registry;
    pack->buildScreens(registry);
    bool found = false;
    for (std::size_t i = 0; i < registry.size(); ++i)
        found |= registry.items()[i].id == ui_lvgl_ux::ScreenId::Calendar;
    assert(found && registry.size() <= registry.kMaxScreens);
}
