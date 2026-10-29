#include "chat/linux_track_publication.h"
#include "platform/linux/hostlink_track_publication_execution.h"
#include "platform/linux/hostlink_track_signer.h"
#include "platform/linux/track_publication_history.h"
#include "platform/linux/track_publication_runtime.h"
#include <algorithm>

namespace trailmate::linux_app
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
namespace native = platform::linux_runtime;
namespace ts = track_sharing;
namespace mgmt = ::hostlink::management;
using Status = mgmt::Status;
struct LinuxTrackPublication::Impl
{
    Impl(std::filesystem::path storage, std::filesystem::path database, const native::LinuxReticulumIdentity& identity, Directory directory, Transport transport, MessageSource messages)
        : storage(std::move(storage)), database(std::move(database)), identity(identity), directory(std::move(directory)), transport(std::move(transport)), messages(std::move(messages)) {}
    bool current() const { return identity.ready() && identity.generation() == generation && publisher && publisher->valid(); }
    void remember(const uint8_t* hash)
    {
        for (size_t i = 0; i < accepted_count; ++i)
            if (!std::memcmp(accepted[i].data(), hash, 32)) return;
        std::copy_n(hash, 32, accepted[accepted_cursor].begin());
        accepted_cursor = (accepted_cursor + 1) % accepted.size();
        accepted_count = std::min(accepted_count + 1, accepted.size());
    }
    void retireExecution()
    {
        if (execution && execution->result()) publisher->update(*execution->result(), false);
        else if (execution) publisher->invalidate();
        execution.reset();
        admission.reset();
        io.reset();
    }
    void failRequest(Status status)
    {
        publisher->complete(command.serial, status, nullptr, execution != nullptr);
        admission.reset();
        history.reset();
        if (!execution) io.reset();
        working = false;
    }
    static bool query(const native::TrackPublicationRequest& command, std::array<uint8_t, 16>& operation, uint32_t& generation)
    {
        geocaching::protocol::CmpReader reader({command.body.data(), command.bytes});
        size_t fields = 0;
        uint64_t epoch = 0, type = 0;
        geocaching::ByteView id;
        if (!reader.array(fields, 3) || fields != 3 || !reader.binary(id, 16) || !ts::networkRequestId(id) || !reader.unsignedInteger(epoch) || !epoch || epoch > UINT32_MAX ||
            !reader.unsignedInteger(type) || type != 1 || !reader.finished()) return false;
        std::copy_n(id.data, 16, operation.begin());
        generation = uint32_t(epoch);
        return true;
    }
    void start(const native::TrackPublicationRequest& request)
    {
        command = request;
        working = true;
        if (request.code == uint16_t(mgmt::Operation::TrackCommit))
        {
            ts::CommitRequest decoded;
            if (!ts::decodeCommitRequest({command.body.data(), command.bytes}, decoded))
            {
                failRequest(Status::InvalidField);
                return;
            }
            if (decoded.generation != generation)
            {
                failRequest(Status::StaleIdentity);
                return;
            }
            if (execution)
            {
                const auto* job = execution->result();
                const bool same = job && !std::memcmp(job->operation, decoded.operation.data, 16) && !std::memcmp(job->object, decoded.object.data, 32) &&
                                  !std::memcmp(job->revision_hash, decoded.revision_hash.data, 32) && !std::memcmp(job->directory, decoded.directory.data, 16) && !std::memcmp(job->directory_epoch, decoded.directory_epoch.data, 16) && job->revision == decoded.revision;
                publisher->complete(command.serial, same ? Status::Ok : Status::Busy, same ? job : nullptr, true);
                working = false;
                return;
            }
            if (!enabled || !directory)
            {
                failRequest(Status::Busy);
                return;
            }
            ts::PublicationDirectory selected;
            const auto pinned = directory(command.session.data(), decoded.directory, decoded.directory_epoch, selected);
            if (pinned != Status::Ok)
            {
                failRequest(pinned);
                return;
            }
            selected_directory = selected;
            // Operation IDs are global to the author-facing JobGet selector.
            // Reject reuse for another signed object, including after restart.
            history = std::make_unique<native::TrackPublicationHistory>(storage, identity, decoded.operation.data, generation, actor.data(), false, true);
            return;
        }
        std::array<uint8_t, 16> operation{};
        uint32_t requested = 0;
        if (!query(request, operation, requested))
        {
            failRequest(Status::InvalidField);
            return;
        }
        if (requested != generation)
        {
            failRequest(Status::StaleIdentity);
            return;
        }
        if (request.code != uint16_t(mgmt::Operation::JobGet) && request.code != uint16_t(mgmt::Operation::JobCancel))
        {
            failRequest(Status::Unsupported);
            return;
        }
        if (execution)
        {
            const auto* job = execution->result();
            if (!job || std::memcmp(job->operation, operation.data(), 16))
            {
                failRequest(Status::Busy);
                return;
            }
            if (request.code == uint16_t(mgmt::Operation::JobCancel))
            {
                const auto cancelled = execution->cancel(generation, {actor.data(), 32});
                if (cancelled != ts::PublicationStoreResult::Ready)
                {
                    failRequest(cancelled == ts::PublicationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
                    return;
                }
                publisher->complete(command.serial, Status::Ok, execution->result(), false);
                retireExecution();
                working = false;
                return;
            }
            publisher->complete(command.serial, Status::Ok, job, true);
            working = false;
            return;
        }
        history = std::make_unique<native::TrackPublicationHistory>(storage, identity, operation.data(), generation, actor.data(), request.code == uint16_t(mgmt::Operation::JobCancel));
    }
    void stepRequest()
    {
        if (history)
        {
            if (!publisher->current(command.serial))
            {
                history.reset();
                working = false;
                return;
            }
            history->step();
            if (!history->terminal()) return;
            if (command.code == uint16_t(mgmt::Operation::TrackCommit))
            {
                const auto checked = history->status();
                if (checked != Status::Ok && checked != Status::Expired)
                {
                    failRequest(checked);
                    return;
                }
                ts::CommitRequest decoded;
                if (!ts::decodeCommitRequest({command.body.data(), command.bytes}, decoded))
                {
                    failRequest(Status::InvalidField);
                    return;
                }
                if (const auto* existing = history->result())
                {
                    if (std::memcmp(existing->object, decoded.object.data, 32) || std::memcmp(existing->revision_hash, decoded.revision_hash.data, 32) ||
                        std::memcmp(existing->directory, decoded.directory.data, 16) || std::memcmp(existing->directory_epoch, decoded.directory_epoch.data, 16) ||
                        existing->revision != decoded.revision)
                    {
                        failRequest(Status::Conflict);
                        return;
                    }
                }
                io = history->takeStorage();
                history.reset();
                if (!io || !io->current())
                {
                    failRequest(Status::StorageUnavailable);
                    return;
                }
                admission = std::make_unique<native::HostlinkTrackPublicationAdmission>(decoded, selected_directory, *io, identity.publicKey().data(), actor.data());
                return;
            }
            publisher->complete(command.serial, history->status(), history->result(), false);
            history.reset();
            working = false;
            return;
        }
        if (!admission)
        {
            working = false;
            return;
        }
        if (!enabled)
        {
            failRequest(Status::Busy);
            return;
        }
        native::HostlinkTrackSigner signer(identity);
        const auto status = admission->step(signer, scratch.data(), scratch.size());
        if (status == Status::Busy) return;
        if (status != Status::Ok)
        {
            failRequest(status);
            return;
        }
        execution = std::make_unique<native::HostlinkTrackPublicationExecution>(std::move(admission), identity, transport, messages);
        if (!execution->ready())
        {
            failRequest(Status::Conflict);
            retireExecution();
            return;
        }
        publisher->complete(command.serial, Status::Ok, execution->result(), true);
        working = false;
    }
    std::filesystem::path storage, database;
    const native::LinuxReticulumIdentity& identity;
    Directory directory;
    Transport transport;
    MessageSource messages;
    std::unique_ptr<native::TrackPublicationPublisher> publisher;
    uint32_t generation = 0;
    std::array<uint8_t, 32> actor{};
    std::array<uint8_t, 448> scratch{};
    uint64_t context_generation = 0, register_after = 0;
    native::TrackPublicationRequest command;
    ts::PublicationDirectory selected_directory;
    bool enabled = false, working = false;
    std::unique_ptr<native::UploadIo> io;
    std::unique_ptr<native::HostlinkTrackPublicationAdmission> admission;
    std::unique_ptr<native::HostlinkTrackPublicationExecution> execution;
    std::unique_ptr<native::TrackPublicationHistory> history;
    std::array<std::array<uint8_t, 32>, 32> accepted{};
    size_t accepted_count = 0, accepted_cursor = 0;
};
LinuxTrackPublication::LinuxTrackPublication(std::filesystem::path storage, std::filesystem::path database, const native::LinuxReticulumIdentity& identity, Directory directory, Transport transport, MessageSource messages)
    : impl_(std::make_unique<Impl>(std::move(storage), std::move(database), identity, std::move(directory), std::move(transport), std::move(messages))) {}
LinuxTrackPublication::~LinuxTrackPublication() = default;
void LinuxTrackPublication::tick(uint64_t now, bool context_current, uint64_t context_generation)
{
    auto& s = *impl_;
    if (!s.identity.ready() || (s.publisher && s.generation != s.identity.generation()))
    {
        s.execution.reset();
        s.admission.reset();
        s.history.reset();
        s.io.reset();
        s.publisher.reset();
        s.working = false;
        s.generation = 0;
        s.accepted_count = s.accepted_cursor = 0;
        if (!s.identity.ready()) return;
    }
    if (!s.publisher || !s.publisher->valid())
    {
        if (now < s.register_after) return;
        s.register_after = now + 1000;
        native::LinuxGeocachingCrypto crypto;
        if (!crypto.sha256({s.identity.publicKey().data(), 64}, s.actor.data())) return;
        s.generation = s.identity.generation();
        s.publisher = std::make_unique<native::TrackPublicationPublisher>(s.database, s.generation, s.actor.data());
        if (!s.publisher->valid()) return;
    }
    const bool changed = s.context_generation != context_generation;
    s.context_generation = context_generation;
    s.enabled = context_current;
    if (changed || !context_current)
    {
        if (s.working && s.command.code == uint16_t(mgmt::Operation::TrackCommit)) s.failRequest(Status::Busy);
        s.retireExecution();
        s.accepted_count = s.accepted_cursor = 0;
    }
    if (s.working)
    {
        for (unsigned step = 0; step < 32 && s.working; ++step) s.stepRequest();
    }
    else
    {
        native::TrackPublicationRequest command;
        if (s.publisher->take(command)) s.start(command);
    }
    if (!s.execution || !s.enabled) return;
    const auto result = s.execution->advance(now);
    if (s.execution->result()) s.publisher->update(*s.execution->result(), result != ts::NetworkSessionResult::Confirmed);
    if (result == ts::NetworkSessionResult::Confirmed || result == ts::NetworkSessionResult::Paused || result == ts::NetworkSessionResult::Conflict ||
        result == ts::NetworkSessionResult::Invalid || result == ts::NetworkSessionResult::StorageFailure) s.retireExecution();
}
bool LinuxTrackPublication::receive(chat::lxmf::ByteSpan type, const chat::lxmf::CustomDeliveryView& view, uint64_t)
{
    auto& s = *impl_;
    if (!s.enabled || !s.current() || !chat::lxmf::isTrackCustomType(type) || !view.source.data || view.source.size != 16 || !view.destination.data || view.destination.size != 16 ||
        !view.message_hash.data || view.message_hash.size != 32 || !view.data.data || !view.data.size || view.data.size > 8192 || std::memcmp(view.destination.data, s.identity.destinationHash().data(), 16)) return false;
    for (size_t i = 0; i < s.accepted_count; ++i)
        if (!std::memcmp(s.accepted[i].data(), view.message_hash.data, 32)) return true;
    if (!s.execution || !s.execution->receive(type, view)) return false;
    s.remember(view.message_hash.data);
    if (s.execution->result()) s.publisher->update(*s.execution->result(), true);
    return true;
}
#else
struct LinuxTrackPublication::Impl
{
};
LinuxTrackPublication::LinuxTrackPublication(std::filesystem::path, std::filesystem::path, const platform::linux_runtime::LinuxReticulumIdentity&, Directory, Transport, MessageSource) : impl_(std::make_unique<Impl>()) {}
LinuxTrackPublication::~LinuxTrackPublication() = default;
void LinuxTrackPublication::tick(uint64_t, bool, uint64_t) {}
bool LinuxTrackPublication::receive(chat::lxmf::ByteSpan, const chat::lxmf::CustomDeliveryView&, uint64_t) { return false; }
#endif
} // namespace trailmate::linux_app
