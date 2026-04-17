/**
 * @file lxmf_identity.cpp
 * @brief Reticulum/LXMF identity persistence for ESP Arduino targets
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"

#include "../../internal/blob_store_io.h"
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"

#include <Arduino.h>
#include <Curve25519.h>
#include <RNG.h>

#include <cstring>
#include <vector>

namespace chat::lxmf
{
namespace
{
constexpr const char* kPrefsNs = "lxmf_ident";
constexpr const char* kEncPubKey = "enc_pub";
constexpr const char* kEncPrivKey = "enc_priv";
constexpr const char* kSigPubKey = "sig_pub";
constexpr const char* kSigPrivKey = "sig_priv";

void fillRandomBytes(uint8_t* out, size_t len)
{
    if (!out || len == 0)
    {
        return;
    }

    size_t offset = 0;
    while (offset < len)
    {
        const uint32_t rnd = static_cast<uint32_t>(esp_random());
        const size_t chunk = (len - offset >= sizeof(rnd)) ? sizeof(rnd) : (len - offset);
        memcpy(out + offset, &rnd, chunk);
        offset += chunk;
    }
}

bool isAllZero(const uint8_t* data, size_t len)
{
    if (!data)
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

} // namespace

bool LxmfIdentity::init()
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

void LxmfIdentity::combinedPublicKey(uint8_t out_key[reticulum::kCombinedPublicKeySize]) const
{
    if (!out_key)
    {
        return;
    }
    memcpy(out_key, enc_pub_.data(), enc_pub_.size());
    memcpy(out_key + enc_pub_.size(), sig_pub_.data(), sig_pub_.size());
}

bool LxmfIdentity::sign(const uint8_t* message, size_t message_len,
                        uint8_t out_signature[kSignatureSize]) const
{
    if (!ready_ || !message || !out_signature)
    {
        return false;
    }

    ed25519_sign(out_signature, message, message_len, sig_pub_.data(), sig_priv_.data());
    return true;
}

bool LxmfIdentity::verify(const uint8_t sign_pub[kSigPubKeySize],
                          const uint8_t signature[kSignatureSize],
                          const uint8_t* message, size_t message_len)
{
    if (!sign_pub || !signature || !message)
    {
        return false;
    }
    return ed25519_verify(signature, message, message_len, sign_pub) != 0;
}

bool LxmfIdentity::deriveSharedSecret(const uint8_t peer_public_key[kEncPubKeySize],
                                      uint8_t out_secret[kEncPubKeySize]) const
{
    if (!ready_ || !peer_public_key || !out_secret)
    {
        return false;
    }

    memcpy(out_secret, peer_public_key, kEncPubKeySize);
    uint8_t local_priv[kEncPrivKeySize] = {};
    memcpy(local_priv, enc_priv_.data(), sizeof(local_priv));
    return Curve25519::dh2(out_secret, local_priv);
}

bool LxmfIdentity::loadFromPrefs()
{
    std::vector<uint8_t> blob;
    if (!chat::infra::loadRawBlobFromPreferences(kPrefsNs, kEncPubKey, blob) ||
        blob.size() != enc_pub_.size())
    {
        return false;
    }
    memcpy(enc_pub_.data(), blob.data(), enc_pub_.size());

    if (!chat::infra::loadRawBlobFromPreferences(kPrefsNs, kEncPrivKey, blob) ||
        blob.size() != enc_priv_.size())
    {
        return false;
    }
    memcpy(enc_priv_.data(), blob.data(), enc_priv_.size());

    if (!chat::infra::loadRawBlobFromPreferences(kPrefsNs, kSigPubKey, blob) ||
        blob.size() != sig_pub_.size())
    {
        return false;
    }
    memcpy(sig_pub_.data(), blob.data(), sig_pub_.size());

    if (!chat::infra::loadRawBlobFromPreferences(kPrefsNs, kSigPrivKey, blob) ||
        blob.size() != sig_priv_.size())
    {
        return false;
    }
    memcpy(sig_priv_.data(), blob.data(), sig_priv_.size());

    if (isAllZero(enc_pub_.data(), enc_pub_.size()) ||
        isAllZero(enc_priv_.data(), enc_priv_.size()) ||
        isAllZero(sig_pub_.data(), sig_pub_.size()) ||
        isAllZero(sig_priv_.data(), sig_priv_.size()))
    {
        return false;
    }

    uint8_t derived_sig_pub[kSigPubKeySize] = {};
    ed25519_derive_pub(derived_sig_pub, sig_priv_.data());
    if (memcmp(derived_sig_pub, sig_pub_.data(), sizeof(derived_sig_pub)) != 0)
    {
        memcpy(sig_pub_.data(), derived_sig_pub, sizeof(derived_sig_pub));
        saveToPrefs();
    }

    recomputeDerivedFields();
    return true;
}

bool LxmfIdentity::saveToPrefs() const
{
    const bool enc_pub_ok = chat::infra::saveRawBlobToPreferences(
        kPrefsNs, kEncPubKey, enc_pub_.data(), enc_pub_.size());
    const bool enc_priv_ok = chat::infra::saveRawBlobToPreferences(
        kPrefsNs, kEncPrivKey, enc_priv_.data(), enc_priv_.size());
    const bool sig_pub_ok = chat::infra::saveRawBlobToPreferences(
        kPrefsNs, kSigPubKey, sig_pub_.data(), sig_pub_.size());
    const bool sig_priv_ok = chat::infra::saveRawBlobToPreferences(
        kPrefsNs, kSigPrivKey, sig_priv_.data(), sig_priv_.size());
    return enc_pub_ok && enc_priv_ok && sig_pub_ok && sig_priv_ok;
}

bool LxmfIdentity::generateAndPersist()
{
    RNG.begin("trail-mate-lxmf");

    for (size_t attempt = 0; attempt < 16; ++attempt)
    {
        memset(enc_pub_.data(), 0, enc_pub_.size());
        memset(enc_priv_.data(), 0, enc_priv_.size());
        Curve25519::dh1(enc_pub_.data(), enc_priv_.data());
        if (!isAllZero(enc_priv_.data(), enc_priv_.size()))
        {
            break;
        }
    }

    if (isAllZero(enc_priv_.data(), enc_priv_.size()))
    {
        return false;
    }

    uint8_t seed[32] = {};
    fillRandomBytes(seed, sizeof(seed));
    ed25519_create_keypair(sig_pub_.data(), sig_priv_.data(), seed);
    memset(seed, 0, sizeof(seed));

    if (isAllZero(sig_priv_.data(), sig_priv_.size()) || isAllZero(sig_pub_.data(), sig_pub_.size()))
    {
        return false;
    }

    recomputeDerivedFields();
    ready_ = saveToPrefs();
    return ready_;
}

void LxmfIdentity::recomputeDerivedFields()
{
    uint8_t combined[reticulum::kCombinedPublicKeySize] = {};
    combinedPublicKey(combined);
    reticulum::computeIdentityHash(combined, identity_hash_.data());

    uint8_t name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", "delivery", name_hash);
    reticulum::computeDestinationHash(name_hash, identity_hash_.data(), destination_hash_.data());
    node_id_ = reticulum::nodeIdFromDestinationHash(destination_hash_.data());
}

} // namespace chat::lxmf
