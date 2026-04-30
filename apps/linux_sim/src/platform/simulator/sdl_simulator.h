#pragma once

#include <memory>

#include "platform/surface_presenter.h"

namespace trailmate::cardputer_zero::platform::simulator
{

class SdlSimulator : public SurfacePresenter
{
  public:
    explicit SdlSimulator(int scale = 1);
    ~SdlSimulator() override;

    [[nodiscard]] bool pump() override;
    [[nodiscard]] std::vector<app::InputEvent> drainInput() override;
    void present(const core::Canvas& canvas) override;

  private:
    struct Impl;

    std::unique_ptr<Impl> impl_{};
};

} // namespace trailmate::cardputer_zero::platform::simulator
