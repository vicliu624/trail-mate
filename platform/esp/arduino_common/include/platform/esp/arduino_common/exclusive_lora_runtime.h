#pragma once

class LoraBoard;

namespace platform::esp::arduino_common::exclusive_lora_runtime
{

struct Session
{
    LoraBoard* lora = nullptr;
    bool paused_radio_tasks = false;
};

bool tryAcquire(Session* out_session);
Session acquire();
LoraBoard* resolveLoraBoard();
void release(Session* session);

} // namespace platform::esp::arduino_common::exclusive_lora_runtime
