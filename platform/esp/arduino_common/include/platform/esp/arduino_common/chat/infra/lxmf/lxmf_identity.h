/*
 * Reticulum License - RNS/LXMF-derived portions.
 * Derived from https://github.com/markqvist/Reticulum
 * Copyright (c) 2016-2026 Mark Qvist (Reticulum-derived portions).
 * Derived from https://github.com/markqvist/LXMF
 * Copyright (c) 2020-2026 Mark Qvist (LXMF-derived portions).
 * Full license: LICENSES/Reticulum-Derivatives.txt.
 * Original notices: LICENSES/Reticulum-RNS.txt and LICENSES/Reticulum-LXMF.txt.
 * Other contributions retain their applicable terms; see LICENSE.
 * Component scope: docs/licensing/reticulum-sources.csv.
 */

/**
 * @file lxmf_identity.h
 * @brief Reticulum/LXMF identity persistence for ESP Arduino targets
 */

#pragma once

#include "chat/infra/reticulum/reticulum_wire.h"
#include "chat/ports/device_identity_restore.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hostlink::management
{
struct IdentityRestoreView;
}
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
    // Local USB account access may load an existing identity without starting
    // Reticulum or creating replacement keys. Epoch migration remains shared.
    bool initExisting();
    bool isReady() const { return ready_; }

    const uint8_t* encryptionPublicKey() const { return enc_pub_.data(); }
    const uint8_t* signingPublicKey() const { return sig_pub_.data(); }
    const uint8_t* identityHash() const { return identity_hash_.data(); }
    const uint8_t* destinationHash() const { return destination_hash_.data(); }
    uint32_t nodeId() const { return reticulum::nodeIdFromDestinationHash(destination_hash_.data()); }
    uint32_t authorGeneration() const { return author_generation_; }
    bool identityStorageError() const { return identity_storage_error_; }
    // Identity restore must advance even when restoring the same public key.
    bool advanceAuthorGeneration();
    // Caller holds the identity owner lock, excludes active publication/storage,
    // and owns a work area separate from the request. Successful or uncertain
    // writes disable this identity until reboot; no live key replacement.
    DeviceIdentityRestoreResult restoreNativeIdentity(const hostlink::management::IdentityRestoreView& request,
                                                      uint8_t* work, size_t capacity);
    // Internal export primitive. Caller owns serialization and authorization
    // under its identity/router lock; no private copy is retained here.
    bool copyNativePrivateKey(uint32_t expected_generation, uint8_t* output, size_t capacity) const
    {
        constexpr size_t size = kEncPrivKeySize + kSigPrivKeySize;
        if (!output) return false;
        const auto out = reinterpret_cast<uintptr_t>(output), self = reinterpret_cast<uintptr_t>(this);
        if (out <= self ? self - out < size : out - self < sizeof(*this)) return false;
        if (capacity < size || !ready_ || identity_storage_error_ || !expected_generation || author_generation_ != expected_generation)
        {
            std::memset(output, 0, capacity < size ? capacity : size);
            return false;
        }
        std::memcpy(output, enc_priv_.data(), kEncPrivKeySize);
        std::memcpy(output + kEncPrivKeySize, sig_priv_.data(), kSigPrivKeySize);
        return true;
    }

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
    bool identity_storage_error_ = false; // Uses existing alignment padding.
    uint32_t author_generation_ = 0;      // Replaces the derivable node-ID cache; no object growth.
    std::array<uint8_t, kEncPubKeySize> enc_pub_ = {};
    std::array<uint8_t, kEncPrivKeySize> enc_priv_ = {};
    std::array<uint8_t, kSigPubKeySize> sig_pub_ = {};
    std::array<uint8_t, kSigPrivKeySize> sig_priv_ = {};
    std::array<uint8_t, reticulum::kTruncatedHashSize> identity_hash_ = {};
    std::array<uint8_t, reticulum::kTruncatedHashSize> destination_hash_ = {};
};
static_assert(sizeof(LxmfIdentity) == 200, "Device author support must not grow the identity object");

} // namespace chat::lxmf
