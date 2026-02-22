/**
 * @file meshcore_identity.h
 * @brief MeshCore identity key management (Ed25519 + ECDH)
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace chat
{
namespace meshcore
{

class MeshCoreIdentity
{
  public:
    static constexpr size_t kPubKeySize = 32;
    static constexpr size_t kPrivKeySize = 64;
    static constexpr size_t kSignatureSize = 64;

    bool init();
    bool isReady() const { return ready_; }
    const uint8_t* publicKey() const { return pub_.data(); }
    uint8_t selfHash() const { return ready_ ? pub_[0] : 0; }

    bool sign(const uint8_t* message, size_t message_len,
              uint8_t out_signature[kSignatureSize]) const;

    static bool verify(const uint8_t pubkey[kPubKeySize],
                       const uint8_t signature[kSignatureSize],
                       const uint8_t* message, size_t message_len);

    bool deriveSharedSecret(const uint8_t peer_pubkey[kPubKeySize],
                            uint8_t out_secret[kPubKeySize]) const;

  private:
    bool loadFromPrefs();
    bool saveToPrefs() const;
    bool generateAndPersist();
    static bool isValidPublicHash(uint8_t hash);

    bool ready_ = false;
    std::array<uint8_t, kPubKeySize> pub_ = {};
    std::array<uint8_t, kPrivKeySize> priv_ = {};
};

} // namespace meshcore
} // namespace chat
