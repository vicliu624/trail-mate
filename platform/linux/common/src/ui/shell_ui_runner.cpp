#include "ui/shell_ui_runner.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <thread>
#include <vector>

#include "lvgl.h"

#include "app/input_event.h"
#include "app/linux_app_facade.h"
#include "core/canvas.h"
#include "core/display_profile.h"
#include "ui/screens/team/team_page_shell.h"
#include "ui/shared_ui_shell.h"

namespace trailmate::cardputer_zero::linux_ui
{
namespace
{

using clock = std::chrono::steady_clock;

constexpr auto kFrameTime = std::chrono::milliseconds(16);

std::chrono::steady_clock::time_point g_lvgl_start_time = clock::now();

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

[[nodiscard]] std::uint32_t mapInputEvent(const app::InputEvent& event) noexcept
{
    switch (event.key)
    {
    case app::InputKey::Character:
        return event.text == '\0' ? 0U
                                  : static_cast<std::uint8_t>(event.text);
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
        std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - g_lvgl_start_time)
            .count());
}

struct QueuedKeyEvent
{
    std::uint32_t key{};
    lv_indev_state_t state{LV_INDEV_STATE_RELEASED};
};

bool dispatchTeamUiEvent(sys::Event* event)
{
    return team::ui::shell::handle_event(nullptr, event);
}

} // namespace

// ===================================================================
// ShellSession
// ===================================================================

struct ShellSession::Impl
{
    SharedUiShellStartup startup{};
    std::deque<QueuedKeyEvent> key_events{};
};

ShellSession::ShellSession() : impl_(std::make_unique<Impl>()) {}

ShellSession::~ShellSession()
{
    if (initialized_)
    {
        setTeamUiEventDispatcher(nullptr);
        app_facade_.shutdown();
    }
}

bool ShellSession::begin()
{
    if (initialized_) return true;
    if (!app_facade_.initialize()) return false;
    setTeamUiEventDispatcher(dispatchTeamUiEvent);
    if (!impl_->startup.begin()) return false;
    initialized_ = true;
    return true;
}

void ShellSession::tick()
{
    if (!initialized_) return;
    if (!impl_->startup.tick())
        throw std::runtime_error("Shared UI startup sequence failed.");
    app_facade_.updateCoreServices();
    app_facade_.tickEventRuntime();
    app_facade_.dispatchPendingEvents();
}

bool ShellSession::ready() const noexcept
{
    return initialized_ && impl_->startup.ready();
}

void ShellSession::enqueueInputs(const std::vector<app::InputEvent>& events)
{
    for (const auto& event : events)
    {
        const std::uint32_t mapped = mapInputEvent(event);
        if (mapped == 0U) continue;
        impl_->key_events.push_back(
            QueuedKeyEvent{mapped, LV_INDEV_STATE_PRESSED});
        impl_->key_events.push_back(
            QueuedKeyEvent{mapped, LV_INDEV_STATE_RELEASED});
    }
}

bool ShellSession::dequeueKeyEvent(std::uint32_t* key, lv_indev_state_t* state)
{
    if (impl_->key_events.empty()) return false;
    const QueuedKeyEvent next = impl_->key_events.front();
    impl_->key_events.pop_front();
    *key = next.key;
    *state = next.state;
    return true;
}

bool ShellSession::hasPendingKeyEvent() const noexcept
{
    return !impl_->key_events.empty();
}

// ===================================================================
// CanvasLvglHost
// ===================================================================

