#include "apps/esp_pio/app_context.h"
#include "platform/esp/arduino_common/device_identity.h"
#include "platform/ui/device_runtime.h"

#include <ctime>

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "platform/esp/idf_common/tab5_rtc_runtime.h"
#else
#include <sys/time.h>
#endif

namespace app
{

bool AppContext::getDeviceMacAddress(uint8_t out_mac[6]) const
{
    if (!out_mac)
    {
        return false;
    }

    const auto mac = platform::esp::arduino_common::device_identity::getSelfMacAddress();
    for (size_t i = 0; i < mac.size(); ++i)
    {
        out_mac[i] = mac[i];
    }
    return true;
}

bool AppContext::syncCurrentEpochSeconds(uint32_t epoch_seconds)
{
    if (epoch_seconds == 0)
    {
        return false;
    }

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return platform::esp::idf_common::tab5_rtc_runtime::apply_system_time_and_sync_rtc(
        static_cast<time_t>(epoch_seconds), "app_context");
#else
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(epoch_seconds);
    tv.tv_usec = 0;
    return settimeofday(&tv, nullptr) == 0;
#endif
}

void AppContext::restartDevice()
{
#if __has_include(<Arduino.h>)
    platform::ui::device::restart();
#endif
}

} // namespace app
