#include "platform/esp/boards/board_runtime.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "boards/tab5/platform_esp_board_runtime.h"
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "boards/t_display_p4/platform_esp_board_runtime.h"
#elif defined(ARDUINO_T_DECK_PRO)
#include "boards/tdeck_pro/platform_esp_board_runtime.h"
#elif defined(ARDUINO_T_DECK)
#include "boards/tdeck/platform_esp_board_runtime.h"
#elif defined(ARDUINO_T_WATCH_S3)
#include "boards/twatchs3/platform_esp_board_runtime.h"
#else
#include "boards/tlora_pager/platform_esp_board_runtime.h"
#endif

namespace platform::esp::boards
{

void initializeBoard(bool waking_from_sleep)
{
    detail::initializeBoard(waking_from_sleep);
}

void initializeDisplay()
{
    detail::initializeDisplay();
}

bool tryResolveAppContextInitHandles(AppContextInitHandles* out_handles)
{
    return detail::tryResolveAppContextInitHandles(out_handles);
}

AppContextInitHandles resolveAppContextInitHandles()
{
    return detail::resolveAppContextInitHandles();
}

} // namespace platform::esp::boards
