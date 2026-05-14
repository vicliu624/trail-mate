#pragma once

#include <cstddef>
#include <cstdint>

namespace ui_lvgl_ux
{

enum class InputAction : uint8_t
{
    Up,
    Down,
    Left,
    Right,
    Select,
    Back,
    Menu,
    Compose,
    MapZoomIn,
    MapZoomOut,
};

struct InputBinding
{
    constexpr InputBinding() = default;

    constexpr InputBinding(InputAction input_action, const char* input_hint)
        : action(input_action),
          hint(input_hint)
    {
    }

    InputAction action = InputAction::Select;
    const char* hint = nullptr;
};

class InputBindingSet
{
  public:
    static constexpr std::size_t kMaxBindings = 24;

    void clear();
    bool add(const InputBinding& binding);
    std::size_t size() const;
    const InputBinding* items() const;

  private:
    InputBinding items_[kMaxBindings]{};
    std::size_t size_ = 0;
};

} // namespace ui_lvgl_ux
