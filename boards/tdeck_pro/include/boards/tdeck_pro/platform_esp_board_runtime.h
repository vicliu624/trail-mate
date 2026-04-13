#pragma once

#include "boards/tdeck_pro/tdeck_pro_board.h"
#include "platform/esp/boards/board_runtime.h"
#include "ui/LV_Helper.h"

namespace platform::esp::boards::detail
{

inline void initializeBoard(bool waking_from_sleep)
{
    (void)waking_from_sleep;
    ::boards::tdeck_pro::board.begin();
}

inline void initializeDisplay()
{
    beginLvglHelper(static_cast<LilyGo_Display&>(::boards::tdeck_pro::instance));
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
    return {&::boards::tdeck_pro::board, &::boards::tdeck_pro::instance, &::boards::tdeck_pro::instance, &::boards::tdeck_pro::instance};
}

} // namespace platform::esp::boards::detail
