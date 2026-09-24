#pragma once
#include <cstddef>
#include <cstdint>
// Deterministic attempt IDs only for native dispatcher tests.
inline void esp_fill_random(void* output, std::size_t size)
{
    static uint8_t generation = 0;
    auto* bytes = static_cast<uint8_t*>(output);
    ++generation;
    for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(generation + i);
}
