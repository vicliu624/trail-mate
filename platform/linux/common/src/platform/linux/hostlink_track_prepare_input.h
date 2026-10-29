#pragma once
#include "platform/linux/hostlink_attachment_input.h"
#include "track_sharing/prepare_upload_inputs.h"
#include "track_sharing/uploaded_candidate_check.h"

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
// Unsigned local-author input. Ready proves uploaded bytes, KML geometry and
// attachment bindings; signing and revision ownership remain a separate stage.
struct HostlinkTrackPrepareInput
{
    using Result = track_sharing::ManifestFileResult;
    HostlinkTrackPrepareInput(UploadIo& io, const track_sharing::PrepareRequest& source,
                              const uint8_t* author, const uint8_t* actor)
        : io(io), generation(source.generation),
          attachment_input(io, generation, actor, source.package_upload, view),
          check(io, hash, manifest_upload, kml_upload, generation, this->actor.data(), names.data(), names.size(), view)
    {
        if (!author || !actor || source.install_request || source.query_request ||
            !source.operation.data || source.operation.size != 16 || !source.kml_upload.data || source.kml_upload.size != 16 ||
            !source.manifest_upload.data || source.manifest_upload.size != 16 || !generation ||
            (source.expected_parent.size && (!source.expected_parent.data || source.expected_parent.size != 32)) ||
            (source.package_upload.size && (!source.package_request || !source.package_upload.data || source.package_upload.size != 16))) return;
        std::copy_n(author, 64, public_key.begin());
        std::copy_n(actor, 32, this->actor.begin());
        std::copy_n(source.operation.data, 16, bindings.begin());
        std::copy_n(source.kml_upload.data, 16, bindings.begin() + 16);
        std::copy_n(source.manifest_upload.data, 16, bindings.begin() + 32);
        request = source;
        request.operation = {bindings.data(), 16};
        request.kml_upload = {bindings.data() + 16, 16};
        request.manifest_upload = {bindings.data() + 32, 16};
        if (source.expected_parent.size)
        {
            std::copy_n(source.expected_parent.data, 32, bindings.begin() + 48);
            request.expected_parent = {bindings.data() + 48, 32};
        }
        if (source.package_upload.size)
        {
            std::copy_n(source.package_upload.data, 16, bindings.begin() + 80);
            request.package_upload = {bindings.data() + 80, 16};
        }
        valid = true;
    }
    ~HostlinkTrackPrepareInput() { cancel(); }
    Result begin()
    {
        if (state != Result::Idle) return Result::Busy;
        if (!valid) return state = Result::Invalid;
        if (!io.current()) return state = Result::StorageFailure;
        const auto loaded = track_sharing::loadPrepareUploadInputs(io, request, generation, actor.data(), kml_upload, manifest_upload,
                                                                   verify, kml_path.data(), manifest_path.data(), scratch.data(), scratch.size());
        if (loaded != track_sharing::JournalStoreResult::Ready)
            return state = loaded == track_sharing::JournalStoreResult::StorageFailure ? Result::StorageFailure : Result::Invalid;
        return state = check.begin(request, manifest_path.data(), kml_path.data(), raw.data(), raw.size(), {public_key.data(), 64});
    }
    Result step(uint32_t current_generation, const uint8_t* current_actor)
    {
        if (!io.current())
        {
            cancel();
            return state = Result::StorageFailure;
        }
        if (!current_actor || current_generation != generation || std::memcmp(current_actor, actor.data(), 32))
        {
            cancel();
            return state = Result::Invalid;
        }
        if (state != Result::Pending) return state;
        state = check.step(scratch.data(), scratch.size(), *this);
        if (state == Result::Ready && request.package_request && view.version != 2) state = Result::Invalid;
        return state;
    }
    Result checkAttachments(const track_sharing::ManifestView& candidate, uint8_t* bytes, size_t capacity)
    {
        if (!request.package_request) return Result::Invalid;
        return attachment_input.checkAttachments(candidate, bytes, capacity);
    }
    bool matchesAttachments(const track_sharing::ManifestView& candidate) const
    {
        // Recovery verifies a durable signed manifest in a separate arena.
        // Accept the same canonical bytes, never an unrelated view that only
        // happens to describe a matching asset index.
        return request.package_request && candidate.raw.data && view.raw.data && candidate.raw.size == view.raw.size &&
               !std::memcmp(candidate.raw.data, view.raw.data, view.raw.size) && attachment_input.matchesAttachments(view);
    }
    bool ready() const { return state == Result::Ready && io.current() && check.ready(); }
    bool matchesCandidate(const track_sharing::ManifestView& candidate) const
    {
        return ready() && check.matchesCandidate(candidate) && (candidate.version == 1 || matchesAttachments(candidate));
    }
    geocaching::ByteView manifest() const { return ready() ? check.manifest() : geocaching::ByteView{}; }
    void cancel()
    {
        check.cancel();
        attachment_input.cancel();
        state = Result::Cancelled;
    }

    UploadIo& io;
    uint32_t generation;
    bool valid = false;
    std::array<uint8_t, 64> public_key{};
    std::array<uint8_t, 32> actor{};
    std::array<uint8_t, 96> bindings{};
    std::array<char, 64> kml_path{}, manifest_path{};
    std::array<char, 2048> names{};
    std::array<uint8_t, 4096> raw{}, scratch{};
    track_sharing::PrepareRequest request;
    track_sharing::UploadJournal kml_upload, manifest_upload, verify;
    track_sharing::ManifestView view;
    RouteSourceHash hash;
    HostlinkAttachmentInput attachment_input;
    track_sharing::UploadedCandidateCheck<UploadIo, RouteSourceHash> check;
    Result state = Result::Idle;
};
#endif
} // namespace platform::linux_runtime
