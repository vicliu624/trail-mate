#pragma once

#include <memory>
#include <string>
#include <vector>

#include "platform/surface_presenter.h"

namespace trailmate::cardputer_zero::platform::device
{

class LinuxFramebufferPlatform : public SurfacePresenter
{
  public:
    explicit LinuxFramebufferPlatform(std::string framebuffer_path);
    ~LinuxFramebufferPlatform() override;

    [[nodiscard]] bool pump() override;
    [[nodiscard]] std::vector<app::InputEvent> drainInput() override;
    void present(const core::Canvas& canvas) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace trailmate::cardputer_zero::platform::device