CanvasLvglHost::CanvasLvglHost(ShellSession& shell)
    : shell_(shell),
      canvas_(core::kDisplayWidth, core::kDisplayHeight),
      frame_buffer_(
          static_cast<std::size_t>(core::kDisplayWidth * core::kDisplayHeight),
          0)
{
    g_lvgl_start_time = clock::now();
    lv_init();
    lv_tick_set_cb(tickNow);

    display_ = lv_display_create(core::kDisplayWidth, core::kDisplayHeight);
    if (display_ == nullptr)
        throw std::runtime_error(
            "Failed to create LVGL display for the Linux shell.");

    lv_display_set_default(display_);
    lv_display_set_user_data(display_, this);
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(
        display_,
        frame_buffer_.data(),
        nullptr,
        static_cast<std::uint32_t>(frame_buffer_.size() * sizeof(std::uint16_t)),
        LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display_, flushCb);

    keypad_ = lv_indev_create();
    if (keypad_ == nullptr)
        throw std::runtime_error(
            "Failed to create LVGL keypad input for the Linux shell.");

    lv_indev_set_type(keypad_, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_display(keypad_, display_);
    lv_indev_set_user_data(keypad_, this);
    lv_indev_set_read_cb(keypad_, readInputCb);

    // Render the first frame before the main loop starts.
    tick();
}

CanvasLvglHost::~CanvasLvglHost()
{
    if (keypad_ != nullptr)
    {
        lv_indev_delete(keypad_);
        keypad_ = nullptr;
    }
    if (display_ != nullptr)
    {
        lv_display_delete(display_);
        display_ = nullptr;
    }
    lv_deinit();
}

void CanvasLvglHost::tick()
{
    shell_.tick();
    lv_timer_handler();
    if (dirty_)
    {
        copyFrameBufferToCanvas();
        dirty_ = false;
    }
}

const core::Canvas& CanvasLvglHost::canvas() const noexcept
{
    return canvas_;
}

// ---------------------------------------------------------------------------
// Static LVGL callbacks (cast void* args back to typed pointers)
// ---------------------------------------------------------------------------

void CanvasLvglHost::flushCb(lv_display_t* display,
                             const lv_area_t* /*area*/,
                             uint8_t* /*px_map*/)
{
    auto* host = static_cast<CanvasLvglHost*>(
        lv_display_get_user_data(display));
    if (host != nullptr) host->dirty_ = true;
    lv_display_flush_ready(display);
}

void CanvasLvglHost::readInputCb(lv_indev_t* indev, lv_indev_data_t* d)
{
    auto* host = static_cast<CanvasLvglHost*>(lv_indev_get_user_data(indev));
    if (host == nullptr || d == nullptr) return;

    d->state = LV_INDEV_STATE_RELEASED;
    d->key = 0U;

    std::uint32_t key = 0U;
    lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
    if (!host->shell_.dequeueKeyEvent(&key, &state)) return;

    d->state = state;
    d->key = key;
    // Peek the queue without consuming the next event.
    d->continue_reading = host->shell_.hasPendingKeyEvent();
}

// ---------------------------------------------------------------------------
// Framebuffer -> Canvas copy
// ---------------------------------------------------------------------------

void CanvasLvglHost::copyFrameBufferToCanvas()
{
    for (int y = 0; y < core::kDisplayHeight; ++y)
    {
        for (int x = 0; x < core::kDisplayWidth; ++x)
        {
            const auto index =
                static_cast<std::size_t>((y * core::kDisplayWidth) + x);
            canvas_.setPixel(x, y, rgb565ToColor(frame_buffer_[index]));
        }
    }
}

// ===================================================================
// runShellUi (backward-compatible convenience)
// ===================================================================

void runShellUi(platform::SurfacePresenter& presenter,
                std::chrono::milliseconds auto_exit_after)
{
    ShellSession shell;
    if (!shell.begin())
        throw std::runtime_error(
            "Failed to begin ShellSession for the shared UI shell.");

    CanvasLvglHost host{shell};

    auto next_frame = clock::now();
    const auto started_at = next_frame;

    while (presenter.pump())
    {
        shell.enqueueInputs(presenter.drainInput());

        if (auto_exit_after > std::chrono::milliseconds::zero() &&
            (clock::now() - started_at) >= auto_exit_after)
        {
            break;
        }

        host.tick();
        presenter.present(host.canvas());

        next_frame += kFrameTime;
        std::this_thread::sleep_until(next_frame);

        if (clock::now() > next_frame + std::chrono::milliseconds(250))
            next_frame = clock::now();
    }
}

} // namespace trailmate::cardputer_zero::linux_ui
