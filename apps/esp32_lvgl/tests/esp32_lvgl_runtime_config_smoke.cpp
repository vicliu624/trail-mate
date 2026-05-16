#include "esp32_lvgl_runtime_config.h"

#include "product_composition/target_profile.h"

#include <cassert>
#include <cstring>

int main()
{
    const auto& config = trailmate::apps::esp32_lvgl::esp32LvglRuntimeConfig();
    assert(config.target_id != nullptr);
    assert(config.log_tag != nullptr);
    assert(config.target_name != nullptr);
    assert(config.loop_task_name != nullptr);
    assert(config.loop_delay_ms == 10);
    assert(config.loop_stack_size == 4096);
    assert(config.loop_priority == 5);

    assert(product_composition::findTargetProfile("tab5") != nullptr);
    assert(product_composition::findTargetProfile("tdisplayp4_tft") != nullptr);
    assert(product_composition::findTargetProfile("tdisplayp4_amoled") != nullptr);

    if (std::strcmp(config.target_id, "esp_idf") != 0)
    {
        assert(trailmate::apps::esp32_lvgl::hasEsp32LvglRuntimeTargetProfile());
    }
    return 0;
}
