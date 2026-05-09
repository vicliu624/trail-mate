#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "app/linux_app_facade.h"
#include "core/canvas.h"
#include "platform/surface_presenter.h"

// LVGL types used by value in this header are forward-declared.
// The concrete LVGL callbacks live entirely inside shell_ui_runner.cpp
// so that this header only needs the callback type, not the full LVGL tree.
struct _lv_disp_t;
struct _lv_indev_t;
struct _lv_area_t;
struct _lv_indev_data_t;
typedef _lv_disp_t lv_display_t;
typedef _lv_indev_t lv_indev_t;
typedef _lv_area_t lv_area_t;
typedef _lv_indev_data_t lv_indev_data_t;
typedef uint8_t lv_indev_state_t;

namespace trailmate::cardputer_zero
{
namespace app
{
struct InputEvent;
}

namespace linux_ui
{

// ---------------------------------------------------------------------------
// ShellSession
//
// Owns the app facade, shared UI startup sequence, input event queue, and
// per-frame tick logic.  Does NOT own LVGL lifecycle (lv_init/lv_deinit),
// display, or framebuffer.  That responsibility lives in the *LvglHost
// classes.
//
// Both the simulator and the real device shell share this session object.
// ---------------------------------------------------------------------------

class ShellSession
{
  public:
    ShellSession();
    ~ShellSession();

    ShellSession(const ShellSession&) = delete;
    ShellSession& operator=(const ShellSession&) = delete;

    /// Initialize the app facade and begin the shared UI startup sequence.
    /// Must be called after LVGL has been initialized by the host.
    bool begin();

    /// Drive one frame worth of app-layer work: update core services, tick
    /// the event runtime, dispatch pending events.
    void tick();

    /// Push simulated or evdev input events into the session queue.
    void enqueueInputs(const std::vector<app::InputEvent>& events);

    /// Returns true once the startup sequence has finalized.
    bool ready() const noexcept;

    /// Access the underlying app facade.
    MinimalLinuxAppFacade& facade() noexcept { return app_facade_; }
    const MinimalLinuxAppFacade& facade() const noexcept
    {
        return app_facade_;
    }

  private:
    friend class CanvasLvglHost;

    struct QueuedKeyEvent
    {
        std::uint32_t key{};
        lv_indev_state_t state{};
    };

    /// Called by the LVGL host's read-input callback to dequeue one key event.
    /// Returns false when the queue is empty.
    bool dequeueKeyEvent(std::uint32_t* key, lv_indev_state_t* state);

    /// Peek: returns true if at least one event is enqueued (used to set
    /// continue_reading without consuming the next event).
    bool hasPendingKeyEvent() const noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    MinimalLinuxAppFacade app_facade_;
    bool initialized_{false};
};

// ---------------------------------------------------------------------------
// CanvasLvglHost
//
// Owns the LVGL lifecycle (lv_init / lv_deinit), display, RGB565 buffer,
// keypad input device, and the framebuffer-to-Canvas copy step.
//
// Suitable for the SDL simulator and the custom Linux framebuffer shell.
// ---------------------------------------------------------------------------

class CanvasLvglHost
{
  public:
    explicit CanvasLvglHost(ShellSession& shell);
    ~CanvasLvglHost();

    CanvasLvglHost(const CanvasLvglHost&) = delete;
    CanvasLvglHost& operator=(const CanvasLvglHost&) = delete;

    /// Drive one frame: tick the shell session, run lv_timer_handler(), and
    /// (if dirty) copy the LVGL framebuffer to the output canvas.
    void tick();

    /// Read-only access to the output canvas for the surface presenter.
    const core::Canvas& canvas() const noexcept;

    /// LVGL callbacks — exact typed signatures matching LVGL's typedefs.
    /// Implemented in the .cpp, where lvgl.h provides the full definitions.
    static void flushCb(lv_display_t* display, const lv_area_t* area,
                        uint8_t* px_map);
    static void readInputCb(lv_indev_t* indev, lv_indev_data_t* data);

  private:
    void copyFrameBufferToCanvas();

    ShellSession& shell_;
    lv_display_t* display_{};
    lv_indev_t* keypad_{};
    core::Canvas canvas_;
    std::vector<std::uint16_t> frame_buffer_{};
    bool dirty_{true};
};

// ---------------------------------------------------------------------------
// NativeLvglHost
//
// Thin host for the M5Stack Linux SDK path where LVGL display and input are
// already owned by the SDK.  Only drives the shell session tick each frame.
//
// Usage in SDK main loop:
//
//   lv_init();
//   initLinuxDisplay();   // SDK-owned fbdev / evdev
//
//   ShellSession shell;
//   shell.begin();
//
//   NativeLvglHost host{shell};
//   while (true) {
//       host.tick();
//       lv_timer_handler();
//       usleep(1000);
//   }
// ---------------------------------------------------------------------------

class NativeLvglHost
{
  public:
    explicit NativeLvglHost(ShellSession& shell) : shell_(shell) {}

    /// Drive the shell session tick.  Caller is responsible for
    /// lv_timer_handler() and the main-loop sleep.
    void tick() { shell_.tick(); }

  private:
    ShellSession& shell_;
};

// ---------------------------------------------------------------------------
// Convenience – desktop simulator / custom-fbdev main loop
// ---------------------------------------------------------------------------

/// Create a ShellSession + CanvasLvglHost and run the classic frame-paced
/// main loop driven by a SurfacePresenter.  Backward-compatible entrypoint.
void runShellUi(
    platform::SurfacePresenter& presenter,
    std::chrono::milliseconds auto_exit_after = std::chrono::milliseconds::zero());

} // namespace linux_ui
} // namespace trailmate::cardputer_zero
