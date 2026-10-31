#include "platform/esp/idf_common/p4_iq_wait.h"
#include <cassert>

struct FakeTimer
{
    esp_timer_create_args_t args;
    bool pending = false;
};
static FakeTimer timer;
static bool create_failure = true, start_failure = false, auto_fire = true, stale_wake = false;
static unsigned creates = 0, semaphores = 0, starts = 0, blocking_waits = 0;
static uint64_t timeout_us = 0;
static SemaphoreHandle_t semaphore;
static void fire()
{
    assert(timer.pending);
    timer.pending = false;
    timer.args.callback(timer.args.arg);
}
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage)
{
    ++semaphores;
    return semaphore = storage;
}
int xSemaphoreGive(SemaphoreHandle_t sem)
{
    sem->token = true;
    return pdTRUE;
}
int xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{
    if (ticks)
    {
        assert(ticks == 2); // A failure bound, not the 100 us normal interval.
        ++blocking_waits;
        if (stale_wake)
        {
            stale_wake = false;
            sem->token = true;
        }
        else if (timer.pending && auto_fire) fire();
    }
    const bool token = sem->token;
    sem->token = false;
    return token ? pdTRUE : 0;
}
esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* output)
{
    ++creates;
    if (create_failure) return -1;
    assert(args->dispatch_method == ESP_TIMER_TASK);
    timer.args = *args;
    *output = &timer;
    return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t us)
{
    assert(handle == &timer && !timer.pending);
    ++starts;
    if (start_failure) return -1;
    timeout_us = us;
    timer.pending = true;
    return ESP_OK;
}
int main()
{
    // Matches the production singleton lifetime, including a callback arriving
    // after wait() has timed out. No callback references a terminated task.
    static platform::esp::idf_common::P4IqWait waiter;
    assert(!waiter.prepare());
    create_failure = false;
    assert(waiter.prepare() && creates == 2 && semaphores == 1);
    assert(!waiter.wait(0) && !waiter.wait(1001) && !starts);
    assert(waiter.wait(100) && timeout_us == 100 && blocking_waits == 1);
    assert(waiter.wait(200) && timeout_us == 200 && creates == 2);
    start_failure = true;
    assert(!waiter.wait(100));
    start_failure = false;
    assert(waiter.wait(100));
    auto_fire = false;
    assert(!waiter.wait(100));
    const auto timed_out_starts = starts;
    assert(!waiter.prepare() && !waiter.wait(100) && starts == timed_out_starts);
    fire(); // Late completion unlocks a future session, not an old task handle.
    assert(waiter.prepare());
    auto_fire = true;
    assert(waiter.wait(100));
    auto_fire = false;
    stale_wake = true;
    semaphore->token = true;   // A stale pre-arm token must also be drained.
    assert(!waiter.wait(100)); // Spurious wake does not prove timer completion.
    fire();
    auto_fire = true;
    for (unsigned n = 0; n < 10000; ++n) assert(waiter.wait(100));
    assert(!timer.pending && creates == 2 && semaphores == 1);
}
