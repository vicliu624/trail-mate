#include "platform/ui/music_runtime.h"
#include "platform/linux/music_service.h"
namespace platform::ui
{
::music::IService* music_service()
{
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
    return ::platform::linux_runtime::linux_music_service();
#else
    return nullptr;
#endif
}
} // namespace platform::ui
#if !defined(TRAIL_MATE_LINUX_HAS_MEDIA)
namespace platform::linux_runtime
{
::music::IService* linux_music_service() { return nullptr; }
void prepare_linux_music() {}
void shutdown_linux_music() {}
} // namespace platform::linux_runtime
#endif
