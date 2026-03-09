#pragma once

#include "board/TWatchS3Board.h"
#include "platform/esp/boards/board_runtime.h"
#include "ui/LV_Helper.h"

namespace platform::esp::boards::detail
{

inline void initializeBoard(bool waking_from_sleep)
{
#if HAS_GPS
    board.begin(NO_HW_SD | NO_HW_NFC);
#else
    board.begin(NO_HW_GPS | NO_HW_SD | NO_HW_NFC);
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
    return {&board, &instance, nullptr, nullptr};
}

} // namespace platform::esp::boards::detail
