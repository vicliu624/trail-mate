#include "platform/linux/tms_import_bridge.h"
#include "platform/linux/application_lifecycle.h"
#include <mutex>

namespace platform::linux_runtime
{
namespace
{
using Status = hostlink::management::Status;
std::mutex mutex;
void* owner = nullptr;
uint64_t generation = 0;
enum class Phase
{
    Idle,
    Requested,
    Applying,
    Complete
};
Phase phase = Phase::Idle;
TmsImportCommand command;
Status outcome = Status::Internal;
void clear_locked()
{
    ++generation;
    phase = Phase::Idle;
    command = TmsImportCommand{};
}
} // namespace
void bind_tms_import(void* value)
{
    std::lock_guard<std::mutex> lock(mutex);
    owner = value;
    clear_locked();
}
void unbind_tms_import(void* value)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (owner == value)
    {
        owner = nullptr;
        clear_locked();
    }
}
bool tms_import_available()
{
    std::lock_guard<std::mutex> lock(mutex);
    return owner && application_lifecycle_active();
}
Status request_tms_import(const TmsImportCommand& incoming)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!owner || !application_lifecycle_active()) return Status::Unsupported;
    if (incoming.document.empty() || incoming.document.size() > app::tms::kMaxDocumentBytes ||
        incoming.commit.receipt.empty() || incoming.commit.receipt.size() > app::tms::kMaxDocumentBytes + 111) return Status::InvalidField;
    if (phase == Phase::Idle)
    {
        command = incoming;
        phase = Phase::Requested;
        return Status::Busy;
    }
    if (command.id != incoming.id) return Status::Busy;
    if (command.document != incoming.document || command.commit.expected_base_hash != incoming.commit.expected_base_hash ||
        command.commit.receipt != incoming.commit.receipt) return Status::Conflict;
    if (phase != Phase::Complete) return Status::Busy;
    const auto result = outcome;
    clear_locked();
    return result;
}
void service_tms_import(void* caller, Status (*apply)(void*, const TmsImportCommand&))
{
    uint64_t work = 0;
    TmsImportCommand copy;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!caller || owner != caller || !apply || phase != Phase::Requested) return;
        copy = command;
        phase = Phase::Applying;
        work = generation;
    }
    Status result = Status::Internal;
    try
    {
        result = apply(caller, copy);
    }
    catch (...)
    {
        result = Status::Internal;
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (owner == caller && generation == work && phase == Phase::Applying)
    {
        outcome = result;
        phase = Phase::Complete;
    }
}
bool cancel_tms_import(const std::array<uint8_t, 16>& id)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (phase == Phase::Idle || command.id != id) return true;
    if (phase == Phase::Applying || (phase == Phase::Complete && outcome == Status::Ok)) return false;
    clear_locked();
    return true;
}
} // namespace platform::linux_runtime
