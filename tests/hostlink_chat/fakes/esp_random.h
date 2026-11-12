#pragma once
#include <cstddef>
#include <cstdint>
inline void esp_fill_random(void* output, size_t size)
{
    static uint8_t sequence = 10;
    auto* bytes = static_cast<uint8_t*>(output);
    for (size_t i = 0; i < size; ++i) bytes[i] = ++sequence;
}
