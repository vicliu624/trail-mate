#pragma once

#include <cstdint>

namespace trailmate::cardputer_zero::core
{

struct Color
{
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};
};

constexpr bool operator==(const Color& lhs, const Color& rhs) noexcept
{
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

constexpr bool operator!=(const Color& lhs, const Color& rhs) noexcept
{
    return !(lhs == rhs);
}

constexpr Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) noexcept
{
    return Color{r, g, b, a};
}

constexpr std::uint32_t pack_key(Color color) noexcept
{
    return (static_cast<std::uint32_t>(color.r) << 24U) | (static_cast<std::uint32_t>(color.g) << 16U) | (static_cast<std::uint32_t>(color.b) << 8U) | static_cast<std::uint32_t>(color.a);
}

} // namespace trailmate::cardputer_zero::core
