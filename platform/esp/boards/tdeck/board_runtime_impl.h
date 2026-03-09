#pragma once

#include "board/TDeckBoard.h"
#include "platform/esp/boards/board_runtime.h"
#include "ui/LV_Helper.h"

namespace platform::esp::boards::detail
{

inline void initializeBoard(bool waking_from_sleep)
{
#if HAS_GPS
    board.begin();
#else
    board.begin(NO_HW_GPS);
#endif

    if (waking_from_sleep)
    {
        board.wakeUp();
    }
}

inline void initializeDisplay()
{
    beginLvglHelper(static_cast<LilyGo_Display&>(instance));
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
#if HAS_GPS
    return {&board, &instance, &instance, &instance};
#else
    return {&board, &instance, nullptr, nullptr};
#endif
}

} // namespace platform::esp::boards::detail
