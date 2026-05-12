/**
 * @file meshcore_protocol_helpers.cpp
 * @brief Shared MeshCore protocol helper utilities
 */

#include "chat/infra/meshcore/meshcore_protocol_helpers.h"

#if defined(ESP_PLATFORM)
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#elif defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#include <openssl/hmac.h>
#else
#if defined(ARDUINO)
#include <AES.h>
#include <SHA256.h>
#else
#define CHAT_MESHCORE_NO_CRYPTO_BACKEND 1
#endif
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace chat
{
namespace meshcore
{
namespace
{
constexpr uint8_t kPayloadTypeTrace = 0x09;
constexpr uint32_t kSendTimeoutBaseMs = 500;
constexpr float kFloodSendTimeoutFactor = 16.0f;
constexpr float kDirectSendPerhopFactor = 6.0f;
constexpr uint32_t kDirectSendPerhopExtraMs = 250;
constexpr size_t kCipherBlockSize = 16;
constexpr size_t kCipherMacSize = 2;
constexpr size_t kCipherKeySize = 16;
constexpr size_t kCipherHmacKeySize = 32;

template <typename T>
T clampValue(T value, T min_value, T max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

#if defined(ESP_PLATFORM)
class Sha256Accumulator
{
  public:
    Sha256Accumulator()
    {
        mbedtls_sha256_init(&ctx_);
        mbedtls_sha256_starts(&ctx_, 0);
        valid_ = true;
    }

    ~Sha256Accumulator()
    {
        mbedtls_sha256_free(&ctx_);
    }

    void update(const void* data, size_t len)
    {
        if (!valid_ || !data || len == 0)
        {
            return;
        }
        mbedtls_sha256_update(&ctx_,
                              static_cast<const unsigned char*>(data),
                              len);
    }

    bool finalize(uint8_t out_hash[32])
    {
        if (!valid_ || !out_hash)
        {
            return false;
        }
        mbedtls_sha256_finish(&ctx_, out_hash);
        return true;
    }

  private:
    mbedtls_sha256_context ctx_{};
    bool valid_ = false;
};

class Aes128EcbCipher
{
  public:
    Aes128EcbCipher()
    {
        mbedtls_aes_init(&encrypt_);
        mbedtls_aes_init(&decrypt_);
    }

    ~Aes128EcbCipher()
    {
        mbedtls_aes_free(&encrypt_);
        mbedtls_aes_free(&decrypt_);
    }

    bool setKey(const uint8_t* key, size_t len)
    {
        if (!key || len != kCipherKeySize)
        {
            return false;
        }
        return mbedtls_aes_setkey_enc(&encrypt_, key, static_cast<unsigned>(len * 8U)) == 0 &&
               mbedtls_aes_setkey_dec(&decrypt_, key, static_cast<unsigned>(len * 8U)) == 0;
    }

    bool encryptBlock(uint8_t* out, const uint8_t* in)
    {
        return out && in &&
               mbedtls_aes_crypt_ecb(&encrypt_, MBEDTLS_AES_ENCRYPT, in, out) == 0;
    }

    bool decryptBlock(uint8_t* out, const uint8_t* in)
    {
        return out && in &&
               mbedtls_aes_crypt_ecb(&decrypt_, MBEDTLS_AES_DECRYPT, in, out) == 0;
    }

  private:
    mbedtls_aes_context encrypt_{};
    mbedtls_aes_context decrypt_{};
};

bool hmacSha256(const uint8_t* key,
                size_t key_len,
                const uint8_t* data,
                size_t data_len,
                uint8_t out_hash[32])
{
    if (!key || key_len == 0 || !out_hash)
    {
        return false;
    }

    static constexpr uint8_t kEmpty = 0;
    const uint8_t* input = (data && data_len != 0) ? data : &kEmpty;
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info &&
           mbedtls_md_hmac(info, key, key_len, input, data_len, out_hash) == 0;
}
#elif defined(TRAIL_MATE_HAS_OPENSSL)
class Sha256Accumulator
{
  public:
    Sha256Accumulator()
        : ctx_(EVP_MD_CTX_new())
    {
        valid_ = ctx_ && EVP_DigestInit_ex(ctx_, EVP_sha256(), nullptr) == 1;
    }

    ~Sha256Accumulator()
    {
        if (ctx_)
        {
            EVP_MD_CTX_free(ctx_);
        }
    }

    void update(const void* data, size_t len)
    {
        if (!valid_ || !data || len == 0)
        {
            return;
        }
        valid_ = EVP_DigestUpdate(ctx_, data, len) == 1;
    }

    bool finalize(uint8_t out_hash[32])
    {
        if (!valid_ || !out_hash)
        {
            return false;
        }
        unsigned int out_len = 0;
        return EVP_DigestFinal_ex(ctx_, out_hash, &out_len) == 1 && out_len == 32;
    }

  private:
    EVP_MD_CTX* ctx_ = nullptr;
    bool valid_ = false;
};

class Aes128EcbCipher
{
  public:
    Aes128EcbCipher()
        : encrypt_(EVP_CIPHER_CTX_new()),
          decrypt_(EVP_CIPHER_CTX_new())
    {
    }

    ~Aes128EcbCipher()
    {
        if (encrypt_)
        {
            EVP_CIPHER_CTX_free(encrypt_);
        }
        if (decrypt_)
        {
            EVP_CIPHER_CTX_free(decrypt_);
        }
    }

    bool setKey(const uint8_t* key, size_t len)
    {
        if (!key || len != kCipherKeySize || !encrypt_ || !decrypt_)
        {
            return false;
        }
        if (EVP_EncryptInit_ex(encrypt_, EVP_aes_128_ecb(), nullptr, key, nullptr) != 1 ||
            EVP_CIPHER_CTX_set_padding(encrypt_, 0) != 1 ||
            EVP_DecryptInit_ex(decrypt_, EVP_aes_128_ecb(), nullptr, key, nullptr) != 1 ||
            EVP_CIPHER_CTX_set_padding(decrypt_, 0) != 1)
        {
            return false;
        }
        return true;
    }

    bool encryptBlock(uint8_t* out, const uint8_t* in)
    {
        if (!out || !in)
        {
            return false;
        }
        int out_len = 0;
        return EVP_EncryptUpdate(encrypt_, out, &out_len, in, kCipherBlockSize) == 1 &&
               out_len == static_cast<int>(kCipherBlockSize);
    }

    bool decryptBlock(uint8_t* out, const uint8_t* in)
    {
        if (!out || !in)
        {
            return false;
        }
        int out_len = 0;
        return EVP_DecryptUpdate(decrypt_, out, &out_len, in, kCipherBlockSize) == 1 &&
               out_len == static_cast<int>(kCipherBlockSize);
    }

  private:
    EVP_CIPHER_CTX* encrypt_ = nullptr;
    EVP_CIPHER_CTX* decrypt_ = nullptr;
};

bool hmacSha256(const uint8_t* key,
                size_t key_len,
                const uint8_t* data,
                size_t data_len,
                uint8_t out_hash[32])
{
    if (!key || key_len == 0 || !out_hash)
    {
        return false;
    }

    static constexpr uint8_t kEmpty = 0;
    const uint8_t* input = (data && data_len != 0) ? data : &kEmpty;
    unsigned int out_len = 0;
    return HMAC(EVP_sha256(), key, static_cast<int>(key_len),
                input, data_len, out_hash, &out_len) != nullptr &&
           out_len == 32;
}
#endif

} // namespace

std::string toHex(const uint8_t* data, size_t len, size_t max_len)
{
    if (!data || len == 0)
    {
        return {};
    }
    size_t capped = (len > max_len) ? max_len : len;
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(capped * 2 + 2);
    for (size_t i = 0; i < capped; ++i)
    {
        uint8_t b = data[i];
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    if (capped < len)
    {
        out.append("..");
    }
    return out;
}

uint8_t buildHeader(uint8_t route_type, uint8_t payload_type, uint8_t payload_ver)
{
    return (route_type & 0x03) | ((payload_type & 0x0F) << 2) | ((payload_ver & 0x03) << 6);
}

bool parsePacket(const uint8_t* data, size_t len, ParsedPacket* out)
{
    if (!data || len < 2 || !out)
    {
        return false;
    }

    uint8_t header = data[0];
    out->route_type = header & 0x03;
    out->payload_type = (header >> 2) & 0x0F;
    out->payload_ver = (header >> 6) & 0x03;

    size_t index = 1;
    if (out->route_type == 0 || out->route_type == 3)
    {
        if (len < index + 4 + 1)
        {
            return false;
        }
        index += 4;
    }

    if (index >= len)
    {
        return false;
    }

    out->path_len_index = index;
    out->path_len = data[index++];
    if (index + out->path_len > len)
    {
        return false;
    }
    out->path = &data[index];
    index += out->path_len;

    if (index > len)
    {
        return false;
    }

    out->payload = &data[index];
    out->payload_len = len - index;
    return true;
}

uint32_t hashFrame(const uint8_t* data, size_t len)
{
    if (!data || len == 0)
    {
        return 0;
    }
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i)
    {
        hash ^= static_cast<uint32_t>(data[i]);
        hash *= 16777619u;
    }
    return hash;
}

uint32_t packetSignature(uint8_t payload_type, size_t path_len,
                         const uint8_t* payload, size_t payload_len)
{
    uint8_t sig_bytes[sizeof(uint32_t)] = {};
#if defined(ESP_PLATFORM) || defined(TRAIL_MATE_HAS_OPENSSL)
    uint8_t hash_bytes[32] = {};
    Sha256Accumulator sha;
    sha.update(&payload_type, sizeof(payload_type));
    if (payload_type == kPayloadTypeTrace)
    {
        uint8_t p = static_cast<uint8_t>(path_len & 0xFFU);
        sha.update(&p, sizeof(p));
    }
    if (payload && payload_len > 0)
    {
        sha.update(payload, payload_len);
    }
    if (!sha.finalize(hash_bytes))
    {
        return 0;
    }
    memcpy(sig_bytes, hash_bytes, sizeof(sig_bytes));
#elif defined(ARDUINO)
    SHA256 sha;
    sha.update(&payload_type, sizeof(payload_type));
    if (payload_type == kPayloadTypeTrace)
    {
        uint8_t p = static_cast<uint8_t>(path_len & 0xFFU);
        sha.update(&p, sizeof(p));
    }
    if (payload && payload_len > 0)
    {
        sha.update(payload, payload_len);
    }
    sha.finalize(sig_bytes, sizeof(sig_bytes));
#else
    uint32_t fallback = 2166136261u;
    fallback ^= static_cast<uint32_t>(payload_type);
    fallback *= 16777619u;
    fallback ^= static_cast<uint32_t>(path_len & 0xFFU);
    fallback *= 16777619u;
    if (payload && payload_len > 0)
    {
        for (size_t index = 0; index < payload_len; ++index)
        {
            fallback ^= static_cast<uint32_t>(payload[index]);
            fallback *= 16777619u;
        }
    }
    memcpy(sig_bytes, &fallback, sizeof(sig_bytes));
#endif

    uint32_t sig = 0;
    memcpy(&sig, sig_bytes, sizeof(sig));
    return sig;
}

float estimateLoRaAirtimeMs(size_t frame_len, float bw_khz, uint8_t sf, uint8_t cr_denom)
{
    if (bw_khz <= 0.0f || sf < 5 || sf > 12)
    {
        return 0.0f;
    }

    const float bw_hz = bw_khz * 1000.0f;
    const float tsym = std::pow(2.0f, static_cast<float>(sf)) / bw_hz;
    const float de = (sf >= 11 && bw_khz <= 125.0f) ? 1.0f : 0.0f;
    const float ih = 0.0f;
    const float crc = 1.0f;
    const float cr = static_cast<float>(cr_denom);
    const float payload_bits = (8.0f * static_cast<float>(frame_len)) - (4.0f * sf) + 28.0f +
                               (16.0f * crc) - (20.0f * ih);
    const float denom = 4.0f * (static_cast<float>(sf) - (2.0f * de));
    float payload_sym = 8.0f;
    if (denom > 0.0f)
    {
        payload_sym += std::max(0.0f, std::ceil(payload_bits / denom) * cr);
    }
    const float preamble_sym = 8.0f + 4.25f;
    const float total_sec = (preamble_sym + payload_sym) * tsym;
    return total_sec * 1000.0f;
}

uint32_t estimateSendTimeoutMs(size_t frame_len, size_t path_len, bool flood,
                               float bw_khz, uint8_t sf, uint8_t cr_denom)
{
    float air_ms_f = estimateLoRaAirtimeMs(frame_len, bw_khz, sf, cr_denom);
    uint32_t air_ms = (air_ms_f > 0.0f && std::isfinite(air_ms_f))
                          ? static_cast<uint32_t>(std::lround(air_ms_f))
                          : 0U;
    if (flood)
    {
        return kSendTimeoutBaseMs +
               static_cast<uint32_t>(kFloodSendTimeoutFactor * static_cast<float>(air_ms));
    }
    const float perhop = static_cast<float>(air_ms) * kDirectSendPerhopFactor +
                         static_cast<float>(kDirectSendPerhopExtraMs);
    return kSendTimeoutBaseMs +
           static_cast<uint32_t>(perhop * static_cast<float>(path_len + 1));
}

uint32_t saturatingAddU32(uint32_t base, uint32_t delta)
{
    const uint64_t total = static_cast<uint64_t>(base) + static_cast<uint64_t>(delta);
    return (total > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
               ? std::numeric_limits<uint32_t>::max()
               : static_cast<uint32_t>(total);
}

float scoreFromSnr(float snr, uint8_t sf, size_t packet_len)
{
    static const float kSnrThreshold[] = {-7.5f, -10.0f, -12.5f, -15.0f, -17.5f, -20.0f};
    if (!std::isfinite(snr) || sf < 7 || sf > 12)
    {
        return 0.0f;
    }
    float threshold = kSnrThreshold[sf - 7];
    if (snr < threshold)
    {
        return 0.0f;
    }
    float success = (snr - threshold) / 10.0f;
    float collision = 1.0f - (std::min<size_t>(packet_len, 256U) / 256.0f);
    return clampValue<float>(success * collision, 0.0f, 1.0f);
}

uint32_t computeRxDelayMs(float rx_delay_base, float score, uint32_t air_ms)
{
    if (rx_delay_base <= 0.0f || air_ms == 0)
    {
        return 0;
    }
    float delay = (std::pow(rx_delay_base, 0.85f - score) - 1.0f) * static_cast<float>(air_ms);
    if (!std::isfinite(delay) || delay <= 0.0f)
    {
        return 0;
    }
    if (delay > 32000.0f)
    {
        delay = 32000.0f;
    }
    return static_cast<uint32_t>(delay);
}

bool isZeroKey(const uint8_t* key, size_t len)
{
    if (!key || len == 0)
    {
        return true;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (key[i] != 0)
        {
            return false;
        }
    }
    return true;
}

void toHmacKey32(const uint8_t* key16, uint8_t* out_key32)
{
    if (!key16 || !out_key32)
    {
        return;
    }
    memset(out_key32, 0, kCipherHmacKeySize);
    memcpy(out_key32, key16, kCipherKeySize);
}

void sharedSecretToKeys(const uint8_t* secret, uint8_t* out_key16, uint8_t* out_key32)
{
    if (!secret || !out_key16 || !out_key32)
    {
        return;
    }
    memcpy(out_key16, secret, kCipherKeySize);
    memcpy(out_key32, secret, kCipherHmacKeySize);
}

void sha256Trunc(uint8_t* out_hash, size_t out_len, const uint8_t* msg, size_t msg_len)
{
    if (!out_hash || out_len == 0)
    {
        return;
    }
#if defined(ESP_PLATFORM) || defined(TRAIL_MATE_HAS_OPENSSL)
    uint8_t full_hash[32] = {};
    Sha256Accumulator sha;
    if (msg && msg_len != 0)
    {
        sha.update(msg, msg_len);
    }
    if (!sha.finalize(full_hash))
    {
        memset(out_hash, 0, out_len);
        return;
    }
    memcpy(out_hash, full_hash, std::min(out_len, sizeof(full_hash)));
#elif defined(ARDUINO)
    SHA256 sha;
    sha.update(msg, static_cast<size_t>(msg_len));
    sha.finalize(out_hash, out_len);
#else
    uint32_t fallback = 2166136261u;
    if (msg && msg_len != 0)
    {
        for (size_t index = 0; index < msg_len; ++index)
        {
            fallback ^= static_cast<uint32_t>(msg[index]);
            fallback *= 16777619u;
        }
    }
    memset(out_hash, 0, out_len);
    memcpy(out_hash, &fallback, std::min(out_len, sizeof(fallback)));
#endif
}

uint8_t computeChannelHash(const uint8_t* key16)
{
    uint8_t hash = 0;
    if (!key16)
    {
        return hash;
    }
    sha256Trunc(&hash, 1, key16, kCipherKeySize);
    return hash;
}

size_t aesEncrypt(const uint8_t* key16, uint8_t* dest, const uint8_t* src, size_t src_len)
{
    if (!key16 || !dest || !src || src_len == 0)
    {
        return 0;
    }
#if defined(ESP_PLATFORM) || defined(TRAIL_MATE_HAS_OPENSSL)
    Aes128EcbCipher aes;
    if (!aes.setKey(key16, kCipherKeySize))
    {
        return 0;
    }
#elif defined(ARDUINO)
    AES128 aes;
    aes.setKey(key16, kCipherKeySize);
#else
    return 0;
#endif

    uint8_t* dest_ptr = dest;
    const uint8_t* src_ptr = src;
    size_t remaining = src_len;
    while (remaining >= kCipherBlockSize)
    {
#if defined(ESP_PLATFORM) || defined(TRAIL_MATE_HAS_OPENSSL)
        if (!aes.encryptBlock(dest_ptr, src_ptr))
        {
            return 0;
        }
#elif defined(ARDUINO)
        aes.encryptBlock(dest_ptr, src_ptr);
#endif
        dest_ptr += kCipherBlockSize;
        src_ptr += kCipherBlockSize;
        remaining -= kCipherBlockSize;
    }
    if (remaining > 0)
    {
        uint8_t tail[kCipherBlockSize];
        memset(tail, 0, sizeof(tail));
        memcpy(tail, src_ptr, remaining);
#if defined(ESP_PLATFORM) || defined(TRAIL_MATE_HAS_OPENSSL)
        if (!aes.encryptBlock(dest_ptr, tail))
        {
            return 0;
        }
#elif defined(ARDUINO)
        aes.encryptBlock(dest_ptr, tail);
#endif
        dest_ptr += kCipherBlockSize;
    }
    return static_cast<size_t>(dest_ptr - dest);
}

size_t aesDecrypt(const uint8_t* key16, uint8_t* dest, const uint8_t* src, size_t src_len)
{
    if (!key16 || !dest || !src || src_len == 0 || (src_len % kCipherBlockSize) != 0)
    {
        return 0;
    }
#if defined(ESP_PLATFORM) || defined(TRAIL_MATE_HAS_OPENSSL)
    Aes128EcbCipher aes;
    if (!aes.setKey(key16, kCipherKeySize))
    {
        return 0;
    }
#elif defined(ARDUINO)
    AES128 aes;
    aes.setKey(key16, kCipherKeySize);
#else
    return 0;
#endif

    uint8_t* dest_ptr = dest;
    const uint8_t* src_ptr = src;
    size_t remaining = src_len;
    while (remaining >= kCipherBlockSize)
    {
#if defined(ESP_PLATFORM) || defined(TRAIL_MATE_HAS_OPENSSL)
        if (!aes.decryptBlock(dest_ptr, src_ptr))
        {
            return 0;
        }
#elif defined(ARDUINO)
        aes.decryptBlock(dest_ptr, src_ptr);
#endif
        dest_ptr += kCipherBlockSize;
        src_ptr += kCipherBlockSize;
        remaining -= kCipherBlockSize;
    }
    return static_cast<size_t>(dest_ptr - dest);
}

size_t encryptThenMac(const uint8_t* key16, const uint8_t* key32,
                      uint8_t* out, size_t out_cap,
                      const uint8_t* plain, size_t plain_len)
{
    if (!key16 || !key32 || !out || !plain || plain_len == 0 || out_cap <= kCipherMacSize)
    {
        return 0;
    }

    size_t max_cipher = out_cap - kCipherMacSize;
    size_t cipher_len = ((plain_len + (kCipherBlockSize - 1)) / kCipherBlockSize) * kCipherBlockSize;
    if (cipher_len == 0 || cipher_len > max_cipher)
    {
        return 0;
    }

    size_t enc_len = aesEncrypt(key16, out + kCipherMacSize, plain, plain_len);
    if (enc_len != cipher_len)
    {
        return 0;
    }

#if defined(ESP_PLATFORM) || defined(TRAIL_MATE_HAS_OPENSSL)
    uint8_t full_hash[32] = {};
    if (!hmacSha256(key32, kCipherHmacKeySize, out + kCipherMacSize, enc_len, full_hash))
    {
        return 0;
    }
    memcpy(out, full_hash, kCipherMacSize);
#elif defined(ARDUINO)
    SHA256 sha;
    sha.resetHMAC(key32, kCipherHmacKeySize);
    sha.update(out + kCipherMacSize, enc_len);
    sha.finalizeHMAC(key32, kCipherHmacKeySize, out, kCipherMacSize);
#else
    return 0;
#endif
    return kCipherMacSize + enc_len;
}

bool macThenDecrypt(const uint8_t* key16, const uint8_t* key32,
                    const uint8_t* src, size_t src_len, uint8_t* out_plain, size_t* out_plain_len)
{
    if (!key16 || !key32 || !src || src_len <= kCipherMacSize || !out_plain || !out_plain_len)
    {
        return false;
    }
    size_t cipher_len = src_len - kCipherMacSize;
    if ((cipher_len % kCipherBlockSize) != 0)
    {
        return false;
    }

    uint8_t expected[kCipherMacSize];
#if defined(ESP_PLATFORM) || defined(TRAIL_MATE_HAS_OPENSSL)
    uint8_t full_hash[32] = {};
    if (!hmacSha256(key32, kCipherHmacKeySize, src + kCipherMacSize, cipher_len, full_hash))
    {
        return false;
    }
    memcpy(expected, full_hash, sizeof(expected));
#elif defined(ARDUINO)
    SHA256 sha;
    sha.resetHMAC(key32, kCipherHmacKeySize);
    sha.update(src + kCipherMacSize, cipher_len);
    sha.finalizeHMAC(key32, kCipherHmacKeySize, expected, kCipherMacSize);
#else
    return false;
#endif
    if (memcmp(expected, src, kCipherMacSize) != 0)
    {
        return false;
    }

    size_t plain_len = aesDecrypt(key16, out_plain, src + kCipherMacSize, cipher_len);
    if (plain_len == 0)
    {
        return false;
    }
    *out_plain_len = plain_len;
    return true;
}

size_t trimTrailingZeros(const uint8_t* buf, size_t len)
{
    if (!buf || len == 0)
    {
        return 0;
    }
    while (len > 0 && buf[len - 1] == 0)
    {
        --len;
    }
    return len;
}

} // namespace meshcore
} // namespace chat
