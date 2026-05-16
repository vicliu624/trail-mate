#include "product_composition/target_build_binding.h"

#include <cassert>
#include <cstring>

int main()
{
    std::size_t count = 0;
    const auto* all = product_composition::allTargetBuildBindings(&count);
    assert(all != nullptr);
    assert(count == 9);

    const auto* bindings = product_composition::esp32LvglTargetBuildBindings(&count);
    assert(bindings != nullptr);
    assert(count == 6);

    const auto* tab5 = product_composition::findTargetBuildBinding("tab5");
    assert(tab5 != nullptr);
    assert(std::strcmp(tab5->build_entrypoint, "builds/esp_idf") == 0);
    assert(std::strcmp(tab5->app_shell, "apps/esp32_lvgl") == 0);
    assert(std::strcmp(tab5->sdkconfig_defaults,
                       "builds/esp_idf/targets/tab5/sdkconfig.defaults") == 0);

    const auto* tft = product_composition::findTargetBuildBinding("tdisplayp4_tft");
    assert(tft != nullptr);
    assert(std::strcmp(tft->sdkconfig_defaults,
                       "builds/esp_idf/targets/tdisplayp4_tft/sdkconfig.defaults") == 0);

    const auto* amoled = product_composition::findTargetBuildBinding("tdisplayp4_amoled");
    assert(amoled != nullptr);
    assert(std::strcmp(amoled->sdkconfig_defaults,
                       "builds/esp_idf/targets/tdisplayp4_amoled/sdkconfig.defaults") == 0);

    const auto* pager = product_composition::findTargetBuildBinding("tlora_pager");
    assert(pager != nullptr);
    assert(std::strcmp(pager->build_entrypoint, "builds/esp_idf") == 0);
    assert(pager->sdkconfig_defaults == nullptr);
    assert(std::strcmp(pager->status, "pending_target_defaults") == 0);

    const auto* deck = product_composition::findTargetBuildBinding("tdeck");
    assert(deck != nullptr);
    assert(std::strcmp(deck->build_entrypoint, "builds/esp_idf") == 0);

    const auto* watch = product_composition::findTargetBuildBinding("twatch");
    assert(watch != nullptr);
    assert(std::strcmp(watch->build_entrypoint, "builds/esp_idf") == 0);

    const auto* uconsole = product_composition::findTargetBuildBinding("uconsole");
    assert(uconsole != nullptr);
    assert(std::strcmp(uconsole->build_entrypoint, "builds/linux_cmake") == 0);
    assert(std::strcmp(uconsole->app_shell, "apps/linux_uconsole_gtk") == 0);

    const auto* cardputer = product_composition::findTargetBuildBinding("cardputerzero");
    assert(cardputer != nullptr);
    assert(std::strcmp(cardputer->build_entrypoint, "builds/linux_cmake") == 0);
    assert(std::strcmp(cardputer->app_shell, "apps/linux_sim_shell") == 0);

    const auto* gat562 = product_composition::findTargetBuildBinding("gat562_mesh_evb_pro");
    assert(gat562 != nullptr);
    assert(std::strcmp(gat562->build_entrypoint, "builds/pio_nrf52") == 0);
    assert(std::strcmp(gat562->app_shell, "apps/nrf52_node") == 0);

    assert(product_composition::findTargetBuildBinding("unknown") == nullptr);
    return 0;
}
