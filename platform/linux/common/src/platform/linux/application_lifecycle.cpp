#include "platform/linux/application_lifecycle.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#if defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <sched.h>
#include <unistd.h>
#endif

namespace platform::linux_runtime
{
namespace
{
std::atomic<bool> owned{false};
enum class Intent
{
    None,
    Restart,
    Exit
};
std::atomic<Intent> intent{Intent::None};
#if defined(__linux__)
bool prepareExecDescriptors()
{
    // Some platform services have process lifetime. Isolate this thread's table
    // before marking residual descriptors, so another thread cannot race an open.
    if (::unshare(CLONE_FILES) != 0) return false;
    auto* directory = ::opendir("/proc/self/fd");
    if (!directory) return false;
    int failure = 0;
    for (;;)
    {
        errno = 0;
        const auto* entry = ::readdir(directory);
        if (!entry)
        {
            failure = errno;
            break;
        }
        char* end = nullptr;
        const auto value = std::strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end || value < 3 || value > std::numeric_limits<int>::max()) continue;
        const int descriptor = static_cast<int>(value);
        const int flags = ::fcntl(descriptor, F_GETFD);
        if (flags < 0)
        {
            if (errno != EBADF)
            {
                failure = errno;
                break;
            }
        }
        else if (::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) < 0)
        {
            failure = errno;
            break;
        }
    }
    if (::closedir(directory) != 0 && !failure) failure = errno;
    errno = failure;
    return failure == 0;
}
#endif
} // namespace
ApplicationLifecycleOwner::ApplicationLifecycleOwner()
{
    if (owned.exchange(true)) throw std::logic_error("Application lifecycle already has an owner");
    intent = Intent::None;
}
ApplicationLifecycleOwner::~ApplicationLifecycleOwner() { owned = false; }
bool application_lifecycle_active() noexcept { return owned.load(); }
bool request_application_restart() noexcept
{
    if (!owned) return false;
    auto expected = Intent::None;
    (void)intent.compare_exchange_strong(expected, Intent::Restart);
    return true;
}
bool request_application_exit() noexcept
{
    if (!owned) return false;
    intent = Intent::Exit;
    return true;
}
bool application_restart_requested() noexcept { return intent == Intent::Restart; }
int finish_application(int result, char* const argv[])
{
    if (result != 0 || !application_restart_requested()) return result;
    std::fflush(nullptr);
    if (!argv || !argv[0]) errno = EINVAL;
    else
    {
#if defined(__linux__)
        // Kernel-owned executable identity: independent of install path, argv[0]
        // spelling or shell quoting. Preserve all arguments and the environment.
        if (prepareExecDescriptors()) ::execv("/proc/self/exe", argv);
#else
        errno = ENOSYS;
#endif
    }
    const auto failure = errno;
    std::fprintf(stderr, "Application restart failed: %s\n", std::strerror(failure));
    return 1;
}
} // namespace platform::linux_runtime
