#pragma once

#include <cstdint>

#include "device/host_types.h"

namespace device
{

enum class UiShellKind : std::uint8_t
{
    None,
    Lvgl,
    Ascii,
    Gtk,
    Cli,
    Headless,
};

enum class RendererKind : std::uint8_t
{
    None,
    Lvgl,
    AsciiCanvas,
    Gtk4,
    Stdout,
};

struct UiBinding
{
    UiShellKind shell = UiShellKind::None;
    RendererKind renderer = RendererKind::None;
    HostKind presentation_model_host = HostKind::None;
    HostKind ui_host = HostKind::None;
};

} // namespace device
