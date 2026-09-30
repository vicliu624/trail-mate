#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chat::reticulum
{
// Automatic public discovery must not dial a peer's private LAN address.
// Explicit interfaces continue to accept local endpoints independently.
inline bool publicGatewayHost(const char* host, size_t size)
{
    if (!host || !size || size > 63) return false;
    bool numeric = true, dot = false;
    bool label_start = true;
    char previous = 0;
    for (size_t i = 0; i < size; ++i)
    {
        const char c = host[i];
        const bool digit = c >= '0' && c <= '9';
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (c == '.')
        {
            if (label_start || previous == '-') return false;
            dot = label_start = true;
        }
        else
        {
            if (!digit && !letter && (c != '-' || label_start)) return false;
            label_start = false;
            numeric &= digit;
        }
        previous = c;
    }
    if (label_start || previous == '-') return false;
    if (!numeric)
    {
        if (!dot) return false;
        constexpr const char* suffixes[] = {".local", ".localhost", ".home.arpa"};
        for (const char* suffix : suffixes)
        {
            const size_t suffix_size = std::strlen(suffix);
            if (size >= suffix_size)
            {
                bool equal = true;
                for (size_t i = 0; i < suffix_size; ++i)
                {
                    char c = host[size - suffix_size + i];
                    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                    equal &= c == suffix[i];
                }
                if (equal) return false;
            }
        }
        return true;
    }
    uint8_t octets[4]{};
    size_t position = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        unsigned value = 0, digits = 0;
        while (position < size && host[position] != '.')
        {
            value = value * 10 + unsigned(host[position++] - '0');
            if (++digits > 3 || value > 255) return false;
        }
        if (!digits) return false;
        octets[i] = static_cast<uint8_t>(value);
        if (i < 3 && (position == size || host[position++] != '.')) return false;
    }
    if (position != size) return false;
    const auto a = octets[0], b = octets[1], c = octets[2];
    return a != 0 && a != 10 && a != 127 && a < 224 &&
           !(a == 100 && b >= 64 && b <= 127) &&
           !(a == 169 && b == 254) && !(a == 172 && b >= 16 && b <= 31) &&
           !(a == 192 && b == 168) && !(a == 192 && b == 0 && (c == 0 || c == 2)) &&
           !(a == 198 && (b == 18 || b == 19)) && !(a == 198 && b == 51 && c == 100) &&
           !(a == 203 && b == 0 && c == 113);
}
} // namespace chat::reticulum
