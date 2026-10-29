#pragma once
#include "hostlink/management_envelope.h"
#include <filesystem>
#include <utility>
namespace platform::linux_runtime
{
class HostlinkTrackPublication
{
  public:
    explicit HostlinkTrackPublication(std::filesystem::path database) : database_(std::move(database)) {}
    bool supported() const;
    bool identityBusy() const;
    void close(const uint8_t* session = nullptr) const;
    ::hostlink::management::Status process(const ::hostlink::management::EnvelopeView&, uint8_t*, size_t, size_t&) const;

  private:
    std::filesystem::path database_;
};
} // namespace platform::linux_runtime
