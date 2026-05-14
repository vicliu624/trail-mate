#pragma once

#include "ui_lvgl_ux_packs/ux/screen_registry.h"

#include <cstddef>

namespace ui_lvgl_ux
{

struct UxMenuItem
{
    constexpr UxMenuItem() = default;
    constexpr UxMenuItem(ScreenId screen_id, const char* label, bool enabled)
        : screen_id(screen_id), label(label), enabled(enabled)
    {
    }

    ScreenId screen_id = ScreenId::Dashboard;
    const char* label = nullptr;
    bool enabled = false;
};

class UxMenuModel
{
  public:
    static constexpr std::size_t kMaxItems = 16;

    void clear();
    bool add(const UxMenuItem& item);
    std::size_t size() const;
    const UxMenuItem* items() const;

  private:
    UxMenuItem items_[kMaxItems]{};
    std::size_t size_ = 0;
};

} // namespace ui_lvgl_ux
