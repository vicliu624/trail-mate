#include "platform/ui/quick_controls_runtime.h"
#include "ui_presentation/controls/quick_controls.h"

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2)
#include "app/app_facade_access.h"
#include "board/LoraBoard.h"
#include "music/service.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/radio/radio_power.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/music_runtime.h"
#include "platform/ui/reticulum_call_runtime.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/wifi_access_runtime.h"
#include "platform/ui/wifi_runtime.h"
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "platform/esp/idf_common/selected_radio.h"
#endif
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace platform::ui
{
namespace
{
using namespace ::ui::controls;
enum class Error : uint8_t
{
    None,
    Busy,
    Unsupported,
    Apply,
    Persist
};
// Five coalescing control words, no dynamic queue, worker, timer or hardware lease.
// State touched by multiple tasks is kept internal; view snapshots live in PSRAM.
std::atomic<uint32_t> pending[5]{};
std::atomic<bool> applying[5]{};
std::atomic<Error> errors[5]{};
bool radio_online()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::platform::esp::idf_common::SelectedRadio::instance().isOnline();
#else
    ::platform::esp::boards::AppContextInitHandles handles;
    return ::platform::esp::boards::tryResolveAppContextInitHandles(&handles) && handles.lora_board && handles.lora_board->isRadioOnline();
#endif
}
bool radio_available()
{
    ::platform::esp::boards::AppContextInitHandles handles;
    return ::platform::esp::boards::tryResolveAppContextInitHandles(&handles) && handles.lora_board;
}
bool wifi_busy()
{
    const auto status = wifi_access::status();
    return status.non_preemptible_active || status.ota_active || status.exclusive_owner != wifi_access::ExclusiveOwner::None;
}
class Source final : public IQuickControls
{
  public:
    bool snapshot(Snapshot& out) override
    {
        const auto wireless = wifi::status();
        const auto radio = ::platform::radio::power_policy().status();
        out.wifi_enabled = wireless.enabled;
        out.lora_enabled = radio.applied;
        out.gps_enabled = device::gps_supported() && gps::is_enabled();
        out.wifi = wireless.supported ? (wireless.enabled ? SwitchState::On : SwitchState::Off) : SwitchState::Unavailable;
        out.lora = radio_available() ? (radio.applied ? SwitchState::On : SwitchState::Off) : SwitchState::Unavailable;
        if (radio.enabled && radio.applied && !radio_online()) out.lora = SwitchState::Unavailable;
        out.gps = device::gps_supported() ? (out.gps_enabled ? SwitchState::On : SwitchState::Off) : SwitchState::Unavailable;
        if (wireless.state == wifi::ConnectionState::ResourceDeferred) out.wifi = SwitchState::Unavailable;
        if (out.lora != SwitchState::Unavailable && radio.enabled != radio.applied)
            out.lora = radio.failed ? SwitchState::Failed : SwitchState::Changing;
        SwitchState* switches[] = {&out.wifi, &out.lora, &out.gps};
        for (unsigned i = 0; i < 3; ++i)
            if (*switches[i] != SwitchState::Unavailable && (pending[i].load() || applying[i].load())) *switches[i] = SwitchState::Changing;
        auto* music = music_service();
        out.volume_supported = music != nullptr;
        out.volume = music ? music->volume() : 0;
        const auto maximum = device::screen_brightness_max();
        out.brightness_supported = device::supports_screen_brightness() && maximum;
        out.brightness = maximum ? std::clamp<unsigned>((device::screen_brightness() * 100U + maximum / 2U) / maximum, 10, 100) : 0;
        out.message[0] = 0;
        static constexpr const char* names[] = {"Wi-Fi", "LoRa", "GPS", "Volume", "Brightness"};
        for (unsigned i = 0; i < 5; ++i)
        {
            const auto error = errors[i].load();
            if (error == Error::None) continue;
            std::snprintf(out.message, sizeof(out.message), "%s: %s", names[i], error == Error::Busy ? "in use; try again when finished" : error == Error::Unsupported ? "unavailable"
                                                                                                                                       : error == Error::Persist       ? "preference could not be saved"
                                                                                                                                                                       : "change failed; try again");
            if (i < 3 && *switches[i] != SwitchState::Unavailable && *switches[i] != SwitchState::Changing) *switches[i] = SwitchState::Failed;
            break;
        }
        return true;
    }
    bool set(Control control, uint8_t value) override
    {
        const auto index = static_cast<unsigned>(control);
        if (index >= 5 || value > (index < 3 ? 1 : 100) || (control == Control::Brightness && value < 10)) return false;
        // The service rechecks ownership when consuming this intent.
        errors[index].store(Error::None);
        pending[index].store(0x100U | value);
        return true;
    }
};
Error apply(Control control, uint8_t value)
{
    switch (control)
    {
    case Control::Wifi:
        if (wifi_busy()) return Error::Busy;
        return wifi::set_user_enabled(value != 0) ? Error::None : Error::Apply;
    case Control::Lora:
        if (!radio_available()) return Error::Unsupported;
        if (reticulum_call::realtime_mode_active() || !::platform::radio::power_policy().request(value != 0)) return Error::Busy;
        settings_store::put_bool("settings", "radio_enabled", value != 0);
        return settings_store::get_bool("settings", "radio_enabled", value == 0) == (value != 0) ? Error::None : Error::Persist;
    case Control::Gps:
    {
        if (!device::gps_supported() || !app::hasAppFacade()) return Error::Unsupported;
        // Config edits and their live projections share the existing display boundary.
        if (!::platform::esp::boards::lockDisplay(50)) return Error::Busy;
        bool edited = false;
        {
            auto edit = app::configFacade().beginConfigEdit();
            if (edit)
            {
                edit.config().gps_enabled = value != 0;
                edit.commit(app::AppConfigChangeSet::gps());
                gps::set_enabled(value != 0);
                app::configFacade().applyPositionConfig();
                edited = true;
            }
        }
        ::platform::esp::boards::unlockDisplay();
        return edited ? Error::None : Error::Busy;
    }
    case Control::Volume:
    {
        auto* music = music_service();
        if (!music) return Error::Unsupported;
        ::music::Request request;
        request.action = ::music::Action::Volume;
        request.value = value;
        return music->send(request) ? Error::None : Error::Apply;
    }
    case Control::Brightness:
    {
        const auto maximum = device::screen_brightness_max();
        if (!device::supports_screen_brightness() || !maximum) return Error::Unsupported;
        const auto level = std::max<unsigned>(1, (value * maximum + 50U) / 100U);
        device::set_screen_brightness(level);
        settings_store::put_int("settings", "screen_brightness", level);
        return settings_store::get_int("settings", "screen_brightness", -1) == static_cast<int>(level) ? Error::None : Error::Persist;
    }
    }
    return Error::Unsupported;
}
} // namespace
::ui::controls::IQuickControls* quick_controls_source()
{
    static Source source;
    return &source;
}
void service_quick_controls()
{
    for (unsigned index = 0; index < 5; ++index)
    {
        if (!pending[index].load()) continue;
        applying[index].store(true);
        const auto request = pending[index].exchange(0);
        errors[index].store(apply(static_cast<Control>(index), request & 0xFFU));
        applying[index].store(false);
        return; // One user request per existing service iteration.
    }
}
} // namespace platform::ui
#else
namespace platform::ui
{
::ui::controls::IQuickControls* quick_controls_source() { return nullptr; }
void service_quick_controls() {}
} // namespace platform::ui
#endif
