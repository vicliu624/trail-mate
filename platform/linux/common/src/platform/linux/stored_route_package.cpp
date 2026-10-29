#include "platform/linux/stored_route_package.h"
#include "hostlink_upload_io.h"
#include "platform/linux/track_manifest_crypto.h"
#include "route_image_source_io.h"
#include "track_sharing/attachment_package_file_check.h"
#include "track_sharing/installed_package_binding.h"
#include "track_sharing/package_cache_paths.h"
#include "track_sharing/signed_manifest_reader.h"
#include <array>

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
namespace ts = track_sharing;
struct StoredRoutePackage::Impl
{
    using Result = ts::ManifestFileResult;
    using Attachments = ts::AttachmentPackageFileCheck<UploadIo, RouteSourceHash, Impl, 160>;
    Impl(UploadIo& io, const uint8_t* object, const uint8_t* revision)
        : io(io), reader(io), kml(io, hash, names.data(), names.size())
    {
        if (object && revision)
        {
            std::copy_n(object, 32, this->object.begin());
            std::copy_n(revision, 32, this->revision.begin());
            valid = true;
        }
    }
    ~Impl() { cancel(); }
    ts::AttachmentResolveResult resolve(uint8_t ordinal, const ts::AttachmentEntryView& entry, char* path, size_t capacity)
    {
        if (ordinal >= binding.count) return ts::AttachmentResolveResult::Invalid;
        if (!(binding.availability & (uint64_t(1) << ordinal))) return ts::AttachmentResolveResult::Missing;
        return ts::resolvePackageCachePath({revision.data(), 32}, ts::ObjectFile::AttachmentResource, entry.hash, path, capacity)
                   ? ts::AttachmentResolveResult::Ready
                   : ts::AttachmentResolveResult::Invalid;
    }
    bool matchesAttachments(const ts::ManifestView& candidate) const
    {
        return authenticated && candidate.raw.data == verified.manifest.raw.data && candidate.raw.size == verified.manifest.raw.size && (!binding.count || (attachments && attachments->ready()));
    }
    Result begin()
    {
        if (state != Result::Idle) return Result::Busy;
        if (!valid || !io.current() || !ts::routePackageTarget({revision.data(), 32}, ts::PackageRouteFile::Binding, path.data(), path.size()) ||
            !ts::routePackageTarget({revision.data(), 32}, ts::PackageRouteFile::Kml, route.data(), route.size())) return state = Result::Invalid;
        const auto marker = io.open(path.data(), UploadIo::Mode::Read);
        if (!marker.handle)
        {
            if (marker.status != UploadIo::OpenStatus::Missing) return state = Result::StorageFailure;
            const auto file = io.open(route.data(), UploadIo::Mode::Read);
            if (file.handle)
            {
                io.close(file.handle);
                return state = Result::Invalid;
            }
            absent = file.status == UploadIo::OpenStatus::Missing;
            return state = absent ? Result::Ready : Result::StorageFailure;
        }
        uint64_t bytes = 0;
        const bool read = io.size(marker.handle, bytes) && bytes == ts::kInstalledPackageBindingBytes && io.read(marker.handle, scratch.data(), bytes) == bytes;
        const bool closed = io.close(marker.handle);
        if (!read || !closed || !ts::decodeInstalledPackageBinding(scratch.data(), bytes, binding) ||
            std::memcmp(binding.object, object.data(), 32) || std::memcmp(binding.revision, revision.data(), 32)) return state = Result::Invalid;
        const auto file = io.open(route.data(), UploadIo::Mode::Read);
        if (!file.handle)
        {
            absent = file.status == UploadIo::OpenStatus::Missing;
            return state = absent ? Result::Ready : Result::StorageFailure;
        }
        if (!io.close(file.handle) || !ts::resolvePackageCachePath({revision.data(), 32}, ts::ObjectFile::SignedManifest, {}, path.data(), path.size())) return state = Result::StorageFailure;
        return state = reader.begin(path.data(), raw.data(), raw.size()) == ts::FileIntegrityResult::Pending ? Result::Pending : Result::StorageFailure;
    }
    Result step()
    {
        if (!io.current())
        {
            cancel();
            return state = Result::StorageFailure;
        }
        if (state != Result::Pending) return state;
        if (checking_kml) return state = kml.step(scratch.data(), scratch.size());
        if (!authenticated)
        {
            const auto read = reader.step();
            if (read == ts::FileIntegrityResult::Pending) return state;
            if (read != ts::FileIntegrityResult::Ready) return state = Result::StorageFailure;
            if (ts::verifyManifest(reader.bytes(), crypto, hash, workspace.data(), workspace.size(), verified) != ts::ManifestVerification::Valid ||
                std::memcmp(verified.object_id, object.data(), 32) || std::memcmp(verified.revision_hash, revision.data(), 32) ||
                verified.manifest.file_bytes != binding.kml_bytes || std::memcmp(verified.manifest.file_hash.data, binding.kml_hash, 32)) return state = Result::Invalid;
            if (verified.manifest.version == 1)
            {
                if (binding.count) return state = Result::Invalid;
            }
            else if (!ts::getManifestExtensions(verified.manifest, extensions) || extensions.assets.count != binding.count || extensions.assets.bytes != binding.index_bytes ||
                     (binding.count && std::memcmp(extensions.assets.hash.data, binding.index_hash, 32))) return state = Result::Invalid;
            authenticated = true;
            if (binding.count)
            {
                attachments = std::make_unique<Attachments>(io, hash, asset_hash, *this, window.data(), window.size(), fingerprints.data(), fingerprints.size(),
                                                            extensions.assets, verified.manifest.file_hash, verified.manifest.file_bytes);
                if (!ts::resolvePackageCachePath({revision.data(), 32}, ts::ObjectFile::AttachmentIndex, {}, path.data(), path.size()) ||
                    attachments->begin(path.data()) != ts::AttachmentPackageResult::Pending) return state = Result::Invalid;
            }
        }
        if (attachments && !attachments->ready())
        {
            const auto checked = attachments->step();
            if (checked == ts::AttachmentPackageResult::Pending) return state;
            if (checked != ts::AttachmentPackageResult::Ready) return state = checked == ts::AttachmentPackageResult::HashMismatch ? Result::HashMismatch : checked == ts::AttachmentPackageResult::StorageFailure ? Result::StorageFailure
                                                                                                                                                                                                                   : Result::Invalid;
        }
        const auto begun = verified.manifest.version == 2 ? kml.beginVerifiedPackage(route.data(), verified, *this) : kml.begin(route.data(), verified);
        checking_kml = begun == Result::Pending;
        return state = begun;
    }
    void cancel()
    {
        reader.cancel();
        kml.cancel();
        if (attachments) attachments->cancel();
        state = Result::Cancelled;
    }
    UploadIo& io;
    std::array<uint8_t, 32> object{}, revision{};
    std::array<char, 160> path{}, route{};
    std::array<char, 2048> names{};
    std::array<uint8_t, 4166> raw{};
    std::array<uint8_t, 4200> workspace{};
    std::array<uint8_t, 4352> window{};
    std::array<uint8_t, 4096> scratch{};
    std::array<ts::AttachmentFingerprint, 64> fingerprints{};
    RouteSourceHash hash, asset_hash;
    LinuxTrackManifestCrypto crypto;
    ts::InstalledPackageBinding binding;
    ts::VerifiedManifestView verified;
    ts::ManifestExtensionsView extensions;
    ts::SignedManifestReader<UploadIo> reader;
    ts::ManifestFileCheck<UploadIo, RouteSourceHash> kml;
    std::unique_ptr<Attachments> attachments;
    Result state = Result::Idle;
    bool valid = false, authenticated = false, checking_kml = false, absent = false;
};
StoredRoutePackage::StoredRoutePackage(UploadIo& io, const uint8_t object[32], const uint8_t revision[32]) : impl_(std::make_unique<Impl>(io, object, revision)) {}
StoredRoutePackage::~StoredRoutePackage() = default;
ts::ManifestFileResult StoredRoutePackage::begin() { return impl_->begin(); }
ts::ManifestFileResult StoredRoutePackage::step() { return impl_->step(); }
bool StoredRoutePackage::ready() const { return impl_->state == ts::ManifestFileResult::Ready && !impl_->absent && impl_->authenticated && impl_->kml.ready() && impl_->io.current(); }
bool StoredRoutePackage::missing() const { return impl_->state == ts::ManifestFileResult::Ready && impl_->absent && impl_->io.current(); }
void StoredRoutePackage::cancel() { impl_->cancel(); }
#else
struct StoredRoutePackage::Impl
{
};
StoredRoutePackage::StoredRoutePackage(UploadIo&, const uint8_t*, const uint8_t*) : impl_(std::make_unique<Impl>()) {}
StoredRoutePackage::~StoredRoutePackage() = default;
track_sharing::ManifestFileResult StoredRoutePackage::begin() { return track_sharing::ManifestFileResult::Invalid; }
track_sharing::ManifestFileResult StoredRoutePackage::step() { return begin(); }
bool StoredRoutePackage::ready() const { return false; }
bool StoredRoutePackage::missing() const { return false; }
void StoredRoutePackage::cancel() {}
#endif
} // namespace platform::linux_runtime
