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
 * @file lxmf_identity.cpp
 * @brief Reticulum/LXMF identity persistence for ESP Arduino targets
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"

#include "../../internal/blob_store_io.h"
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "hostlink/identity_restore_request.h"
#include "platform/esp/common/config_preferences.h"
#include "platform/esp/common/identity_generation_recovery.h"
#include "platform/esp/common/identity_generation_store.h"
#include "platform/esp/common/identity_key_state.h"
#include "platform/esp/common/identity_native_key_validation.h"
#include "platform/esp/common/identity_seed_key_conversion.h"
#include "platform/esp/common/reticulum_crypto_compat.h"
#include "platform/esp/common/reticulum_runtime_compat.h"

#include <cstdlib>
#include <cstring>

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
    if (!ready_ && identity_storage_error_ && author_generation_ == UINT32_MAX) return false;
    if (ready_)
    {
        return true;
    }
    identity_storage_error_ = false;
    author_generation_ = 0;
    if (loadFromPrefs())
    {
        if (!author_generation_ && !platform::esp::common::syncIdentityGeneration(identity_hash_.data(), author_generation_))
        {
            identity_storage_error_ = true;
            return false;
        }
        ready_ = true;
        return true;
    }
    if (!platform::esp::common::lxmfIdentityKeysAbsent())
    {
        identity_storage_error_ = true;
        author_generation_ = 0;
        Serial.println("[LXMF][Identity] existing identity storage requires repair; refusing regeneration");
        return false;
    }
    return generateAndPersist();
}

bool LxmfIdentity::initExisting()
{
    if (!ready_ && identity_storage_error_ && author_generation_ == UINT32_MAX) return false;
    if (ready_) return true;
    identity_storage_error_ = false;
    author_generation_ = 0;
    if (!loadFromPrefs()) return false;
    if (!author_generation_ && !platform::esp::common::syncIdentityGeneration(identity_hash_.data(), author_generation_))
    {
        identity_storage_error_ = true;
        return false;
    }
    ready_ = true;
    return true;
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
    using namespace platform::esp::common;
    // Probe first: legacy-only devices incur no new key-buffer allocation.
    nvs_handle_t record_handle = 0;
    const auto opened = nvs_open(kPrefsNs, NVS_READONLY, &record_handle);
    if (opened != ESP_OK && opened != ESP_ERR_NVS_NOT_FOUND) return false;
    size_t record_size = 0;
    const auto record_status = opened == ESP_OK ? nvs_get_blob(record_handle, "native_v1", nullptr, &record_size) : ESP_ERR_NVS_NOT_FOUND;
    if (opened == ESP_OK) nvs_close(record_handle);
    if (record_status != ESP_ERR_NVS_NOT_FOUND)
    {
        if (record_status != ESP_OK || record_size != kIdentityRecordBytes) return false;
        // Startup-only transient ownership; never a large automatic stack object.
        auto* record = static_cast<uint8_t*>(std::malloc(kIdentityRecordWorkBytes));
        if (!record) return false;
        struct ReleaseRecord
        {
            uint8_t* record;
            ~ReleaseRecord()
            {
                clearIdentityRecordWork(record, kIdentityRecordWorkBytes);
                std::free(record);
            }
        } release_record{record};
        if (readIdentityRecord(record, kIdentityRecordWorkBytes) != IdentityRecordResult::Ready ||
            !validateNativeIdentityKeys(record + 12, record + 76, record + kIdentityRecordBytes, 32)) return false;
        std::memcpy(enc_pub_.data(), record + 12, enc_pub_.size());
        std::memcpy(sig_pub_.data(), record + 44, sig_pub_.size());
        std::memcpy(enc_priv_.data(), record + 76, enc_priv_.size());
        std::memcpy(sig_priv_.data(), record + 108, sig_priv_.size());
        recomputeDerivedFields();
        return recoverIdentityGeneration(identity_hash_.data(), identityRecordU32(record + 8), author_generation_);
    }
    platform::esp::common::ConfigPreferences preferences;
    if (!preferences.begin(kPrefsNs, true) ||
        preferences.getBytes(kEncPubKey, enc_pub_.data(), enc_pub_.size()) != enc_pub_.size() ||
        preferences.getBytes(kEncPrivKey, enc_priv_.data(), enc_priv_.size()) != enc_priv_.size() ||
        preferences.getBytes(kSigPubKey, sig_pub_.data(), sig_pub_.size()) != sig_pub_.size() ||
        preferences.getBytes(kSigPrivKey, sig_priv_.data(), sig_priv_.size()) != sig_priv_.size())
    {
        return false;
    }
    preferences.end();

    if (isAllZero(enc_pub_.data(), enc_pub_.size()) ||
        isAllZero(enc_priv_.data(), enc_priv_.size()) ||
        isAllZero(sig_pub_.data(), sig_pub_.size()) ||
        isAllZero(sig_priv_.data(), sig_priv_.size()))
    {
        return false;
    }

    uint8_t key_work[kSigPubKeySize] = {};
    // Keep the existing small work area. No combined key buffers on task stack.
    // The combined validator is used by restore; storage has separate arrays.
    if (!hostlink::validateExpandedSigningKey(sig_pub_.data(), sig_priv_.data(), key_work, sizeof(key_work)))
    {
        return false;
    }

    // dh1() stores an already-clamped native scalar; eval() does not clamp it.
    if ((enc_priv_[0] & 7) != 0 || (enc_priv_[31] & 0xc0) != 0x40 ||
        !Curve25519::eval(key_work, enc_priv_.data(), nullptr))
    {
        memset(key_work, 0, sizeof(key_work));
        return false;
    }
    const bool enc_matches = memcmp(key_work, enc_pub_.data(), sizeof(key_work)) == 0;
    memset(key_work, 0, sizeof(key_work));
    if (!enc_matches) return false;

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
    identity_storage_error_ = !ready_;
    if (ready_) (void)platform::esp::common::syncIdentityGeneration(identity_hash_.data(), author_generation_);
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
}

