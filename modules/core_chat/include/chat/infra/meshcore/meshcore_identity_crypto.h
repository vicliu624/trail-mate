#pragma once

#include <cstddef>
#include <cstdint>

namespace chat
{
namespace meshcore
{

constexpr size_t kMeshCorePubKeySize = 32;
constexpr size_t kMeshCorePrivKeySize = 64;
constexpr size_t kMeshCoreSignatureSize = 64;
constexpr size_t kMeshCoreSeedSize = 32;

bool meshcoreIsZeroBytes(const uint8_t* data, size_t len);
bool meshcoreIsValidPublicHash(uint8_t hash);
bool meshcoreDerivePublicKey(const uint8_t in_priv[kMeshCorePrivKeySize],
                             uint8_t out_pub[kMeshCorePubKeySize]);
bool meshcoreCreateKeypair(const uint8_t seed[kMeshCoreSeedSize],
                           uint8_t out_pub[kMeshCorePubKeySize],
                           uint8_t out_priv[kMeshCorePrivKeySize]);
bool meshcoreSign(const uint8_t priv[kMeshCorePrivKeySize],
                  const uint8_t pub[kMeshCorePubKeySize],
                  const uint8_t* message,
                  size_t message_len,
                  uint8_t out_signature[kMeshCoreSignatureSize]);
bool meshcoreVerify(const uint8_t pub[kMeshCorePubKeySize],
                    const uint8_t signature[kMeshCoreSignatureSize],
                    const uint8_t* message,
                    size_t message_len);
bool meshcoreDeriveSharedSecret(const uint8_t priv[kMeshCorePrivKeySize],
                                const uint8_t peer_pub[kMeshCorePubKeySize],
                                uint8_t out_secret[kMeshCorePubKeySize]);

} // namespace meshcore
} // namespace chat
