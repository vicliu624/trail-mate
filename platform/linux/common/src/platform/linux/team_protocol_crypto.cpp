#include "platform/linux/team_protocol_crypto.h"
#include <SHA256.h>
#include <climits>
#include <cstring>
#include <memory>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#endif

namespace platform::linux_runtime
{
bool TeamProtocolCrypto::deriveKey(const uint8_t* key, size_t key_len, const char* info,
                                   uint8_t* out, size_t out_len)
{
    if (!key || !key_len || !info || !out || out_len > 32) return false;
    SHA256 hash;
    hash.reset();
    hash.update(key, key_len);
    hash.update(info, std::strlen(info));
    uint8_t digest[32]{};
    hash.finalize(digest, sizeof(digest));
    std::memcpy(out, digest, out_len);
    return true;
}

bool TeamProtocolCrypto::aeadEncrypt(const uint8_t* key, size_t key_len, const uint8_t* nonce, size_t nonce_len,
                                     const uint8_t* aad, size_t aad_len, const uint8_t* plain, size_t plain_len,
                                     std::vector<uint8_t>& out)
{
    out.clear();
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!key || key_len != 32 || !nonce || nonce_len != 12 || (!plain && plain_len) ||
        (!aad && aad_len) || plain_len > INT_MAX || aad_len > INT_MAX) return false;
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr, key, nonce) != 1) return false;
    int size = 0;
    if (aad_len && EVP_EncryptUpdate(ctx.get(), nullptr, &size, aad, static_cast<int>(aad_len)) != 1) return false;
    out.resize(plain_len + 16);
    int written = 0;
    if (EVP_EncryptUpdate(ctx.get(), out.data(), &written, plain, static_cast<int>(plain_len)) != 1 ||
        EVP_EncryptFinal_ex(ctx.get(), out.data() + written, &size) != 1 ||
        static_cast<size_t>(written + size) != plain_len ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_GET_TAG, 16, out.data() + plain_len) != 1)
    {
        out.clear();
        return false;
    }
    return true;
#else
    (void)key;
    (void)key_len;
    (void)nonce;
    (void)nonce_len;
    (void)aad;
    (void)aad_len;
    (void)plain;
    (void)plain_len;
    return false;
#endif
}

bool TeamProtocolCrypto::aeadDecrypt(const uint8_t* key, size_t key_len, const uint8_t* nonce, size_t nonce_len,
                                     const uint8_t* aad, size_t aad_len, const uint8_t* cipher, size_t cipher_len,
                                     std::vector<uint8_t>& out)
{
    out.clear();
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!key || key_len != 32 || !nonce || nonce_len != 12 || !cipher || cipher_len < 16 ||
        (!aad && aad_len) || cipher_len > INT_MAX || aad_len > INT_MAX) return false;
    const size_t plain_len = cipher_len - 16;
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr, key, nonce) != 1) return false;
    int size = 0;
    if (aad_len && EVP_DecryptUpdate(ctx.get(), nullptr, &size, aad, static_cast<int>(aad_len)) != 1) return false;
    out.resize(plain_len + 16);
    int written = 0;
    if (EVP_DecryptUpdate(ctx.get(), out.data(), &written, cipher, static_cast<int>(plain_len)) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_TAG, 16, const_cast<uint8_t*>(cipher + plain_len)) != 1 ||
        EVP_DecryptFinal_ex(ctx.get(), out.data() + written, &size) != 1 ||
        static_cast<size_t>(written + size) != plain_len)
    {
        out.clear();
        return false;
    }
    out.resize(plain_len);
    return true;
#else
    (void)key;
    (void)key_len;
    (void)nonce;
    (void)nonce_len;
    (void)aad;
    (void)aad_len;
    (void)cipher;
    (void)cipher_len;
    return false;
#endif
}
} // namespace platform::linux_runtime
