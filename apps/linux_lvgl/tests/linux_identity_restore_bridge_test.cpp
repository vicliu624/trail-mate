#include "platform/linux/application_lifecycle.h"
#include "platform/linux/identity_restore_bridge.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

namespace native = platform::linux_runtime;
using Result = chat::DeviceIdentityRestoreResult;
using namespace std::chrono_literals;
template <class Predicate>
void wait(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!predicate())
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(1ms);
    }
}
struct Frontend
{
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, release = false;
    static Result apply(void* pointer, const native::IdentityRestoreCommand& command)
    {
        auto& self = *static_cast<Frontend*>(pointer);
        std::unique_lock<std::mutex> lock(self.mutex);
        self.entered = true;
        self.changed.notify_all();
        self.changed.wait(lock, [&]
                          { return self.release; });
        assert(command.size == 1 && command.body[0] == 0xa7);
        return Result::CommittedRebootRequired;
    }
};
int main()
{
    native::ApplicationLifecycleOwner lifecycle;
    native::IdentityRestoreCommand command;
    command.body[0] = 0xa7;
    command.size = 1;
    const std::filesystem::path path("/tmp/trailmate-bridge-test.db");
    assert(native::request_identity_restore(command, path) == Result::Unsupported);
    Frontend frontend;
    native::bind_identity_restore(&frontend, path);
    assert(native::identity_restore_available(path) && !native::identity_restore_available(path.string() + ".wrong"));
    auto queued = std::async(std::launch::async, [&]
                             { return native::request_identity_restore(command, path); });
    wait([]
         { return native::identity_restore_busy(); });
    native::unbind_identity_restore(&frontend);
    assert(queued.get() == Result::Unavailable && !native::identity_restore_busy());
    native::bind_identity_restore(&frontend, path);
    auto applying = std::async(std::launch::async, [&]
                               { return native::request_identity_restore(command, path); });
    wait([]
         { return native::identity_restore_busy(); });
    auto worker = std::async(std::launch::async, [&]
                             { native::service_identity_restore(&frontend, Frontend::apply); });
    {
        std::unique_lock<std::mutex> lock(frontend.mutex);
        frontend.changed.wait(lock, [&]
                              { return frontend.entered; });
    }
    command.body.fill(0); // The applying callback must retain its own command.
    auto retiring = std::async(std::launch::async, [&]
                               { native::unbind_identity_restore(&frontend); });
    assert(retiring.wait_for(20ms) == std::future_status::timeout);
    {
        std::lock_guard<std::mutex> lock(frontend.mutex);
        frontend.release = true;
        frontend.changed.notify_all();
    }
    worker.get();
    retiring.get();
    assert(applying.get() == Result::CommittedRebootRequired);
    native::bind_identity_restore(&frontend, path);
    command.body[0] = 0xa7;
    const auto start = std::chrono::steady_clock::now();
    assert(native::request_identity_restore(command, path) == Result::Unavailable);
    assert(std::chrono::steady_clock::now() - start >= 4s && !native::identity_restore_busy());
    native::service_identity_restore(&frontend, [](void*, const native::IdentityRestoreCommand&) -> Result
                                     { std::abort(); });
    native::unbind_identity_restore(&frontend);
}
