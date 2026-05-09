/**
 * @file task_diag.cpp
 * @brief FreeRTOS task watermark + queue depth diagnostics.
 */

#include "platform/esp/idf_common/task_diag.h"

#include <cstdint>
#include <cstdio>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace platform::esp::idf_common::task_diag
{
namespace
{

constexpr const char* kTag = "task-diag";
constexpr std::uint32_t kMaxTasks = 32;

} // namespace

void logTaskWatermarks()
{
    TaskStatus_t tasks[kMaxTasks]{};
    unsigned long total_runtime = 0;
    const std::uint32_t actual =
        uxTaskGetSystemState(tasks, kMaxTasks, &total_runtime);

    if (actual == 0 || actual > kMaxTasks)
    {
        ESP_LOGE(kTag, "uxTaskGetSystemState returned %lu", static_cast<unsigned long>(actual));
        return;
    }

    ESP_LOGI(kTag, "=== Task watermarks (%lu tasks) ===", static_cast<unsigned long>(actual));
    for (std::uint32_t i = 0; i < actual; ++i)
    {
        const auto& t = tasks[i];
        const unsigned long stack_free =
            static_cast<unsigned long>(t.usStackHighWaterMark);
        const unsigned long stack_words = stack_free * sizeof(StackType_t);

        if (stack_words < 512U)
        {
            ESP_LOGW(kTag, "  %-20s pri=%lu stack-free=%lu B  *** LOW ***",
                     t.pcTaskName,
                     static_cast<unsigned long>(t.uxCurrentPriority),
                     static_cast<unsigned long>(stack_words));
        }
        else
        {
            ESP_LOGI(kTag, "  %-20s pri=%lu stack-free=%lu B",
                     t.pcTaskName,
                     static_cast<unsigned long>(t.uxCurrentPriority),
                     static_cast<unsigned long>(stack_words));
        }
    }
}

void logQueueDepth(const char* label, void* queue_handle)
{
    if (queue_handle == nullptr || label == nullptr)
    {
        return;
    }

    const auto handle = static_cast<QueueHandle_t>(queue_handle);
    const unsigned long waiting = static_cast<unsigned long>(uxQueueMessagesWaiting(handle));
    ESP_LOGI(kTag, "Queue '%s': %lu messages waiting", label, waiting);
}

} // namespace platform::esp::idf_common::task_diag
