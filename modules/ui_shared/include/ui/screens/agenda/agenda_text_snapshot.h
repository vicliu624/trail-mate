#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ui::agenda::page
{
// Transient text only, never a disk format. LVGL limits Unicode characters,
// while UTF-8 needs up to four bytes per character. Unicode scalars fit in 21
// bits, giving a fixed worst-case bound without lowering the editor limit.
template <std::size_t Capacity>
class TextSnapshot
{
    static_assert(Capacity > 0 && Capacity <= 255);

  public:
    bool assign(const char* text)
    {
        if (!text) return false;
        auto* cursor = reinterpret_cast<const unsigned char*>(text);
        std::size_t count = 0;
        uint32_t scalar;
        // Validate before writing: rejected input leaves the snapshot intact.
        while (*cursor)
        {
            if (count == Capacity || !next(cursor, scalar)) return false;
            ++count;
        }
        bits_.fill(0);
        count_ = static_cast<uint8_t>(count);
        cursor = reinterpret_cast<const unsigned char*>(text);
        for (std::size_t index = 0; index < count; ++index)
        {
            next(cursor, scalar);
            for (unsigned bit = 0; bit < 21; ++bit)
            {
                const auto offset = index * 21 + bit;
                if (scalar & (uint32_t{1} << bit)) bits_[offset / 8] |= uint8_t{1} << (offset % 8);
            }
        }
        return true;
    }

    template <typename Consumer>
    bool forEachUtf8(Consumer consume) const
    {
        for (std::size_t index = 0; index < count_; ++index)
        {
            uint32_t scalar = 0;
            for (unsigned bit = 0; bit < 21; ++bit)
            {
                const auto offset = index * 21 + bit;
                if (bits_[offset / 8] & (uint8_t{1} << (offset % 8))) scalar |= uint32_t{1} << bit;
            }
            char text[5]{}; // One scalar, not a second full decoded draft.
            if (scalar < 0x80) text[0] = static_cast<char>(scalar);
            else if (scalar < 0x800)
            {
                text[0] = static_cast<char>(0xC0 | (scalar >> 6));
                text[1] = static_cast<char>(0x80 | (scalar & 0x3F));
            }
            else if (scalar < 0x10000)
            {
                text[0] = static_cast<char>(0xE0 | (scalar >> 12));
                text[1] = static_cast<char>(0x80 | ((scalar >> 6) & 0x3F));
                text[2] = static_cast<char>(0x80 | (scalar & 0x3F));
            }
            else
            {
                text[0] = static_cast<char>(0xF0 | (scalar >> 18));
                text[1] = static_cast<char>(0x80 | ((scalar >> 12) & 0x3F));
                text[2] = static_cast<char>(0x80 | ((scalar >> 6) & 0x3F));
                text[3] = static_cast<char>(0x80 | (scalar & 0x3F));
            }
            if (!consume(text)) return false;
        }
        return true;
    }

  private:
    static bool next(const unsigned char*& cursor, uint32_t& scalar)
    {
        const unsigned char first = *cursor++;
        if (first < 0x80)
        {
            scalar = first;
            return true;
        }
        unsigned remaining;
        uint32_t minimum;
        if (first >= 0xC2 && first <= 0xDF)
        {
            scalar = first & 0x1F;
            remaining = 1;
            minimum = 0x80;
        }
        else if (first >= 0xE0 && first <= 0xEF)
        {
            scalar = first & 0x0F;
            remaining = 2;
            minimum = 0x800;
        }
        else if (first >= 0xF0 && first <= 0xF4)
        {
            scalar = first & 7;
            remaining = 3;
            minimum = 0x10000;
        }
        else return false;
        while (remaining--)
        {
            if ((*cursor & 0xC0) != 0x80) return false;
            scalar = (scalar << 6) | (*cursor++ & 0x3F);
        }
        return scalar >= minimum && scalar <= 0x10FFFF && !(scalar >= 0xD800 && scalar <= 0xDFFF);
    }

    std::array<uint8_t, (Capacity * 21 + 7) / 8> bits_{};
    uint8_t count_ = 0;
};
static_assert(sizeof(TextSnapshot<39>) == 104 && sizeof(TextSnapshot<79>) == 209);
} // namespace ui::agenda::page
