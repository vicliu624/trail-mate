#pragma once
#include "platform/ui/walkie_runtime.h"
namespace platform::linux_runtime
{
bool native_walkie_built();
bool start_native_walkie();
void stop_native_walkie();
void shutdown_native_walkie();
void native_walkie_ptt(bool pressed);
bool native_walkie_monitor(bool enabled);
::platform::ui::walkie::Status native_walkie_status();
const char* native_walkie_error();
} // namespace platform::linux_runtime
