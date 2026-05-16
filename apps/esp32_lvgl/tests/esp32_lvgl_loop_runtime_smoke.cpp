#include "esp32_lvgl_loop_runtime.h"

#include <cassert>

int main()
{
    auto config = trailmate::apps::esp32_lvgl::esp32LvglRuntimeConfig();
    assert(trailmate::apps::esp32_lvgl::canStartEsp32LvglLoopRuntime(config));

    config.loop_task_name = nullptr;
    assert(!trailmate::apps::esp32_lvgl::canStartEsp32LvglLoopRuntime(config));
    return 0;
}
