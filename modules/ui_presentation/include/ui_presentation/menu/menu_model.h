#pragma once

#include <cstddef>
#include <cstdint>

namespace ui
{
namespace menu
{

enum class MenuScreenId : uint8_t
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

struct MenuItem
{
    constexpr MenuItem() = default;
    constexpr MenuItem(MenuScreenId screen_id, const char* label, bool enabled)
        : screen_id(screen_id), label(label), enabled(enabled)
    {
    }

    MenuScreenId screen_id = MenuScreenId::Dashboard;
    const char* label = nullptr;
    bool enabled = false;
};

class MenuModel
{
  public:
    static constexpr std::size_t kMaxItems = 16;

    void clear();
    bool add(const MenuItem& item);
    std::size_t size() const;
    const MenuItem* items() const;

  private:
    MenuItem items_[kMaxItems]{};
    std::size_t size_ = 0;
};

} // namespace menu
} // namespace ui
