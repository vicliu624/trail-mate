#include "platform/linux/track_read_runtime.h"
#include "track_sharing/read_usb_request.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace platform::linux_runtime
{
namespace
{
using Status = ::hostlink::management::Status;
using Operation = ::hostlink::management::Operation;
struct State
{
    std::mutex mutex;
    std::condition_variable changed;
    std::filesystem::path database;
    uint64_t owner = 0, sequence = 0, commands = 0;
    uint32_t generation = 0;
    TrackReadCommand command;
    std::array<uint8_t, 448> reply{};
    size_t bytes = 0;
    Status status = Status::Busy;
    std::array<uint8_t, 16> session{};
    bool active = false, pending = false, taken = false, replied = false, retiring = false;
};
State& state = *new State;
bool matches(const std::filesystem::path& database) { return state.owner && state.database == database.lexically_normal(); }
} // namespace
TrackReadPublisher::TrackReadPublisher(std::filesystem::path database, uint32_t generation)
{
    if (database.empty() || !database.is_absolute() || !generation) return;
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.owner) return;
    if (++state.sequence == 0) ++state.sequence;
    token_ = state.owner = state.sequence;
    state.database = database.lexically_normal();
    state.generation = generation;
    state.pending = state.taken = state.replied = state.active = state.retiring = false;
}
TrackReadPublisher::~TrackReadPublisher()
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!token_ || state.owner != token_) return;
    state.owner = 0;
    state.database.clear();
    state.pending = state.taken = state.replied = state.active = state.retiring = false;
    state.changed.notify_all();
}
bool TrackReadPublisher::take(TrackReadCommand& output)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!token_ || state.owner != token_ || !state.pending || state.taken) return false;
    state.taken = true;
    output = state.command;
    return true;
}
bool TrackReadPublisher::current(uint64_t serial) const
{
    std::lock_guard<std::mutex> lock(state.mutex);
    return token_ && state.owner == token_ && state.pending && state.command.serial == serial;
}
void TrackReadPublisher::complete(uint64_t serial, Status status, const uint8_t* output, size_t bytes)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!token_ || state.owner != token_ || !state.pending || state.command.serial != serial) return;
    state.bytes = 0;
    state.status = status;
    if (status == Status::Ok)
    {
        if (!output || !bytes || bytes > state.reply.size()) state.status = Status::Internal;
        else
        {
            std::copy_n(output, bytes, state.reply.begin());
            state.bytes = bytes;
        }
    }
    state.replied = true;
    state.changed.notify_all();
}
void TrackReadPublisher::active(const uint8_t* session)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!token_ || state.owner != token_) return;
    state.active = session != nullptr;
    if (session) std::copy_n(session, 16, state.session.begin());
}
bool TrackReadPublisher::retiring()
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!token_ || state.owner != token_) return true;
    const bool value = state.retiring;
    state.retiring = false;
    return value;
}
bool HostlinkTrackRead::supported() const
{
    std::lock_guard<std::mutex> lock(state.mutex);
    return matches(database_);
}
bool HostlinkTrackRead::identityBusy() const
{
    std::lock_guard<std::mutex> lock(state.mutex);
    return matches(database_) && (state.active || state.pending);
}
void HostlinkTrackRead::close(const uint8_t* session) const
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!matches(database_)) return;
    if (!session || (state.active && !std::memcmp(state.session.data(), session, 16))) state.retiring = true;
    if (!session || (state.pending && !std::memcmp(state.command.session.data(), session, 16))) state.pending = state.taken = state.replied = false;
    state.changed.notify_all();
}
Status HostlinkTrackRead::process(const ::hostlink::management::EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written) const
{
    written = 0;
    if (!request.session || !track_sharing::networkRequestId({request.session, 16}) || !request.body || !request.body_size || request.body_size > 471 || !output || capacity < 448) return Status::InvalidField;
    uint32_t generation = 0;
    if (request.code == uint16_t(Operation::TrackReadBegin))
    {
        track_sharing::ReadUsbRequest decoded;
        if (!track_sharing::decodeReadUsbRequest({request.body, request.body_size}, decoded)) return Status::InvalidField;
        generation = decoded.generation;
    }
    else
    {
        const bool chunk = request.code == uint16_t(Operation::TrackReadChunk);
        if (!chunk && request.code != uint16_t(Operation::TrackReadStatus) && request.code != uint16_t(Operation::TrackReadRelease)) return Status::Unsupported;
        geocaching::protocol::CmpReader reader({request.body, request.body_size});
        geocaching::ByteView id;
        size_t fields = 0;
        uint64_t offset = 0, count = 0;
        if (!reader.array(fields, 3) || fields != (chunk ? 3u : 1u) || !reader.binary(id, 16) || !track_sharing::networkRequestId(id) ||
            (chunk && (!reader.unsignedInteger(offset) || offset > 8191 || !reader.unsignedInteger(count) || !count || count > 320)) || !reader.finished()) return Status::InvalidField;
    }
    std::unique_lock<std::mutex> lock(state.mutex);
    if (!matches(database_)) return Status::Unsupported;
    if (generation && generation != state.generation) return Status::StaleIdentity;
    if (state.pending && (state.command.code != request.code || state.command.bytes != request.body_size || std::memcmp(state.command.session.data(), request.session, 16) || std::memcmp(state.command.body.data(), request.body, request.body_size))) return Status::Busy;
    if (!state.pending)
    {
        if (++state.commands == 0) ++state.commands;
        state.command = {};
        state.command.serial = state.commands;
        state.command.code = request.code;
        state.command.bytes = request.body_size;
        std::copy_n(request.session, 16, state.command.session.begin());
        std::copy_n(request.body, request.body_size, state.command.body.begin());
        state.pending = true;
        state.taken = state.replied = false;
    }
    const auto owner = state.owner, serial = state.command.serial;
    // Dedicated HostLink worker only; no UI/router lock is held while waiting.
    // A chunk normally completes in one mesh-worker visit, without a Busy ACK.
    state.changed.wait_for(lock, std::chrono::seconds(1), [&]
                           { return state.owner != owner || !state.pending || state.command.serial != serial || state.replied; });
    if (state.owner != owner) return Status::Unsupported;
    if (!state.pending || state.command.serial != serial) return Status::Expired;
    if (!state.replied) return Status::Busy;
    const auto status = state.status;
    std::copy_n(state.reply.data(), state.bytes, output);
    written = state.bytes;
    state.pending = state.taken = state.replied = false;
    return status;
}
} // namespace platform::linux_runtime
