#pragma once
#include "chat/ports/device_author.h"
#include "hostlink/management_envelope.h"
#include <filesystem>
#include <utility>

namespace platform::linux_runtime
{
// Serialized HostLink owner. No cached identity/private bytes survive a request.
class HostlinkIdentityBackup
{
  public:
    explicit HostlinkIdentityBackup(std::filesystem::path database) : database_(std::move(database)) {}
    static bool supported();
    bool restoreSupported() const;
    chat::DeviceAuthorResult exportDeviceIdentityBackup(uint32_t generation, uint8_t*, size_t capacity, size_t& written) const;
    hostlink::management::Status process(const hostlink::management::EnvelopeView&, uint8_t*, size_t, size_t&);

  private:
    std::filesystem::path database_;
};
} // namespace platform::linux_runtime
