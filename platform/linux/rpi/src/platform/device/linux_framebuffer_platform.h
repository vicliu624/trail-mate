#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "platform/surface_presenter.h"

namespace trailmate::cardputer_zero::platform::device
{

struct FramebufferStatistics
{
    std::uint64_t requests = 0;
    std::uint64_t commits = 0;
    std::uint64_t bytes_written = 0;
};

class LinuxFramebufferPlatform : public SurfacePresenter
{
  public:
    explicit LinuxFramebufferPlatform(std::string framebuffer_path);
    ~LinuxFramebufferPlatform() override;

    [[nodiscard]] bool pump() override;
    [[nodiscard]] std::vector<app::InputEvent> drainInput() override;
    void present(const core::Canvas& canvas) override;
    [[nodiscard]] FramebufferStatistics statistics() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace trailmate::cardputer_zero::platform::device
