#include "chat/infra/meshcore/meshcore_identity_crypto.h"

#include <cstring>

extern "C"
{
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
}

namespace chat
{
namespace meshcore
{

bool meshcoreIsZeroBytes(const uint8_t* data, size_t len)
{
    if (!data || len == 0)
    {
        return true;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (data[i] != 0)
        {
            return false;
        }
    }
    return true;
}

bool meshcoreIsValidPublicHash(uint8_t hash)
{
    return hash != 0x00 && hash != 0xFF;
}

bool meshcoreDerivePublicKey(const uint8_t in_priv[kMeshCorePrivKeySize],
                             uint8_t out_pub[kMeshCorePubKeySize])
{
    if (!in_priv || !out_pub)
    {
        return false;
    }
    ed25519_derive_pub(out_pub, in_priv);
    return !meshcoreIsZeroBytes(out_pub, kMeshCorePubKeySize) &&
           meshcoreIsValidPublicHash(out_pub[0]);
}

bool meshcoreCreateKeypair(const uint8_t seed[kMeshCoreSeedSize],
                           uint8_t out_pub[kMeshCorePubKeySize],
                           uint8_t out_priv[kMeshCorePrivKeySize])
{
    if (!seed || !out_pub || !out_priv)
    {
        return false;
    }
    ed25519_create_keypair(out_pub, out_priv, seed);
    return !meshcoreIsZeroBytes(out_priv, kMeshCorePrivKeySize) &&
           meshcoreIsValidPublicHash(out_pub[0]);
}

bool meshcoreSign(const uint8_t priv[kMeshCorePrivKeySize],
                  const uint8_t pub[kMeshCorePubKeySize],
                  const uint8_t* message,
                  size_t message_len,
                  uint8_t out_signature[kMeshCoreSignatureSize])
{
    if (!priv || !pub || !message || !out_signature)
    {
        return false;
    }
    ed25519_sign(out_signature, message, message_len, pub, priv);
    return true;
}

bool meshcoreVerify(const uint8_t pub[kMeshCorePubKeySize],
                    const uint8_t signature[kMeshCoreSignatureSize],
                    const uint8_t* message,
                    size_t message_len)
{
    if (!pub || !signature || !message)
    {
        return false;
    }
    return ed25519_verify(signature, message, message_len, pub) != 0;
}

bool meshcoreDeriveSharedSecret(const uint8_t priv[kMeshCorePrivKeySize],
                                const uint8_t peer_pub[kMeshCorePubKeySize],
                                uint8_t out_secret[kMeshCorePubKeySize])
{
    if (!priv || !peer_pub || !out_secret)
    {
        return false;
    }
    ed25519_key_exchange(out_secret, peer_pub, priv);
    return !meshcoreIsZeroBytes(out_secret, kMeshCorePubKeySize);
}

} // namespace meshcore
} // namespace chat
