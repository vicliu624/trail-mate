#include "platform/linux/hostlink_package_input.h"
#include "hostlink_upload_io.h"
#include "platform/linux/hostlink_attachment_input.h"
#include "platform/linux/track_manifest_crypto.h"
#include "route_image_source_io.h"
#include "track_sharing/bound_sealed_upload.h"
#include "track_sharing/sealed_attachment_uploads.h"
#include "track_sharing/signed_package_file_check.h"
#include <vector>

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
namespace ts = track_sharing;
struct HostlinkPackageInput::Impl
{
    using Result = ts::ManifestFileResult;
    Impl(UploadIo& io, const ts::PrepareRequest& request, uint32_t generation, const uint8_t* actor)
        : io(io), generation(generation), has_mapping(request.package_upload.size == 16),
          attachment_input(io, generation, actor, request.package_upload, verified.manifest),
          check(io, hash, crypto, signed_upload, kml_upload, this->generation, this->actor.data(), names.data(), names.size(), verified, workspace.data(), workspace.size())
    {
        valid = request.install_request && request.package_request && !request.expected_parent.size && request.generation == generation && generation && actor &&
                request.kml_upload.data && request.kml_upload.size == 16 && request.manifest_upload.data && request.manifest_upload.size == 16 &&
                request.expected_object.data && request.expected_object.size == 32 && request.expected_revision.data && request.expected_revision.size == 32 &&
                (!request.package_upload.size || (request.package_upload.data && has_mapping));
        if (!valid) return;
        std::copy_n(actor, 32, this->actor.begin());
        std::copy_n(request.kml_upload.data, 16, ids.begin());
        std::copy_n(request.manifest_upload.data, 16, ids.begin() + 16);
        if (has_mapping) std::copy_n(request.package_upload.data, 16, ids.begin() + 32);
        std::copy_n(request.expected_object.data, 32, object.begin());
        std::copy_n(request.expected_revision.data, 32, revision.begin());
    }
    ~Impl() { cancel(); }
    Result begin()
    {
        if (state != Result::Idle) return Result::Busy;
        if (!valid) return state = Result::Invalid;
        if (!io.current()) return state = Result::StorageFailure;
        auto loaded = ts::loadBoundSealedUpload(io, {ids.data(), 16}, ts::UploadKind::Kml, generation, actor.data(), kml_upload, verify,
                                                kml_path.data(), scratch.data(), scratch.size());
        if (loaded == ts::JournalStoreResult::Ready)
            loaded = ts::loadBoundSealedUpload(io, {ids.data() + 16, 16}, ts::UploadKind::SignedManifest, generation, actor.data(), signed_upload, verify,
                                               signed_path.data(), scratch.data(), scratch.size());
        if (loaded != ts::JournalStoreResult::Ready) return state = loaded == ts::JournalStoreResult::StorageFailure ? Result::StorageFailure : Result::Invalid;
        return state = check.begin(signed_path.data(), kml_path.data(), {object.data(), 32}, {revision.data(), 32}, raw.data(), raw.size());
    }
    Result checkAttachments(const ts::ManifestView& candidate, uint8_t* scratch, size_t capacity)
    {
        return attachment_input.checkAttachments(candidate, scratch, capacity);
    }
    bool matchesAttachments(const ts::ManifestView& candidate) const { return attachment_input.matchesAttachments(candidate); }
    bool ready() const { return state == Result::Ready && io.current() && check.ready() && matchesAttachments(verified.manifest); }
    void cancel()
    {
        check.cancel();
        attachment_input.cancel();
        state = Result::Cancelled;
    }
    UploadIo& io;
    uint32_t generation;
    bool has_mapping, valid = false;
    std::array<uint8_t, 32> actor{}, object{}, revision{};
    std::array<uint8_t, 48> ids{};
    std::array<char, 64> kml_path{}, signed_path{};
    std::array<char, 2048> names{};
    std::array<uint8_t, 4166> raw{};
    std::array<uint8_t, 4200> workspace{};
    std::array<uint8_t, 4096> scratch{};
    ts::UploadJournal signed_upload, kml_upload, verify;
    ts::VerifiedManifestView verified;
    RouteSourceHash hash;
    LinuxTrackManifestCrypto crypto;
    HostlinkAttachmentInput attachment_input;
    ts::SignedPackageFileCheck<UploadIo, RouteSourceHash, LinuxTrackManifestCrypto> check;
    Result state = Result::Idle;
};
HostlinkPackageInput::HostlinkPackageInput(UploadIo& io, const ts::PrepareRequest& request, uint32_t generation, const uint8_t actor[32])
    : impl_(std::make_unique<Impl>(io, request, generation, actor)) {}
