#pragma once

#include <vector>

#include "app/input_event.h"
#include "core/canvas.h"

namespace trailmate::cardputer_zero::platform
{

class SurfacePresenter
{
  public:
    struct PointerState
    {
        int x = 0;
        int y = 0;
        bool pressed = false;
    };

    virtual ~SurfacePresenter() = default;

    [[nodiscard]] virtual bool pump() = 0;
    [[nodiscard]] virtual std::vector<app::InputEvent> drainInput() = 0;
    [[nodiscard]] virtual bool supportsPointer() const noexcept
    {
        return false;
    }
    [[nodiscard]] virtual PointerState pointerState() const noexcept
    {
        return {};
    }
    virtual void present(const core::Canvas& canvas) = 0;
    virtual void setTextInputState(const app::TextInputState&) {}
};

} // namespace trailmate::cardputer_zero::platform
