#include "apps/gat562_mesh_evb_pro/ui_runtime.h"

#include "apps/gat562_mesh_evb_pro/app_facade_runtime.h"
#include "boards/gat562_mesh_evb_pro/gat562_board.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/time_runtime.h"
#include "sys/clock.h"
#include "ui/mono_128x64/runtime.h"

#include <Arduino.h>
#include <ctime>

namespace apps::gat562_mesh_evb_pro::ui_runtime
{
namespace
{
using boards::gat562_mesh_evb_pro::BoardInputEvent;
using boards::gat562_mesh_evb_pro::BoardInputKey;

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
    case BoardInputKey::JoystickUp: return ui::mono_128x64::InputAction::Up;
    case BoardInputKey::JoystickDown: return ui::mono_128x64::InputAction::Down;
    case BoardInputKey::JoystickLeft: return ui::mono_128x64::InputAction::Left;
    case BoardInputKey::JoystickRight: return ui::mono_128x64::InputAction::Right;
    case BoardInputKey::JoystickPress: return ui::mono_128x64::InputAction::Select;
    case BoardInputKey::PrimaryButton: return ui::mono_128x64::InputAction::Primary;
    case BoardInputKey::SecondaryButton: return ui::mono_128x64::InputAction::Secondary;
    default: return ui::mono_128x64::InputAction::None;
    }
}

bool s_initialized = false;
ui::mono_128x64::Runtime* s_runtime = nullptr;

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
    return s_runtime->begin();
}

void appendBootLog(const char* line)
{
    if (initialize() && s_runtime)
    {
        s_runtime->appendBootLog(line);
        s_runtime->tick(ui::mono_128x64::InputAction::None);
    }
}

void tick(const BoardInputEvent* event)
{
    if (initialize() && s_runtime)
    {
        s_runtime->tick(to_input_action(event));
    }
}

} // namespace apps::gat562_mesh_evb_pro::ui_runtime
