#pragma once

#include <vector>

#include "app/input_event.h"
#include "core/canvas.h"

namespace trailmate::cardputer_zero::platform
{

class SurfacePresenter
{
  public:
    virtual ~SurfacePresenter() = default;

    [[nodiscard]] virtual bool pump() = 0;
    [[nodiscard]] virtual std::vector<app::InputEvent> drainInput() = 0;
    virtual void present(const core::Canvas& canvas) = 0;
};

} // namespace trailmate::cardputer_zero::platform
