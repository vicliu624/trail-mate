#pragma once

#include "geocaching/domain/record.h"
#include <limits>

namespace geocaching::protocol
{

// Allocation-free cursor for the deterministic MessagePack subset. A failed
// operation poisons the cursor: callers cannot accidentally accept its suffix.
class CmpReader
{
  public:
    explicit CmpReader(ByteView bytes)
        : bytes_(bytes), valid_(bytes.data != nullptr || bytes.size == 0) {}

    bool good() const { return valid_; }
    // Only use a position after checking the preceding decode succeeded.
    std::size_t position() const { return offset_; }
    bool finished() const { return valid_ && offset_ == bytes_.size; }

    bool nil()
    {
        std::uint8_t tag = 0;
        return take(tag) && (tag == 0xc0 || fail());
    }

    bool unsignedInteger(std::uint64_t& out)
    {
        std::uint8_t tag = 0;
        if (!take(tag)) return false;
        if (tag <= 0x7f)
        {
            out = tag;
            return true;
        }
        if (tag < 0xcc || tag > 0xcf) return fail();
        const unsigned width = 1U << (tag - 0xcc);
        std::uint64_t value = 0;
        if (!number(width, value)) return false;
        const std::uint64_t minimum = width == 1 ? 128ULL : (1ULL << ((width / 2) * 8));
        if (value < minimum) return fail();
        out = value;
        return true;
    }

    bool signedInteger(std::int64_t& out)
    {
        if (!valid_ || offset_ == bytes_.size) return fail();
        const auto tag = bytes_.data[offset_];
        if (tag <= 0x7f || (tag >= 0xcc && tag <= 0xcf))
        {
            std::uint64_t value = 0;
            if (!unsignedInteger(value) || value > static_cast<std::uint64_t>(INT64_MAX)) return fail();
            out = static_cast<std::int64_t>(value);
            return true;
        }
        ++offset_;
        if (tag >= 0xe0)
        {
            out = static_cast<int>(tag) - 256;
            return true;
        }
        if (tag < 0xd0 || tag > 0xd3) return fail();
        const unsigned width = 1U << (tag - 0xd0);
        std::uint64_t raw = 0;
        if (!number(width, raw)) return false;
        const unsigned bits = width * 8;
        const std::uint64_t mask = width == 8 ? UINT64_MAX : (1ULL << bits) - 1;
        if ((raw & (1ULL << (bits - 1))) == 0) return fail();
        const auto value = -1 - static_cast<std::int64_t>((~raw) & mask);
        const std::int64_t maximum = width == 1 ? -33 : -(1LL << ((width / 2) * 8 - 1)) - 1;
        if (value > maximum) return fail();
        out = value;
        return true;
    }

    bool array(std::size_t& count, std::size_t limit)
    {
        std::uint8_t tag = 0;
        if (!take(tag)) return false;
        std::uint64_t n = 0;
        if (tag >= 0x90 && tag <= 0x9f) n = tag - 0x90;
        else if (tag == 0xdc || tag == 0xdd)
        {
            if (!number(tag == 0xdc ? 2 : 4, n)) return false;
            if (n < (tag == 0xdc ? 16ULL : 65536ULL)) return fail();
        }
        else return fail();
        if (n > limit) return fail();
        count = static_cast<std::size_t>(n);
        return true;
    }

    bool binary(ByteView& out, std::size_t limit)
    {
        std::uint8_t tag = 0;
        if (!take(tag) || tag < 0xc4 || tag > 0xc6) return fail();
        const unsigned width = 1U << (tag - 0xc4);
        std::uint64_t n = 0;
        if (!number(width, n)) return false;
        if (width > 1 && n < (1ULL << ((width / 2) * 8))) return fail();
        return slice(n, limit, out);
    }

    // Encoding and bounds only; record-level Unicode policy is checked by the
    // record decoder before exposing a successfully parsed RecordView.
    bool text(std::string_view& out, std::size_t limit)
    {
        std::uint8_t tag = 0;
        if (!take(tag)) return false;
        std::uint64_t n = 0;
        if (tag >= 0xa0 && tag <= 0xbf) n = tag - 0xa0;
        else if (tag >= 0xd9 && tag <= 0xdb)
        {
            const unsigned width = 1U << (tag - 0xd9);
            if (!number(width, n)) return false;
            const std::uint64_t minimum = width == 1 ? 32ULL : (1ULL << ((width / 2) * 8));
            if (n < minimum) return fail();
        }
        else return fail();
        ByteView bytes;
        if (!slice(n, limit, bytes)) return false;
        out = std::string_view(reinterpret_cast<const char*>(bytes.data), bytes.size);
        return true;
    }

  private:
    bool fail()
    {
        valid_ = false;
        return false;
    }
    bool take(std::uint8_t& out)
    {
        if (!valid_ || offset_ == bytes_.size) return fail();
        out = bytes_.data[offset_++];
        return true;
    }
    bool number(unsigned width, std::uint64_t& out)
    {
        out = 0;
        for (unsigned i = 0; i < width; ++i)
        {
            std::uint8_t value = 0;
            if (!take(value)) return false;
            out = (out << 8) | value;
        }
        return true;
    }
    bool slice(std::uint64_t n, std::size_t limit, ByteView& out)
    {
        if (!valid_ || n > limit || n > bytes_.size - offset_) return fail();
        out = {bytes_.data + offset_, static_cast<std::size_t>(n)};
        offset_ += out.size;
        return true;
    }
    ByteView bytes_;
    std::size_t offset_ = 0;
    bool valid_ = false;
};

} // namespace geocaching::protocol
