#pragma once
#include "chat/ports/device_author.h"
#include "hostlink/management_envelope.h"
#include <filesystem>
#include <utility>

namespace platform::linux_runtime
{
class HostlinkRadioIdentity
{
  public:
    explicit HostlinkRadioIdentity(std::filesystem::path database) : database_(std::move(database)) {}
    static bool supported();
    bool restoreSupported() const;
    chat::DeviceAuthorResult readRadioIdentityInfo(uint8_t kind, uint32_t author_generation, uint8_t*, size_t, size_t&) const;
    chat::DeviceAuthorResult exportRadioIdentityBackup(uint8_t kind, uint32_t author_generation, uint8_t*, size_t, size_t&, const uint8_t* session, const uint8_t* request) const;
    hostlink::management::Status process(const hostlink::management::EnvelopeView&, uint8_t*, size_t, size_t&);

  private:
    std::filesystem::path database_;
};
} // namespace platform::linux_runtime
