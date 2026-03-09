#include "platform/esp/boards/board_runtime.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "../tab5/board_runtime_impl.h"
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "../t_display_p4/board_runtime_impl.h"
#elif defined(ARDUINO_T_DECK)
#include "../tdeck/board_runtime_impl.h"
#elif defined(ARDUINO_T_WATCH_S3)
#include "../twatchs3/board_runtime_impl.h"
#else
#include "../tlora_pager/board_runtime_impl.h"
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