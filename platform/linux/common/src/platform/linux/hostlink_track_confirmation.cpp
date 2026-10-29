#include "platform/linux/hostlink_track_confirmation.h"
#include "geocaching/protocol/cmp_writer.h"
#include "platform/linux/hostlink_track_signer.h"
#include "platform/linux/hostlink_upload_io.h"
#include "platform/linux/hostlink_uploads.h"
#include "platform/linux/route_image_source_io.h"
#include "platform/linux/track_manifest_crypto.h"
#include "platform/linux/track_receipt_crypto.h"
#include "track_sharing/ip_confirmation_work.h"
#include "track_sharing/network_request.h"
#include <vector>

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
namespace
{
namespace ts = track_sharing;
namespace mgmt = ::hostlink::management;
using Status = mgmt::Status;
Status admit_import(UploadIo& io, const ts::IpConfirmationRequest& request, uint8_t* scratch, size_t capacity)
{
    if (!io.current()) return Status::StorageUnavailable;
    std::array<char, 160> path{};
    for (const auto kind : {ts::ObjectFile::HeadA, ts::ObjectFile::HeadB})
    {
        if (!ts::resolveObjectPath(request.object, kind, {}, path.data(), path.size())) return Status::InvalidField;
        const auto file = io.open(path.data(), UploadIo::Mode::Read);
        if (file.status == UploadIo::OpenStatus::Ready)
        {
            io.close(file.handle);
            return Status::Ok; // Shared work validates both heads before writing.
        }
        if (file.status != UploadIo::OpenStatus::Missing) return Status::StorageUnavailable;
    }
    if (!ts::resolveObjectPath(request.object, ts::ObjectFile::Directory, {}, path.data(), path.size())) return Status::InvalidField;
    std::array<UploadIo::ScanEntry, 4> entries;
    size_t count = 0;
    bool ended = false;
    const auto scan = io.scan(path.data(), 0, entries, count, ended, 5);
    if (scan != UploadIo::ScanStatus::Missing)
    {
        if (scan != UploadIo::ScanStatus::Ready) return Status::StorageUnavailable;
        // A directory without heads may contain only this exact authenticated
        // import's manifest and receipt evidence. Unknown files block adoption.
        if (!ended)
        {
            std::array<UploadIo::ScanEntry, 4> remaining;
            size_t extra = 0;
            if (io.scan(path.data(), uint32_t(count), remaining, extra, ended, 5) != UploadIo::ScanStatus::Ready) return Status::StorageUnavailable;
            if (extra || !ended) return Status::Conflict;
        }
        constexpr char hex[] = "0123456789abcdef";
        for (size_t i = 0; i < count; ++i)
        {
            const auto& entry = entries[i];
            if (!entry.regular) return Status::Conflict;
            const auto key = entry.name.size() == 68 ? request.revision_hash : entry.name.size() == 36 ? request.network_request
                                                                                                       : geocaching::ByteView{};
            if (!key.size) return Status::Conflict;
            for (size_t j = 0; j < key.size; ++j)
                if (entry.name[j * 2] != hex[key.data[j] >> 4] || entry.name[j * 2 + 1] != hex[key.data[j] & 15]) return Status::Conflict;
            const auto suffix = entry.name.substr(key.size * 2);
            if (key.size == 32 ? suffix != ".cmp" && suffix != ".tmp" : suffix != ".rcp" && suffix != ".rct") return Status::Conflict;
        }
    }
    (void)scratch;
    (void)capacity;
    return io.mkdirs(path.data()) ? Status::Ok : Status::StorageUnavailable;
}
Status status_of(ts::IpConfirmationResult result)
{
    switch (result)
    {
    case ts::IpConfirmationResult::Pending:
        return Status::Busy;
    case ts::IpConfirmationResult::Ready:
        return Status::Ok;
    case ts::IpConfirmationResult::StorageFailure:
        return Status::StorageUnavailable;
    case ts::IpConfirmationResult::StaleIdentity:
        return Status::StaleIdentity;
    case ts::IpConfirmationResult::HashMismatch:
        return Status::HashMismatch;
    case ts::IpConfirmationResult::Cancelled:
        return Status::Expired;
    case ts::IpConfirmationResult::Conflict:
        return Status::Conflict;
    default:
        return Status::InvalidField;
    }
}
struct ConfirmationJob
{
    using Work = ts::IpConfirmationWork<UploadIo, RouteSourceHash, LinuxTrackManifestCrypto, LinuxTrackReceiptCrypto>;
    ConfirmationJob(const std::filesystem::path& root, const mgmt::EnvelopeView& envelope, const ts::IpConfirmationRequest& request,
                    const uint8_t* author, const uint8_t* actor, uint64_t now)
        : io(root), body(envelope.body, envelope.body + envelope.body_size), code(envelope.code), generation(request.generation), touched(now)
    {
        std::copy_n(envelope.session, 16, session.begin());
        std::copy_n(actor, 32, this->actor.begin());
        if (!io.current())
        {
            status = Status::StorageUnavailable;
            return;
        }
        if (!io.lock())
        {
            status = Status::Busy;
            return;
        }
        work = std::make_unique<Work>(io, hash, manifest_crypto, receipt_crypto, request, geocaching::ByteView{author, 64}, geocaching::ByteView{actor, 32},
                                      signed_bytes.data(), signed_bytes.size(), receipt_bytes.data(), receipt_bytes.size(), domain.data(), domain.size());
    }
    bool matches(const mgmt::EnvelopeView& request) const
    {
        return code == request.code && request.session && !std::memcmp(session.data(), request.session, 16) &&
               body.size() == request.body_size && !std::memcmp(body.data(), request.body, body.size());
    }
    void pulse(uint32_t epoch, const uint8_t* author, const uint8_t* actor)
    {
        if (!work) return;
        if (!io.current())
        {
            status = Status::StorageUnavailable;
            work->cancel();
            io.unlock();
            return;
        }
        if (epoch != generation || std::memcmp(this->actor.data(), actor, 32))
        {
            status = Status::StaleIdentity;
            work->cancel();
            io.unlock();
            return;
        }
        if (!io.lock())
        {
            status = Status::Busy;
            return;
        }
        if (const auto* evidence = work->importEvidence())
        {
            status = admit_import(io, *evidence, scratch.data(), scratch.size());
            if (status != Status::Ok)
            {
                work->cancel();
                io.unlock();
                return;
            }
        }
        status = status_of(work->step(epoch, {author, 64}, {actor, 32}, true, scratch.data(), scratch.size()));
        if (status != Status::Busy) io.unlock();
    }
    UploadIo io;
    std::vector<uint8_t> body;
    uint16_t code;
    uint32_t generation;
    uint64_t touched;
    std::array<uint8_t, 16> session{};
    std::array<uint8_t, 32> actor{};
    std::array<uint8_t, 4166> signed_bytes{}, domain{};
    std::array<uint8_t, 1100> receipt_bytes{};
    std::array<uint8_t, 448> scratch{};
    RouteSourceHash hash;
    LinuxTrackManifestCrypto manifest_crypto;
    LinuxTrackReceiptCrypto receipt_crypto;
    std::unique_ptr<Work> work;
    Status status = Status::Busy;
};
} // namespace
struct HostlinkTrackConfirmation::Impl
{
    Impl(std::filesystem::path root, std::filesystem::path database, HostlinkUploads& uploads)
        : root(std::move(root)), database(std::move(database)), uploads(uploads) {}
    std::filesystem::path root, database;
    HostlinkUploads& uploads;
    std::unique_ptr<ConfirmationJob> job;
};
HostlinkTrackConfirmation::HostlinkTrackConfirmation(std::filesystem::path root, std::filesystem::path database, HostlinkUploads& uploads)
    : impl_(std::make_unique<Impl>(std::move(root), std::move(database), uploads)) {}
