#pragma once
#include "chat/infra/voice/vmp_contact_secrets.h"
#include "chat/ports/i_mesh_peer_directory.h"
#include <functional>
#include <memory>

namespace platform::linux_runtime
{
// Publish public trust on the directory owner thread. Lookup/identity derivation
// runs only on the voice worker; no private key enters the UI snapshot.
class LinuxVoiceContacts final : public ::chat::voice::vmp::IVerifiedContactSecretProvider
{
  public:
    using Deriver = std::function<bool(::chat::MeshProtocol, uint32_t self, uint32_t peer,
                                       const uint8_t public_key[32], uint8_t secret[32])>;
    explicit LinuxVoiceContacts(Deriver = {});
    // Returns true on any trust/context change, including read failure. The
    // caller must cancel in-flight sessions when this returns true.
    bool refresh(uint32_t self, ::chat::MeshProtocol, ::chat::IMeshPeerDirectory&);
    bool contains(uint32_t peer, ::chat::MeshProtocol) const;
    bool lookupVerifiedContactSecret(uint32_t peer, uint8_t secret[32]) const override;
    // Shared primitive for the native identity adapter and independent peers.
    static bool deriveMeshtastic(const uint8_t private_key[32], const uint8_t public_key[32],
                                 const uint8_t peer_key[32], uint32_t self, uint32_t peer, uint8_t secret[32]);

  private:
    struct Snapshot;
    std::shared_ptr<const Snapshot> snapshot_;
    Deriver derive_;
};
} // namespace platform::linux_runtime
