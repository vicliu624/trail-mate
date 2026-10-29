#pragma once
#include <utility>

namespace platform::linux_runtime
{
class ApplicationLifecycleOwner
{
  public:
    ApplicationLifecycleOwner();
    ~ApplicationLifecycleOwner();
    ApplicationLifecycleOwner(const ApplicationLifecycleOwner&) = delete;
    ApplicationLifecycleOwner& operator=(const ApplicationLifecycleOwner&) = delete;
};
// Thread-safe and coalesced. False means the composition has no lifecycle owner.
bool request_application_restart() noexcept;
bool application_lifecycle_active() noexcept;
// A window close/Alt+F4 takes precedence over an overlapping restart request.
bool request_application_exit() noexcept;
bool application_restart_requested() noexcept;
int finish_application(int result, char* const argv[]);

// Run-local resources must be destroyed before exec replaces the process image.
template <typename Run>
int run_application(char* const argv[], Run&& run)
{
    int result;
    {
        ApplicationLifecycleOwner owner;
        result = std::forward<Run>(run)();
    }
    return finish_application(result, argv);
}
} // namespace platform::linux_runtime