HostlinkTrackConfirmation::~HostlinkTrackConfirmation() = default;
bool HostlinkTrackConfirmation::supported() { return true; }
bool HostlinkTrackConfirmation::identityBusy() const { return impl_->job && impl_->job->work && impl_->job->status == Status::Busy; }
void HostlinkTrackConfirmation::advance(uint64_t now, bool connected)
{
    auto& job = impl_->job;
    if (job && (!connected || now < job->touched || now - job->touched >= 30000)) job.reset();
    if (!job || !job->work || job->status != Status::Busy) return;
    LinuxReticulumIdentity identity(impl_->database);
    HostlinkTrackSigner signer(identity);
    std::array<uint8_t, 64> author{};
    std::array<uint8_t, 32> actor{};
    uint32_t generation = 0;
    if (!identity.begin() || signer.readAuthor(author.data(), actor.data(), generation) != Status::Ok)
    {
        job->work->cancel();
        job->status = Status::StorageUnavailable;
        job->io.unlock();
        return;
    }
    for (unsigned step = 0; step < 32 && job->status == Status::Busy; ++step) job->pulse(generation, author.data(), actor.data());
}
void HostlinkTrackConfirmation::close(const uint8_t* session)
{
    if (impl_->job && session && !std::memcmp(impl_->job->session.data(), session, 16)) impl_->job.reset();
}
::hostlink::management::Status HostlinkTrackConfirmation::process(const mgmt::EnvelopeView& envelope, uint64_t now, uint8_t* output, size_t capacity, size_t& written)
try
{
    written = 0;
    const bool import = envelope.code == uint16_t(mgmt::Operation::TrackImportHistory);
    if (!import && envelope.code != uint16_t(mgmt::Operation::TrackConfirmPublication)) return Status::Unsupported;
    ts::IpConfirmationRequest request;
    if (!envelope.session || !ts::networkRequestId({envelope.session, 16}) || !output || capacity < 192 ||
        !ts::decodeIpConfirmationRequest({envelope.body, envelope.body_size}, request, import)) return Status::InvalidField;
    advance(now, true);
    LinuxReticulumIdentity identity(impl_->database);
    HostlinkTrackSigner signer(identity);
    std::array<uint8_t, 64> author{};
    std::array<uint8_t, 32> actor{};
    uint32_t generation = 0;
    if (!identity.begin() || signer.readAuthor(author.data(), actor.data(), generation) != Status::Ok) return Status::StorageUnavailable;
    if (generation != request.generation) return Status::StaleIdentity;
    auto& job = impl_->job;
    if (job && !job->matches(envelope))
    {
        if (job->status == Status::Busy || std::memcmp(job->session.data(), envelope.session, 16)) return Status::Conflict;
        job.reset();
    }
    const auto handoff = impl_->uploads.prepare_confirmation(envelope.session, request, now, actor.data());
    if (handoff != Status::Ok) return handoff;
    if (!job) job = std::make_unique<ConfirmationJob>(impl_->root, envelope, request, author.data(), actor.data(), now);
    if (!job->work)
    {
        const auto status = job->status;
        job.reset();
        return status;
    }
    job->touched = now;
    if (job->status != Status::Busy && job->status != Status::Ok) return job->status;
    // Revalidate the durable head even for a cached completed retry.
    job->pulse(generation, author.data(), actor.data());
    if (job->status != Status::Busy && job->status != Status::Ok) return job->status;
    if (job->status == Status::Busy && job->work->result()) return Status::Busy;
    const auto* result = job->work->result();
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(8) || !writer.binary(request.operation) || !writer.binary(request.transfer) || !writer.unsignedInteger(generation) ||
        !writer.unsignedInteger(result ? 1 : 0) || !(result ? writer.binary({result->object, 32}) : writer.nil()) ||
        !(result ? writer.binary({result->hash, 32}) : writer.nil()) || !writer.unsignedInteger(result ? result->revision : 0) || !writer.unsignedInteger(result ? 1 : 0)) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
catch (...)
{
    written = 0;
    return Status::StorageUnavailable;
}
#else
struct HostlinkTrackConfirmation::Impl
{
};
HostlinkTrackConfirmation::HostlinkTrackConfirmation(std::filesystem::path, std::filesystem::path, HostlinkUploads&) : impl_(std::make_unique<Impl>()) {}
HostlinkTrackConfirmation::~HostlinkTrackConfirmation() = default;
bool HostlinkTrackConfirmation::supported() { return false; }
bool HostlinkTrackConfirmation::identityBusy() const { return false; }
void HostlinkTrackConfirmation::advance(uint64_t, bool) {}
void HostlinkTrackConfirmation::close(const uint8_t*) {}
::hostlink::management::Status HostlinkTrackConfirmation::process(const ::hostlink::management::EnvelopeView&, uint64_t, uint8_t*, size_t, size_t& written)
{
    written = 0;
    return ::hostlink::management::Status::Unsupported;
}
#endif
} // namespace platform::linux_runtime
