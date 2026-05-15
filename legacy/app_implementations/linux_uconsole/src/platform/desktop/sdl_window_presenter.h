#pragma once

#include <memory>
#include <string>

#include "platform/surface_presenter.h"

namespace trailmate::uconsole::desktop
{

struct SdlWindowOptions
{
    int width = 1280;
    int height = 720;
    int scale = 1;
    bool fullscreen = false;
    std::string title = "Trail Mate uConsole";
};

class SdlWindowPresenter : public cardputer_zero::platform::SurfacePresenter
{
  public:
    explicit SdlWindowPresenter(SdlWindowOptions options = {});
    ~SdlWindowPresenter() override;

    [[nodiscard]] bool pump() override;
    [[nodiscard]] std::vector<cardputer_zero::app::InputEvent> drainInput() override;
    void present(const cardputer_zero::core::Canvas& canvas) override;

  private:
    struct Impl;

    std::unique_ptr<Impl> impl_{};
};

} // namespace trailmate::uconsole::desktop
