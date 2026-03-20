#include "ui/mono_128x64/text_renderer.h"

#include "ui/mono_128x64/runtime.h"

#include <algorithm>
#include <cstring>

namespace ui::mono_128x64
{
namespace
{

constexpr uint32_t kReplacementCodepoint = 0xFFFD;

struct GlyphView
{
    constexpr GlyphView(const uint8_t* bitmap_in = nullptr,
                        uint8_t width_in = 0,
                        uint8_t height_in = 0,
                        int8_t x_offset_in = 0,
                        int8_t y_offset_in = 0,
                        uint8_t advance_in = 0)
        : bitmap(bitmap_in),
          width(width_in),
          height(height_in),
          x_offset(x_offset_in),
          y_offset(y_offset_in),
          advance(advance_in)
    {
    }

    const uint8_t* bitmap;
    uint8_t width;
    uint8_t height;
    int8_t x_offset;
    int8_t y_offset;
    uint8_t advance;
};

size_t utf8SequenceLength(unsigned char lead)
{
    if ((lead & 0x80U) == 0)
    {
        return 1;
    }
    if ((lead & 0xE0U) == 0xC0U)
    {
        return 2;
    }
    if ((lead & 0xF0U) == 0xE0U)
    {
        return 3;
    }
    if ((lead & 0xF8U) == 0xF0U)
    {
        return 4;
    }
    return 1;
}

bool isContinuation(unsigned char ch)
{
    return (ch & 0xC0U) == 0x80U;
}

uint32_t decodeUtf8(const char* text, size_t len, size_t& consumed)
{
    consumed = 0;
    if (!text || len == 0)
    {
        return 0;
    }

    const unsigned char lead = static_cast<unsigned char>(text[0]);
    if ((lead & 0x80U) == 0)
    {
        consumed = 1;
        return lead;
    }

    const size_t seq_len = utf8SequenceLength(lead);
    if (seq_len == 1 || seq_len > len)
    {
        consumed = 1;
        return kReplacementCodepoint;
    }

    uint32_t codepoint = 0;
    if (seq_len == 2)
    {
        if (!isContinuation(static_cast<unsigned char>(text[1])))
        {
            consumed = 1;
            return kReplacementCodepoint;
        }
        codepoint = static_cast<uint32_t>(lead & 0x1FU) << 6;
        codepoint |= static_cast<uint32_t>(text[1] & 0x3FU);
    }
    else if (seq_len == 3)
    {
        if (!isContinuation(static_cast<unsigned char>(text[1])) || !isContinuation(static_cast<unsigned char>(text[2])))
        {
            consumed = 1;
            return kReplacementCodepoint;
        }
        codepoint = static_cast<uint32_t>(lead & 0x0FU) << 12;
        codepoint |= static_cast<uint32_t>(text[1] & 0x3FU) << 6;
        codepoint |= static_cast<uint32_t>(text[2] & 0x3FU);
    }
    else
    {
        if (!isContinuation(static_cast<unsigned char>(text[1])) ||
            !isContinuation(static_cast<unsigned char>(text[2])) ||
            !isContinuation(static_cast<unsigned char>(text[3])))
        {
            consumed = 1;
            return kReplacementCodepoint;
        }
        codepoint = static_cast<uint32_t>(lead & 0x07U) << 18;
        codepoint |= static_cast<uint32_t>(text[1] & 0x3FU) << 12;
        codepoint |= static_cast<uint32_t>(text[2] & 0x3FU) << 6;
        codepoint |= static_cast<uint32_t>(text[3] & 0x3FU);
    }

    consumed = seq_len;
    return codepoint;
}

uint16_t lookupGlyphIndex(const MonoFont& font, uint32_t codepoint)
{
    if (!font.bitmap || font.glyph_count == 0)
    {
        return font.glyph_count;
    }

    const bool is_compact16 = (font.codepoints16 != nullptr) && (font.glyphs == nullptr);
    if (is_compact16)
    {
        if (!font.codepoints16 || codepoint > 0xFFFFU)
        {
            return font.fallback_glyph_index < font.glyph_count ? font.fallback_glyph_index : font.glyph_count;
        }
        const uint16_t wanted = static_cast<uint16_t>(codepoint);
        const uint16_t* begin = font.codepoints16;
        const uint16_t* end = font.codepoints16 + font.glyph_count;
        const uint16_t* it = std::lower_bound(begin, end, wanted);
        if (it != end && *it == wanted)
        {
            return static_cast<uint16_t>(it - begin);
        }
    }
    else
    {
        if (!font.glyphs || !font.codepoints32)
        {
            return font.glyph_count;
        }
        const uint32_t* begin = font.codepoints32;
        const uint32_t* end = font.codepoints32 + font.glyph_count;
        const uint32_t* it = std::lower_bound(begin, end, codepoint);
        if (it != end && *it == codepoint)
        {
            return static_cast<uint16_t>(it - begin);
        }
    }

    return font.fallback_glyph_index < font.glyph_count ? font.fallback_glyph_index : font.glyph_count;
}

GlyphView resolveGlyph(const MonoFont& font, uint16_t glyph_index)
{
    if (!font.bitmap || glyph_index >= font.glyph_count)
    {
        return {};
    }

    const bool is_compact16 = (font.codepoints16 != nullptr) && (font.glyphs == nullptr);
    if (is_compact16)
    {
        const uint32_t offset = static_cast<uint32_t>(glyph_index) * static_cast<uint32_t>(font.glyph_height);
        const uint8_t advance = font.advances ? font.advances[glyph_index] : font.fixed_advance;
        return GlyphView{
            font.bitmap + offset,
            font.glyph_width,
            font.glyph_height,
            0,
            0,
            advance,
        };
    }

    if (!font.glyphs)
    {
        return {};
    }

    const MonoGlyph& glyph = font.glyphs[glyph_index];
    return GlyphView{
        font.bitmap + glyph.bitmap_offset,
        glyph.width,
        glyph.height,
        glyph.x_offset,
        glyph.y_offset,
        glyph.advance,
    };
}

void drawGlyphBitmap(MonoDisplay& display, const MonoFont& font, int x, int y, const GlyphView& glyph, bool inverse)
{
    if (!glyph.bitmap)
    {
        return;
    }

    if (inverse)
    {
        display.fillRect(x, y, glyph.advance, font.line_height, true);
    }

    for (int row = 0; row < glyph.height; ++row)
    {
        const uint8_t bits = glyph.bitmap[row];
        for (int col = 0; col < glyph.width; ++col)
        {
            const bool on = ((bits >> (7 - col)) & 0x01U) != 0;
            if (!on)
            {
                continue;
            }
            display.drawPixel(x + glyph.x_offset + col, y + glyph.y_offset + row, !inverse);
        }
    }
}

} // namespace

const MonoFont& builtin_ui_font()
{
    static const uint8_t kBitmap[] = {
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x40,
        0x40,
        0x40,
        0x40,
        0x00,
        0x40,
        0x00,
        0x00,
        0xA0,
        0xA0,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0xA0,
        0xE0,
        0xA0,
        0xA0,
        0xE0,
        0xA0,
        0x00,
        0x00,
        0x40,
        0xE0,
        0xC0,
        0x60,
        0x60,
        0xC0,
        0x40,
        0x00,
        0x00,
        0xA0,
        0x20,
        0x40,
        0x80,
        0xA0,
        0x00,
        0x00,
        0x40,
        0xA0,
        0x40,
        0xA0,
        0xA0,
        0x60,
        0x00,
        0x00,
        0x40,
        0x40,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
    };
    static const MonoGlyph kGlyphs[] = {
        {0, 8, 8, 0, -1, 4},
        {8, 8, 8, 0, -1, 4},
        {16, 8, 8, 0, -1, 4},
        {24, 8, 8, 0, -1, 4},
        {32, 8, 8, 0, -1, 4},
        {40, 8, 8, 0, -1, 4},
        {48, 8, 8, 0, -1, 4},
        {56, 8, 8, 0, -1, 4},
    };
    static const uint32_t kCodepoints[] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27};
    static const MonoFont kFont = MonoFont::makeLegacy(
        kBitmap,
        kGlyphs,
        kCodepoints,
        static_cast<uint16_t>(sizeof(kCodepoints) / sizeof(kCodepoints[0])),
        8,
        7,
        4,
        0);
    return kFont;
}

