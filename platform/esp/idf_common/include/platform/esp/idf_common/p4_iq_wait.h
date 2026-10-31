#pragma once
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <atomic>
#include <cstdint>

namespace platform::esp::idf_common
{
// Process-lifetime singleton storage only. A late timer callback may still give
// the semaphore after a timed-out capture has returned. It never retains a task
// handle or accesses radio hardware. One producer owns prepare/wait; the ESP
// timer task only publishes completion and gives the static semaphore.
class P4IqWait
{
  public:
    P4IqWait() = default;
    P4IqWait(const P4IqWait&) = delete;
    P4IqWait& operator=(const P4IqWait&) = delete;
    bool prepare()
    {
        if (pending_ && completed_.load(std::memory_order_acquire) != generation_) return false;
        if (timer_) return true;
        if (!semaphore_) semaphore_ = xSemaphoreCreateBinaryStatic(&storage_);
        if (!semaphore_) return false;
        esp_timer_create_args_t args{};
        args.callback = expired;
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "fm_iq_wait";
        return esp_timer_create(&args, &timer_) == ESP_OK;
    }
    bool wait(unsigned microseconds)
    {
        if (!microseconds || microseconds > 1000 || !prepare()) return false;
        pending_ = false;
        // An earlier callback can finish giving after publishing completion.
        // Ignore stale semaphore tokens; only the generation proves expiry.
        while (xSemaphoreTake(semaphore_, 0) == pdTRUE)
        {
        }
        if (!++generation_) ++generation_;
        armed_.store(generation_, std::memory_order_release);
        pending_ = true;
        if (esp_timer_start_once(timer_, microseconds) != ESP_OK)
        {
            pending_ = false;
            return false;
        }
        while (completed_.load(std::memory_order_acquire) != generation_)
        {
            // The timer wakes us at microsecond resolution. This tick timeout
            // is only a bounded failure path, not the normal service interval.
            if (xSemaphoreTake(semaphore_, pdMS_TO_TICKS(10) + 1) != pdTRUE)
                return completed_.load(std::memory_order_acquire) == generation_;
        }
        return true;
    }

  private:
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static void expired(void* context)
    {
        auto& self = *static_cast<P4IqWait*>(context);
        const auto generation = self.armed_.load(std::memory_order_acquire);
        self.completed_.store(generation, std::memory_order_release);
        xSemaphoreGive(self.semaphore_);
    }
    StaticSemaphore_t storage_{};
    SemaphoreHandle_t semaphore_ = nullptr;
    esp_timer_handle_t timer_ = nullptr;
    std::atomic<uint32_t> completed_{0};
    std::atomic<uint32_t> armed_{0};
    uint32_t generation_ = 0;
    bool pending_ = false;
};
} // namespace platform::esp::idf_common
