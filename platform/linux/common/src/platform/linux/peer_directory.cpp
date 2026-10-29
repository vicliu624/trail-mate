#include "platform/linux/peer_directory.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace platform::linux_runtime
{
namespace
{
using Status = ::chat::MeshPeerDirectoryStatus;
using Code = ::chat::MeshPeerDirectoryStatusCode;
::chat::MeshPeerDirectoryCore::Options options()
{
    ::chat::MeshPeerDirectoryCore::Options value;
    value.auto_save = false;
    return value;
}
bool atLeast(uint32_t value, uint32_t target) { return static_cast<int32_t>(value - target) >= 0; }
} // namespace
struct LinuxPeerDirectory::Impl
{
    ::chat::IMeshPeerDirectoryBlobStore& blob;
    ::chat::MeshPeerDirectoryCore core;
    mutable std::recursive_mutex mutex;
    std::condition_variable_any wake;
    std::atomic<bool> ready{false};
    bool initial_done = false, stopping = false, stopped = false;
    bool flush_requested = false, attempted = false, committed = false;
    uint32_t last_attempt = 0, last_commit = 0;
    uint64_t attempt_count = 0;
    Status load_status = Status::fail(Code::StorageUnavailable);
    unsigned visiting = 0;
    std::thread::id visitor_thread;
    std::thread worker;
    explicit Impl(::chat::IMeshPeerDirectoryBlobStore& value) : blob(value), core(blob, options()), worker([this]
                                                                                                           { run(); }) {}
    ~Impl() { stop(); }
    Status unavailable() const { return Status::fail(Code::StorageUnavailable); }
    template <class Operation>
    Status read(Operation operation)
    {
        if (!ready.load()) return unavailable();
        std::lock_guard<std::recursive_mutex> lock(mutex);
        return !stopped ? operation() : unavailable();
    }
    template <class Operation>
    Status mutate(Operation operation)
    {
        if (!ready.load()) return unavailable();
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (stopping || (visiting && visitor_thread == std::this_thread::get_id())) return Status::fail(Code::Busy);
        const auto result = operation();
        if (result.succeeded()) wake.notify_all();
        return result;
    }
    void stop()
    {
        {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            stopping = true;
            wake.notify_all();
        }
        if (worker.joinable()) worker.join();
    }
    void run() noexcept
    {
        try
        {
            std::vector<uint8_t> loaded;
            const auto read = blob.loadBlob(loaded);
            load_status = core.beginEmpty();
            if (read == ::chat::MeshPeerDirectoryBlobLoadResult::Loaded)
                load_status = !loaded.empty() ? core.hydratePersistenceBlob(loaded.data(), loaded.size()) : Status::fail(Code::IoError);
            else if (read != ::chat::MeshPeerDirectoryBlobLoadResult::Missing) load_status = Status::fail(Code::IoError);
            std::vector<uint8_t>().swap(loaded);
            {
                std::lock_guard<std::recursive_mutex> lock(mutex);
                initial_done = true;
                ready = load_status.succeeded();
                wake.notify_all();
            }
            std::vector<uint8_t> snapshot;
            for (;;)
            {
                uint32_t revision = 0;
                size_t size = 0;
                {
                    std::unique_lock<std::recursive_mutex> lock(mutex);
                    wake.wait_for(lock, std::chrono::milliseconds(250), [&]
                                  { return stopping || flush_requested || (ready && core.persistencePending()); });
                    if (stopping && (!ready || !core.persistencePending())) break;
                    if (!ready || !core.persistencePending())
                    {
                        flush_requested = false;
                        continue;
                    }
                    size = core.persistenceSnapshotSize();
                    flush_requested = false;
                }
                snapshot.resize(size);
                {
                    std::lock_guard<std::recursive_mutex> lock(mutex);
                    if (snapshot.size() < core.persistenceSnapshotSize()) continue;
                    size = core.persistenceSnapshotSize();
                    if (!core.encodePersistenceSnapshot(snapshot.data(), size, &revision)) continue;
                }
                bool saved = false;
                try
                {
                    saved = blob.saveBlob(snapshot.data(), size);
                }
                catch (...)
                {
                }
                {
                    std::unique_lock<std::recursive_mutex> lock(mutex);
                    attempted = true;
                    ++attempt_count;
                    last_attempt = revision;
                    if (saved)
                    {
                        committed = true;
                        last_commit = revision;
                        (void)core.acknowledgePersistedRevision(revision);
                    }
                    wake.notify_all();
                    if (stopping && (!saved || !core.persistencePending())) break;
                    if (!saved) wake.wait_for(lock, std::chrono::milliseconds(250), [&]
                                              { return stopping || flush_requested; });
                }
            }
        }
        catch (...)
        {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            load_status = Status::fail(Code::IoError);
            initial_done = true;
        }
        std::lock_guard<std::recursive_mutex> lock(mutex);
        stopped = true;
        ready = false;
        wake.notify_all();
    }
};
LinuxPeerDirectory::LinuxPeerDirectory(::chat::IMeshPeerDirectoryBlobStore& blob) : impl_(std::make_unique<Impl>(blob)) {}
LinuxPeerDirectory::~LinuxPeerDirectory() = default;
void LinuxPeerDirectory::shutdown() { impl_->stop(); }
Status LinuxPeerDirectory::begin()
{
    std::unique_lock<std::recursive_mutex> lock(impl_->mutex);
    impl_->wake.wait(lock, [&]
                     { return impl_->initial_done; });
    return impl_->stopped ? Status::fail(Code::IoError) : impl_->load_status;
}
Status LinuxPeerDirectory::record(const ::chat::MeshPeerRecord& value)
{
    return impl_->mutate([&]
                         { return impl_->core.record(value); });
}
Status LinuxPeerDirectory::find(const ::chat::MeshPeerIdentity& id, ::chat::MeshPeerRecord& out)
{
    out = {};
    return impl_->read([&]
                       { return impl_->core.find(id, out); });
}
Status LinuxPeerDirectory::findByNodeId(::chat::MeshProtocol protocol, ::chat::NodeId id, ::chat::MeshPeerRecord& out)
{
    out = {};
    return impl_->read([&]
                       { return impl_->core.findByNodeId(protocol, id, out); });
}
Status LinuxPeerDirectory::loadRecent(::chat::MeshProtocol protocol, ::chat::MeshPeerRecord* out, size_t size, size_t* count)
{
    if (count) *count = 0;
    return impl_->read([&]
                       { return impl_->core.loadRecent(protocol, out, size, count); });
}
Status LinuxPeerDirectory::search(::chat::MeshProtocol protocol, const char* text, ::chat::MeshPeerRecord* out, size_t size, size_t* count)
{
    if (count) *count = 0;
    return impl_->read([&]
                       { return impl_->core.search(protocol, text, out, size, count); });
}
Status LinuxPeerDirectory::visit(::chat::MeshProtocol protocol, ::chat::MeshPeerDirectoryView view, ::chat::IMeshPeerDirectoryVisitor& visitor)
{
    return impl_->read([&]
                       {
        struct Guard
        {
            Impl& owner;
            explicit Guard(Impl& value) : owner(value) { if (!owner.visiting++) owner.visitor_thread = std::this_thread::get_id(); }
            ~Guard() { --owner.visiting; }
        } guard(*impl_);
        return impl_->core.visit(protocol, view, visitor); });
}
Status LinuxPeerDirectory::setUserAlias(const ::chat::MeshPeerIdentity& id, const char* alias)
{
    const auto result = impl_->mutate([&]
                                      { return impl_->core.setUserAlias(id, alias); });
    return result.succeeded() ? flush() : result;
}
Status LinuxPeerDirectory::setUserFlags(const ::chat::MeshPeerIdentity& id, const ::chat::MeshPeerUserFlags& flags)
{
    const auto result = impl_->mutate([&]
                                      { return impl_->core.setUserFlags(id, flags); });
    return result.succeeded() ? flush() : result;
}
Status LinuxPeerDirectory::setKeyManuallyVerified(const ::chat::MeshPeerIdentity& id, bool value)
{
    const auto result = impl_->mutate([&]
                                      { return impl_->core.setKeyManuallyVerified(id, value); });
    return result.succeeded() ? flush() : result;
}
Status LinuxPeerDirectory::remove(const ::chat::MeshPeerIdentity& id)
{
    const auto result = impl_->mutate([&]
                                      { return impl_->core.remove(id); });
    return result.succeeded() ? flush() : result;
}
Status LinuxPeerDirectory::clearProtocol(::chat::MeshProtocol protocol)
{
    const auto result = impl_->mutate([&]
                                      { return impl_->core.clearProtocol(protocol); });
    return result.succeeded() ? flush() : result;
}
::chat::MeshPeerDirectoryCapacity LinuxPeerDirectory::capacityFor(::chat::MeshProtocol protocol) const { return impl_->core.capacityFor(protocol); }
Status LinuxPeerDirectory::flush()
{
    std::unique_lock<std::recursive_mutex> lock(impl_->mutex);
    if (!impl_->ready || impl_->stopped) return Status::fail(Code::StorageUnavailable);
    if (impl_->visiting && impl_->visitor_thread == std::this_thread::get_id()) return Status::fail(Code::Busy);
    if (!impl_->core.persistencePending()) return Status::success();
    const auto target = impl_->core.persistenceRevision();
    const auto attempts = impl_->attempt_count;
    impl_->flush_requested = true;
    impl_->wake.notify_all();
    impl_->wake.wait(lock, [&]
                     { return impl_->stopped || (impl_->committed && atLeast(impl_->last_commit, target)) ||
                              (impl_->attempt_count != attempts && impl_->attempted && atLeast(impl_->last_attempt, target)); });
    return impl_->committed && atLeast(impl_->last_commit, target) ? Status::success() : Status::fail(Code::IoError);
}
} // namespace platform::linux_runtime
