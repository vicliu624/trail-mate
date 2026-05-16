#include "product_composition/target_build_binding.h"

#include <cassert>
#include <cstring>

int main()
{
    std::size_t count = 0;
    const auto* bindings = product_composition::esp32LvglTargetBuildBindings(&count);
    assert(bindings != nullptr);
    assert(count == 3);

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

    assert(product_composition::findTargetBuildBinding("twatch") == nullptr);
    return 0;
}
