#pragma once

#include "platform/linux/backlight.h"
#include "platform/linux/screen_service.h"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace platform::linux_runtime
{
// Native framebuffer composition: reuse the asynchronous backlight worker.
// Screen state still belongs to the shared runtime used by auto reply.
class BacklightScreenBinding
{
  public:
    explicit BacklightScreenBinding(BacklightOptions backlight = {}, std::function<uint32_t()> now = {})
    {
        if (!configure_backlight(backlight)) throw std::runtime_error("Backlight already configured");
        prepare_backlight();
        ScreenRuntimeOptions options;
        options.now_ms = std::move(now);
        options.power_snapshot = [this]
        {
            const auto light = backlight_snapshot();
            if (light.available && light.level > 0) awake_level_ = light.level;
            return OutputPowerSnapshot{light.available, light.selection_required, light.failed,
                                       light.level > 0, "backlight"};
        };
        options.request_power = [this](bool on)
        {
            const bool accepted = request_backlight(on ? awake_level_ : 0);
            if (!on && accepted) owns_off_ = true;
            return accepted;
        };
        if (!configure_screen_runtime(std::move(options))) throw std::runtime_error("Screen already configured");
    }
    ~BacklightScreenBinding()
    {
        shutdown_screen_runtime();
        if (owns_off_)
        {
            (void)request_backlight(awake_level_);
            for (unsigned attempt = 0; attempt < 100 && backlight_snapshot().level != awake_level_; ++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        shutdown_backlight();
    }

    // The first input wakes; a later confirm dismisses the saver. Neither is
    // forwarded to the page underneath it.
    static bool consumeWakeInput(bool confirm)
    {
        namespace screen = ::platform::ui::screen;
        if (screen::is_sleeping() || screen::is_saver_active())
        {
            if (screen::is_saver_active() && confirm) screen::request_resume();
            else screen::handle_input();
            service_screen_runtime();
            return true;
        }
        screen::record_activity();
        return false;
    }

  private:
    uint8_t awake_level_ = 255;
    bool owns_off_ = false;
};
} // namespace platform::linux_runtime
