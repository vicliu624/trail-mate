#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace platform::desktop
{
class DirtyRegions
{
  public:
    struct Area
    {
        int x1, y1, x2, y2;
    };
    bool pending() const { return full_ || count_ != 0; }
    bool full() const { return full_; }
    std::size_t size() const { return count_; }
    const Area& operator[](std::size_t index) const { return areas_[index]; }
    void invalidate()
    {
        full_ = true;
        count_ = 0;
    }
    void clear()
    {
        full_ = false;
        count_ = 0;
    }
    void add(Area area, int width, int height)
    {
        if (full_ || width <= 0 || height <= 0) return;
        area.x1 = std::max(0, area.x1);
        area.y1 = std::max(0, area.y1);
        area.x2 = std::min(width - 1, area.x2);
        area.y2 = std::min(height - 1, area.y2);
        if (area.x1 > area.x2 || area.y1 > area.y2) return;
        for (std::size_t i = 0; i < count_;)
        {
            const auto& other = areas_[i];
            if (area.x1 <= other.x2 + 1 && other.x1 <= area.x2 + 1 &&
                area.y1 <= other.y2 + 1 && other.y1 <= area.y2 + 1)
            {
                area = {std::min(area.x1, other.x1), std::min(area.y1, other.y1),
                        std::max(area.x2, other.x2), std::max(area.y2, other.y2)};
                areas_[i] = areas_[--count_];
                i = 0; // The expanded area may now intersect an earlier region.
            }
            else ++i;
        }
        if (count_ == areas_.size()) invalidate();
        else areas_[count_++] = area;
    }

  private:
    std::array<Area, 32> areas_{};
    std::size_t count_ = 0;
    bool full_ = true;
};
} // namespace platform::desktop
