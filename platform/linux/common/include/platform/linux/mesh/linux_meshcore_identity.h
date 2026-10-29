#pragma once
#include "platform/linux/identity_owner_lease.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace platform::linux_runtime
{
// Persistent protocol identity, independent of Meshtastic's X25519 key tables.
// Initialization does storage work; getters/signing never reopen the database.
class LinuxMeshCoreIdentity
{
  public:
    ~LinuxMeshCoreIdentity();
    LinuxMeshCoreIdentity() = default;
    LinuxMeshCoreIdentity(const LinuxMeshCoreIdentity&) = delete;
    LinuxMeshCoreIdentity& operator=(const LinuxMeshCoreIdentity&) = delete;
    bool begin();
    bool ready() const { return ready_; }
    uint32_t nodeId() const;
    const std::array<uint8_t, 32>& publicKey() const { return public_; }
    bool sign(const uint8_t* message, std::size_t size, uint8_t signature[64]) const;
    bool sharedSecret(const uint8_t peer[32], uint8_t secret[32]) const;
    const char* lastError() const { return error_.c_str(); }

  private:
    IdentityOwnerLease owner_;
    std::array<uint8_t, 32> public_{};
    std::array<uint8_t, 64> private_{};
    bool ready_ = false;
    std::string error_;
};
} // namespace platform::linux_runtime