HostlinkPackageInput::~HostlinkPackageInput() = default;
ts::ManifestFileResult HostlinkPackageInput::begin() { return impl_->begin(); }
ts::ManifestFileResult HostlinkPackageInput::step(uint32_t generation, const uint8_t actor[32])
{
    if (!impl_->io.current())
    {
        impl_->cancel();
        return impl_->state = ts::ManifestFileResult::StorageFailure;
    }
    if (!actor || generation != impl_->generation || std::memcmp(actor, impl_->actor.data(), 32))
    {
        impl_->cancel();
        return impl_->state = ts::ManifestFileResult::Invalid;
    }
    return impl_->state == ts::ManifestFileResult::Pending ? impl_->state = impl_->check.step(*impl_, impl_->scratch.data(), impl_->scratch.size()) : impl_->state;
}
void HostlinkPackageInput::cancel() { impl_->cancel(); }
bool HostlinkPackageInput::ready() const { return impl_->ready(); }
const ts::VerifiedManifestView* HostlinkPackageInput::verified() const { return ready() ? &impl_->verified : nullptr; }
geocaching::ByteView HostlinkPackageInput::signedBytes() const { return ready() ? impl_->check.signedBytes() : geocaching::ByteView{}; }
const char* HostlinkPackageInput::kmlPath() const { return ready() ? impl_->kml_path.data() : nullptr; }
const char* HostlinkPackageInput::signedPath() const { return ready() ? impl_->signed_path.data() : nullptr; }
const char* HostlinkPackageInput::indexPath() const { return ready() ? impl_->attachment_input.indexPath() : nullptr; }
uint64_t HostlinkPackageInput::availability() const { return ready() ? impl_->attachment_input.availability() : 0; }
size_t HostlinkPackageInput::resourceCount() const { return ready() ? impl_->attachment_input.resourceCount() : 0; }
bool HostlinkPackageInput::resource(size_t index, PackageInputResource& output) const
{
    output = {};
    return ready() && impl_->attachment_input.resource(index, output);
}
#else
struct HostlinkPackageInput::Impl
{
};
HostlinkPackageInput::HostlinkPackageInput(UploadIo&, const track_sharing::PrepareRequest&, uint32_t, const uint8_t*) : impl_(std::make_unique<Impl>()) {}
HostlinkPackageInput::~HostlinkPackageInput() = default;
track_sharing::ManifestFileResult HostlinkPackageInput::begin() { return track_sharing::ManifestFileResult::Invalid; }
track_sharing::ManifestFileResult HostlinkPackageInput::step(uint32_t, const uint8_t*) { return begin(); }
void HostlinkPackageInput::cancel() {}
bool HostlinkPackageInput::ready() const { return false; }
const track_sharing::VerifiedManifestView* HostlinkPackageInput::verified() const { return nullptr; }
geocaching::ByteView HostlinkPackageInput::signedBytes() const { return {}; }
const char* HostlinkPackageInput::kmlPath() const { return nullptr; }
const char* HostlinkPackageInput::signedPath() const { return nullptr; }
const char* HostlinkPackageInput::indexPath() const { return nullptr; }
uint64_t HostlinkPackageInput::availability() const { return 0; }
size_t HostlinkPackageInput::resourceCount() const { return 0; }
bool HostlinkPackageInput::resource(size_t, PackageInputResource& output) const
{
    output = {};
    return false;
}
#endif
} // namespace platform::linux_runtime
