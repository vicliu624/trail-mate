/**
 * @file mt_aes_ctr.cpp
 * @brief AES-CTR implementation used by the Meshtastic wire codec.
 */

#include "chat/infra/meshtastic/mt_aes_ctr.h"

#include <cstring>

#if defined(ESP_PLATFORM) || defined(TRAILMATE_MESHTASTIC_FORCE_SOFTWARE_AES)
#define TRAILMATE_MT_AES_CTR_USE_SOFTWARE 1
#endif

#ifndef TRAILMATE_MT_AES_CTR_USE_SOFTWARE
#define TRAILMATE_MT_AES_CTR_USE_SOFTWARE 0
#endif

#if !TRAILMATE_MT_AES_CTR_USE_SOFTWARE && __has_include(<AES.h>) && __has_include(<CTR.h>)
#include <AES.h>
#include <CTR.h>
#define TRAILMATE_MT_AES_CTR_USE_ARDUINO_CRYPTO 1
#endif

#ifndef TRAILMATE_MT_AES_CTR_USE_ARDUINO_CRYPTO
#define TRAILMATE_MT_AES_CTR_USE_ARDUINO_CRYPTO 0
#endif

#if !TRAILMATE_MT_AES_CTR_USE_SOFTWARE && !TRAILMATE_MT_AES_CTR_USE_ARDUINO_CRYPTO && defined(TRAIL_MATE_HAS_OPENSSL)
#include <limits>
#include <openssl/evp.h>
#endif

