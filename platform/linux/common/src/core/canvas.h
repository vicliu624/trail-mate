#pragma once

#include <vector>

#include "core/color.h"

namespace trailmate::cardputer_zero::core
{

class Canvas
{
  public:
    Canvas(int width, int height);

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

    void clear(Color color) noexcept;
    void setPixel(int x, int y, Color color) noexcept;
    void fillRect(int x, int y, int width, int height, Color color) noexcept;
    void fillRoundedRect(int x, int y, int width, int height, int radius, Color color) noexcept;
    void strokeRect(int x, int y, int width, int height, Color color, int thickness = 1) noexcept;
    void blit(const Canvas& source, int x, int y) noexcept;

    [[nodiscard]] const Color& pixel(int x, int y) const noexcept;
    [[nodiscard]] const std::vector<Color>& pixels() const noexcept;

  private:
    [[nodiscard]] int indexOf(int x, int y) const noexcept;

    int width_{};
    int height_{};
    std::vector<Color> pixels_{};
};

} // namespace trailmate::cardputer_zero::core
