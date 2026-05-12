/**
 * @file mt_pki_crypto.cpp
 * @brief Shared Meshtastic PKI/AES-CCM crypto helpers
 */

#include "chat/infra/meshtastic/mt_pki_crypto.h"

#include <cstring>

#if __has_include(<AES.h>) && __has_include(<Crypto.h>) && __has_include(<SHA256.h>)
#include <AES.h>
#include <Crypto.h>
#include <SHA256.h>
#define TRAILMATE_MESHTASTIC_PKI_HAS_ARDUINO_CRYPTO 1
#else
#define TRAILMATE_MESHTASTIC_PKI_HAS_ARDUINO_CRYPTO 0
#endif

#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#endif

namespace chat
{
namespace meshtastic
{
namespace
{
constexpr size_t kAesBlockSize = 16;
constexpr size_t kMaxAadLen = 30;
#if TRAILMATE_MESHTASTIC_PKI_HAS_ARDUINO_CRYPTO || defined(TRAIL_MATE_HAS_OPENSSL)
constexpr size_t kCcmLengthSize = 2;
#endif

#if TRAILMATE_MESHTASTIC_PKI_HAS_ARDUINO_CRYPTO
int constantTimeCompare(const void* a_, const void* b_, size_t len)
{
    const volatile uint8_t* volatile a =
        reinterpret_cast<const volatile uint8_t*>(a_);
    const volatile uint8_t* volatile b =
        reinterpret_cast<const volatile uint8_t*>(b_);
    if (len == 0)
    {
        return 0;
    }
    if (!a || !b)
    {
        return -1;
    }
    volatile uint8_t diff = 0U;
    for (size_t index = 0; index < len; ++index)
    {
        diff |= static_cast<uint8_t>(a[index] ^ b[index]);
    }
    return diff;
}

class AesCcmCipher
{
  public:
    ~AesCcmCipher()
    {
        delete aes_;
    }

    void setKey(const uint8_t* key, size_t key_len)
    {
        delete aes_;
        aes_ = nullptr;
        if (key && key_len != 0)
        {
            aes_ = new AESSmall256();
            aes_->setKey(key, key_len);
        }
    }

    void encryptBlock(uint8_t* out, const uint8_t* in)
    {
        if (!out || !in)
        {
            return;
        }
        if (!aes_)
        {
            memset(out, 0, kAesBlockSize);
            return;
        }
        aes_->encryptBlock(out, in);
    }

