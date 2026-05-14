#pragma once

#include <cstddef>
#include <cstdint>

namespace ui_lvgl_ux
{

enum class ScreenId : uint8_t
{
    Dashboard,
    Chat,
    Contacts,
    Map,
    Gps,
    Team,
    Tracker,
    Settings,
    WalkieTalkie,
    Sstv,
    Extensions,
};

struct ScreenDescriptor
{
    constexpr ScreenDescriptor() = default;

    constexpr ScreenDescriptor(ScreenId screen_id,
                               const char* screen_label,
                               bool screen_enabled)
        : id(screen_id),
          label(screen_label),
          enabled(screen_enabled)
    {
    }

    ScreenId id = ScreenId::Dashboard;
    const char* label = nullptr;
    bool enabled = false;
};

class ScreenRegistry
{
  public:
    static constexpr std::size_t kMaxScreens = 16;

    void clear();
    bool add(const ScreenDescriptor& descriptor);
    std::size_t size() const;
    const ScreenDescriptor* items() const;

  private:
    ScreenDescriptor items_[kMaxScreens]{};
    std::size_t size_ = 0;
};

} // namespace ui_lvgl_ux
