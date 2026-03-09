#include "platform/esp/arduino_common/exclusive_lora_runtime.h"

#include "board/LoraBoard.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/boards/board_runtime.h"

namespace platform::esp::arduino_common::exclusive_lora_runtime
{

LoraBoard* resolveLoraBoard()
{
    platform::esp::boards::AppContextInitHandles handles;
    if (!platform::esp::boards::tryResolveAppContextInitHandles(&handles))
    {
        return nullptr;
    }
    return handles.lora_board;
}

bool tryAcquire(Session* out_session)
{
    if (!out_session)
    {
        return false;
    }

    *out_session = {};
    out_session->lora = resolveLoraBoard();
    if (!out_session->lora)
    {
        return false;
    }

    if (!app::AppTasks::areRadioTasksPaused())
    {
        app::AppTasks::pauseRadioTasks();
        out_session->paused_radio_tasks = true;
    }

    return true;
}

Session acquire()
{
    Session session{};
    tryAcquire(&session);
    return session;
}

void release(Session* session)
{
    if (!session)
    {
        return;
    }

    if (session->paused_radio_tasks)
    {
        app::AppTasks::resumeRadioTasks();
    }

    *session = {};
}

} // namespace platform::esp::arduino_common::exclusive_lora_runtime
