#pragma once
#include "platform/linux/hostlink_package_input.h"
#include "platform/linux/hostlink_upload_io.h"
#include "platform/linux/route_image_source_io.h"
#include "track_sharing/attachment_upload_bindings.h"
#include "track_sharing/bound_sealed_upload.h"
#include "track_sharing/sealed_attachment_uploads.h"
#include "track_sharing/uploaded_manifest_reader.h"
#include <vector>

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
namespace ts = track_sharing;
// Shared native binding of immutable uploads to the generic attachment checker.
// The parent owns the storage lease and complete candidate view lifetime.
struct HostlinkAttachmentInput
{
    using Result = ts::ManifestFileResult;
    struct Resolver
    {
        explicit Resolver(HostlinkAttachmentInput& source) : source(source) {}
        ts::AttachmentResolveResult resolve(uint8_t ordinal, const ts::AttachmentEntryView& entry, char* path, size_t capacity)
        {
            const auto result = source.sealed->resolve(ordinal, entry, path, capacity);
            if (result != ts::AttachmentResolveResult::Ready) return result;
            if (source.resources.size() >= 64) return ts::AttachmentResolveResult::Invalid;
            PackageInputResource resource;
            if (std::strlen(path) >= resource.path.size()) return ts::AttachmentResolveResult::Invalid;
            std::copy_n(entry.hash.data, 32, resource.hash.begin());
            std::strcpy(resource.path.data(), path);
            resource.bytes = entry.bytes;
            source.resources.push_back(resource);
            return result;
        }
        HostlinkAttachmentInput& source;
    } resolver;
    using Attachments = ts::AttachmentPackageFileCheck<UploadIo, RouteSourceHash, Resolver, 64>;

    HostlinkAttachmentInput(UploadIo& io, uint32_t generation, const uint8_t* actor,
                            geocaching::ByteView mapping_id, const ts::ManifestView& candidate)
        : resolver(*this), io(io), generation(generation), has_mapping(mapping_id.size == 16), bound(candidate),
          mapping_reader(io, hash, mapping_upload, this->generation, this->actor.data())
    {
        if (actor) std::copy_n(actor, 32, this->actor.begin());
        if (has_mapping && mapping_id.data) std::copy_n(mapping_id.data, 16, this->mapping_id.begin());
    }
    Result checkAttachments(const ts::ManifestView& candidate, uint8_t* scratch, size_t capacity)
    {
        if (candidate.version == 1) return has_mapping ? Result::Invalid : Result::Ready;
        if (!ts::getManifestExtensions(candidate, extensions)) return Result::Invalid;
        if (!extensions.assets.count) return has_mapping ? Result::Invalid : Result::Ready;
        if (!has_mapping) return Result::Invalid;
        if (!mapping_started)
        {
            const auto loaded = ts::loadBoundSealedUpload(io, {mapping_id.data(), 16}, ts::UploadKind::AttachmentBindings, generation, actor.data(),
                                                          mapping_upload, verify, mapping_path.data(), scratch, capacity);
            if (loaded != ts::JournalStoreResult::Ready) return loaded == ts::JournalStoreResult::StorageFailure ? Result::StorageFailure : Result::Invalid;
            if (mapping_reader.begin(mapping_path.data(), mapping_raw.data(), mapping_raw.size()) != ts::FileIntegrityResult::Pending) return Result::Invalid;
            mapping_started = true;
            return Result::Pending;
        }
        if (!attachments)
        {
            const auto read = mapping_reader.step(scratch, capacity);
            if (read == ts::FileIntegrityResult::Pending) return Result::Pending;
            if (read != ts::FileIntegrityResult::Ready) return read == ts::FileIntegrityResult::HashMismatch ? Result::HashMismatch : Result::StorageFailure;
            if (!ts::decodeAttachmentUploadBindings(mapping_reader.bytes(), extensions.assets.count, mapping)) return Result::Invalid;
            sealed = std::make_unique<ts::SealedAttachmentUploads<UploadIo>>(io, mapping.ids, generation, actor.data());
            if (sealed->index(mapping.index_id.data, extensions.assets, index_path.data(), index_path.size()) != ts::AttachmentResolveResult::Ready) return Result::Invalid;
            attachments = std::make_unique<Attachments>(io, index_hash, asset_hash, resolver, window.data(), window.size(), fingerprints.data(), fingerprints.size(),
                                                        extensions.assets, candidate.file_hash, candidate.file_bytes);
            if (attachments->begin(index_path.data()) != ts::AttachmentPackageResult::Pending) return Result::Invalid;
        }
        const auto checked = attachments->step();
        return checked == ts::AttachmentPackageResult::Pending ? Result::Pending : checked == ts::AttachmentPackageResult::Ready        ? Result::Ready
                                                                               : checked == ts::AttachmentPackageResult::HashMismatch   ? Result::HashMismatch
                                                                               : checked == ts::AttachmentPackageResult::StorageFailure ? Result::StorageFailure
                                                                                                                                        : Result::Invalid;
    }
    bool matchesAttachments(const ts::ManifestView& candidate) const
    {
        if (candidate.raw.data != bound.raw.data || candidate.raw.size != bound.raw.size) return false;
        if (candidate.version == 1) return !has_mapping && !attachments;
        if (!ts::getManifestExtensions(candidate, extensions)) return false;
        return extensions.assets.count ? attachments && attachments->matchesIndex(index_path.data(), extensions.assets) : !has_mapping && !attachments;
    }

    void cancel()
    {
        mapping_reader.cancel();
        if (attachments) attachments->cancel();
    }
    const char* indexPath() const { return attachments ? index_path.data() : nullptr; }
    uint64_t availability() const { return attachments ? attachments->availability() : 0; }
    size_t resourceCount() const { return resources.size(); }
    bool resource(size_t index, PackageInputResource& output) const
    {
        output = {};
        if (index >= resources.size()) return false;
        output = resources[index];
        return true;
    }
    UploadIo& io;
    uint32_t generation;
    bool has_mapping, mapping_started = false;
    const ts::ManifestView& bound;
    std::array<uint8_t, 32> actor{};
    std::array<uint8_t, 16> mapping_id{};
    std::array<char, 64> mapping_path{}, index_path{};
    std::array<uint8_t, 1100> mapping_raw{};
    std::array<uint8_t, 4352> window{};
    std::array<ts::AttachmentFingerprint, 64> fingerprints{};
    ts::UploadJournal mapping_upload, verify;
    mutable ts::ManifestExtensionsView extensions;
    ts::AttachmentUploadBindingsView mapping;
    RouteSourceHash hash, index_hash, asset_hash;
    ts::UploadedManifestReader<UploadIo, RouteSourceHash, 1100> mapping_reader;
    std::unique_ptr<ts::SealedAttachmentUploads<UploadIo>> sealed;
    std::unique_ptr<Attachments> attachments;
    std::vector<PackageInputResource> resources;
};
#endif
} // namespace platform::linux_runtime
