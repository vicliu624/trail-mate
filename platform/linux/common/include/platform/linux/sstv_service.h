#pragma once
#include "platform/ui/sstv_runtime.h"
namespace platform::linux_runtime
{
bool native_sstv_built();
bool start_native_sstv();
void stop_native_sstv();
void shutdown_native_sstv();
bool native_sstv_active();
::platform::ui::sstv::Status native_sstv_status();
const char* native_sstv_error();
const char* native_sstv_saved_path();
const char* native_sstv_mode();
const uint16_t* native_sstv_frame();
} // namespace platform::linux_runtime
