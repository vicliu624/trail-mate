#include "platform/linux/hostlink_package_install.h"
#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/route_storage_lease.h"
#include "hostlink_upload_io.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/hostlink_package_input.h"
#include "platform/linux/hostlink_uploads.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "platform/linux/stored_route_package.h"
#include "route_image_source_io.h"
#include "track_sharing/package_cache_paths.h"
#include "track_sharing/package_query_request.h"
#include "track_sharing/package_route_publication.h"
#include <array>

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
namespace
{
namespace ts = track_sharing;
using Status = ::hostlink::management::Status;
using Operation = ::hostlink::management::Operation;
Status checked_status(ts::ManifestFileResult result)
{
    return result == ts::ManifestFileResult::StorageFailure ? Status::StorageUnavailable : result == ts::ManifestFileResult::HashMismatch ? Status::HashMismatch
                                                                                                                                          : Status::InvalidField;
}
bool author_identity(const std::filesystem::path& database, uint32_t& generation, std::array<uint8_t, 32>& actor)
{
    LinuxReticulumIdentity identity(database);
    LinuxGeocachingCrypto crypto;
    if (!identity.begin() || !crypto.sha256({identity.publicKey().data(), 64}, actor.data())) return false;
    generation = identity.generation();
    return generation != 0;
}
struct RouteLease
{
    bool held = ::hostlink::route_storage_lease::acquire();
    ~RouteLease() { release(); }
    void release()
    {
        if (held)
        {
            ::hostlink::route_storage_lease::release();
            held = false;
        }
    }
};
struct Job
{
    Job(const std::filesystem::path& root, const ts::PrepareRequest& request, const uint8_t* session_id,
        const std::array<uint8_t, 32>& actor, uint64_t now)
        : io(root), generation(request.generation), actor(actor), query(request.query_request), touched(now)
    {
        std::copy_n(session_id, 16, session.begin());
        std::copy_n(request.operation.data, 16, operation.begin());
        std::copy_n(request.expected_object.data, 32, object.begin());
        std::copy_n(request.expected_revision.data, 32, revision.begin());
        if (!query)
        {
            std::copy_n(request.kml_upload.data, 16, ids.begin());
            std::copy_n(request.manifest_upload.data, 16, ids.begin() + 16);
            mapped = request.package_upload.size == 16;
            if (mapped) std::copy_n(request.package_upload.data, 16, ids.begin() + 32);
        }
        if (!route_lease.held || !io.current() || !io.lock())
        {
            fail(io.current() ? Status::Busy : Status::StorageUnavailable);
            return;
        }
        held = true;
        ts::ManifestFileResult begun;
        if (query)
        {
            stored = std::make_unique<StoredRoutePackage>(io, object.data(), revision.data());
            begun = stored->begin();
        }
        else
        {
            input = std::make_unique<HostlinkPackageInput>(io, request, generation, actor.data());
            begun = input->begin();
        }
        if (begun == ts::ManifestFileResult::Ready && query) finish(stored->missing());
        else if (begun != ts::ManifestFileResult::Pending) fail(checked_status(begun));
    }
    ~Job() { release(); }
    bool matches(const ts::PrepareRequest& request, const uint8_t* session_id) const
    {
        if (query != request.query_request || generation != request.generation || std::memcmp(session.data(), session_id, 16) ||
            std::memcmp(operation.data(), request.operation.data, 16) || std::memcmp(object.data(), request.expected_object.data, 32) || std::memcmp(revision.data(), request.expected_revision.data, 32)) return false;
        return query || (!std::memcmp(ids.data(), request.kml_upload.data, 16) && !std::memcmp(ids.data() + 16, request.manifest_upload.data, 16) &&
                         mapped == (request.package_upload.size == 16) && (!mapped || !std::memcmp(ids.data() + 32, request.package_upload.data, 16)));
    }
    void release()
    {
        publication.reset();
        copy.reset();
        input.reset();
        stored.reset();
        io.unlock();
        route_lease.release();
        held = false;
    }
    void fail(Status error)
    {
        status = error;
        terminal = true;
        release();
    }
    void finish(bool missing = false)
    {
        absent = missing;
        status = Status::Ok;
        terminal = true;
        release();
    }
    void pulse(uint32_t current_generation, const std::array<uint8_t, 32>& current_actor)
    {
        if (!io.current())
        {
            fail(Status::StorageUnavailable);
            return;
        }
        if (generation != current_generation || actor != current_actor)
        {
            fail(Status::StaleIdentity);
            return;
        }
        if (terminal) return;
        if (query)
        {
            const auto result = stored->step();
            if (result == ts::ManifestFileResult::Pending) return;
            if (result != ts::ManifestFileResult::Ready || (!stored->ready() && !stored->missing()))
            {
                fail(checked_status(result));
                return;
            }
            finish(stored->missing());
            return;
        }
        if (!input->ready())
        {
            const auto result = input->step(generation, actor.data());
            if (result == ts::ManifestFileResult::Pending) return;
            if (result != ts::ManifestFileResult::Ready || !input->ready())
            {
                fail(checked_status(result));
                return;
            }
            const auto* verified = input->verified();
            std::copy_n(verified->object_id, 32, binding.object);
            std::copy_n(verified->revision_hash, 32, binding.revision);
            std::copy_n(verified->manifest.file_hash.data, 32, binding.kml_hash);
            binding.kml_bytes = verified->manifest.file_bytes;
            if (verified->manifest.version == 2)
            {
                if (!ts::getManifestExtensions(verified->manifest, extensions))
                {
                    fail(Status::InvalidField);
                    return;
                }
                binding.count = extensions.assets.count;
                binding.index_bytes = extensions.assets.bytes;
                binding.availability = input->availability();
                if (binding.count) std::copy_n(extensions.assets.hash.data, 32, binding.index_hash);
            }
            return;
        }
        if (publication)
        {
            std::array<char, 80> temporary{}, final{}, record_tmp{}, record{};
            const geocaching::ByteView revision_view{revision.data(), 32};
            if (!ts::routePackageTarget(revision_view, ts::PackageRouteFile::Temporary, temporary.data(), temporary.size()) ||
                !ts::routePackageTarget(revision_view, ts::PackageRouteFile::Kml, final.data(), final.size()) ||
                !ts::routePackageTarget(revision_view, ts::PackageRouteFile::BindingTemporary, record_tmp.data(), record_tmp.size()) ||
                !ts::routePackageTarget(revision_view, ts::PackageRouteFile::Binding, record.data(), record.size()))
            {
                fail(Status::Internal);
                return;
            }
            if (!io.admit_copy(temporary.data(), final.data(), binding.kml_bytes, 32ULL * 1024 * 1024 + 65536, 256, true) ||
                !io.admit_copy(record_tmp.data(), record.data(), ts::kInstalledPackageBindingBytes, 32ULL * 1024 * 1024 + 65536, 256, true))
            {
                fail(Status::LimitExceeded);
                return;
            }
            const auto result = publication->step(scratch.data(), scratch.size(), [&]
                                                  { return io.current() && generation == current_generation && actor == current_actor; });
            if (result == ts::PackageRouteResult::Pending) return;
            if (result != ts::PackageRouteResult::Ready)
            {
                fail(result == ts::PackageRouteResult::HashMismatch ? Status::HashMismatch : result == ts::PackageRouteResult::Conflict ? Status::Conflict
                                                                                                                                        : Status::StorageUnavailable);
                return;
            }
            finish();
            return;
        }
        if (copy)
        {
            const auto result = copy->step(scratch.data(), scratch.size());
            if (result == ts::ImmutableCopyResult::Pending) return;
            if (result != ts::ImmutableCopyResult::Ready)
            {
                fail(result == ts::ImmutableCopyResult::HashMismatch ? Status::HashMismatch : result == ts::ImmutableCopyResult::Conflict ? Status::Conflict
                                                                                                                                          : Status::StorageUnavailable);
                return;
            }
            copy.reset();
            ++stage;
            return;
        }
        if (stage == 2 && !binding.count) ++stage;
        if (stage >= 3 + input->resourceCount())
        {
            if (!io.mkdirs("/routes") || !ts::resolvePackageCachePath({revision.data(), 32}, ts::ObjectFile::KmlFile, {}, cached_kml.data(), cached_kml.size()))
            {
                fail(Status::StorageUnavailable);
                return;
            }
            publication = std::make_unique<ts::PackageRoutePublication<UploadIo, RouteSourceHash>>(io, hash, binding, cached_kml.data());
            if (publication->begin() != ts::PackageRouteResult::Pending) fail(Status::InvalidField);
            return;
        }
        const auto* verified = input->verified();
        ts::ObjectFile final_kind, temporary_kind;
        uint32_t bytes = 0;
        const char* source = nullptr;
        if (stage == 0)
        {
            final_kind = ts::ObjectFile::SignedManifest;
            temporary_kind = ts::ObjectFile::SignedTemporary;
            const auto signed_bytes = input->signedBytes();
            bytes = signed_bytes.size;
            source = input->signedPath();
            LinuxGeocachingCrypto crypto;
            if (!crypto.sha256(signed_bytes, expected.data()))
            {
                fail(Status::Internal);
                return;
            }
        }
        else if (stage == 1)
        {
            final_kind = ts::ObjectFile::KmlFile;
            temporary_kind = ts::ObjectFile::KmlTemporary;
            bytes = verified->manifest.file_bytes;
            source = input->kmlPath();
            std::copy_n(verified->manifest.file_hash.data, 32, expected.begin());
        }
        else if (stage == 2)
        {
            final_kind = ts::ObjectFile::AttachmentIndex;
            temporary_kind = ts::ObjectFile::AttachmentIndexTemporary;
            bytes = binding.index_bytes;
            source = input->indexPath();
            std::copy_n(binding.index_hash, 32, expected.begin());
        }
        else
        {
            if (!input->resource(stage - 3, resource))
            {
                fail(Status::Internal);
                return;
            }
            final_kind = ts::ObjectFile::AttachmentResource;
            temporary_kind = ts::ObjectFile::AttachmentResourceTemporary;
            bytes = resource.bytes;
            source = resource.path.data();
            expected = resource.hash;
        }
        const geocaching::ByteView key = stage >= 3 ? geocaching::ByteView{expected.data(), 32} : geocaching::ByteView{};
        std::array<char, 160> directory{};
        if (!source || std::strlen(source) >= source_path.size() || !ts::resolvePackageCachePath({revision.data(), 32}, ts::ObjectFile::Directory, {}, directory.data(), directory.size()) ||
            !ts::resolvePackageCachePath({revision.data(), 32}, final_kind, key, final_path.data(), final_path.size()) ||
            !ts::resolvePackageCachePath({revision.data(), 32}, temporary_kind, key, temporary_path.data(), temporary_path.size()))
        {
            fail(Status::InvalidField);
            return;
        }
        if (!io.mkdirs(ts::kPackageCacheDirectory))
        {
            fail(Status::StorageUnavailable);
            return;
        }
        if (!io.admit_directory(ts::kPackageCacheDirectory, std::strrchr(directory.data(), '/') + 1, 64))
        {
            fail(Status::LimitExceeded);
            return;
        }
        if (!io.mkdirs(directory.data()))
        {
            fail(Status::StorageUnavailable);
            return;
        }
        if (!io.admit_copy(temporary_path.data(), final_path.data(), bytes, 64ULL * 1024 * 1024 + 65536, 134))
        {
            fail(Status::LimitExceeded);
            return;
        }
        std::strcpy(source_path.data(), source);
        copy = std::make_unique<ts::ImmutableFileCopy<UploadIo, RouteSourceHash>>(io, hash);
        if (copy->begin(source_path.data(), temporary_path.data(), final_path.data(), bytes, expected.data()) != ts::ImmutableCopyResult::Pending) fail(Status::Conflict);
    }
    UploadIo io;
    RouteLease route_lease;
    uint32_t generation;
    std::array<uint8_t, 32> actor{}, object{}, revision{}, expected{};
    std::array<uint8_t, 16> session{}, operation{};
    std::array<uint8_t, 48> ids{};
    bool query, mapped = false, held = false, terminal = false, absent = false, delivered = false;
    uint64_t touched;
    unsigned stage = 0;
    Status status = Status::Busy;
    RouteSourceHash hash;
    ts::InstalledPackageBinding binding;
    ts::ManifestExtensionsView extensions;
    PackageInputResource resource;
    std::array<uint8_t, 4096> scratch{};
    std::array<char, 160> source_path{}, temporary_path{}, final_path{}, cached_kml{};
    std::unique_ptr<HostlinkPackageInput> input;
    std::unique_ptr<StoredRoutePackage> stored;
    std::unique_ptr<ts::ImmutableFileCopy<UploadIo, RouteSourceHash>> copy;
    std::unique_ptr<ts::PackageRoutePublication<UploadIo, RouteSourceHash>> publication;
};
} // namespace
struct HostlinkPackageInstall::Impl
{
    std::filesystem::path root, database;
    HostlinkUploads& uploads;
    std::unique_ptr<Job> job;
    Impl(std::filesystem::path root, std::filesystem::path database, HostlinkUploads& uploads) : root(std::move(root)), database(std::move(database)), uploads(uploads) {}
};
HostlinkPackageInstall::HostlinkPackageInstall(std::filesystem::path root, std::filesystem::path database, HostlinkUploads& uploads)
    : impl_(std::make_unique<Impl>(std::move(root), std::move(database), uploads)) {}
