#pragma once

#include <string_view>

#include "core/canvas.h"

namespace trailmate::cardputer_zero::core::bitmap_font {

inline constexpr int kGlyphWidth = 5;
inline constexpr int kGlyphHeight = 7;
inline constexpr int kGlyphSpacing = 1;

[[nodiscard]] int measureText(std::string_view text, int scale = 1) noexcept;
[[nodiscard]] int lineHeight(int scale = 1) noexcept;
void drawText(Canvas& canvas, std::string_view text, int x, int y, Color color, int scale = 1) noexcept;

} // namespace trailmate::cardputer_zero::core::bitmap_font
