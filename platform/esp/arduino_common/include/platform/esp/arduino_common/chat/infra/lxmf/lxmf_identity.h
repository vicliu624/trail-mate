/**
 * @file lxmf_identity.h
 * @brief Reticulum/LXMF identity persistence for ESP Arduino targets
 */

#pragma once

#include "chat/infra/reticulum/reticulum_wire.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace chat::lxmf
{

class LxmfIdentity
{
  public:
    static constexpr size_t kEncPubKeySize = reticulum::kEncryptionPublicKeySize;
    static constexpr size_t kEncPrivKeySize = reticulum::kEncryptionPublicKeySize;
    static constexpr size_t kSigPubKeySize = reticulum::kSigningPublicKeySize;
    static constexpr size_t kSigPrivKeySize = reticulum::kSignatureSize;
    static constexpr size_t kSignatureSize = reticulum::kSignatureSize;

    bool init();
    bool isReady() const { return ready_; }

    const uint8_t* encryptionPublicKey() const { return enc_pub_.data(); }
    const uint8_t* signingPublicKey() const { return sig_pub_.data(); }
    const uint8_t* identityHash() const { return identity_hash_.data(); }
    const uint8_t* destinationHash() const { return destination_hash_.data(); }
    uint32_t nodeId() const { return node_id_; }

    void combinedPublicKey(uint8_t out_key[reticulum::kCombinedPublicKeySize]) const;

    bool sign(const uint8_t* message, size_t message_len,
              uint8_t out_signature[kSignatureSize]) const;

    static bool verify(const uint8_t sign_pub[kSigPubKeySize],
                       const uint8_t signature[kSignatureSize],
                       const uint8_t* message, size_t message_len);

    bool deriveSharedSecret(const uint8_t peer_public_key[kEncPubKeySize],
                            uint8_t out_secret[kEncPubKeySize]) const;

  private:
    bool loadFromPrefs();
    bool saveToPrefs() const;
    bool generateAndPersist();
    void recomputeDerivedFields();

    bool ready_ = false;
    uint32_t node_id_ = 0;
    std::array<uint8_t, kEncPubKeySize> enc_pub_ = {};
    std::array<uint8_t, kEncPrivKeySize> enc_priv_ = {};
    std::array<uint8_t, kSigPubKeySize> sig_pub_ = {};
    std::array<uint8_t, kSigPrivKeySize> sig_priv_ = {};
    std::array<uint8_t, reticulum::kTruncatedHashSize> identity_hash_ = {};
    std::array<uint8_t, reticulum::kTruncatedHashSize> destination_hash_ = {};
};

} // namespace chat::lxmf
