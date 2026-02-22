/**
 * @file meshcore_identity.cpp
 * @brief MeshCore identity key management (Ed25519 + ECDH)
 */

#include "meshcore_identity.h"
#include <Arduino.h>
#include <Preferences.h>
#include <cstring>

extern "C"
{
#include "crypto/ed25519/ed_25519.h"
}

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

bool isZeroBytes(const uint8_t* data, size_t len)
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
    return hash != 0x00 && hash != 0xFF;
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
    Preferences prefs;
    if (!prefs.begin(kIdentityPrefsNs, true))
    {
        return false;
    }

    const size_t priv_len = prefs.getBytesLength(kIdentityPrefsPriv);
    if (priv_len != priv_.size())
    {
        prefs.end();
        return false;
    }

    if (prefs.getBytes(kIdentityPrefsPriv, priv_.data(), priv_.size()) != priv_.size())
    {
        prefs.end();
        return false;
    }

    const size_t stored_pub_len = prefs.getBytes(kIdentityPrefsPub, pub_.data(), pub_.size());
    prefs.end();

    uint8_t derived_pub[kPubKeySize] = {};
    ed25519_derive_pub(derived_pub, priv_.data());
    if (isZeroBytes(derived_pub, sizeof(derived_pub)) || !isValidPublicHash(derived_pub[0]))
    {
        memset(priv_.data(), 0, priv_.size());
        return false;
    }

    bool needs_save = (stored_pub_len != pub_.size());
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
    Preferences prefs;
    if (!prefs.begin(kIdentityPrefsNs, false))
    {
        return false;
    }

    const size_t priv_written = prefs.putBytes(kIdentityPrefsPriv, priv_.data(), priv_.size());
    const size_t pub_written = prefs.putBytes(kIdentityPrefsPub, pub_.data(), pub_.size());
    prefs.putUChar(kIdentityPrefsVer, kIdentityPrefsVersion);
    prefs.end();

    return priv_written == priv_.size() && pub_written == pub_.size();
}

bool MeshCoreIdentity::generateAndPersist()
{
    uint8_t seed[32] = {};
    bool ok = false;

    for (size_t attempt = 0; attempt < 16; ++attempt)
    {
        fillRandomBytes(seed, sizeof(seed));
        ed25519_create_keypair(pub_.data(), priv_.data(), seed);
        if (!isZeroBytes(priv_.data(), priv_.size()) && isValidPublicHash(pub_[0]))
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

    ed25519_sign(out_signature, message, message_len, pub_.data(), priv_.data());
    return true;
}

bool MeshCoreIdentity::verify(const uint8_t pubkey[kPubKeySize],
                              const uint8_t signature[kSignatureSize],
                              const uint8_t* message, size_t message_len)
{
    if (!pubkey || !signature || !message)
    {
        return false;
    }
    return ed25519_verify(signature, message, message_len, pubkey) != 0;
}

bool MeshCoreIdentity::deriveSharedSecret(const uint8_t peer_pubkey[kPubKeySize],
                                          uint8_t out_secret[kPubKeySize]) const
{
    if (!ready_ || !peer_pubkey || !out_secret)
    {
        return false;
    }

    ed25519_key_exchange(out_secret, peer_pubkey, priv_.data());
    return !isZeroBytes(out_secret, kPubKeySize);
}

} // namespace meshcore
} // namespace chat
