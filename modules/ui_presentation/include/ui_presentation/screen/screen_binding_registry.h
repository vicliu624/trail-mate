#pragma once

#include "ui_presentation/menu/menu_model.h"

#include <cstddef>

namespace ui
{
namespace screen
{

struct ScreenBinding
{
    constexpr ScreenBinding() = default;
    constexpr ScreenBinding(ui::menu::MenuScreenId screen_id,
                            const char* binding_id,
                            bool available)
        : screen_id(screen_id), binding_id(binding_id), available(available)
    {
    }

    ui::menu::MenuScreenId screen_id = ui::menu::MenuScreenId::Dashboard;
    const char* binding_id = nullptr;
    bool available = false;
};

class ScreenBindingRegistry
{
  public:
    static constexpr std::size_t kMaxBindings = 16;

    void clear();
    bool add(const ScreenBinding& binding);
    const ScreenBinding* find(ui::menu::MenuScreenId screen_id) const;
    std::size_t size() const;
    const ScreenBinding* items() const;

  private:
    ScreenBinding items_[kMaxBindings]{};
    std::size_t size_ = 0;
};

} // namespace screen
} // namespace ui