  private:
    AESSmall256* aes_ = nullptr;
};

void putBe16(uint8_t* out, uint16_t value)
{
    if (!out)
    {
        return;
    }
    out[0] = static_cast<uint8_t>(value >> 8);
    out[1] = static_cast<uint8_t>(value & 0xFFU);
}

void xorAesBlock(uint8_t* dst, const uint8_t* src)
{
    if (!dst || !src)
    {
        return;
    }
    for (size_t index = 0; index < kAesBlockSize; ++index)
    {
        dst[index] ^= src[index];
    }
}

void aesCcmAuthStart(AesCcmCipher& cipher,
                     size_t auth_len,
                     size_t length_size,
                     const uint8_t* nonce,
                     const uint8_t* aad,
                     size_t aad_len,
                     size_t plain_len,
                     uint8_t* out_x)
{
    uint8_t aad_buf[2 * kAesBlockSize] = {};
    uint8_t block[kAesBlockSize] = {};
    block[0] = static_cast<uint8_t>(((aad && aad_len) ? 0x40 : 0x00) |
                                    ((((auth_len - 2U) / 2U) & 0x07U) << 3U) |
                                    ((length_size - 1U) & 0x07U));
    memcpy(&block[1], nonce, 15U - length_size);
    putBe16(&block[kAesBlockSize - length_size], static_cast<uint16_t>(plain_len));
    cipher.encryptBlock(out_x, block);
    if (!(aad && aad_len))
    {
        return;
    }
    putBe16(aad_buf, static_cast<uint16_t>(aad_len));
    memcpy(aad_buf + 2, aad, aad_len);
    xorAesBlock(aad_buf, out_x);
    cipher.encryptBlock(out_x, aad_buf);
    if (aad_len > kAesBlockSize - 2)
    {
        xorAesBlock(&aad_buf[kAesBlockSize], out_x);
        cipher.encryptBlock(out_x, &aad_buf[kAesBlockSize]);
    }
}

void aesCcmAuth(AesCcmCipher& cipher, const uint8_t* data, size_t len, uint8_t* x)
{
    size_t last = len % kAesBlockSize;
    for (size_t index = 0; index < len / kAesBlockSize; ++index)
    {
        xorAesBlock(x, data);
        data += kAesBlockSize;
        cipher.encryptBlock(x, x);
    }
    if (last)
    {
        for (size_t index = 0; index < last; ++index)
        {
            x[index] ^= *data++;
        }
        cipher.encryptBlock(x, x);
    }
}

void aesCcmEncrStart(size_t length_size, const uint8_t* nonce, uint8_t* out_block)
{
    out_block[0] = static_cast<uint8_t>(length_size - 1U);
    memcpy(&out_block[1], nonce, 15U - length_size);
}

void aesCcmEncr(AesCcmCipher& cipher,
                size_t length_size,
                const uint8_t* in,
                size_t len,
                uint8_t* out,
                uint8_t* block)
{
    size_t last = len % kAesBlockSize;
    size_t counter = 0;
    for (counter = 1; counter <= len / kAesBlockSize; ++counter)
    {
        putBe16(&block[kAesBlockSize - 2], static_cast<uint16_t>(counter));
        cipher.encryptBlock(out, block);
        xorAesBlock(out, in);
        out += kAesBlockSize;
        in += kAesBlockSize;
    }
    if (last)
    {
        putBe16(&block[kAesBlockSize - 2], static_cast<uint16_t>(counter));
        cipher.encryptBlock(out, block);
        for (size_t index = 0; index < last; ++index)
        {
            *out++ ^= *in++;
        }
    }
}

void aesCcmEncryptAuth(AesCcmCipher& cipher,
                       size_t auth_len,
                       const uint8_t* x,
                       uint8_t* block,
                       uint8_t* out_auth)
{
    uint8_t tmp[kAesBlockSize];
    putBe16(&block[kAesBlockSize - 2], 0);
    cipher.encryptBlock(tmp, block);
    for (size_t index = 0; index < auth_len; ++index)
    {
        out_auth[index] = x[index] ^ tmp[index];
    }
}

void aesCcmDecryptAuth(AesCcmCipher& cipher,
                       size_t auth_len,
                       uint8_t* block,
                       const uint8_t* auth,
                       uint8_t* out_tag)
{
    uint8_t tmp[kAesBlockSize];
    putBe16(&block[kAesBlockSize - 2], 0);
    cipher.encryptBlock(tmp, block);
    for (size_t index = 0; index < auth_len; ++index)
    {
        out_tag[index] = auth[index] ^ tmp[index];
    }
}
#endif

#if defined(TRAIL_MATE_HAS_OPENSSL)
constexpr size_t kOpenSslCcmNonceLen = 15 - kCcmLengthSize;

const EVP_CIPHER* opensslAesCcmCipher(size_t key_len)
{
    if (key_len == 16)
    {
        return EVP_aes_128_ccm();
    }
    if (key_len == 32)
    {
        return EVP_aes_256_ccm();
    }
    return nullptr;
}

bool opensslSha256Update(EVP_MD_CTX* ctx, const void* data, size_t len)
{
    if (!ctx || !data || len == 0)
    {
        return true;
    }
    return EVP_DigestUpdate(ctx, data, len) == 1;
}

bool opensslSha256(const void* first,
                   size_t first_len,
                   const void* second,
                   size_t second_len,
                   const void* third,
                   size_t third_len,
                   const void* fourth,
                   size_t fourth_len,
                   const void* fifth,
                   size_t fifth_len,
                   const void* sixth,
                   size_t sixth_len,
                   uint8_t out[32])
{
    if (!out)
    {
        return false;
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
    {
        return false;
    }
    bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1;
    ok = ok && opensslSha256Update(ctx, first, first_len);
    ok = ok && opensslSha256Update(ctx, second, second_len);
    ok = ok && opensslSha256Update(ctx, third, third_len);
    ok = ok && opensslSha256Update(ctx, fourth, fourth_len);
    ok = ok && opensslSha256Update(ctx, fifth, fifth_len);
    ok = ok && opensslSha256Update(ctx, sixth, sixth_len);
    unsigned int out_len = 0;
    ok = ok && EVP_DigestFinal_ex(ctx, out, &out_len) == 1 && out_len == 32;
    EVP_MD_CTX_free(ctx);
    return ok;
}

bool opensslAesCcmDecrypt(const uint8_t* key,
                          size_t key_len,
                          const uint8_t nonce[kPkiNonceSize],
                          size_t auth_len,
                          const uint8_t* crypt,
                          size_t crypt_len,
                          const uint8_t* aad,
                          size_t aad_len,
                          const uint8_t* auth,
                          uint8_t* out_plain)
{
    const EVP_CIPHER* cipher = opensslAesCcmCipher(key_len);
    if (!cipher || !nonce || !crypt || !auth || !out_plain ||
        auth_len == 0 || auth_len > kAesBlockSize || aad_len > kMaxAadLen)
    {
        return false;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        return false;
    }

    int out_len = 0;
    bool ok =
        EVP_DecryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx,
                            EVP_CTRL_CCM_SET_IVLEN,
                            static_cast<int>(kOpenSslCcmNonceLen),
                            nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx,
                            EVP_CTRL_CCM_SET_TAG,
                            static_cast<int>(auth_len),
                            const_cast<uint8_t*>(auth)) == 1 &&
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce) == 1 &&
        EVP_DecryptUpdate(ctx,
                          nullptr,
                          &out_len,
                          nullptr,
                          static_cast<int>(crypt_len)) == 1;
    if (ok && aad && aad_len > 0)
    {
        ok = EVP_DecryptUpdate(ctx,
                               nullptr,
                               &out_len,
                               aad,
                               static_cast<int>(aad_len)) == 1;
    }
    if (ok)
    {
        ok = EVP_DecryptUpdate(ctx,
                               out_plain,
                               &out_len,
                               crypt,
                               static_cast<int>(crypt_len)) == 1 &&
             static_cast<size_t>(out_len) == crypt_len;
    }

    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

bool opensslAesCcmEncrypt(const uint8_t* key,
                          size_t key_len,
                          const uint8_t nonce[kPkiNonceSize],
                          size_t auth_len,
                          const uint8_t* aad,
                          size_t aad_len,
                          const uint8_t* plain,
                          size_t plain_len,
                          uint8_t* out_cipher,
                          uint8_t* out_auth)
{
    const EVP_CIPHER* cipher = opensslAesCcmCipher(key_len);
    if (!cipher || !nonce || !plain || !out_cipher || !out_auth ||
        auth_len == 0 || auth_len > kAesBlockSize || aad_len > kMaxAadLen)
    {
        return false;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        return false;
    }

    int out_len = 0;
    bool ok =
        EVP_EncryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx,
                            EVP_CTRL_CCM_SET_IVLEN,
                            static_cast<int>(kOpenSslCcmNonceLen),
                            nullptr) == 1 &&
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce) == 1 &&
        EVP_EncryptUpdate(ctx,
                          nullptr,
                          &out_len,
                          nullptr,
                          static_cast<int>(plain_len)) == 1;
    if (ok && aad && aad_len > 0)
    {
        ok = EVP_EncryptUpdate(ctx,
                               nullptr,
                               &out_len,
                               aad,
                               static_cast<int>(aad_len)) == 1;
    }
    if (ok)
    {
        ok = EVP_EncryptUpdate(ctx,
                               out_cipher,
                               &out_len,
                               plain,
                               static_cast<int>(plain_len)) == 1 &&
             static_cast<size_t>(out_len) == plain_len &&
             EVP_EncryptFinal_ex(ctx, out_cipher + out_len, &out_len) == 1 &&
             EVP_CIPHER_CTX_ctrl(ctx,
                                 EVP_CTRL_CCM_GET_TAG,
                                 static_cast<int>(auth_len),
                                 out_auth) == 1;
    }

