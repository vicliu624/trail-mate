#pragma once
#include "hostlink/management_envelope.h"
#include <filesystem>
#include <memory>

namespace platform::linux_runtime
{
class HostlinkUploads;
// HostLink-worker owner for IP receipt confirmation and imported history.
class HostlinkTrackConfirmation
{
  public:
    HostlinkTrackConfirmation(std::filesystem::path root, std::filesystem::path database, HostlinkUploads& uploads);
    ~HostlinkTrackConfirmation();
    static bool supported();
    bool identityBusy() const;
    void advance(uint64_t now, bool connected);
    void close(const uint8_t* session);
    ::hostlink::management::Status process(const ::hostlink::management::EnvelopeView&, uint64_t now, uint8_t* output, size_t capacity, size_t& written);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
