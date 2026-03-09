/**
 * @file mt_pki_crypto.cpp
 * @brief Shared Meshtastic PKI/AES-CCM crypto helpers
 */

#include "chat/infra/meshtastic/mt_pki_crypto.h"

#include <AES.h>
#include <Crypto.h>
#include <SHA256.h>

#include <cstring>

namespace chat
{
namespace meshtastic
{
namespace
{
constexpr size_t kAesBlockSize = 16;
constexpr size_t kMaxAadLen = 30;

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

int constantTimeCompare(const void* a_, const void* b_, size_t len)
{
    const volatile uint8_t* volatile a = (const volatile uint8_t* volatile)a_;
    const volatile uint8_t* volatile b = (const volatile uint8_t* volatile)b_;
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

} // namespace

void hashSharedKey(uint8_t* bytes, size_t num_bytes)
{
    if (!bytes || num_bytes == 0)
    {
        return;
    }

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

    const size_t length_size = 2;
    uint8_t x[kAesBlockSize];
    uint8_t block[kAesBlockSize];
    uint8_t tag[kAesBlockSize];
    AesCcmCipher cipher;
    cipher.setKey(key, key_len);
    aesCcmEncrStart(length_size, nonce, block);
    aesCcmDecryptAuth(cipher, auth_len, block, auth, tag);
    aesCcmEncr(cipher, length_size, crypt, crypt_len, out_plain, block);
    aesCcmAuthStart(cipher, auth_len, length_size, nonce, aad, aad_len, crypt_len, x);
    aesCcmAuth(cipher, out_plain, crypt_len, x);
    return constantTimeCompare(x, tag, auth_len) == 0;
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

    const size_t length_size = 2;
    uint8_t x[kAesBlockSize];
    uint8_t block[kAesBlockSize];
    AesCcmCipher cipher;
    cipher.setKey(key, key_len);
    aesCcmAuthStart(cipher, auth_len, length_size, nonce, aad, aad_len, plain_len, x);
    aesCcmAuth(cipher, plain, plain_len, x);
    aesCcmEncrStart(length_size, nonce, block);
    aesCcmEncr(cipher, length_size, plain, plain_len, out_cipher, block);
    aesCcmEncryptAuth(cipher, auth_len, x, block, out_auth);
    return true;
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
}

} // namespace meshtastic
} // namespace chat