    EVP_CIPHER_CTX_free(ctx);
    return ok;
}
#endif

} // namespace

void hashSharedKey(uint8_t* bytes, size_t num_bytes)
{
    if (!bytes || num_bytes == 0)
    {
        return;
    }

#if TRAILMATE_MESHTASTIC_PKI_HAS_ARDUINO_CRYPTO
    SHA256 hash;
    uint8_t size = static_cast<uint8_t>(num_bytes);
    constexpr uint8_t kChunkSize = 16;
    hash.reset();
    for (size_t pos = 0; pos < size; pos += kChunkSize)
    {
        size_t len = size - pos;
        if (len > kChunkSize)
        {
            len = kChunkSize;
        }
        hash.update(bytes + pos, len);
    }
    hash.finalize(bytes, 32);
#elif defined(TRAIL_MATE_HAS_OPENSSL)
    uint8_t digest[32] = {};
    if (opensslSha256(bytes, num_bytes,
                      nullptr, 0,
                      nullptr, 0,
                      nullptr, 0,
                      nullptr, 0,
                      nullptr, 0,
                      digest))
    {
        memcpy(bytes, digest, sizeof(digest));
    }
#else
    (void)bytes;
    (void)num_bytes;
#endif
}

void initPkiNonce(uint32_t from,
                  uint64_t packet_id,
                  uint32_t extra_nonce,
                  uint8_t out_nonce[kPkiNonceSize])
{
    if (!out_nonce)
    {
        return;
    }

    memset(out_nonce, 0, kPkiNonceSize);
    memcpy(out_nonce, &packet_id, sizeof(packet_id));
    memcpy(out_nonce + sizeof(packet_id), &from, sizeof(from));
    if (extra_nonce)
    {
        memcpy(out_nonce + sizeof(uint32_t), &extra_nonce, sizeof(extra_nonce));
    }
}

