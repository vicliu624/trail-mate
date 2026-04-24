#pragma once

#include <chrono>

#include "platform/surface_presenter.h"

namespace trailmate::cardputer_zero::simulator::ui_shell {

void runShellUi(
    platform::SurfacePresenter& presenter,
    std::chrono::milliseconds auto_exit_after = std::chrono::milliseconds::zero());

} // namespace trailmate::cardputer_zero::simulator::ui_shell
