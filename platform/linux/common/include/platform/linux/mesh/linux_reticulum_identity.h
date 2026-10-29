#pragma once
#include "chat/ports/device_identity_restore.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace platform::linux_runtime
{
// Worker-owned Reticulum identity. Existing seed records and formal expanded
// backup records remain distinct; no MeshCore key is reused. A lifetime Linux
// OFD read lease excludes replacement while another identity owner is live.
class LinuxReticulumIdentity
{
  public:
    explicit LinuxReticulumIdentity(std::filesystem::path database);
    ~LinuxReticulumIdentity();
    LinuxReticulumIdentity(const LinuxReticulumIdentity&) = delete;
    LinuxReticulumIdentity& operator=(const LinuxReticulumIdentity&) = delete;
    bool begin();
    bool ready() const { return ready_; }
    uint32_t generation() const { return ready_ ? generation_ : 0; }
    const std::array<uint8_t, 64>& publicKey() const { return public_; }
    const std::array<uint8_t, 16>& identityHash() const { return identity_; }
    const std::array<uint8_t, 16>& destinationHash() const { return destination_; }
    uint32_t nodeId() const;
    bool sign(const uint8_t*, size_t, uint8_t signature[64]) const;
    static bool verify(const uint8_t public_key[32], const uint8_t*, size_t, const uint8_t signature[64]);
    bool sharedSecret(const uint8_t peer[32], uint8_t secret[32]) const;
    // Format 1: X25519 private32 + Ed25519 seed32. Format 0: clamped
    // X25519 private32 + expanded Ed25519 private64, as in formal backups.
    uint8_t privateFormat() const { return format_; }
    bool copyPrivateKey(uint32_t expected_generation, uint8_t*, size_t capacity, size_t& written) const;
    static bool validateKeyPair(uint8_t format, const uint8_t* private_key, size_t size, const uint8_t public_key[64]);
    // Composition must quiesce all identity users before calling. This only
    // commits storage; a successful/uncertain write retires this object until
    // process restart. Expected generation/public key are compared atomically.
    chat::DeviceIdentityRestoreResult replaceKeyPair(uint32_t expected_generation, const uint8_t expected_public[64],
                                                     uint8_t format, const uint8_t* private_key, size_t size,
                                                     const uint8_t public_key[64]);
    const std::string& lastError() const { return error_; }

  private:
    bool overlaps(const void*, size_t) const;
    std::filesystem::path database_;
    std::array<uint8_t, 96> private_{};
    std::array<uint8_t, 64> public_{};
    std::array<uint8_t, 16> identity_{}, destination_{};
    uint32_t generation_ = 0;
    uint8_t format_ = 1;
    int owner_fd_ = -1;
    bool retired_ = false;
    bool ready_ = false;
    std::string error_;
};
} // namespace platform::linux_runtime
