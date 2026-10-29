#pragma once

namespace music
{
class IService;
}
namespace platform::linux_runtime
{
// Resolves a stable proxy only; a worker is created by attach/send.
::music::IService* linux_music_service();
void prepare_linux_music();
// Owning application calls this before tearing down LVGL or SDL video.
void shutdown_linux_music();
} // namespace platform::linux_runtime
