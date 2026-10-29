#pragma once
#include "chat/infra/reticulum/reticulum_wire.h"
#include <cstring>

namespace chat::lxmf::runtime
{
constexpr uint16_t kPropagationStampExpandRounds = 1000;
constexpr size_t kPropagationStampRoundBytes = 256;
constexpr size_t kPropagationStampWorkblockBytes = kPropagationStampExpandRounds * kPropagationStampRoundBytes;

inline bool expandPropagationStampRound(const uint8_t transient[32], uint16_t round, uint8_t output[kPropagationStampRoundBytes])
{
    if (!transient || !output || round >= kPropagationStampExpandRounds) return false;
    uint8_t material[35], salt[32];
    std::memcpy(material, transient, 32);
    size_t size = 32;
    if (round <= 0x7f) material[size++] = uint8_t(round);
    else if (round <= 0xff)
    {
        material[size++] = 0xcc;
        material[size++] = uint8_t(round);
    }
    else
    {
        material[size++] = 0xcd;
        material[size++] = uint8_t(round >> 8);
        material[size++] = uint8_t(round);
    }
    reticulum::fullHash(material, size, salt);
    return reticulum::hkdfSha256(transient, 32, salt, sizeof(salt), nullptr, 0, output, kPropagationStampRoundBytes);
}
inline bool propagationStampMeetsTarget(const uint8_t hash[32], uint8_t cost)
{
    if (!hash) return false;
    if (!cost) return true;
    // Official LXMF accepts hash <= 2**(256-cost), including equality.
    const size_t byte = (cost - 1U) / 8U;
    const uint8_t threshold = uint8_t(0x80U >> ((cost - 1U) % 8U));
    for (size_t i = 0; i < 32; ++i)
    {
        const uint8_t expected = i == byte ? threshold : 0;
        if (hash[i] < expected) return true;
        if (hash[i] > expected) return false;
    }
    return true;
}
} // namespace chat::lxmf::runtime
