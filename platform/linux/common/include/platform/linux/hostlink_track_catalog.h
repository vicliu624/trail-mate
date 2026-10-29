#pragma once
#include "hostlink/management_envelope.h"
#include <filesystem>
#include <utility>

namespace platform::linux_runtime
{
// Stateless, worker-owned catalog reads. File locks observe active recordings
// without borrowing UI-thread tracker state. Cursors bind to the directory and
// HostLink session; changes require a fresh listing.
class HostlinkTrackCatalog
{
  public:
    explicit HostlinkTrackCatalog(std::filesystem::path root) : root_(std::move(root)) {}
    ::hostlink::management::Status read(const ::hostlink::management::EnvelopeView& request,
                                        uint8_t* output, size_t capacity, size_t& written) const;

  private:
    std::filesystem::path root_;
};
} // namespace platform::linux_runtime
