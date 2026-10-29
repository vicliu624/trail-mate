#pragma once

#include <memory>
#include <vector>

#include "app/input_event.h"
#include "platform/surface_presenter.h"

namespace trailmate::cardputer_zero::platform::wayland
{

class WaylandPresenter final : public SurfacePresenter
{
  public:
    WaylandPresenter();
    ~WaylandPresenter() override;

    WaylandPresenter(const WaylandPresenter&) = delete;
    WaylandPresenter& operator=(const WaylandPresenter&) = delete;

    [[nodiscard]] bool pump() override;
    [[nodiscard]] std::vector<app::InputEvent> drainInput() override;
    void present(const core::Canvas& canvas) override;
    void setTextInputState(const app::TextInputState&) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace trailmate::cardputer_zero::platform::wayland