bool decryptPkiAesCcm(const uint8_t* key,
                      size_t key_len,
                      const uint8_t nonce[kPkiNonceSize],
                      size_t auth_len,
                      const uint8_t* crypt,
                      size_t crypt_len,
                      const uint8_t* aad,
                      size_t aad_len,
                      const uint8_t* auth,
                      uint8_t* out_plain)
{
    if (!key || key_len == 0 || !nonce || !crypt || !auth || !out_plain ||
        aad_len > kMaxAadLen || auth_len > kAesBlockSize)
    {
        return false;
    }

#if TRAILMATE_MESHTASTIC_PKI_HAS_ARDUINO_CRYPTO
    uint8_t x[kAesBlockSize];
    uint8_t block[kAesBlockSize];
    uint8_t tag[kAesBlockSize];
    AesCcmCipher cipher;
    cipher.setKey(key, key_len);
    aesCcmEncrStart(kCcmLengthSize, nonce, block);
    aesCcmDecryptAuth(cipher, auth_len, block, auth, tag);
    aesCcmEncr(cipher, kCcmLengthSize, crypt, crypt_len, out_plain, block);
    aesCcmAuthStart(cipher, auth_len, kCcmLengthSize, nonce, aad, aad_len, crypt_len, x);
    aesCcmAuth(cipher, out_plain, crypt_len, x);
    return constantTimeCompare(x, tag, auth_len) == 0;
#elif defined(TRAIL_MATE_HAS_OPENSSL)
    return opensslAesCcmDecrypt(key, key_len, nonce, auth_len,
                                crypt, crypt_len, aad, aad_len, auth, out_plain);
#else
    (void)key;
    (void)key_len;
    (void)nonce;
    (void)auth_len;
    (void)crypt;
    (void)crypt_len;
    (void)aad;
    (void)aad_len;
    (void)auth;
    (void)out_plain;
    return false;
#endif
}

