#pragma once
#include "geocaching/domain/record.h"
#include <cstring>

namespace geocaching::protocol
{
class CmpWriter
{
  public:
    CmpWriter(std::uint8_t* data, std::size_t capacity)
        : data_(data), capacity_(capacity), good_(data != nullptr) {}
    bool good() const { return good_; }
    std::size_t size() const { return good_ ? size_ : 0; }
    bool nil() { return byte(0xc0); }
    bool text(std::string_view value)
    {
        if ((!value.data() && value.size()) || value.size() > UINT32_MAX) return fail();
        if (value.size() < 32)
        {
            if (!byte(static_cast<uint8_t>(0xa0 + value.size()))) return false;
        }
        else
        {
            const unsigned width = value.size() <= UINT8_MAX ? 1 : value.size() <= UINT16_MAX ? 2
                                                                                              : 4;
            if (!byte(width == 1 ? 0xd9 : width == 2 ? 0xda
                                                     : 0xdb) ||
                !number(value.size(), width)) return false;
        }
        if (!good_ || value.size() > capacity_ - size_) return fail();
        if (value.size()) std::memcpy(data_ + size_, value.data(), value.size());
        size_ += value.size();
        return true;
    }
    bool unsignedInteger(std::uint64_t value)
    {
        if (value < 128) return byte(static_cast<std::uint8_t>(value));
        const unsigned width = value <= UINT8_MAX ? 1 : value <= UINT16_MAX ? 2
                                                    : value <= UINT32_MAX   ? 4
                                                                            : 8;
        return byte(width == 1 ? 0xcc : width == 2 ? 0xcd
                                    : width == 4   ? 0xce
                                                   : 0xcf) &&
               number(value, width);
    }
    bool signedInteger(std::int64_t value)
    {
        if (value >= 0) return unsignedInteger(static_cast<std::uint64_t>(value));
        if (value >= -32) return byte(static_cast<std::uint8_t>(value));
        const unsigned width = value >= INT8_MIN ? 1 : value >= INT16_MIN ? 2
                                                   : value >= INT32_MIN   ? 4
                                                                          : 8;
        return byte(width == 1 ? 0xd0 : width == 2 ? 0xd1
                                    : width == 4   ? 0xd2
                                                   : 0xd3) &&
               number(static_cast<std::uint64_t>(value), width);
    }
    bool array(std::uint32_t count)
    {
        if (count < 16) return byte(static_cast<std::uint8_t>(0x90 + count));
        return byte(count <= UINT16_MAX ? 0xdc : 0xdd) && number(count, count <= UINT16_MAX ? 2 : 4);
    }
    // Emit only the canonical length prefix for a borrowed binary span.
    bool binaryHeader(std::size_t length)
    {
        if (length > UINT32_MAX) return fail();
        const unsigned width = length <= UINT8_MAX ? 1 : length <= UINT16_MAX ? 2
                                                                              : 4;
        return byte(width == 1 ? 0xc4 : width == 2 ? 0xc5
                                                   : 0xc6) &&
               number(length, width);
    }
    bool binary(ByteView value)
    {
        if ((!value.data && value.size) || !binaryHeader(value.size)) return fail();
        if (!good_ || value.size > capacity_ - size_) return fail();
        if (value.size) std::memcpy(data_ + size_, value.data, value.size);
        size_ += value.size;
        return true;
    }

  private:
    bool fail()
    {
        good_ = false;
        return false;
    }
    bool byte(std::uint8_t value)
    {
        if (!good_ || size_ == capacity_) return fail();
        data_[size_++] = value;
        return true;
    }
    bool number(std::uint64_t value, unsigned width)
    {
        for (unsigned i = width; i > 0; --i)
            if (!byte(static_cast<std::uint8_t>(value >> ((i - 1) * 8)))) return false;
        return true;
    }
    std::uint8_t* data_;
    std::size_t capacity_;
    std::size_t size_ = 0;
    bool good_;
};

inline bool encodeGetRequest(const RequestId& request, const GeocacheId& id,
                             const RevisionHash* wanted, const RevisionHash* known,
                             std::uint16_t budget, std::uint8_t* output,
                             std::size_t capacity, std::size_t& written)
{
    written = 0;
    if (budget < 512 || budget > kMaxApplicationBytes || (wanted && known)) return false;
    CmpWriter writer(output, capacity);
    if (!writer.array(6) || !writer.unsignedInteger(1) || !writer.unsignedInteger(0) ||
        !writer.unsignedInteger(3) || !writer.binary({request.bytes.data(), request.bytes.size()}) ||
        !writer.unsignedInteger(budget) || !writer.array(3) || !writer.binary({id.bytes.data(), id.bytes.size()}) ||
        !(wanted ? writer.binary({wanted->bytes.data(), wanted->bytes.size()}) : writer.nil()) ||
        !(known ? writer.binary({known->bytes.data(), known->bytes.size()}) : writer.nil())) return false;
    written = writer.size();
    return writer.good();
}
} // namespace geocaching::protocol
