#include "apps/esp_idf/loop_runtime.h"

#include "apps/esp_idf/app_runtime_access.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui/loop_shell.h"

namespace apps::esp_idf::loop_runtime
{
namespace
{

struct LoopState
{
    TaskHandle_t task = nullptr;
    RuntimeConfig config{};
};

LoopState s_loop{};

void loopTask(void* parameter)
{
    (void)parameter;

    ui::loop_shell::Hooks hooks{};
    hooks.now_ms = []() -> uint32_t
    {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    };
    hooks.update_runtime = []()
    { app_runtime_access::tick(); };
    hooks.yield_now = []()
    { taskYIELD(); };
    hooks.sleep_ms = [](uint32_t ms)
    { vTaskDelay(pdMS_TO_TICKS(ms)); };
    hooks.overlay_sleep_ms = s_loop.config.loop_delay_ms;
    hooks.idle_sleep_ms = s_loop.config.loop_delay_ms;

    while (true)
    {
        ui::loop_shell::tick(hooks);
    }
}

} // namespace

void start(const RuntimeConfig& config)
{
    if (s_loop.task)
    {
        return;
    }

    s_loop.config = config;

    const BaseType_t rc = xTaskCreate(loopTask,
                                      s_loop.config.loop_task_name,
                                      s_loop.config.loop_stack_size,
                                      nullptr,
                                      static_cast<UBaseType_t>(s_loop.config.loop_priority),
                                      &s_loop.task);
    if (rc != pdPASS)
    {
        ESP_LOGE(s_loop.config.log_tag,
                 "Failed to start IDF app loop task rc=%ld target=%s",
                 static_cast<long>(rc),
                 s_loop.config.target_name);
        s_loop.task = nullptr;
        return;
    }

    ESP_LOGI(s_loop.config.log_tag, "IDF app loop task started for %s", s_loop.config.target_name);
}

} // namespace apps::esp_idf::loop_runtime
