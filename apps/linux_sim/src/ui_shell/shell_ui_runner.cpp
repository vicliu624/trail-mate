#include "ui_shell/shell_ui_runner.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <thread>
#include <vector>

#include "lvgl.h"

#include "app/input_event.h"
#include "core/canvas.h"
#include "core/display_profile.h"
#include "ui_shell/shared_ui_shell.h"

namespace trailmate::cardputer_zero::simulator::ui_shell {
namespace {

using clock = std::chrono::steady_clock;

constexpr auto kFrameTime = std::chrono::milliseconds(16);

std::chrono::steady_clock::time_point g_lvgl_start_time = clock::now();

struct QueuedKeyEvent {
    std::uint32_t key{};
    lv_indev_state_t state{LV_INDEV_STATE_RELEASED};
};

[[nodiscard]] std::uint32_t mapInputEvent(const app::InputEvent& event) noexcept
{
    switch (event.key) {
    case app::InputKey::Character:
        return event.text == '\0' ? 0U : static_cast<std::uint8_t>(event.text);
    case app::InputKey::Backspace:
        return LV_KEY_BACKSPACE;
    case app::InputKey::Enter:
        return LV_KEY_ENTER;
    case app::InputKey::Tab:
        return LV_KEY_NEXT;
    case app::InputKey::Home:
        return LV_KEY_ESC;
    case app::InputKey::Next:
        return LV_KEY_NEXT;
    case app::InputKey::Power:
        return LV_KEY_ESC;
    case app::InputKey::Left:
        return LV_KEY_LEFT;
    case app::InputKey::Right:
        return LV_KEY_RIGHT;
    case app::InputKey::Up:
        return LV_KEY_UP;
    case app::InputKey::Down:
        return LV_KEY_DOWN;
    case app::InputKey::Unknown:
    case app::InputKey::Fn:
    case app::InputKey::Ctrl:
    case app::InputKey::Alt:
    case app::InputKey::Shift:
        return 0U;
    }

    return 0U;
}

[[nodiscard]] std::uint8_t expand5(std::uint16_t value) noexcept
{
    return static_cast<std::uint8_t>((value * 255U) / 31U);
}

[[nodiscard]] std::uint8_t expand6(std::uint16_t value) noexcept
{
    return static_cast<std::uint8_t>((value * 255U) / 63U);
}

[[nodiscard]] core::Color rgb565ToColor(std::uint16_t pixel) noexcept
{
    const auto red = static_cast<std::uint16_t>((pixel >> 11U) & 0x1FU);
    const auto green = static_cast<std::uint16_t>((pixel >> 5U) & 0x3FU);
    const auto blue = static_cast<std::uint16_t>(pixel & 0x1FU);
    return core::rgba(expand5(red), expand6(green), expand5(blue));
}

[[nodiscard]] std::uint32_t tickNow() noexcept
{
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - g_lvgl_start_time).count());
}

