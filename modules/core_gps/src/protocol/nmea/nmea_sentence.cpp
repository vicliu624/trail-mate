#include "gps/protocol/nmea/nmea_sentence.h"

#include <cctype>
#include <cstring>

namespace gps::nmea
{
namespace
{
int hexValue(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return -1;
}
} // namespace

void NmeaSentenceAssembler::reset()
{
    len_ = 0;
    collecting_ = false;
    buffer_[0] = '\0';
}

bool NmeaSentenceAssembler::feed(uint8_t byte, NmeaSentence& out)
{
    const char c = static_cast<char>(byte);
    if (c == '$')
    {
        collecting_ = true;
        len_ = 0;
        buffer_[len_++] = c;
        return false;
    }

    if (!collecting_)
    {
        return false;
    }

    if (c == '\r')
    {
        return false;
    }

    if (c == '\n')
    {
        buffer_[len_] = '\0';
        out = {};
        std::memcpy(out.text, buffer_, len_ + 1U);
        out.len = len_;
        out.checksum_valid = verifyChecksum(out.text, &out.has_checksum);
        reset();
        return out.len > 0;
    }

    if (len_ + 1U >= kMaxNmeaSentenceLen)
    {
        reset();
        return false;
    }

    buffer_[len_++] = c;
    return false;
}

bool verifyChecksum(const char* sentence, bool* out_has_checksum)
{
    if (out_has_checksum)
    {
        *out_has_checksum = false;
    }
    if (!sentence || sentence[0] != '$')
    {
        return false;
    }

    const char* star = std::strchr(sentence, '*');
    if (!star)
    {
        return true;
    }
    if (!star[1] || !star[2])
    {
        return false;
    }
    const int hi = hexValue(star[1]);
    const int lo = hexValue(star[2]);
    if (hi < 0 || lo < 0)
    {
        return false;
    }
    if (out_has_checksum)
    {
        *out_has_checksum = true;
    }

    uint8_t checksum = 0;
    for (const char* cursor = sentence + 1; cursor < star; ++cursor)
    {
        checksum ^= static_cast<uint8_t>(*cursor);
    }
    return checksum == static_cast<uint8_t>((hi << 4) | lo);
}

} // namespace gps::nmea
