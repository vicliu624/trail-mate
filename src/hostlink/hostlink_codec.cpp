#include "hostlink_codec.h"

#include <algorithm>
#include <cstring>

namespace hostlink
{

uint16_t crc16_ccitt(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 0x8000)
            {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

bool encode_frame(uint8_t type, uint16_t seq, const uint8_t* payload, size_t len,
                  std::vector<uint8_t>& out)
{
    if (len > kMaxFrameLen)
    {
        return false;
    }
    out.clear();
    out.reserve(kHeaderSize + len + kCrcSize);

    out.push_back(kMagic0);
    out.push_back(kMagic1);
    out.push_back(kProtocolVersion);
    out.push_back(type);
    out.push_back(static_cast<uint8_t>(seq & 0xFF));
    out.push_back(static_cast<uint8_t>((seq >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(len & 0xFF));
    out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));

    if (payload && len)
    {
        out.insert(out.end(), payload, payload + len);
    }

    uint16_t crc = crc16_ccitt(out.data(), out.size());
    out.push_back(static_cast<uint8_t>(crc & 0xFF));
    out.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return true;
}

Decoder::Decoder(size_t max_len) : max_len_(max_len)
{
    buffer_.reserve(kMaxFrameLen + kHeaderSize + kCrcSize);
}

void Decoder::reset()
{
    buffer_.clear();
}

void Decoder::push(const uint8_t* data, size_t len)
{
    if (!data || len == 0)
    {
        return;
    }
    buffer_.insert(buffer_.end(), data, data + len);
    if (buffer_.size() > (max_len_ + kHeaderSize + kCrcSize) * 4)
    {
        buffer_.erase(buffer_.begin(), buffer_.end() - (max_len_ + kHeaderSize + kCrcSize));
    }
}

bool Decoder::next(Frame& out)
{
    size_t idx = 0;
    while (buffer_.size() - idx >= kHeaderSize)
    {
        if (buffer_[idx] != kMagic0 || buffer_[idx + 1] != kMagic1)
        {
            ++idx;
            continue;
        }
        if (buffer_.size() - idx < kHeaderSize)
        {
            break;
        }

        uint8_t ver = buffer_[idx + 2];
        uint16_t len = static_cast<uint16_t>(buffer_[idx + 6]) |
                       (static_cast<uint16_t>(buffer_[idx + 7]) << 8);
        if (ver != kProtocolVersion || len > max_len_)
        {
            ++idx;
            continue;
        }

        size_t total = kHeaderSize + len + kCrcSize;
        if (buffer_.size() - idx < total)
        {
            break;
        }

        uint16_t expected_crc = crc16_ccitt(buffer_.data() + idx, kHeaderSize + len);
        uint16_t got_crc = static_cast<uint16_t>(buffer_[idx + kHeaderSize + len]) |
                           (static_cast<uint16_t>(buffer_[idx + kHeaderSize + len + 1]) << 8);
        if (expected_crc != got_crc)
        {
            ++idx;
            continue;
        }

        out.type = buffer_[idx + 3];
        out.seq = static_cast<uint16_t>(buffer_[idx + 4]) |
                  (static_cast<uint16_t>(buffer_[idx + 5]) << 8);
        out.payload.assign(buffer_.begin() + idx + kHeaderSize,
                           buffer_.begin() + idx + kHeaderSize + len);

        buffer_.erase(buffer_.begin(), buffer_.begin() + idx + total);
        return true;
    }

    if (idx > 0)
    {
        buffer_.erase(buffer_.begin(), buffer_.begin() + idx);
    }
    return false;
}

} // namespace hostlink