class ShellUiRuntime {
public:
    ShellUiRuntime()
        : canvas_(core::kDisplayWidth, core::kDisplayHeight),
          frame_buffer_(static_cast<std::size_t>(core::kDisplayWidth * core::kDisplayHeight), 0)
    {
        g_lvgl_start_time = clock::now();
        lv_init();
        lv_tick_set_cb(tickNow);

        display_ = lv_display_create(core::kDisplayWidth, core::kDisplayHeight);
        if (display_ == nullptr) {
            throw std::runtime_error("Failed to create LVGL display for the shell simulator.");
        }

        lv_display_set_default(display_);
        lv_display_set_user_data(display_, this);
        lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(
            display_,
            frame_buffer_.data(),
            nullptr,
            static_cast<std::uint32_t>(frame_buffer_.size() * sizeof(std::uint16_t)),
            LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_flush_cb(display_, flushCallback);

        keypad_ = lv_indev_create();
        if (keypad_ == nullptr) {
            throw std::runtime_error("Failed to create LVGL keypad input for the shell simulator.");
        }

        lv_indev_set_type(keypad_, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_display(keypad_, display_);
        lv_indev_set_user_data(keypad_, this);
        lv_indev_set_read_cb(keypad_, readInputCallback);

        if (!initialize()) {
            throw std::runtime_error("Failed to initialize the shared UI shell.");
        }

        render();
    }

    ~ShellUiRuntime()
    {
        if (keypad_ != nullptr) {
            lv_indev_delete(keypad_);
            keypad_ = nullptr;
        }
        if (display_ != nullptr) {
            lv_display_delete(display_);
            display_ = nullptr;
        }
        lv_deinit();
    }

    void enqueueInputs(const std::vector<app::InputEvent>& events)
    {
        for (const auto& event : events) {
            const std::uint32_t mapped = mapInputEvent(event);
            if (mapped == 0U) {
                continue;
            }

            key_events_.push_back(QueuedKeyEvent{mapped, LV_INDEV_STATE_PRESSED});
            key_events_.push_back(QueuedKeyEvent{mapped, LV_INDEV_STATE_RELEASED});
        }
    }

    void render()
    {
        lv_timer_handler();
        if (dirty_) {
            copyFrameBufferToCanvas();
            dirty_ = false;
        }
    }

    [[nodiscard]] const core::Canvas& canvas() const noexcept
    {
        return canvas_;
    }

private:
    static void flushCallback(lv_display_t* display, const lv_area_t* area, std::uint8_t* px_map)
    {
        LV_UNUSED(area);
        LV_UNUSED(px_map);

        auto* runtime = static_cast<ShellUiRuntime*>(lv_display_get_user_data(display));
        if (runtime != nullptr) {
            runtime->dirty_ = true;
        }

        lv_display_flush_ready(display);
    }

    static void readInputCallback(lv_indev_t* indev, lv_indev_data_t* data)
    {
        auto* runtime = static_cast<ShellUiRuntime*>(lv_indev_get_user_data(indev));
        if (runtime == nullptr || data == nullptr) {
            return;
        }

        data->state = LV_INDEV_STATE_RELEASED;
        data->key = 0U;
        data->continue_reading = false;

        if (runtime->key_events_.empty()) {
            return;
        }

        const QueuedKeyEvent next = runtime->key_events_.front();
        runtime->key_events_.pop_front();

        data->state = next.state;
        data->key = next.key;
        data->continue_reading = !runtime->key_events_.empty();
    }

    void copyFrameBufferToCanvas()
    {
        for (int y = 0; y < core::kDisplayHeight; ++y) {
            for (int x = 0; x < core::kDisplayWidth; ++x) {
                const auto index = static_cast<std::size_t>((y * core::kDisplayWidth) + x);
                canvas_.setPixel(x, y, rgb565ToColor(frame_buffer_[index]));
            }
        }
    }

    lv_display_t* display_{};
    lv_indev_t* keypad_{};
    core::Canvas canvas_;
    std::vector<std::uint16_t> frame_buffer_{};
    std::deque<QueuedKeyEvent> key_events_{};
    bool dirty_{true};
};

} // namespace

void runShellUi(platform::SurfacePresenter& presenter, std::chrono::milliseconds auto_exit_after)
{
    ShellUiRuntime runtime{};

    auto next_frame = clock::now();
    const auto started_at = next_frame;

    while (presenter.pump()) {
        runtime.enqueueInputs(presenter.drainInput());

        if (auto_exit_after > std::chrono::milliseconds::zero() && (clock::now() - started_at) >= auto_exit_after) {
            break;
        }

        runtime.render();
        presenter.present(runtime.canvas());

        next_frame += kFrameTime;
        std::this_thread::sleep_until(next_frame);

        if (clock::now() > next_frame + std::chrono::milliseconds(250)) {
            next_frame = clock::now();
        }
    }
}

} // namespace trailmate::cardputer_zero::simulator::ui_shell
