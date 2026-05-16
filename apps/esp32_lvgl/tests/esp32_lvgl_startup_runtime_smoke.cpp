#include "esp32_lvgl_startup_runtime.h"

#include <cassert>

int main()
{
    auto config = trailmate::apps::esp32_lvgl::esp32LvglRuntimeConfig();
    assert(trailmate::apps::esp32_lvgl::canRunEsp32LvglStartupRuntime(config));

    config.log_tag = nullptr;
    assert(!trailmate::apps::esp32_lvgl::canRunEsp32LvglStartupRuntime(config));
    return 0;
}
