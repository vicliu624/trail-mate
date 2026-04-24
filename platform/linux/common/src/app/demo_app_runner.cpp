#include "app/demo_app_runner.h"

#include <chrono>
#include <thread>

#include "app/demo_app.h"
#include "core/display_profile.h"

namespace trailmate::cardputer_zero::app {

void runDemoApp(platform::SurfacePresenter& presenter, std::chrono::milliseconds auto_exit_after)
{
    DemoApp app{};
    core::Canvas canvas{core::kDisplayWidth, core::kDisplayHeight};

    using clock = std::chrono::steady_clock;
    constexpr auto kFrameTime = std::chrono::milliseconds(33);
    auto next_frame = clock::now();
    const auto started_at = next_frame;

    while (presenter.pump()) {
        for (const auto& event : presenter.drainInput()) {
            app.handleInput(event);
        }

        if (auto_exit_after > std::chrono::milliseconds::zero() && (clock::now() - started_at) >= auto_exit_after) {
            break;
        }

        app.render(canvas);
        presenter.present(canvas);

        next_frame += kFrameTime;
        std::this_thread::sleep_until(next_frame);

        if (clock::now() > next_frame + std::chrono::milliseconds(250)) {
            next_frame = clock::now();
        }
    }
}

} // namespace trailmate::cardputer_zero::app
