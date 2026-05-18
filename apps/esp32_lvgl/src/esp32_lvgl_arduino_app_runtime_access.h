#pragma once

namespace trailmate::apps::esp32_lvgl::arduino_app_runtime_access
{

struct Status
{
    bool initialized = false;
    bool board_handles_ready = false;
    bool app_context_bound = false;
    bool background_tasks_started = false;
};

bool initialize(bool use_mock);
void tick();
const Status& status();

} // namespace trailmate::apps::esp32_lvgl::arduino_app_runtime_access
