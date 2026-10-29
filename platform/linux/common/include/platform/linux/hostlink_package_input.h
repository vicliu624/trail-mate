#pragma once
#include "track_sharing/manifest_file_check.h"
#include "track_sharing/prepare_request.h"
#include <array>
#include <memory>

namespace platform::linux_runtime
{
class UploadIo;
struct PackageInputResource
{
    std::array<uint8_t, 32> hash{};
    std::array<char, 64> path{};
    uint32_t bytes = 0;
};
// The caller retains the immutable upload/storage lease and I/O lifetime.
// Candidate mappings remain private until original signature, all available
// attachments, and KML integrity/geometry pass. No publication or re-signing.
class HostlinkPackageInput
{
  public:
    HostlinkPackageInput(UploadIo&, const track_sharing::PrepareRequest&, uint32_t generation, const uint8_t actor[32]);
    ~HostlinkPackageInput();
    track_sharing::ManifestFileResult begin();
    track_sharing::ManifestFileResult step(uint32_t generation, const uint8_t actor[32]);
    void cancel();
    bool ready() const;
    const track_sharing::VerifiedManifestView* verified() const;
    geocaching::ByteView signedBytes() const;
    const char* kmlPath() const;
    const char* signedPath() const;
    const char* indexPath() const;
    uint64_t availability() const;
    size_t resourceCount() const;
    bool resource(size_t index, PackageInputResource&) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
