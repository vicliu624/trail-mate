#include "platform/linux/tms_snapshot_bridge.h"
#include "app/tms_config_codec.h"
#include <cstdint>
#include <mutex>

namespace platform::linux_runtime
{
namespace
{
std::mutex mutex;
void* owner = nullptr;
uint64_t generation = 0;
enum class Phase
{
    Idle,
    Requested,
    Reading,
    Ready,
    Failed
};
Phase phase = Phase::Idle;
std::string document;
void clear_locked()
{
    ++generation;
    phase = Phase::Idle;
    document.clear();
}
} // namespace
void bind_tms_snapshot(void* next)
{
    std::lock_guard<std::mutex> lock(mutex);
    owner = next;
    clear_locked();
}
void unbind_tms_snapshot(void* previous)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (owner == previous)
    {
        owner = nullptr;
        clear_locked();
    }
}
bool tms_snapshot_available()
{
    std::lock_guard<std::mutex> lock(mutex);
    return owner != nullptr;
}
TmsSnapshotRead request_tms_snapshot(std::string& output)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!owner) return TmsSnapshotRead::Unavailable;
    if (phase == Phase::Idle)
    {
        phase = Phase::Requested;
        return TmsSnapshotRead::Busy;
    }
    if (phase == Phase::Ready)
    {
        output = std::move(document);
        phase = Phase::Idle;
        return TmsSnapshotRead::Ready;
    }
    if (phase == Phase::Failed)
    {
        phase = Phase::Idle;
        return TmsSnapshotRead::Failed;
    }
    return TmsSnapshotRead::Busy;
}
void cancel_tms_snapshot()
{
    std::lock_guard<std::mutex> lock(mutex);
    clear_locked();
}
void service_tms_snapshot(void* caller, bool (*read)(void*, std::string&))
{
    uint64_t work = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!caller || caller != owner || !read || phase != Phase::Requested) return;
        phase = Phase::Reading;
        work = generation;
    }
    std::string result;
    bool ok = false;
    try
    {
        ok = read(caller, result) && !result.empty() && result.size() <= app::tms::kMaxDocumentBytes;
    }
    catch (...)
    {
        ok = false;
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (owner != caller || generation != work || phase != Phase::Reading) return;
    if (ok) document = std::move(result);
    phase = ok ? Phase::Ready : Phase::Failed;
}
} // namespace platform::linux_runtime
