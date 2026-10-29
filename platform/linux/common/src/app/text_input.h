#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace trailmate::cardputer_zero::app
{
struct TextInputState
{
    uint64_t focus = 0;
    bool enabled = false, password = false, multiline = false;
    std::string surrounding;
    int32_t cursor = 0;
    int x = 0, y = 0, width = 1, height = 1;
};
struct TextEdit
{
    uint64_t focus = 0;
    std::string commit, preedit;
    int32_t preedit_begin = 0, preedit_end = 0;
    uint32_t before = 0, after = 0;
};
inline bool utf8Boundary(std::string_view text, size_t index)
{
    return index <= text.size() && (index == text.size() || (static_cast<uint8_t>(text[index]) & 0xc0) != 0x80);
}
inline bool validUtf8(std::string_view text)
{
    for (size_t i = 0; i < text.size();)
    {
        const auto first = static_cast<uint8_t>(text[i++]);
        if (first < 0x80)
        {
            if (!first) return false;
            continue;
        }
        unsigned count = first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2
                                                          : first >= 0xf0 && first <= 0xf4   ? 3
                                                                                             : 0;
        if (!count || i + count > text.size()) return false;
        uint32_t point = first & (0x7f >> (count + 1));
        for (unsigned j = 0; j < count; ++j)
        {
            const auto next = static_cast<uint8_t>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            point = (point << 6) | (next & 0x3f);
        }
        if ((count == 1 && point < 0x80) || (count == 2 && point < 0x800) || (count == 3 && point < 0x10000) ||
            point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return false;
    }
    return true;
}
inline size_t utf8ByteAt(std::string_view text, size_t characters)
{
    size_t offset = 0;
    while (offset < text.size() && characters--)
    {
        ++offset;
        while (!utf8Boundary(text, offset)) ++offset;
    }
    return offset;
}
inline size_t utf8Length(std::string_view text)
{
    size_t count = 0;
    for (size_t i = 0; i < text.size(); ++i)
        if (utf8Boundary(text, i)) ++count;
    return count;
}
} // namespace trailmate::cardputer_zero::app
