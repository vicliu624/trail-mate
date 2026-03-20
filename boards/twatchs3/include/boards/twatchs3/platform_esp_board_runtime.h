#pragma once

#include "boards/twatchs3/twatchs3_board.h"
#include "platform/esp/boards/board_runtime.h"
#include "ui/LV_Helper.h"

namespace platform::esp::boards::detail
{

inline void initializeBoard(bool waking_from_sleep)
{
#if HAS_GPS
    ::boards::twatchs3::instance.begin(NO_HW_SD | NO_HW_NFC);
#else
    ::boards::twatchs3::instance.begin(NO_HW_GPS | NO_HW_SD | NO_HW_NFC);
#endif

    if (waking_from_sleep)
    {
        ::boards::twatchs3::instance.wakeUp();
    }
}

inline void initializeDisplay()
{
    beginLvglHelper(static_cast<LilyGo_Display&>(::boards::twatchs3::instance));
}

inline bool tryResolveAppContextInitHandles(AppContextInitHandles* out_handles)
{
    if (!out_handles)
    {
        return false;
    }
    *out_handles = resolveAppContextInitHandles();
    return true;
}

inline AppContextInitHandles resolveAppContextInitHandles()
{
    return {&board, &::boards::twatchs3::instance, nullptr, nullptr};
}

} // namespace platform::esp::boards::detail
