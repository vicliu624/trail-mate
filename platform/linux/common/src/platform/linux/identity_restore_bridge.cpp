#include "platform/linux/identity_restore_bridge.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "hostlink/identity_restore_request.h"
#include "hostlink/radio_identity_backup.h"
#include "platform/linux/application_lifecycle.h"
#include "platform/linux/mesh/linux_radio_identity_store.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>

namespace platform::linux_runtime
{
namespace
{
using Result = chat::DeviceIdentityRestoreResult;
enum class Phase
{
    Requested,
    Applying,
    Complete
};
struct Ticket
{
    IdentityRestoreCommand command;
    Phase phase = Phase::Requested;
    Result result = Result::Unavailable;
};
std::mutex mutex;
std::condition_variable changed;
void* owner = nullptr;
std::filesystem::path database;
std::shared_ptr<Ticket> pending;
void cancel_requested()
{
    if (pending && pending->phase == Phase::Requested)
    {
        pending->phase = Phase::Complete;
        pending->result = Result::Unavailable;
    }
    pending.reset();
    changed.notify_all();
}
} // namespace
hostlink::management::Status validate_identity_restore(const uint8_t* bytes, size_t size)
{
    namespace mgmt = hostlink::management;
    mgmt::IdentityRestoreView request;
    const auto parsed = mgmt::parseIdentityRestoreRequest({bytes, size}, request);
    if (parsed != mgmt::Status::Ok) return parsed;
    if (request.expected_generation == UINT32_MAX) return mgmt::Status::InvalidField;
    std::array<uint8_t, 352> scratch{};
    if (!mgmt::verifyIdentityRestoreSignature(request, scratch.data(), scratch.size()) ||
        !LinuxReticulumIdentity::validateKeyPair(request.format, request.private_key.data, request.private_key.size, request.public_key.data)) return mgmt::Status::InvalidField;
    std::array<uint8_t, 16> identity{}, delivery{};
    std::array<uint8_t, chat::reticulum::kNameHashSize> name{};
    chat::reticulum::computeIdentityHash(request.public_key.data, identity.data());
    chat::reticulum::computeNameHash("lxmf", "delivery", name.data());
    chat::reticulum::computeDestinationHash(name.data(), identity.data(), delivery.data());
    return std::memcmp(identity.data(), request.identity_hash.data, identity.size()) || std::memcmp(delivery.data(), request.delivery.data, delivery.size())
               ? mgmt::Status::InvalidField
               : mgmt::Status::Ok;
}
hostlink::management::Status validate_radio_identity_restore(const uint8_t* bytes, size_t size)
{
    namespace mgmt = hostlink::management;
    mgmt::RadioIdentityRestoreView request;
    const auto parsed = mgmt::parseRadioIdentityRestoreRequest({bytes, size}, request);
    if (parsed != mgmt::Status::Ok) return parsed;
    std::array<uint8_t, 336> scratch{};
    return mgmt::verifyRadioIdentityRestoreSignature(request, scratch.data(), scratch.size()) &&
                   LinuxRadioIdentityStore::validatePair(request.kind, request.public_key.data, request.private_key.data, request.private_key.size, true)
               ? mgmt::Status::Ok
               : mgmt::Status::InvalidField;
}
void bind_identity_restore(void* value, const std::filesystem::path& path)
{
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait(lock, []
                 { return !pending || pending->phase != Phase::Applying; });
    cancel_requested();
    owner = value;
    database = path.lexically_normal();
}
void unbind_identity_restore(void* value)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (owner != value) return;
    changed.wait(lock, []
                 { return !pending || pending->phase != Phase::Applying; });
    owner = nullptr;
    database.clear();
    cancel_requested();
}
bool identity_restore_available(const std::filesystem::path& path)
{
    std::lock_guard<std::mutex> lock(mutex);
    return owner && database == path.lexically_normal() && application_lifecycle_active() && !application_restart_requested();
}
Result request_identity_restore(const IdentityRestoreCommand& incoming, const std::filesystem::path& path)
{
    if (!incoming.size || incoming.size > incoming.body.size() || (incoming.target != IdentityRestoreTarget::DeviceAuthor && incoming.target != IdentityRestoreTarget::Radio)) return Result::InvalidInput;
    std::unique_lock<std::mutex> lock(mutex);
    if (!owner || database != path.lexically_normal() || !application_lifecycle_active() || application_restart_requested()) return Result::Unsupported;
    if (pending) return Result::Unavailable;
    auto ticket = std::make_shared<Ticket>();
    ticket->command = incoming;
    pending = ticket;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (ticket->phase != Phase::Complete)
    {
        if (ticket->phase == Phase::Applying) changed.wait(lock, [&]
                                                           { return ticket->phase == Phase::Complete; });
        else if (changed.wait_until(lock, deadline) == std::cv_status::timeout && ticket->phase == Phase::Requested)
        {
            ticket->phase = Phase::Complete;
            ticket->result = Result::Unavailable;
            if (pending == ticket) pending.reset();
        }
    }
    if (pending == ticket) pending.reset();
    changed.notify_all();
    return ticket->result;
}
bool identity_restore_busy()
{
    std::lock_guard<std::mutex> lock(mutex);
    return bool(pending);
}
void service_identity_restore(void* caller, Result (*apply)(void*, const IdentityRestoreCommand&))
{
    std::shared_ptr<Ticket> ticket;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!caller || owner != caller || !apply || !pending || pending->phase != Phase::Requested) return;
        if (!application_lifecycle_active() || application_restart_requested())
        {
            cancel_requested();
            return;
        }
        ticket = pending;
        ticket->phase = Phase::Applying;
        changed.notify_all();
    }
    Result result = Result::StorageError;
    try
    {
        result = apply(caller, ticket->command);
    }
    catch (...)
    {
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        ticket->result = result;
        ticket->phase = Phase::Complete;
        changed.notify_all();
    }
}
} // namespace platform::linux_runtime
