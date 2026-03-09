/**
 * @file meshcore_identity.cpp
 * @brief MeshCore identity key management (Ed25519 + ECDH)
 */

#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_identity.h"
#include "../../internal/blob_store_io.h"
#include "chat/infra/meshcore/meshcore_identity_crypto.h"

#include <Arduino.h>
#include <cstring>

namespace chat
{
namespace meshcore
{

namespace
{
constexpr const char* kIdentityPrefsNs = "mc_ident";
constexpr const char* kIdentityPrefsPriv = "priv64";
constexpr const char* kIdentityPrefsPub = "pub32";
constexpr const char* kIdentityPrefsVer = "ver";
constexpr uint8_t kIdentityPrefsVersion = 1;

void fillRandomBytes(uint8_t* out, size_t len)
{
    if (!out || len == 0)
    {
        return;
    }

    size_t index = 0;
    while (index < len)
    {
        const uint32_t rnd = static_cast<uint32_t>(esp_random());
        const size_t chunk = (len - index >= sizeof(rnd)) ? sizeof(rnd) : (len - index);
        memcpy(out + index, &rnd, chunk);
        index += chunk;
    }
}

} // namespace

bool MeshCoreIdentity::isValidPublicHash(uint8_t hash)
{
    return meshcoreIsValidPublicHash(hash);
}

bool MeshCoreIdentity::init()
{
    if (ready_)
    {
        return true;
    }
    if (loadFromPrefs())
    {
        ready_ = true;
        return true;
    }
    return generateAndPersist();
}

bool MeshCoreIdentity::loadFromPrefs()
{
    std::vector<uint8_t> priv_blob;
    chat::infra::PreferencesBlobMetadata meta;
    if (!chat::infra::loadRawBlobFromPreferencesWithMetadata(kIdentityPrefsNs,
                                                             kIdentityPrefsPriv,
                                                             kIdentityPrefsVer,
                                                             nullptr,
                                                             priv_blob,
                                                             &meta) ||
        priv_blob.size() != priv_.size())
    {
        return false;
    }

    memcpy(priv_.data(), priv_blob.data(), priv_.size());

    std::vector<uint8_t> pub_blob;
    const bool have_pub_blob = chat::infra::loadRawBlobFromPreferences(kIdentityPrefsNs,
                                                                       kIdentityPrefsPub,
                                                                       pub_blob) &&
                               (pub_blob.size() == pub_.size());
    if (have_pub_blob)
    {
        memcpy(pub_.data(), pub_blob.data(), pub_.size());
    }

    uint8_t derived_pub[kPubKeySize] = {};
    if (!meshcoreDerivePublicKey(priv_.data(), derived_pub))
    {
        memset(priv_.data(), 0, priv_.size());
        return false;
    }

    bool needs_save = !have_pub_blob;
    if (!needs_save && memcmp(pub_.data(), derived_pub, sizeof(derived_pub)) != 0)
    {
        needs_save = true;
    }
    memcpy(pub_.data(), derived_pub, sizeof(derived_pub));

    if (needs_save)
    {
        saveToPrefs();
    }
    return true;
}

bool MeshCoreIdentity::saveToPrefs() const
{
    chat::infra::PreferencesBlobMetadata meta;
    meta.len = priv_.size();
    meta.has_version = true;
    meta.version = kIdentityPrefsVersion;

    const bool priv_ok = chat::infra::saveRawBlobToPreferencesWithMetadata(
        kIdentityPrefsNs,
        kIdentityPrefsPriv,
        kIdentityPrefsVer,
        nullptr,
        priv_.data(),
        priv_.size(),
        &meta,
        false);
    const bool pub_ok = chat::infra::saveRawBlobToPreferences(kIdentityPrefsNs,
                                                              kIdentityPrefsPub,
                                                              pub_.data(),
                                                              pub_.size());
    return priv_ok && pub_ok;
}

bool MeshCoreIdentity::generateAndPersist()
{
    uint8_t seed[kMeshCoreSeedSize] = {};
    bool ok = false;

    for (size_t attempt = 0; attempt < 16; ++attempt)
    {
        fillRandomBytes(seed, sizeof(seed));
        if (meshcoreCreateKeypair(seed, pub_.data(), priv_.data()))
        {
            ok = true;
            break;
        }
    }

    memset(seed, 0, sizeof(seed));
    if (!ok)
    {
        memset(pub_.data(), 0, pub_.size());
        memset(priv_.data(), 0, priv_.size());
        return false;
    }

    if (!saveToPrefs())
    {
        memset(pub_.data(), 0, pub_.size());
        memset(priv_.data(), 0, priv_.size());
        return false;
    }

    ready_ = true;
    return true;
}

bool MeshCoreIdentity::sign(const uint8_t* message, size_t message_len,
                            uint8_t out_signature[kSignatureSize]) const
{
    if (!ready_ || !message || !out_signature)
    {
        return false;
    }

    return meshcoreSign(priv_.data(), pub_.data(), message, message_len, out_signature);
}

bool MeshCoreIdentity::verify(const uint8_t pubkey[kPubKeySize],
                              const uint8_t signature[kSignatureSize],
                              const uint8_t* message, size_t message_len)
{
    return meshcoreVerify(pubkey, signature, message, message_len);
}

bool MeshCoreIdentity::deriveSharedSecret(const uint8_t peer_pubkey[kPubKeySize],
                                          uint8_t out_secret[kPubKeySize]) const
{
    if (!ready_ || !peer_pubkey || !out_secret)
    {
        return false;
    }

    return meshcoreDeriveSharedSecret(priv_.data(), peer_pubkey, out_secret);
}

bool MeshCoreIdentity::exportPrivateKey(uint8_t out_priv[kPrivKeySize]) const
{
    if (!ready_ || !out_priv)
    {
        return false;
    }
    memcpy(out_priv, priv_.data(), priv_.size());
    return true;
}

bool MeshCoreIdentity::importPrivateKey(const uint8_t in_priv[kPrivKeySize])
{
    if (!in_priv)
    {
        return false;
    }

    uint8_t derived_pub[kPubKeySize] = {};
    if (!meshcoreDerivePublicKey(in_priv, derived_pub))
    {
        return false;
    }

    memcpy(priv_.data(), in_priv, priv_.size());
    memcpy(pub_.data(), derived_pub, sizeof(derived_pub));
    ready_ = true;

    return saveToPrefs();
}

} // namespace meshcore
} // namespace chat
