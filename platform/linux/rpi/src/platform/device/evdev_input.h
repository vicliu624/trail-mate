#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/input_event.h"

namespace trailmate::cardputer_zero::platform::device
{

// ---------------------------------------------------------------------------
// EvdevInput
//
// Non-blocking Linux evdev keyboard adapter for Cardputer Zero.
//
// Responsibilities:
//   - Open an evdev device file (non-blocking).
//   - Read struct input_event (EV_KEY only).
//   - Map Linux key codes to trailmate::cardputer_zero::app::InputEvent.
//   - Respect env override TRAIL_MATE_INPUT_DEVICE.
//   - Probe candidate paths when no explicit device is given.
//
// Does NOT include evdev headers in this header — that stays in the .cpp.
// ---------------------------------------------------------------------------

class EvdevInput
{
  public:
    /// Construct with an explicit device path (e.g. "/dev/input/event3").
    /// If the path is empty, the auto-discovery logic runs.
    explicit EvdevInput(std::string device_path = {});

    ~EvdevInput();

    EvdevInput(const EvdevInput&) = delete;
    EvdevInput& operator=(const EvdevInput&) = delete;

    /// Returns true if a keyboard device was successfully opened.
    bool isOpen() const noexcept;

    /// Non-blocking poll: reads all available input events and returns
    /// the mapped Trail Mate InputEvent list.  Returns an empty vector
    /// when no device is open or no events are available.
    std::vector<app::InputEvent> drainInput();

    /// Returns the device path that was actually opened (for diagnostics).
    const std::string& devicePath() const noexcept;

    /// Re-open a different device.  Closes the current one first.
    bool reopen(std::string device_path);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace trailmate::cardputer_zero::platform::device