DeviceIdentityRestoreResult LxmfIdentity::restoreNativeIdentity(const hostlink::management::IdentityRestoreView& request,
                                                                uint8_t* work, size_t capacity)
{
    using Result = DeviceIdentityRestoreResult;
    using namespace platform::esp::common;
    constexpr size_t work_size = 352;
    if (!ready_ || identity_storage_error_ || !author_generation_) return Result::Unavailable;
    if (!work || capacity < work_size || request.expected_public.size != 64 || !request.expected_public.data ||
        request.public_key.size != 64 || !request.public_key.data || request.format > 1 || request.private_key.size != (request.format == 0 ? 96U : 64U) || !request.private_key.data ||
        request.identity_hash.size != 16 || !request.identity_hash.data || request.delivery.size != 16 || !request.delivery.data) return Result::InvalidInput;
    const auto start = reinterpret_cast<uintptr_t>(work), self = reinterpret_cast<uintptr_t>(this);
    if (start >= self ? start - self < sizeof(*this) : self - start < work_size) return Result::InvalidInput;
    const auto overlaps = [start](const uint8_t* input, size_t size)
    {
        const auto source = reinterpret_cast<uintptr_t>(input);
        return start >= source ? start - source < size : source - start < work_size;
    };
    if (overlaps(request.expected_public.data, 64) || overlaps(request.payload.data, request.payload.size) ||
        overlaps(request.signature.data, request.signature.size) || overlaps(request.public_key.data, 64) ||
        overlaps(request.private_key.data, request.private_key.size) || overlaps(request.identity_hash.data, 16) || overlaps(request.delivery.data, 16)) return Result::InvalidInput;
    struct Clear
    {
        uint8_t* work;
        ~Clear() { clearIdentityRecordWork(work, work_size); }
    } clear{work};
    if (request.expected_generation != author_generation_ ||
        std::memcmp(request.expected_public.data, enc_pub_.data(), 32) ||
        std::memcmp(request.expected_public.data + 32, sig_pub_.data(), 32)) return Result::StaleIdentity;
    if (!hostlink::management::verifyIdentityRestoreSignature(request, work, work_size)) return Result::InvalidInput;
    const uint8_t* private_key = request.private_key.data;
    uint8_t* crypto_work = work;
    size_t crypto_capacity = work_size;
    if (request.format == 1)
    {
        if (!convertSeedIdentityKeys(request.public_key.data, request.private_key.data, work, 96, work + 96, 32)) return Result::InvalidInput;
        private_key = work;
        crypto_work = work + 96;
        crypto_capacity = work_size - 96;
    }
    if (!validateNativeIdentityKeys(request.public_key.data, private_key, crypto_work, 32)) return Result::InvalidInput;
    reticulum::computeIdentityHash(request.public_key.data, crypto_work);
    if (std::memcmp(crypto_work, request.identity_hash.data, 16)) return Result::InvalidInput;
    reticulum::computeNameHash("lxmf", "delivery", crypto_work + 16);
    reticulum::computeDestinationHash(crypto_work + 16, crypto_work, crypto_work + 32);
    if (std::memcmp(crypto_work + 32, request.delivery.data, 16)) return Result::InvalidInput;
    const auto stored = commitIdentityRecord(request.expected_generation, author_generation_, request.public_key.data,
                                             private_key, crypto_work, crypto_capacity, validateNativeIdentityKeys);
    if (stored == IdentityRecordResult::Committed || stored == IdentityRecordResult::Indeterminate)
    {
        ready_ = false;
        identity_storage_error_ = true;
        author_generation_ = UINT32_MAX; // Existing field: reboot-required latch.
        clearIdentityRecordWork(enc_priv_.data(), enc_priv_.size());
        clearIdentityRecordWork(sig_priv_.data(), sig_priv_.size());
        return stored == IdentityRecordResult::Committed ? Result::CommittedRebootRequired : Result::IndeterminateRebootRequired;
    }
    return stored == IdentityRecordResult::TargetChanged ? Result::StaleIdentity : stored == IdentityRecordResult::Invalid ? Result::InvalidInput
                                                                                                                           : Result::StorageError;
}

bool LxmfIdentity::advanceAuthorGeneration()
{
    if (!ready_) return false;
    return platform::esp::common::syncIdentityGeneration(identity_hash_.data(), author_generation_, true);
}

} // namespace chat::lxmf
