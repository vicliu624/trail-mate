#include "team_crypto.h"

#include <ChaChaPoly.h>
#include <SHA256.h>
#include <cstring>

namespace team::infra
{
namespace
{

constexpr size_t kChaChaTagSize = 16;

bool sha256Kdf(const uint8_t* key, size_t key_len, const char* info,
               uint8_t* out, size_t out_len)
{
    if (!key || !info || !out || out_len > 32)
    {
        return false;
    }

    SHA256 hash;
    hash.reset();
    hash.update(key, key_len);
    hash.update(reinterpret_cast<const uint8_t*>(info), strlen(info));
    uint8_t digest[32];
    hash.finalize(digest, sizeof(digest));
    memcpy(out, digest, out_len);
    return true;
}

bool chachaEncrypt(const uint8_t* key, size_t key_len,
                   const uint8_t* nonce, size_t nonce_len,
                   const uint8_t* aad, size_t aad_len,
                   const uint8_t* plain, size_t plain_len,
                   std::vector<uint8_t>& out_cipher)
{
    if (!key || !nonce || !plain)
    {
        return false;
    }

    ChaChaPoly cipher;
    if (!cipher.setKey(key, key_len))
    {
        return false;
    }
    if (!cipher.setIV(nonce, nonce_len))
    {
        return false;
    }

    if (aad_len > 0 && aad)
    {
        cipher.addAuthData(aad, aad_len);
    }

    out_cipher.assign(plain_len + kChaChaTagSize, 0);
    if (plain_len > 0)
    {
        cipher.encrypt(out_cipher.data(), plain, plain_len);
    }
    cipher.computeTag(out_cipher.data() + plain_len, kChaChaTagSize);
    return true;
}

bool chachaDecrypt(const uint8_t* key, size_t key_len,
                   const uint8_t* nonce, size_t nonce_len,
                   const uint8_t* aad, size_t aad_len,
                   const uint8_t* cipher, size_t cipher_len,
                   std::vector<uint8_t>& out_plain)
{
    if (!key || !nonce || !cipher || cipher_len < kChaChaTagSize)
    {
        return false;
    }

    ChaChaPoly cipher_ctx;
    if (!cipher_ctx.setKey(key, key_len))
    {
        return false;
    }
    if (!cipher_ctx.setIV(nonce, nonce_len))
    {
        return false;
    }

    if (aad_len > 0 && aad)
    {
        cipher_ctx.addAuthData(aad, aad_len);
    }

    size_t plain_len = cipher_len - kChaChaTagSize;
    out_plain.assign(plain_len, 0);
    if (plain_len > 0)
    {
        cipher_ctx.decrypt(out_plain.data(), cipher, plain_len);
    }

    const uint8_t* tag = cipher + plain_len;
    if (!cipher_ctx.checkTag(tag, kChaChaTagSize))
    {
        out_plain.clear();
        return false;
    }

    return true;
}

} // namespace

bool TeamCrypto::deriveKey(const uint8_t* key, size_t key_len,
                           const char* info,
                           uint8_t* out, size_t out_len)
{
    return sha256Kdf(key, key_len, info, out, out_len);
}

bool TeamCrypto::aeadEncrypt(const uint8_t* key, size_t key_len,
                             const uint8_t* nonce, size_t nonce_len,
                             const uint8_t* aad, size_t aad_len,
                             const uint8_t* plain, size_t plain_len,
                             std::vector<uint8_t>& out_cipher)
{
    return chachaEncrypt(key, key_len, nonce, nonce_len,
                         aad, aad_len, plain, plain_len, out_cipher);
}

bool TeamCrypto::aeadDecrypt(const uint8_t* key, size_t key_len,
                             const uint8_t* nonce, size_t nonce_len,
                             const uint8_t* aad, size_t aad_len,
                             const uint8_t* cipher, size_t cipher_len,
                             std::vector<uint8_t>& out_plain)
{
    return chachaDecrypt(key, key_len, nonce, nonce_len,
                         aad, aad_len, cipher, cipher_len, out_plain);
}

} // namespace team::infra
