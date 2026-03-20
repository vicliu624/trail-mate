#include "apps/gat562_mesh_evb_pro/ui_runtime.h"

#include "apps/gat562_mesh_evb_pro/app_facade_runtime.h"
#include "apps/gat562_mesh_evb_pro/debug_console.h"
#include "boards/gat562_mesh_evb_pro/gat562_board.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/time_runtime.h"
#include "sys/clock.h"
#include "ui/fonts/fusion_pixel_8_font.h"
#include "ui/mono_128x64/runtime.h"

#include <Arduino.h>
#include <ctime>

namespace apps::gat562_mesh_evb_pro::ui_runtime
{
namespace
{
using boards::gat562_mesh_evb_pro::BoardInputEvent;
using boards::gat562_mesh_evb_pro::BoardInputKey;
constexpr uint32_t kProbeHoldMs = 900;
const char kProbeAscii[] = "ABC123";
const char kProbeCjk[] = "\xE4\xB8\xAD\xE6\x96\x87";
const char kProbeSymbols[] = "\xE2\x94\x80\xE2\x96\x88\xE2\x96\xA0";

uint32_t now_ms() { return millis(); }
time_t utc_now() { return static_cast<time_t>(sys::epoch_seconds_now()); }

uint32_t active_lora_frequency_hz()
{
    return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().activeLoraFrequencyHz();
}

bool format_freq(uint32_t freq_hz, char* out, size_t out_len)
{
    return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().formatLoraFrequencyMHz(freq_hz, out, out_len);
}

ui::mono_128x64::InputAction to_input_action(
    const BoardInputEvent* event)
{
    if (!event || !event->pressed)
    {
        return ui::mono_128x64::InputAction::None;
    }

    switch (event->key)
    {
    case BoardInputKey::JoystickUp:
        return ui::mono_128x64::InputAction::Up;
    case BoardInputKey::JoystickDown:
        return ui::mono_128x64::InputAction::Down;
    case BoardInputKey::JoystickLeft:
        return ui::mono_128x64::InputAction::Left;
    case BoardInputKey::JoystickRight:
        return ui::mono_128x64::InputAction::Right;
    case BoardInputKey::JoystickPress:
        return ui::mono_128x64::InputAction::Select;
    case BoardInputKey::PrimaryButton:
        return ui::mono_128x64::InputAction::Primary;
    case BoardInputKey::SecondaryButton:
        return ui::mono_128x64::InputAction::Secondary;
    default:
        return ui::mono_128x64::InputAction::None;
    }
}

bool s_initialized = false;
ui::mono_128x64::Runtime* s_runtime = nullptr;
bool s_probe_drawn = false;

void drawProbePattern(::ui::mono_128x64::MonoDisplay& display, const ::ui::mono_128x64::MonoFont& font)
{
    ::ui::mono_128x64::TextRenderer renderer(font);
    display.clear();

    const int w = display.width();
    const int h = display.height();
    for (int x = 0; x < w; ++x)
    {
        display.drawPixel(x, 0, true);
        display.drawPixel(x, h - 1, true);
    }
    for (int y = 0; y < h; ++y)
    {
        display.drawPixel(0, y, true);
        display.drawPixel(w - 1, y, true);
    }
    for (int d = 0; d < 16; ++d)
    {
        display.drawPixel(2 + d, 2 + d, true);
        display.drawPixel(w - 3 - d, 2 + d, true);
    }

    display.fillRect(96, 8, 16, 8, true);
    display.fillRect(96, 20, 16, 8, false);
    display.drawHLine(8, 31, 40);

    renderer.drawText(display, 6, 8, kProbeAscii);
    renderer.drawText(display, 6, 20, kProbeCjk);
    renderer.drawText(display, 6, 32, kProbeSymbols);
    display.present();
}

} // namespace

bool initialize()
{
    if (s_initialized)
    {
        return s_runtime != nullptr;
    }
    s_initialized = true;

    static ui::mono_128x64::HostCallbacks callbacks{};
    callbacks.app = &AppFacadeRuntime::instance();
    callbacks.ui_font = &platform::nrf52::ui::fonts::fusion_pixel_8_font();
    callbacks.millis_fn = now_ms;
    callbacks.utc_now_fn = utc_now;
    callbacks.timezone_offset_min_fn = platform::ui::time::timezone_offset_min;
    callbacks.set_timezone_offset_min_fn = platform::ui::time::set_timezone_offset_min;
    callbacks.active_lora_frequency_hz_fn = active_lora_frequency_hz;
    callbacks.format_frequency_fn = format_freq;
    callbacks.battery_info_fn = platform::ui::device::battery_info;
    callbacks.gps_data_fn = platform::ui::gps::get_data;
    callbacks.gps_enabled_fn = platform::ui::gps::is_enabled;
    callbacks.gps_powered_fn = platform::ui::gps::is_powered;

    static ui::mono_128x64::Runtime runtime(::boards::gat562_mesh_evb_pro::Gat562Board::instance().monoDisplay(),
                                            callbacks);
    s_runtime = &runtime;
    const bool ok = s_runtime->begin();
    debug_console::printf("[gat562] ui init display=%s\n", ok ? "ok" : "fail");
    return ok;
}

void appendBootLog(const char* line)
{
    if (initialize() && s_runtime)
    {
        s_runtime->appendBootLog(line);
        s_runtime->tick(ui::mono_128x64::InputAction::None);
    }
}

void bindChatObservers()
{
    if (initialize() && s_runtime)
    {
        s_runtime->bindChatObservers();
    }
}

void tick(const BoardInputEvent* event)
{
    if (initialize() && s_runtime)
    {
        const auto action = to_input_action(event);
        s_runtime->tick(action);
    }
}

void showDisplayProbe()
{
    if (!initialize())
    {
        debug_console::println("[gat562] display probe skipped: ui init failed");
        return;
    }
    if (s_probe_drawn)
    {
        return;
    }

    auto& display = ::boards::gat562_mesh_evb_pro::Gat562Board::instance().monoDisplay();
    drawProbePattern(display, platform::nrf52::ui::fonts::fusion_pixel_8_font());
    s_probe_drawn = true;
    debug_console::println("[gat562] display probe rendered");
    delay(kProbeHoldMs);
}

} // namespace apps::gat562_mesh_evb_pro::ui_runtime
