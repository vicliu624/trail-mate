#pragma once

#include "boards/tlora_pager/tlora_pager_board.h"
#include "platform/esp/boards/board_runtime.h"
#include "ui/LV_Helper.h"

namespace platform::esp::boards::detail
{

inline void initializeBoard(bool waking_from_sleep)
{
#if HAS_GPS
    ::boards::tlora_pager::board.begin(NO_HW_NFC);
#else
    ::boards::tlora_pager::board.begin(NO_HW_GPS | NO_HW_NFC);
#endif

    if (waking_from_sleep)
    {
        ::boards::tlora_pager::board.wakeUp();
    }
}

inline void initializeDisplay()
{
    beginLvglHelper(static_cast<LilyGo_Display&>(::boards::tlora_pager::instance));
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
    return {&::boards::tlora_pager::board, &::boards::tlora_pager::instance, &::boards::tlora_pager::instance, &::boards::tlora_pager::instance};
#else
    return {&::boards::tlora_pager::board, &::boards::tlora_pager::instance, nullptr, nullptr};
#endif
}

} // namespace platform::esp::boards::detail