HostlinkPackageInstall::~HostlinkPackageInstall() = default;
bool HostlinkPackageInstall::supported() { return true; }
bool HostlinkPackageInstall::identityBusy() const { return impl_->job && !impl_->job->terminal; }
void HostlinkPackageInstall::advance(uint64_t now, bool connected)
{
    auto& state = *impl_;
    if (!state.job) return;
    if (!connected || now < state.job->touched || now - state.job->touched >= 30000)
    {
        state.job.reset();
        return;
    }
    if (state.job->terminal) return;
    uint32_t generation = 0;
    std::array<uint8_t, 32> actor{};
    if (!author_identity(state.database, generation, actor))
    {
        state.job->fail(Status::StorageUnavailable);
        return;
    }
    for (unsigned step = 0; step < 256 && !state.job->terminal; ++step) state.job->pulse(generation, actor);
}
void HostlinkPackageInstall::close(const uint8_t* session)
{
    if (impl_->job && session && !std::memcmp(impl_->job->session.data(), session, 16)) impl_->job.reset();
}
::hostlink::management::Status HostlinkPackageInstall::process(const ::hostlink::management::EnvelopeView& request, uint64_t now,
                                                               uint8_t* output, size_t capacity, size_t& written)
try
{
    written = 0;
    const bool query = request.code == uint16_t(Operation::TrackPackageInstallStatus);
    if (!query && request.code != uint16_t(Operation::TrackPackageInstall)) return Status::Unsupported;
    ts::PrepareRequest decoded;
    if (!request.session || !output || capacity < 192 || !(query ? ts::decodePackageQueryRequest({request.body, request.body_size}, decoded) : ts::decodePrepareRequest({request.body, request.body_size}, decoded)) ||
        !decoded.install_request || !decoded.package_request) return Status::InvalidField;
    uint8_t session_bits = 0;
    for (unsigned i = 0; i < 16; ++i) session_bits |= request.session[i];
    if (!session_bits) return Status::InvalidField;
    auto& state = *impl_;
    advance(now, true);
    uint32_t generation = 0;
    std::array<uint8_t, 32> actor{};
    if (!author_identity(state.database, generation, actor)) return Status::StorageUnavailable;
    if (decoded.generation != generation) return Status::StaleIdentity;
    if (query && state.job && !state.job->held && state.job->delivered) state.job.reset();
    if (state.job && !state.job->matches(decoded, request.session))
    {
        if (state.job->held || std::memcmp(state.job->session.data(), request.session, 16)) return Status::Conflict;
        state.job.reset();
    }
    if (!state.job)
    {
        const auto allowed = state.uploads.prepare_install(request.session, decoded, now, actor.data());
        if (allowed != Status::Ok) return allowed;
        state.job = std::make_unique<Job>(state.root, decoded, request.session, actor, now);
    }
    auto& job = *state.job;
    if (!job.io.current()) return Status::StorageUnavailable;
    if (job.generation != generation || job.actor != actor) return Status::StaleIdentity;
    job.touched = now;
    if (job.terminal && job.status != Status::Ok)
    {
        const auto error = job.status;
        job.delivered = true;
        if (error == Status::Busy) state.job.reset();
        return error;
    }
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(5) || !writer.binary({job.operation.data(), 16}) || !writer.unsignedInteger(job.terminal ? (job.absent ? 2 : 1) : 0)) return Status::Internal;
    if (job.terminal)
    {
        char target[80]{};
        if (!ts::routePackageTarget({job.revision.data(), 32}, ts::PackageRouteFile::Kml, target, sizeof(target)) ||
            !writer.binary({job.object.data(), 32}) || !writer.binary({job.revision.data(), 32}) || !writer.text(target)) return Status::Internal;
    }
    else if (!writer.nil() || !writer.nil() || !writer.nil()) return Status::Internal;
    written = writer.size();
    if (job.terminal) job.delivered = true;
    return Status::Ok;
}
catch (...)
{
    written = 0;
    return Status::StorageUnavailable;
}
#else
struct HostlinkPackageInstall::Impl
{
};
HostlinkPackageInstall::HostlinkPackageInstall(std::filesystem::path, std::filesystem::path, HostlinkUploads&) : impl_(std::make_unique<Impl>()) {}
HostlinkPackageInstall::~HostlinkPackageInstall() = default;
bool HostlinkPackageInstall::supported() { return false; }
bool HostlinkPackageInstall::identityBusy() const { return false; }
void HostlinkPackageInstall::advance(uint64_t, bool) {}
void HostlinkPackageInstall::close(const uint8_t*) {}
::hostlink::management::Status HostlinkPackageInstall::process(const ::hostlink::management::EnvelopeView&, uint64_t, uint8_t*, size_t, size_t& written)
{
    written = 0;
    return ::hostlink::management::Status::Unsupported;
}
#endif
} // namespace platform::linux_runtime
