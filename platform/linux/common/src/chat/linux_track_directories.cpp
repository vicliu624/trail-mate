#include "chat/linux_track_directories.h"
#include "platform/linux/track_directory_runtime.h"
#include "track_sharing/directory_pool.h"
#include <algorithm>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/rand.h>
#endif

namespace trailmate::linux_app
{
struct LinuxTrackDirectories::Impl
{
    Impl(std::filesystem::path database, const platform::linux_runtime::LinuxReticulumIdentity& identity, Transport transport)
        : identity(identity), transport(std::move(transport)), database(std::move(database)) {}
    bool current() const { return publisher && publisher->valid() && identity.ready() && identity.generation() == generation; }
    void reset()
    {
        entries = {};
        pool = track_sharing::TrackDirectoryPool(entries.data(), entries.size());
        retry_after = {};
        pending = false;
        next_index = 0;
        accepted_count = accepted_cursor = 0;
    }
    const platform::linux_runtime::LinuxReticulumIdentity& identity;
    Transport transport;
    std::filesystem::path database;
    std::unique_ptr<platform::linux_runtime::TrackDirectoryPublisher> publisher;
    uint32_t generation = 0;
    uint64_t register_after = 0;
    std::array<track_sharing::TrackDirectoryEntry, track_sharing::TrackDirectoryPool::kCapacity> entries{};
    track_sharing::TrackDirectoryPool pool{entries.data(), entries.size()};
    std::array<uint64_t, track_sharing::TrackDirectoryPool::kCapacity> retry_after{};
    std::array<uint8_t, 16> request{}, delivery{}, epoch{};
    std::array<std::array<uint8_t, 32>, 8> accepted{};
    size_t accepted_count = 0, accepted_cursor = 0, selected = 0, next_index = 0;
    uint64_t deadline = 0, context_generation = 0, consumer_generation = 0;
    bool enabled = false, pending = false;
};
LinuxTrackDirectories::LinuxTrackDirectories(std::filesystem::path database, const platform::linux_runtime::LinuxReticulumIdentity& identity, Transport transport)
    : impl_(std::make_unique<Impl>(std::move(database), identity, std::move(transport))) {}
LinuxTrackDirectories::~LinuxTrackDirectories() = default;
bool LinuxTrackDirectories::observe(const chat::lxmf::TrackDirectoryAnnouncementView& view, uint64_t now)
{
    auto& s = *impl_;
    if (!s.enabled || !s.current()) return false;
    const bool accepted = s.pool.observe(view, now);
    if (accepted && s.pending && !s.pool.find({s.delivery.data(), 16}, {s.epoch.data(), 16}))
    {
        s.pending = false;
        s.retry_after[s.selected] = 0;
    }
    return accepted;
}
bool LinuxTrackDirectories::receive(chat::lxmf::ByteSpan type, const chat::lxmf::CustomDeliveryView& view, uint64_t now)
{
    auto& s = *impl_;
    if (!s.enabled || !s.current() || s.publisher->consumer(now).generation != s.consumer_generation || !chat::lxmf::isTrackCustomType(type) || !view.source.data || view.source.size != 16 ||
        !view.destination.data || view.destination.size != 16 || !view.message_hash.data || view.message_hash.size != 32 ||
        !view.data.data || !view.data.size || view.data.size > 8192 ||
        std::memcmp(view.destination.data, s.identity.destinationHash().data(), 16)) return false;
    for (size_t i = 0; i < s.accepted_count; ++i)
        if (!std::memcmp(view.message_hash.data, s.accepted[i].data(), 32)) return true;
    if (!s.pending || now > s.deadline || std::memcmp(view.source.data, s.delivery.data(), 16) ||
        !s.pool.confirmCapabilities({s.delivery.data(), 16}, {s.epoch.data(), 16}, {view.data.data, view.data.size}, {s.request.data(), 16})) return false;
    // The complete control result is now copied into the pool. Remember its
    // authenticated message hash so a lost transport proof remains retryable.
    std::copy_n(view.message_hash.data, 32, s.accepted[s.accepted_cursor].begin());
    s.accepted_cursor = (s.accepted_cursor + 1) % s.accepted.size();
    s.accepted_count = std::min(s.accepted_count + 1, s.accepted.size());
    s.pending = false;
    return true;
}
void LinuxTrackDirectories::tick(uint64_t now, bool enabled, bool online, uint64_t context_generation)
{
    auto& s = *impl_;
    if (!s.identity.ready() || (s.publisher && s.generation != s.identity.generation()))
    {
        s.reset();
        s.publisher.reset();
        s.generation = 0;
        if (!s.identity.ready()) return;
    }
    if (!s.publisher || !s.publisher->valid())
    {
        if (now < s.register_after) return;
        s.register_after = now + 1000;
        s.publisher = std::make_unique<platform::linux_runtime::TrackDirectoryPublisher>(s.database);
        if (!s.publisher->valid()) return;
        s.generation = s.identity.generation();
    }
    const auto consumer = s.publisher->consumer(now);
    const bool active = enabled && s.current() && consumer.active;
    if (s.context_generation != context_generation || s.consumer_generation != consumer.generation || (s.enabled && !active)) s.reset();
    s.consumer_generation = consumer.generation;
    s.context_generation = context_generation;
    s.enabled = active;
    s.publisher->publish(s.pool, enabled && s.current() && online, consumer.generation);
    if (!active || !online) return;
    std::array<uint8_t, 16> discover{};
    if (s.publisher->takeDiscovery(discover) && s.transport.request_path) (void)s.transport.request_path(discover.data());
    if (s.pending && now >= s.deadline)
    {
        s.pending = false;
        s.retry_after[s.selected] = now + 30000;
    }
    if (s.pending || !s.transport.send || !s.transport.next_message) return;
    const track_sharing::TrackDirectoryEntry* chosen = nullptr;
    for (size_t seen = 0; seen < s.pool.size(); ++seen)
    {
        const size_t index = s.next_index++ % s.pool.size();
        const auto* entry = s.pool.at(index);
        if (entry->capabilities_verified || now < s.retry_after[index]) continue;
        chosen = entry;
        s.selected = index;
        break;
    }
    if (!chosen) return;
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (RAND_bytes(s.request.data(), s.request.size()) != 1 || !track_sharing::networkRequestId({s.request.data(), s.request.size()})) return;
#else
    return;
#endif
    std::copy_n(chosen->delivery, 16, s.delivery.begin());
    std::copy_n(chosen->epoch, 16, s.epoch.begin());
    std::array<uint8_t, 128> packet{};
    geocaching::protocol::CmpWriter writer(packet.data(), packet.size());
    if (!track_sharing::networkRequestHeader(writer, 0, {s.request.data(), 16}, 8192, 0)) return;
    const auto id = s.transport.next_message();
    if (!id) return;
    s.pending = true;
    s.deadline = now + 15000;
    if (!s.transport.send(chosen->delivery, chosen->public_key, {packet.data(), writer.size()}, id))
    {
        s.pending = false;
        s.retry_after[s.selected] = now + 3000;
    }
}
} // namespace trailmate::linux_app
