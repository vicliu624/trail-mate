#pragma once
#include "track_sharing/manifest_file_check.h"
#include <memory>

namespace platform::linux_runtime
{
class UploadIo;
class StoredRoutePackage
{
  public:
    StoredRoutePackage(UploadIo&, const uint8_t object[32], const uint8_t revision[32]);
    ~StoredRoutePackage();
    track_sharing::ManifestFileResult begin();
    track_sharing::ManifestFileResult step();
    bool ready() const;
    bool missing() const;
    void cancel();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