namespace chat
{
namespace meshtastic
{
namespace
{

#if TRAILMATE_MT_AES_CTR_USE_SOFTWARE

// ESP-IDF's AES implementation uses DMA on ESP32-S3/P4. Its per-operation
// descriptor allocations can fail under simultaneous display and SD I/O
// pressure. This local primitive uses only fixed automatic storage and keeps
// the AES-CTR wire output compatible with Meshtastic peers.
struct SoftwareAesContext
{
    uint8_t round_keys[32] = {};
    uint8_t key_words = 0;
    uint8_t rounds = 0;
    uint8_t rcon = 0;
};

uint8_t aesXtime(uint8_t value)
{
    return static_cast<uint8_t>((value << 1U) ^ ((value & 0x80U) ? 0x1BU : 0U));
}

uint8_t aesGfMultiply(uint8_t left, uint8_t right)
{
    uint8_t product = 0;
    for (uint8_t bit = 0; bit < 8; ++bit)
    {
        if ((right & 1U) != 0)
        {
            product ^= left;
        }
        left = aesXtime(left);
        right >>= 1U;
    }
    return product;
}

uint8_t aesRotateLeft(uint8_t value, uint8_t count)
{
    return static_cast<uint8_t>((value << count) | (value >> (8U - count)));
}

uint8_t aesMultiplicativeInverse(uint8_t value)
{
    if (value == 0)
    {
        return 0;
    }

    uint8_t result = 1;
    uint8_t factor = value;
    uint8_t exponent = 254;
    while (exponent != 0)
    {
        if ((exponent & 1U) != 0)
        {
            result = aesGfMultiply(result, factor);
        }
        factor = aesGfMultiply(factor, factor);
        exponent >>= 1U;
    }
    return result;
}

uint8_t aesSbox(uint8_t value)
{
    const uint8_t inverse = aesMultiplicativeInverse(value);
    return static_cast<uint8_t>(inverse ^ aesRotateLeft(inverse, 1) ^
                                aesRotateLeft(inverse, 2) ^ aesRotateLeft(inverse, 3) ^
                                aesRotateLeft(inverse, 4) ^ 0x63U);
}

bool aesLoadKey(SoftwareAesContext* context, const uint8_t* key, size_t key_len)
{
    if (!context || !key || (key_len != 16 && key_len != 32))
    {
        return false;
    }

    context->key_words = static_cast<uint8_t>(key_len / 4U);
    context->rounds = static_cast<uint8_t>(context->key_words + 6U);
    context->rcon = 1;
    memcpy(context->round_keys, key, key_len);
    return true;
}

void aesAdvanceAes128RoundKey(SoftwareAesContext* context)
{
    uint8_t next_key[16];
    uint8_t word[4];
    memcpy(word, context->round_keys + 12, sizeof(word));
    const uint8_t first = word[0];
    word[0] = static_cast<uint8_t>(aesSbox(word[1]) ^ context->rcon);
    word[1] = aesSbox(word[2]);
    word[2] = aesSbox(word[3]);
    word[3] = aesSbox(first);
    context->rcon = aesXtime(context->rcon);

    for (size_t key_word = 0; key_word < 4; ++key_word)
    {
        const uint8_t* previous = context->round_keys + (key_word * 4U);
        uint8_t* next = next_key + (key_word * 4U);
        for (size_t byte = 0; byte < sizeof(word); ++byte)
        {
            next[byte] = static_cast<uint8_t>(previous[byte] ^ word[byte]);
        }
        memcpy(word, next, sizeof(word));
    }

    memcpy(context->round_keys, next_key, sizeof(next_key));
}

void aesAdvanceAes256RoundKey(SoftwareAesContext* context, uint8_t round)
{
    const size_t target_offset = (round & 1U) ? 16U : 0U;
    const size_t previous_offset = target_offset == 0 ? 16U : 0U;
    const uint8_t* target = context->round_keys + target_offset;
    const uint8_t* previous = context->round_keys + previous_offset;

    uint8_t next_key[16];
    uint8_t word[4];
    memcpy(word, previous + 12, sizeof(word));
    if ((round & 1U) == 0)
    {
        const uint8_t first = word[0];
        word[0] = static_cast<uint8_t>(aesSbox(word[1]) ^ context->rcon);
        word[1] = aesSbox(word[2]);
        word[2] = aesSbox(word[3]);
        word[3] = aesSbox(first);
        context->rcon = aesXtime(context->rcon);
    }
    else
    {
        for (uint8_t& byte : word)
        {
            byte = aesSbox(byte);
        }
    }

    for (size_t key_word = 0; key_word < 4; ++key_word)
    {
        const uint8_t* older = target + (key_word * 4U);
        uint8_t* next = next_key + (key_word * 4U);
        for (size_t byte = 0; byte < sizeof(word); ++byte)
        {
            next[byte] = static_cast<uint8_t>(older[byte] ^ word[byte]);
        }
        memcpy(word, next, sizeof(word));
    }

    memcpy(context->round_keys + target_offset, next_key, sizeof(next_key));
}

void aesAddRoundKey(uint8_t block[16], const uint8_t* round_key)
{
    for (size_t byte = 0; byte < 16; ++byte)
    {
        block[byte] ^= round_key[byte];
    }
}

void aesSubBytes(uint8_t block[16])
{
    for (size_t byte = 0; byte < 16; ++byte)
    {
        block[byte] = aesSbox(block[byte]);
    }
}

void aesShiftRows(uint8_t block[16])
{
    uint8_t saved = block[1];
    block[1] = block[5];
    block[5] = block[9];
    block[9] = block[13];
    block[13] = saved;

    saved = block[2];
    block[2] = block[10];
    block[10] = saved;
    saved = block[6];
    block[6] = block[14];
    block[14] = saved;

    saved = block[3];
    block[3] = block[15];
    block[15] = block[11];
    block[11] = block[7];
    block[7] = saved;
}

void aesMixColumns(uint8_t block[16])
{
    for (size_t column = 0; column < 4; ++column)
    {
        uint8_t* state = block + (column * 4U);
        const uint8_t first = state[0];
        const uint8_t combined = static_cast<uint8_t>(state[0] ^ state[1] ^ state[2] ^ state[3]);
        const uint8_t pair = static_cast<uint8_t>(state[0] ^ state[1]);
        state[0] ^= static_cast<uint8_t>(combined ^ aesXtime(pair));
        state[1] ^= static_cast<uint8_t>(combined ^ aesXtime(state[1] ^ state[2]));
        state[2] ^= static_cast<uint8_t>(combined ^ aesXtime(state[2] ^ state[3]));
        state[3] ^= static_cast<uint8_t>(combined ^ aesXtime(state[3] ^ first));
    }
}

void aesEncryptBlock(SoftwareAesContext* context, uint8_t block[16])
{
    aesAddRoundKey(block, context->round_keys);
    for (uint8_t round = 1; round <= context->rounds; ++round)
    {
        if (context->key_words == 4)
        {
            aesAdvanceAes128RoundKey(context);
        }
        else if (round >= 2)
        {
            aesAdvanceAes256RoundKey(context, round);
        }

        aesSubBytes(block);
        aesShiftRows(block);
        if (round < context->rounds)
        {
            aesMixColumns(block);
        }

        const size_t round_key_offset = context->key_words == 4 ? 0U : ((round & 1U) ? 16U : 0U);
        aesAddRoundKey(block, context->round_keys + round_key_offset);
    }
}

void aesIncrementCounter(uint8_t counter[16])
{
    for (size_t index = 16; index > 0; --index)
    {
        ++counter[index - 1U];
        if (counter[index - 1U] != 0)
        {
            break;
        }
    }
}

#endif

} // namespace

bool aesCtrCryptInPlace(const uint8_t* key, size_t key_len,
                        const uint8_t counter[16],
                        uint8_t* buffer, size_t len)
{
    if (!key || !counter || !buffer || len == 0 || (key_len != 16 && key_len != 32))
    {
        return false;
    }

#if TRAILMATE_MT_AES_CTR_USE_SOFTWARE
    SoftwareAesContext context;
    uint8_t next_counter[16];
    uint8_t stream_block[16];
    memcpy(next_counter, counter, sizeof(next_counter));
    for (size_t offset = 0; offset < len; offset += sizeof(stream_block))
    {
        if (!aesLoadKey(&context, key, key_len))
        {
            return false;
        }
        memcpy(stream_block, next_counter, sizeof(stream_block));
        aesEncryptBlock(&context, stream_block);
        aesIncrementCounter(next_counter);

        const size_t bytes = (len - offset < sizeof(stream_block)) ? (len - offset) : sizeof(stream_block);
        for (size_t byte = 0; byte < bytes; ++byte)
        {
            buffer[offset + byte] ^= stream_block[byte];
        }
    }
    return true;
#elif TRAILMATE_MT_AES_CTR_USE_ARDUINO_CRYPTO
    constexpr size_t kMaxBlockSize = 256;
    if (len > kMaxBlockSize)
    {
        return false;
    }

    uint8_t scratch[kMaxBlockSize];
    memcpy(scratch, buffer, len);
    memset(scratch + len, 0, sizeof(scratch) - len);
    if (key_len == 16)
    {
        CTR<AES128> ctr;
        ctr.setKey(key, key_len);
        ctr.setIV(counter, 16);
        ctr.setCounterSize(4);
        ctr.encrypt(buffer, scratch, len);
    }
    else
    {
        CTR<AES256> ctr;
        ctr.setKey(key, key_len);
        ctr.setIV(counter, 16);
        ctr.setCounterSize(4);
        ctr.encrypt(buffer, scratch, len);
    }
    return true;
#elif defined(TRAIL_MATE_HAS_OPENSSL)
    if (len > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (!context)
    {
        return false;
    }
    int count = 0;
    int tail = 0;
    // EVP permits identical input/output buffers for CTR. No protocol-sized
    // temporary copy or platform-specific packet implementation is needed.
    const bool ok =
        EVP_EncryptInit_ex(context, key_len == 16 ? EVP_aes_128_ctr() : EVP_aes_256_ctr(),
                           nullptr, key, counter) == 1 &&
        EVP_EncryptUpdate(context, buffer, &count, buffer, static_cast<int>(len)) == 1 &&
        EVP_EncryptFinal_ex(context, buffer + count, &tail) == 1 &&
        static_cast<size_t>(count + tail) == len;
    EVP_CIPHER_CTX_free(context);
    return ok;
#else
    return false;
#endif
}

} // namespace meshtastic
} // namespace chat
