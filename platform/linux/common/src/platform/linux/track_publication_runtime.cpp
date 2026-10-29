#include "platform/linux/track_publication_runtime.h"
#include <algorithm>
#include <mutex>

namespace platform::linux_runtime
{
namespace
{
using Status = ::hostlink::management::Status;
struct State
{
    std::mutex mutex;
    std::filesystem::path database;
    uint64_t owner = 0, sequence = 0, request_sequence = 0;
    uint32_t generation = 0;
    std::array<uint8_t, 32> actor{};
    TrackPublicationRequest request;
    TrackPublicationReply reply, latest;
    bool pending = false, taken = false, replied = false, active = false;
};
State& state = *new State; // Stable during late worker teardown.
bool matches(const std::filesystem::path& database) { return state.owner && state.database == database.lexically_normal(); }
bool authenticated(const track_sharing::PublicationJournal& job)
{
    return track_sharing::publicationJournalValid(job) && job.generation == state.generation && !std::memcmp(job.author_hash, state.actor.data(), 32);
}
} // namespace
TrackPublicationPublisher::TrackPublicationPublisher(std::filesystem::path database, uint32_t generation, const uint8_t actor[32])
{
    if (database.empty() || !database.is_absolute() || !generation || !actor) return;
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.owner) return;
    if (++state.sequence == 0) ++state.sequence;
    token_ = state.owner = state.sequence;
    state.database = database.lexically_normal();
    state.generation = generation;
    std::copy_n(actor, 32, state.actor.begin());
    state.request = {};
    state.reply = {};
    state.latest = {};
    state.pending = state.taken = state.replied = state.active = false;
}
TrackPublicationPublisher::~TrackPublicationPublisher()
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!token_ || state.owner != token_) return;
    state.owner = 0;
    state.database.clear();
    state.generation = 0;
    state.request = {};
    state.reply = {};
    state.latest = {};
    state.pending = state.taken = state.replied = state.active = false;
}
bool TrackPublicationPublisher::take(TrackPublicationRequest& request)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!token_ || state.owner != token_ || !state.pending || state.taken) return false;
    state.taken = true;
    request = state.request;
    return true;
}
bool TrackPublicationPublisher::current(uint64_t serial) const
{
    std::lock_guard<std::mutex> lock(state.mutex);
    return token_ && state.owner == token_ && state.pending && state.request.serial == serial;
}
void TrackPublicationPublisher::complete(uint64_t serial, Status status, const track_sharing::PublicationJournal* job, bool active)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!token_ || state.owner != token_ || !state.pending || state.request.serial != serial) return;
    state.reply = {};
    state.reply.status = status;
    if (status == Status::Ok)
    {
        if (!job || !authenticated(*job)) state.reply.status = Status::Internal;
        else
        {
            state.reply.job = *job;
            state.reply.valid = true;
            state.reply.active = active;
            state.latest = state.reply;
        }
    }
    state.pending = false;
    state.replied = true;
    state.active = active;
}
void TrackPublicationPublisher::update(const track_sharing::PublicationJournal& job, bool active)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!token_ || state.owner != token_ || !authenticated(job)) return;
    state.latest = {};
    state.latest.status = Status::Ok;
    state.latest.job = job;
    state.latest.valid = true;
    state.latest.active = state.active = active;
}
void TrackPublicationPublisher::clearActive()
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!token_ || state.owner != token_) return;
    state.active = false;
    state.latest.active = false;
}
void TrackPublicationPublisher::invalidate()
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!token_ || state.owner != token_) return;
    state.latest = {};
    state.reply = {};
    state.replied = false;
    state.active = false;
}
bool track_publication_available(const std::filesystem::path& database)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    return matches(database);
}
bool track_publication_busy(const std::filesystem::path& database)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    return matches(database) && (state.pending || state.active);
}
Status request_track_publication(const std::filesystem::path& database, const ::hostlink::management::EnvelopeView& envelope,
                                 geocaching::ByteView operation, uint32_t generation, TrackPublicationReply& reply)
{
    reply = {};
    if (!envelope.session || !envelope.body || !envelope.body_size || envelope.body_size > 160 || !operation.data || operation.size != 16 || !generation) return Status::InvalidField;
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!matches(database)) return Status::Unsupported;
    if (generation != state.generation) return Status::StaleIdentity;
    if (envelope.code == uint16_t(::hostlink::management::Operation::JobGet) && state.latest.valid && !std::memcmp(state.latest.job.operation, operation.data, 16))
    {
        reply = state.latest;
        return Status::Ok;
    }
    const bool same = state.request.code == envelope.code && state.request.bytes == envelope.body_size && !std::memcmp(state.request.session.data(), envelope.session, 16) &&
                      !std::memcmp(state.request.body.data(), envelope.body, envelope.body_size);
    if (same && state.replied)
    {
        reply = state.reply;
        if (reply.status != Status::Ok) state.replied = false;
        return reply.status;
    }
    if (state.pending) return Status::Busy;
    if (++state.request_sequence == 0) ++state.request_sequence;
    state.request = {};
    state.request.serial = state.request_sequence;
    state.request.code = envelope.code;
    state.request.bytes = envelope.body_size;
    std::copy_n(envelope.session, 16, state.request.session.begin());
    std::copy_n(envelope.body, envelope.body_size, state.request.body.begin());
    state.pending = true;
    state.taken = state.replied = false;
    return Status::Busy;
}
void close_track_publication(const std::filesystem::path& database, const uint8_t* session)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!matches(database) || (session && std::memcmp(state.request.session.data(), session, 16))) return;
    // An admitted/taken business request owns its bytes and survives the
    // transport. Only work that no protocol worker has taken is retired here.
    if (!state.taken || state.request.code != uint16_t(::hostlink::management::Operation::TrackCommit))
    {
        state.pending = state.replied = false;
        state.request = {};
        state.reply = {};
    }
}
} // namespace platform::linux_runtime
