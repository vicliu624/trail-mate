#pragma once
#include "platform/linux/route_image_source_io.h"
#include "platform/linux/track_blob_upload.h"
#include "track_sharing/attachment_package_file_check.h"
#include "track_sharing/object_paths.h"
#include <vector>
#include <memory>

namespace platform::linux_runtime
{
class TrackPublicationAssets
{
    struct Resource
    {
        std::array<uint8_t, 32> hash{};
        std::array<char, 160> path{};
        uint32_t bytes = 0;
    };
    struct Resolver
    {
        TrackPublicationAssets& owner;
        track_sharing::AttachmentResolveResult resolve(uint8_t, const track_sharing::AttachmentEntryView& entry, char* path, size_t capacity)
        {
            using Result = track_sharing::AttachmentResolveResult;
            if (owner.resources_.size() >= 64 || !track_sharing::resolveObjectPath({owner.object_.data(), 32}, track_sharing::ObjectFile::AttachmentResource, entry.hash, path, capacity)) return Result::Invalid;
            const auto file = owner.io_.open(path, UploadIo::Mode::Read);
            if (!file.handle) return file.status == UploadIo::OpenStatus::Missing ? Result::Missing : Result::StorageFailure;
            if (!owner.io_.close(file.handle)) return Result::StorageFailure;
            Resource resource;
            std::copy_n(entry.hash.data, 32, resource.hash.begin());
            resource.bytes = entry.bytes;
            if (std::strlen(path) >= resource.path.size()) return Result::Invalid;
            std::strcpy(resource.path.data(), path);
            owner.resources_.push_back(resource);
            return Result::Ready;
        }
    } resolver_{*this};

  public:
    TrackPublicationAssets(UploadIo& io, const track_sharing::ManifestView& manifest, const uint8_t object[32],
                           const track_sharing::PublicationDirectory& directory, const uint8_t local[16], TrackPublicationTransport transport,
                           TrackPublicationMessageSource messages, std::function<bool()> current)
        : io_(io), directory_(directory), transport_(std::move(transport)), messages_(std::move(messages)), current_(std::move(current))
    {
        std::copy_n(object, 32, object_.begin());
        std::copy_n(local, 16, local_.begin());
        valid_ = track_sharing::getManifestExtensions(manifest, extensions_) && extensions_.assets.count &&
                 track_sharing::resolveObjectPath({object_.data(), 32}, track_sharing::ObjectFile::AttachmentIndex, extensions_.assets.hash, index_.data(), index_.size());
        if (!valid_) return;
        check_ = std::make_unique<Check>(io_, index_hash_, resource_hash_, resolver_, window_.data(), window_.size(), fingerprints_.data(), fingerprints_.size(),
                                         extensions_.assets, manifest.file_hash, manifest.file_bytes);
        valid_ = check_->begin(index_.data()) == track_sharing::AttachmentPackageResult::Pending;
    }
    bool done() const { return done_; }
    void pause()
    {
        if (upload_) upload_->pause();
    }
    bool resume(uint64_t now) { return !upload_ || upload_->resume(now); }
    track_sharing::NetworkSessionResult advance(uint64_t now)
    {
        using namespace track_sharing;
        if (!valid_ || !current_ || !current_() || !io_.current()) return NetworkSessionResult::Conflict;
        if (done_) return NetworkSessionResult::Progress;
        if (!check_->ready())
        {
            const auto result = check_->step();
            if (result == AttachmentPackageResult::Pending) return NetworkSessionResult::Progress;
            if (result != AttachmentPackageResult::Ready) return result == AttachmentPackageResult::StorageFailure ? NetworkSessionResult::StorageFailure : NetworkSessionResult::Conflict;
        }
        if (upload_)
        {
            const auto result = upload_->advance(now);
            if (!upload_->done()) return result;
            upload_.reset();
            ++selected_;
        }
        if (selected_ > resources_.size())
        {
            done_ = true;
            return NetworkSessionResult::Progress;
        }
        const char* path = selected_ ? resources_[selected_ - 1].path.data() : index_.data();
        const auto* hash = selected_ ? resources_[selected_ - 1].hash.data() : extensions_.assets.hash.data;
        const auto bytes = selected_ ? resources_[selected_ - 1].bytes : extensions_.assets.bytes;
        upload_ = std::make_unique<TrackBlobUpload>(io_, directory_, local_.data(), path, hash, bytes, transport_, messages_, current_);
        return upload_->advance(now);
    }
    bool receive(const chat::lxmf::CustomDeliveryView& view) { return valid_ && !done_ && upload_ && upload_->receive(view); }

  private:
    using Check = track_sharing::AttachmentPackageFileCheck<UploadIo, RouteSourceHash, Resolver, 160>;
    UploadIo& io_;
    const track_sharing::PublicationDirectory& directory_;
    TrackPublicationTransport transport_;
    TrackPublicationMessageSource messages_;
    std::function<bool()> current_;
    std::array<uint8_t, 32> object_{};
    std::array<uint8_t, 16> local_{};
    std::array<char, 160> index_{};
    track_sharing::ManifestExtensionsView extensions_;
    RouteSourceHash index_hash_, resource_hash_;
    std::array<uint8_t, 4352> window_{};
    std::array<track_sharing::AttachmentFingerprint, 64> fingerprints_{};
    std::unique_ptr<Check> check_;
    std::unique_ptr<TrackBlobUpload> upload_;
    std::vector<Resource> resources_;
    size_t selected_ = 0;
    bool valid_ = false, done_ = false;
};
} // namespace platform::linux_runtime