TextRenderer::TextRenderer(const MonoFont& font)
    : font_(font)
{
}

int TextRenderer::lineHeight() const
{
    return font_.line_height;
}

int TextRenderer::measureTextWidth(const char* utf8) const
{
    if (!utf8)
    {
        return 0;
    }

    int width = 0;
    const size_t len = std::strlen(utf8);
    for (size_t i = 0; i < len;)
    {
        size_t consumed = 0;
        const uint32_t cp = decodeUtf8(utf8 + i, len - i, consumed);
        const uint16_t glyph_index = lookupGlyphIndex(font_, cp);
        const GlyphView glyph = resolveGlyph(font_, glyph_index);
        width += glyph.bitmap ? glyph.advance : font_.default_advance;
        i += consumed ? consumed : 1;
    }
    return width;
}

size_t TextRenderer::clipTextToWidth(const char* utf8, int max_width) const
{
    if (!utf8 || max_width <= 0)
    {
        return 0;
    }

    int width = 0;
    size_t bytes = 0;
    const size_t len = std::strlen(utf8);
    for (size_t i = 0; i < len;)
    {
        size_t consumed = 0;
        const uint32_t cp = decodeUtf8(utf8 + i, len - i, consumed);
        const uint16_t glyph_index = lookupGlyphIndex(font_, cp);
        const GlyphView glyph = resolveGlyph(font_, glyph_index);
        const int advance = glyph.bitmap ? glyph.advance : font_.default_advance;
        if (width + advance > max_width)
        {
            break;
        }
        width += advance;
        bytes += consumed ? consumed : 1;
        i += consumed ? consumed : 1;
    }
    return bytes;
}

int TextRenderer::ellipsisWidth() const
{
    return measureTextWidth("...");
}

void TextRenderer::drawText(MonoDisplay& display, int x, int y, const char* utf8, bool inverse) const
{
    if (!utf8)
    {
        return;
    }

    const size_t len = std::strlen(utf8);
    int cursor_x = x;
    for (size_t i = 0; i < len;)
    {
        size_t consumed = 0;
        const uint32_t cp = decodeUtf8(utf8 + i, len - i, consumed);
        const uint16_t glyph_index = lookupGlyphIndex(font_, cp);
        const GlyphView glyph = resolveGlyph(font_, glyph_index);
        if (glyph.bitmap)
        {
            drawGlyphBitmap(display, font_, cursor_x, y, glyph, inverse);
            cursor_x += glyph.advance;
        }
        else
        {
            cursor_x += font_.default_advance;
        }
        i += consumed ? consumed : 1;
    }
}

} // namespace ui::mono_128x64
