#include "platform/linux/track_directory_runtime.h"
#include "track_sharing/directory_list.h"
#include <algorithm>
#include <chrono>
#include <mutex>

namespace platform::linux_runtime
{
namespace
{
struct Registry
{
    std::mutex mutex;
    uint64_t owner = 0, sequence = 0;
    std::filesystem::path database;
    std::array<track_sharing::TrackDirectoryEntry, track_sharing::TrackDirectoryPool::kCapacity> entries{};
    size_t entries_count = 0;
    std::array<std::array<uint8_t, 16>, 8> requests{};
    size_t head = 0, count = 0;
    bool online = false;
    std::array<uint8_t, 16> session{};
    uint64_t consumer_generation = 1, last_used = 0;
    bool consumer_active = false;
};
// Process-lifetime metadata remains valid for late worker shutdown callbacks.
Registry& registry = *new Registry;
uint64_t clock_ms()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
void retire_consumer()
{
    registry.consumer_active = false;
    registry.head = registry.count = registry.entries_count = 0;
    registry.entries = {};
    if (++registry.consumer_generation == 0) ++registry.consumer_generation;
}
void expire_consumer(uint64_t now)
{
    // A producer may capture now just before the frontend refreshes last_used
    // under this mutex. That ordering is not a steady-clock rollback.
    if (registry.consumer_active && now >= registry.last_used && now - registry.last_used >= 30000) retire_consumer();
}
::hostlink::management::Status claim_consumer(const uint8_t* session, bool continuation)
{
    using Status = ::hostlink::management::Status;
    const auto now = clock_ms();
    expire_consumer(now);
    if (!session || !track_sharing::networkRequestId({session, 16})) return Status::InvalidField;
    if (registry.consumer_active && std::memcmp(session, registry.session.data(), 16)) return Status::Conflict;
    if (!registry.consumer_active)
    {
        if (continuation) return Status::Expired;
        retire_consumer();
        std::copy_n(session, 16, registry.session.begin());
        registry.consumer_active = true;
    }
    registry.last_used = now;
    return Status::Ok;
}
bool matches(const std::filesystem::path& database)
{
    return registry.owner && registry.database == database.lexically_normal();
}
} // namespace
TrackDirectoryPublisher::TrackDirectoryPublisher(std::filesystem::path database, bool enabled)
{
    if (!enabled || database.empty() || !database.is_absolute()) return;
    std::lock_guard<std::mutex> lock(registry.mutex);
    if (registry.owner) return; // Never steal another live device context.
    if (++registry.sequence == 0) ++registry.sequence;
    token_ = registry.owner = registry.sequence;
    retire_consumer();
    registry.database = database.lexically_normal();
    registry.online = false;
    registry.entries_count = 0;
    registry.head = registry.count = 0;
}
TrackDirectoryPublisher::~TrackDirectoryPublisher()
{
    std::lock_guard<std::mutex> lock(registry.mutex);
    if (!token_ || registry.owner != token_) return;
    registry.owner = 0;
    registry.online = false;
    registry.database.clear();
    registry.head = registry.count = 0;
    retire_consumer();
    registry.entries = {};
    registry.entries_count = 0;
}
TrackDirectoryPublisher::Consumer TrackDirectoryPublisher::consumer(uint64_t now)
{
    std::lock_guard<std::mutex> lock(registry.mutex);
    if (!token_ || registry.owner != token_) return {};
    expire_consumer(now);
    return {registry.consumer_generation, registry.consumer_active};
}
void TrackDirectoryPublisher::publish(const track_sharing::TrackDirectoryPool& pool, bool online, uint64_t consumer_generation)
{
    std::lock_guard<std::mutex> lock(registry.mutex);
    if (!token_ || registry.owner != token_) return;
    registry.online = online;
    if (!registry.consumer_active || registry.consumer_generation != consumer_generation) return;
    registry.entries = {};
    registry.entries_count = pool.size();
    for (size_t i = 0; i < pool.size(); ++i) registry.entries[i] = *pool.at(i);
    if (!online) registry.head = registry.count = 0;
}
bool TrackDirectoryPublisher::takeDiscovery(std::array<uint8_t, 16>& target)
{
    std::lock_guard<std::mutex> lock(registry.mutex);
    if (!token_ || registry.owner != token_ || !registry.online || !registry.count) return false;
    target = registry.requests[registry.head];
    registry.head = (registry.head + 1) % registry.requests.size();
    --registry.count;
    return true;
}
bool track_directories_available(const std::filesystem::path& database)
{
    std::lock_guard<std::mutex> lock(registry.mutex);
    return matches(database);
}
::hostlink::management::Status read_track_directories(const std::filesystem::path& database, const uint8_t session[16], size_t cursor, uint8_t* output, size_t capacity, size_t& written)
{
    using Status = ::hostlink::management::Status;
    written = 0;
    std::lock_guard<std::mutex> lock(registry.mutex);
    if (!matches(database)) return Status::Unsupported;
    const auto claimed = claim_consumer(session, cursor != 0);
    if (claimed != Status::Ok) return claimed;
    struct SnapshotView
    {
        size_t size() const { return registry.entries_count; }
        const track_sharing::TrackDirectoryEntry* at(size_t index) const { return index < size() ? &registry.entries[index] : nullptr; }
    } view;
    return track_sharing::encodeTrackDirectoryPageView(view, cursor, output, capacity, written) ? Status::Ok : Status::InvalidField;
}
::hostlink::management::Status request_track_directory(const std::filesystem::path& database, const uint8_t session[16], const uint8_t destination[16])
{
    using Status = ::hostlink::management::Status;
    if (!destination || !track_sharing::networkRequestId({destination, 16})) return Status::InvalidField;
    std::lock_guard<std::mutex> lock(registry.mutex);
    if (!matches(database)) return Status::Unsupported;
    const auto claimed = claim_consumer(session, false);
    if (claimed != Status::Ok) return claimed;
    if (!registry.online) return Status::Busy;
    for (size_t i = 0; i < registry.count; ++i)
        if (!std::memcmp(registry.requests[(registry.head + i) % registry.requests.size()].data(), destination, 16)) return Status::Ok;
    if (registry.count == registry.requests.size()) return Status::Busy;
    std::copy_n(destination, 16, registry.requests[(registry.head + registry.count) % registry.requests.size()].begin());
    ++registry.count;
    return Status::Ok;
}
void close_track_directories(const std::filesystem::path& database, const uint8_t* session)
{
    std::lock_guard<std::mutex> lock(registry.mutex);
    if (matches(database) && (!session || (registry.consumer_active && !std::memcmp(session, registry.session.data(), 16)))) retire_consumer();
}
::hostlink::management::Status pin_track_directory(const std::filesystem::path& database, const uint8_t session[16], geocaching::ByteView delivery,
                                                   geocaching::ByteView epoch, track_sharing::PublicationDirectory& output)
{
    using Status = ::hostlink::management::Status;
    auto* target = reinterpret_cast<uint8_t*>(&output);
    if (!session || !track_sharing::networkRequestId(delivery) || !track_sharing::networkRequestId(epoch) ||
        !track_sharing::networkInputDisjoint({session, 16}, target, sizeof(output)) || !track_sharing::networkInputDisjoint(delivery, target, sizeof(output)) ||
        !track_sharing::networkInputDisjoint(epoch, target, sizeof(output))) return Status::InvalidField;
    std::lock_guard<std::mutex> lock(registry.mutex);
    expire_consumer(clock_ms());
    if (!matches(database)) return Status::Unsupported;
    if (!registry.consumer_active) return Status::Expired;
    if (std::memcmp(session, registry.session.data(), 16)) return Status::Unauthorized;
    for (size_t i = 0; i < registry.entries_count; ++i)
    {
        const auto& entry = registry.entries[i];
        if (std::memcmp(entry.delivery, delivery.data, 16) || std::memcmp(entry.epoch, epoch.data, 16)) continue;
        if (!entry.capabilities_verified) return Status::Conflict;
        output = {};
        std::copy_n(entry.delivery, 16, output.delivery);
        std::copy_n(entry.public_key, 64, output.public_key);
        std::copy_n(entry.epoch, 16, output.epoch);
        output.file_limit = entry.file_limit;
        output.chunk_limit = entry.chunk_limit;
        output.query_limit = entry.query_limit;
        registry.last_used = clock_ms();
        return Status::Ok;
    }
    return Status::Conflict;
}
} // namespace platform::linux_runtime