bool encryptPkiAesCcm(const uint8_t* key,
                      size_t key_len,
                      const uint8_t nonce[kPkiNonceSize],
                      size_t auth_len,
                      const uint8_t* aad,
                      size_t aad_len,
                      const uint8_t* plain,
                      size_t plain_len,
                      uint8_t* out_cipher,
                      uint8_t* out_auth)
{
    if (!key || key_len == 0 || !nonce || !plain || !out_cipher || !out_auth ||
        aad_len > kMaxAadLen || auth_len > kAesBlockSize)
    {
        return false;
    }

#if TRAILMATE_MESHTASTIC_PKI_HAS_ARDUINO_CRYPTO
    uint8_t x[kAesBlockSize];
    uint8_t block[kAesBlockSize];
    AesCcmCipher cipher;
    cipher.setKey(key, key_len);
    aesCcmAuthStart(cipher, auth_len, kCcmLengthSize, nonce, aad, aad_len, plain_len, x);
    aesCcmAuth(cipher, plain, plain_len, x);
    aesCcmEncrStart(kCcmLengthSize, nonce, block);
    aesCcmEncr(cipher, kCcmLengthSize, plain, plain_len, out_cipher, block);
    aesCcmEncryptAuth(cipher, auth_len, x, block, out_auth);
    return true;
#elif defined(TRAIL_MATE_HAS_OPENSSL)
    return opensslAesCcmEncrypt(key, key_len, nonce, auth_len,
                                aad, aad_len, plain, plain_len,
                                out_cipher, out_auth);
#else
    (void)key;
    (void)key_len;
    (void)nonce;
    (void)auth_len;
    (void)aad;
    (void)aad_len;
    (void)plain;
    (void)plain_len;
    (void)out_cipher;
    (void)out_auth;
    return false;
#endif
}

bool computeKeyVerificationHashes(uint32_t security_number,
                                  uint64_t nonce,
                                  uint32_t first_node_id,
                                  uint32_t second_node_id,
                                  const uint8_t* first_pubkey,
                                  size_t first_pubkey_len,
                                  const uint8_t* second_pubkey,
                                  size_t second_pubkey_len,
                                  uint8_t out_hash1[32],
                                  uint8_t out_hash2[32])
{
    if (!first_pubkey || first_pubkey_len == 0 ||
        !second_pubkey || second_pubkey_len == 0 ||
        !out_hash1 || !out_hash2)
    {
        return false;
    }

#if TRAILMATE_MESHTASTIC_PKI_HAS_ARDUINO_CRYPTO
    SHA256 hash;
    hash.reset();
    hash.update(&security_number, sizeof(security_number));
    hash.update(&nonce, sizeof(nonce));
    hash.update(&first_node_id, sizeof(first_node_id));
    hash.update(&second_node_id, sizeof(second_node_id));
    hash.update(first_pubkey, first_pubkey_len);
    hash.update(second_pubkey, second_pubkey_len);
    hash.finalize(out_hash1, 32);

    hash.reset();
    hash.update(&nonce, sizeof(nonce));
    hash.update(out_hash1, 32);
    hash.finalize(out_hash2, 32);
    return true;
#elif defined(TRAIL_MATE_HAS_OPENSSL)
    return opensslSha256(&security_number, sizeof(security_number),
                         &nonce, sizeof(nonce),
                         &first_node_id, sizeof(first_node_id),
                         &second_node_id, sizeof(second_node_id),
                         first_pubkey, first_pubkey_len,
                         second_pubkey, second_pubkey_len,
                         out_hash1) &&
           opensslSha256(&nonce, sizeof(nonce),
                         out_hash1, 32,
                         nullptr, 0,
                         nullptr, 0,
                         nullptr, 0,
                         nullptr, 0,
                         out_hash2);
#else
    (void)security_number;
    (void)nonce;
    (void)first_node_id;
    (void)second_node_id;
    (void)first_pubkey;
    (void)first_pubkey_len;
    (void)second_pubkey;
    (void)second_pubkey_len;
    (void)out_hash1;
    (void)out_hash2;
    return false;
#endif
}

} // namespace meshtastic
} // namespace chat
