#include "platform/esp/arduino_common/startup_support.h"

#include <Arduino.h>
#include <ctime>

#include "platform/esp/boards/board_runtime.h"
#include "sys/clock.h"

namespace
{

uint32_t platform_millis_now()
{
    return millis();
}

uint32_t platform_epoch_seconds_now()
{
    const time_t now = time(nullptr);
    return now < 0 ? 0U : static_cast<uint32_t>(now);
}

void platform_sleep_ms(uint32_t ms)
{
    delay(ms);
}

} // namespace

namespace platform::esp::arduino_common::startup_support
{

void initializeClockProviders()
{
    sys::set_millis_provider(platform_millis_now);
    sys::set_epoch_seconds_provider(platform_epoch_seconds_now);
    sys::set_sleep_provider(platform_sleep_ms);
}

esp_sleep_wakeup_cause_t detectWakeupCause()
{
    return esp_sleep_get_wakeup_cause();
}

bool isWakingFromSleep(esp_sleep_wakeup_cause_t cause)
{
    return cause != ESP_SLEEP_WAKEUP_UNDEFINED;
}

void initializeBoard(bool waking_from_sleep)
{
    platform::esp::boards::initializeBoard(waking_from_sleep);
}

} // namespace platform::esp::arduino_common::startup_support
