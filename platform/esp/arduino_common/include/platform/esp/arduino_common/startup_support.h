#pragma once

#include "esp_sleep.h"

namespace platform::esp::arduino_common::startup_support
{

void initializeClockProviders();
esp_sleep_wakeup_cause_t detectWakeupCause();
bool isWakingFromSleep(esp_sleep_wakeup_cause_t cause);
void initializeBoard(bool waking_from_sleep);

} // namespace platform::esp::arduino_common::startup_support
