#include "platform/linux/hostlink_track_prepare.h"
#include "geocaching/protocol/cmp_writer.h"
#include "platform/linux/hostlink_track_prepare_persistence.h"
#include "platform/linux/hostlink_track_signer.h"
#include "platform/linux/hostlink_uploads.h"
#include "track_sharing/immutable_file_copy.h"
#include <vector>

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
namespace
{
namespace ts = track_sharing;
using Operation = ::hostlink::management::Operation;
bool current_author(const std::filesystem::path& database, uint32_t& generation, std::array<uint8_t, 32>& actor)
{
    LinuxReticulumIdentity identity(database);
    HostlinkTrackSigner signer(identity);
    std::array<uint8_t, 64> key{};
    return identity.begin() && signer.readAuthor(key.data(), actor.data(), generation) == Status::Ok;
}
Status input_status(ts::ManifestFileResult result)
{
    return result == ts::ManifestFileResult::StorageFailure ? Status::StorageUnavailable : result == ts::ManifestFileResult::HashMismatch ? Status::HashMismatch
                                                                                                                                          : Status::InvalidField;
}
struct PrepareJob
{
    PrepareJob(const std::filesystem::path& root, const std::filesystem::path& database,
               const ::hostlink::management::EnvelopeView& envelope, const ts::PrepareRequest& request,
               const std::array<uint8_t, 32>& current_actor, uint64_t now)
        : io(root), generation(request.generation), actor(current_actor), body(envelope.body, envelope.body + envelope.body_size), touched(now)
    {
        std::copy_n(envelope.session, 16, session.begin());
        std::copy_n(request.operation.data, 16, operation.begin());
        identity = std::make_unique<LinuxReticulumIdentity>(database);
        signer = std::make_unique<HostlinkTrackSigner>(*identity);
        std::array<uint8_t, 64> key{};
        std::array<uint8_t, 32> hash{};
        uint32_t epoch = 0;
        if (!identity->begin() || signer->readAuthor(key.data(), hash.data(), epoch) != Status::Ok)
        {
            fail(Status::StorageUnavailable);
            return;
        }
        if (epoch != generation || hash != actor)
        {
            fail(Status::StaleIdentity);
            return;
        }
        if (!io.current())
        {
            fail(Status::StorageUnavailable);
            return;
        }
        if (!io.lock())
        {
            fail(Status::Busy);
            return;
        }
        input = std::make_unique<HostlinkTrackPrepareInput>(io, request, key.data(), actor.data());
        const auto started = input->begin();
        if (started != ts::ManifestFileResult::Pending) fail(input_status(started));
    }
    ~PrepareJob() { release(); }
    bool matches(const ::hostlink::management::EnvelopeView& request) const
    {
        return request.session && !std::memcmp(session.data(), request.session, 16) &&
               body.size() == request.body_size && !std::memcmp(body.data(), request.body, body.size());
    }
    void release()
    {
        if (copy) copy->cancel();
        copy.reset();
        persistence.reset();
        input.reset();
        signer.reset();
        identity.reset();
        io.close(nullptr);
        io.unlock();
    }
    void fail(Status failure)
    {
        status = failure;
        terminal = true;
        release();
    }
    void pulse(uint32_t epoch, const std::array<uint8_t, 32>& current_actor)
    {
        if (terminal) return;
        if (!io.current())
        {
            fail(Status::StorageUnavailable);
            return;
        }
        if (epoch != generation || current_actor != actor)
        {
            fail(Status::StaleIdentity);
            return;
        }
        if (!input->ready())
        {
            const auto checked = input->step(epoch, current_actor.data());
            if (checked == ts::ManifestFileResult::Pending) return;
            if (checked != ts::ManifestFileResult::Ready)
            {
                fail(input_status(checked));
                return;
            }
        }
        if (!persistence)
        {
            persistence = std::make_unique<HostlinkTrackPreparePersistence>(*input);
            if (!persistence->initialize())
            {
                fail(Status::InvalidField);
                return;
            }
        }
        if (!persistence->ready())
        {
            const auto finished = persistence->finish(*signer, nullptr, scratch.data(), scratch.size());
            if (finished == Status::Busy) return;
            if (finished != Status::Ok)
            {
                fail(finished);
                return;
            }
            const auto& head = persistence->head();
            std::copy_n(head.object, 32, object.begin());
            std::copy_n(head.hash, 32, revision_hash.begin());
            revision = head.revision;
            const auto signed_bytes = persistence->signature();
            if (signed_bytes.size != signature.size())
            {
                fail(Status::Internal);
                return;
            }
            std::copy_n(signed_bytes.data, 64, signature.begin());
        }
        if (copy)
        {
            const auto retained = copy->step(scratch.data(), scratch.size());
            if (retained == ts::ImmutableCopyResult::Pending) return;
            if (retained != ts::ImmutableCopyResult::Ready)
            {
                fail(retained == ts::ImmutableCopyResult::HashMismatch ? Status::HashMismatch : retained == ts::ImmutableCopyResult::Conflict ? Status::Conflict
                                                                                                                                              : Status::StorageUnavailable);
                return;
            }
            copy.reset();
            ++stage;
            return;
        }
        if (stage == 1 && !input->attachment_input.indexPath()) ++stage;
        if (stage >= 2 + input->attachment_input.resourceCount())
        {
            status = Status::Ok;
            terminal = true;
            release();
            return;
        }
        const char* source = nullptr;
        uint32_t bytes = 0;
        ts::ObjectFile final_kind, temporary_kind;
        if (stage == 0)
        {
            source = input->kml_path.data();
            bytes = input->view.file_bytes;
            std::copy_n(input->view.file_hash.data, 32, expected.begin());
            final_kind = ts::ObjectFile::KmlFile;
            temporary_kind = ts::ObjectFile::KmlTemporary;
        }
        else if (stage == 1)
        {
            ts::ManifestExtensionsView extensions;
            if (!ts::getManifestExtensions(input->view, extensions))
            {
                fail(Status::InvalidField);
                return;
            }
            source = input->attachment_input.indexPath();
            bytes = extensions.assets.bytes;
            std::copy_n(extensions.assets.hash.data, 32, expected.begin());
            final_kind = ts::ObjectFile::AttachmentIndex;
            temporary_kind = ts::ObjectFile::AttachmentIndexTemporary;
        }
        else
        {
            if (!input->attachment_input.resource(stage - 2, resource))
            {
                fail(Status::Internal);
                return;
            }
            source = resource.path.data();
            bytes = resource.bytes;
            expected = resource.hash;
            final_kind = ts::ObjectFile::AttachmentResource;
            temporary_kind = ts::ObjectFile::AttachmentResourceTemporary;
        }
        if (!source || std::strlen(source) >= source_path.size() ||
            !ts::resolveObjectPath({object.data(), 32}, final_kind, {expected.data(), 32}, final_path.data(), final_path.size()) ||
            !ts::resolveObjectPath({object.data(), 32}, temporary_kind, {expected.data(), 32}, temporary_path.data(), temporary_path.size()))
        {
            fail(Status::InvalidField);
            return;
        }
        // Match the existing formal v1/v2 retained-object history budgets.
        const bool package = input->view.version == 2;
        if (!io.admit_copy(temporary_path.data(), final_path.data(), bytes, package ? 129ULL * 1024 * 1024 : 32ULL * 1024 * 1024, package ? 512 : 128))
        {
            fail(Status::LimitExceeded);
            return;
        }
        std::strcpy(source_path.data(), source);
        copy = std::make_unique<ts::ImmutableFileCopy<UploadIo, RouteSourceHash>>(io, hash);
        const auto begun = copy->begin(source_path.data(), temporary_path.data(), final_path.data(), bytes, expected.data());
        if (begun != ts::ImmutableCopyResult::Pending) fail(begun == ts::ImmutableCopyResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
    }
    UploadIo io;
    uint32_t generation, revision = 0;
    std::array<uint8_t, 32> actor{}, object{}, revision_hash{}, expected{};
    std::array<uint8_t, 16> session{}, operation{};
    std::array<uint8_t, 64> signature{};
    std::vector<uint8_t> body;
    uint64_t touched;
    bool terminal = false;
    Status status = Status::Busy;
    unsigned stage = 0;
    std::array<uint8_t, 4096> scratch{};
    std::array<char, 160> source_path{}, temporary_path{}, final_path{};
    PackageInputResource resource;
    RouteSourceHash hash;
    std::unique_ptr<LinuxReticulumIdentity> identity;
    std::unique_ptr<HostlinkTrackSigner> signer;
    std::unique_ptr<HostlinkTrackPrepareInput> input;
    std::unique_ptr<HostlinkTrackPreparePersistence> persistence;
    std::unique_ptr<ts::ImmutableFileCopy<UploadIo, RouteSourceHash>> copy;
};
} // namespace
struct HostlinkTrackPrepare::Impl
{
    Impl(std::filesystem::path root, std::filesystem::path database, HostlinkUploads& uploads)
        : root(std::move(root)), database(std::move(database)), uploads(uploads) {}
    std::filesystem::path root, database;
    HostlinkUploads& uploads;
    std::unique_ptr<PrepareJob> job;
};
HostlinkTrackPrepare::HostlinkTrackPrepare(std::filesystem::path root, std::filesystem::path database, HostlinkUploads& uploads)
    : impl_(std::make_unique<Impl>(std::move(root), std::move(database), uploads)) {}
HostlinkTrackPrepare::~HostlinkTrackPrepare() = default;
bool HostlinkTrackPrepare::supported() { return true; }
bool HostlinkTrackPrepare::identityBusy() const { return impl_->job && !impl_->job->terminal; }
void HostlinkTrackPrepare::advance(uint64_t now, bool connected)
{
    auto& job = impl_->job;
    if (!job) return;
    if (!connected || now < job->touched || now - job->touched >= 30000)
    {
        job.reset();
        return;
    }
    if (job->terminal) return;
    uint32_t generation = 0;
    std::array<uint8_t, 32> actor{};
    if (!current_author(impl_->database, generation, actor))
    {
        job->fail(Status::StorageUnavailable);
        return;
    }
    for (unsigned step = 0; step < 64 && !job->terminal; ++step) job->pulse(generation, actor);
}
void HostlinkTrackPrepare::close(const uint8_t* session)
{
    if (impl_->job && session && !std::memcmp(impl_->job->session.data(), session, 16)) impl_->job.reset();
}
Status HostlinkTrackPrepare::process(const ::hostlink::management::EnvelopeView& envelope, uint64_t now, uint8_t* output, size_t capacity, size_t& written)
try
{
    written = 0;
    const auto operation = static_cast<Operation>(envelope.code);
    const bool package = operation == Operation::TrackPackageSignPrepare;
    const bool signing = operation != Operation::TrackPrepare;
    if (operation != Operation::TrackPrepare && operation != Operation::TrackSignPrepare && !package) return Status::Unsupported;
    ts::PrepareRequest request;
    if (!envelope.session || !output || capacity < 192 || !ts::decodePrepareRequest({envelope.body, envelope.body_size}, request) ||
        request.install_request || request.query_request || request.package_request != package) return Status::InvalidField;
    uint8_t bits = 0;
    for (unsigned i = 0; i < 16; ++i) bits |= envelope.session[i];
    if (!bits) return Status::InvalidField;
    advance(now, true);
    uint32_t generation = 0;
    std::array<uint8_t, 32> actor{};
    if (!current_author(impl_->database, generation, actor)) return Status::StorageUnavailable;
    if (request.generation != generation) return Status::StaleIdentity;
    auto& job = impl_->job;
    if (job && !job->matches(envelope))
    {
        if (!job->terminal || std::memcmp(job->session.data(), envelope.session, 16)) return Status::Conflict;
        job.reset();
    }
    if (!job)
    {
        // This existing handoff only retires this session's sealed owner when
        // its upload ID and author epoch match the immutable input bindings.
        const auto admitted = impl_->uploads.prepare_install(envelope.session, request, now, actor.data());
        if (admitted != Status::Ok) return admitted;
        job = std::make_unique<PrepareJob>(impl_->root, impl_->database, envelope, request, actor, now);
    }
    if (!job->io.current()) return Status::StorageUnavailable;
    if (job->generation != generation || job->actor != actor) return Status::StaleIdentity;
    job->touched = now;
    if (job->terminal && job->status != Status::Ok)
    {
        const auto status = job->status;
        if (status == Status::Busy) job.reset();
        return status;
    }
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(signing ? 6 : 5) || !writer.binary({job->operation.data(), 16}) || !writer.unsignedInteger(job->terminal ? 1 : 0)) return Status::Internal;
    if (job->terminal)
    {
        if (!writer.binary({job->object.data(), 32}) || !writer.binary({job->revision_hash.data(), 32}) || !writer.unsignedInteger(job->revision)) return Status::Internal;
    }
    else if (!writer.nil() || !writer.nil() || !writer.unsignedInteger(0)) return Status::Internal;
    if (signing && !(job->terminal ? writer.binary({job->signature.data(), 64}) : writer.nil())) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
catch (...)
{
    written = 0;
    return Status::StorageUnavailable;
}
#else
struct HostlinkTrackPrepare::Impl
{
};
HostlinkTrackPrepare::HostlinkTrackPrepare(std::filesystem::path, std::filesystem::path, HostlinkUploads&) : impl_(std::make_unique<Impl>()) {}
HostlinkTrackPrepare::~HostlinkTrackPrepare() = default;
bool HostlinkTrackPrepare::supported() { return false; }
bool HostlinkTrackPrepare::identityBusy() const { return false; }
void HostlinkTrackPrepare::advance(uint64_t, bool) {}
void HostlinkTrackPrepare::close(const uint8_t*) {}
::hostlink::management::Status HostlinkTrackPrepare::process(const ::hostlink::management::EnvelopeView&, uint64_t, uint8_t*, size_t, size_t& written)
{
    written = 0;
    return ::hostlink::management::Status::Unsupported;
}
#endif
} // namespace platform::linux_runtime
