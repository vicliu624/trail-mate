/**
 * @file ui_dispatcher.cpp
 * @brief Lock-free event queue + LVGL drain timer for IDF UI serialisation.
 */

#include "platform/esp/idf_common/ui_dispatcher.h"

#include <atomic>
#include <cstdint>
#include <utility>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"

namespace platform::esp::idf_common::ui_dispatcher
{
namespace
{

constexpr const char* kTag = "idf-ui-dispatch";
constexpr std::uint32_t kDefaultDrainPeriodMs = 20;
constexpr std::uint32_t kQueueDepth = 16;

Hooks s_hooks{};
QueueHandle_t s_queue = nullptr;
lv_timer_t* s_drain_timer = nullptr;

} // namespace

void init(const Hooks& hooks)
{
    s_hooks = hooks;

    if (s_queue == nullptr)
    {
        s_queue = xQueueCreate(kQueueDepth, sizeof(Event));
        if (s_queue == nullptr)
        {
            ESP_LOGE(kTag, "Failed to create UI event queue");
        }
    }
}

bool post(Event event)
{
    if (s_queue == nullptr || event == Event::None)
    {
        return false;
    }

    // Non-blocking send from any task context.
    const BaseType_t rc = xQueueSend(s_queue, &event, 0);
    return rc == pdTRUE;
}

void drain()
{
    if (s_queue == nullptr)
    {
        return;
    }

    Event event = Event::None;
    while (xQueueReceive(s_queue, &event, 0) == pdTRUE)
    {
        switch (event)
        {
        case Event::WakeFromSleep:
            if (s_hooks.on_wake_from_sleep)
            {
                s_hooks.on_wake_from_sleep();
            }
            break;

        case Event::ShowMainMenu:
            if (s_hooks.show_main_menu)
            {
                s_hooks.show_main_menu();
            }
            break;

        case Event::None:
        default:
            break;
        }
    }
}

// LVGL timer callback – runs in the LVGL task context.
// Process at most ONE event per tick to avoid starving the render pipeline.
// The drain timer fires at a conservative 100 ms; any remaining events
// are picked up on the next tick.
void drain_timer_cb(lv_timer_t* /*timer*/)
{
    if (s_queue == nullptr)
    {
        return;
    }

    Event event = Event::None;
    if (xQueueReceive(s_queue, &event, 0) != pdTRUE)
    {
        return;
    }

    switch (event)
    {
    case Event::WakeFromSleep:
        if (s_hooks.on_wake_from_sleep) s_hooks.on_wake_from_sleep();
        break;
    case Event::ShowMainMenu:
        if (s_hooks.show_main_menu) s_hooks.show_main_menu();
        break;
    case Event::None:
    default:
        break;
    }
}

void* ensure_drain_timer(std::uint32_t period_ms)
{
    if (s_drain_timer != nullptr)
    {
        return s_drain_timer;
    }

    // Default 100 ms period: fast enough for responsive wake-up,
    // slow enough to avoid starving the LVGL render pipeline.
    const std::uint32_t p = (period_ms > 0) ? period_ms : 100;
    s_drain_timer = lv_timer_create(drain_timer_cb, p, nullptr);
    if (s_drain_timer == nullptr)
    {
        ESP_LOGE(kTag, "Failed to create LVGL drain timer");
    }
    else
    {
        ESP_LOGI(kTag, "Drain timer created period_ms=%lu", static_cast<unsigned long>(p));
    }

    return s_drain_timer;
}

} // namespace platform::esp::idf_common::ui_dispatcher
