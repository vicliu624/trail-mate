#include "core/canvas.h"

#include <algorithm>

namespace trailmate::cardputer_zero::core
{

Canvas::Canvas(int width, int height)
    : width_(width), height_(height), pixels_(static_cast<std::size_t>(width * height), rgba(0, 0, 0, 255))
{
}

int Canvas::width() const noexcept
{
    return width_;
}

int Canvas::height() const noexcept
{
    return height_;
}

void Canvas::clear(Color color) noexcept
{
    std::fill(pixels_.begin(), pixels_.end(), color);
}

void Canvas::setPixel(int x, int y, Color color) noexcept
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
    {
        return;
    }

    pixels_[static_cast<std::size_t>(indexOf(x, y))] = color;
}

void Canvas::fillRect(int x, int y, int width, int height, Color color) noexcept
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    const int start_x = std::max(0, x);
    const int start_y = std::max(0, y);
    const int end_x = std::min(width_, x + width);
    const int end_y = std::min(height_, y + height);

    if (start_x >= end_x || start_y >= end_y)
    {
        return;
    }

    for (int py = start_y; py < end_y; ++py)
    {
        for (int px = start_x; px < end_x; ++px)
        {
            setPixel(px, py, color);
        }
    }
}

void Canvas::fillRoundedRect(int x, int y, int width, int height, int radius, Color color) noexcept
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    const int clamped_radius = std::clamp(radius, 0, std::min(width, height) / 2);
    if (clamped_radius == 0)
    {
        fillRect(x, y, width, height, color);
        return;
    }

    const int start_x = std::max(0, x);
    const int start_y = std::max(0, y);
    const int end_x = std::min(width_, x + width);
    const int end_y = std::min(height_, y + height);

    for (int py = start_y; py < end_y; ++py)
    {
        for (int px = start_x; px < end_x; ++px)
        {
            const int local_x = px - x;
            const int local_y = py - y;

            if ((local_x >= clamped_radius && local_x < width - clamped_radius) || (local_y >= clamped_radius && local_y < height - clamped_radius))
            {
                setPixel(px, py, color);
                continue;
            }

            const int center_x = (local_x < clamped_radius) ? (clamped_radius - 1) : (width - clamped_radius);
            const int center_y = (local_y < clamped_radius) ? (clamped_radius - 1) : (height - clamped_radius);
            const int dx = local_x - center_x;
            const int dy = local_y - center_y;

            if ((dx * dx) + (dy * dy) <= (clamped_radius * clamped_radius))
            {
                setPixel(px, py, color);
            }
        }
    }
}

void Canvas::strokeRect(int x, int y, int width, int height, Color color, int thickness) noexcept
{
    if (width <= 0 || height <= 0 || thickness <= 0)
    {
        return;
    }

    fillRect(x, y, width, thickness, color);
    fillRect(x, y + height - thickness, width, thickness, color);
    fillRect(x, y, thickness, height, color);
    fillRect(x + width - thickness, y, thickness, height, color);
}

void Canvas::blit(const Canvas& source, int x, int y) noexcept
{
    for (int py = 0; py < source.height(); ++py)
    {
        for (int px = 0; px < source.width(); ++px)
        {
            setPixel(x + px, y + py, source.pixel(px, py));
        }
    }
}

const Color& Canvas::pixel(int x, int y) const noexcept
{
    static const Color kFallback = rgba(0, 0, 0, 255);
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
    {
        return kFallback;
    }

    return pixels_[static_cast<std::size_t>(indexOf(x, y))];
}

const std::vector<Color>& Canvas::pixels() const noexcept
{
    return pixels_;
}

int Canvas::indexOf(int x, int y) const noexcept
{
    return (y * width_) + x;
}

} // namespace trailmate::cardputer_zero::core
