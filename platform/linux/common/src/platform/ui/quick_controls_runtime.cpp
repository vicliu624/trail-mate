#include "platform/ui/quick_controls_runtime.h"
#include "app/app_facade_access.h"
#include "music/service.h"
#include "platform/linux/backlight.h"
#include "platform/linux/music_service.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/radio_power.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/music_runtime.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/wifi_runtime.h"
#include "ui_presentation/controls/quick_controls.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace platform::ui
{
namespace
{
using namespace ::ui::controls;
class Source final : public IQuickControls
{
  public:
    bool snapshot(Snapshot& output) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        output = published_;
        const auto radio_power = ::platform::linux_runtime::radio_power_snapshot();
        output.lora_enabled = radio_power.enabled;
        output.lora = !radio_power.available ? SwitchState::Unavailable : radio_power.changing ? SwitchState::Changing
                                                                      : radio_power.failed     ? SwitchState::Failed
                                                                      : radio_power.enabled    ? SwitchState::On
                                                                                               : SwitchState::Off;
        if (radio_power.failed) std::snprintf(output.message, sizeof(output.message), "%s", radio_power.error.c_str());
        SwitchState* switches[] = {&output.wifi, &output.lora, &output.gps};
        for (unsigned index = 0; index < 3; ++index)
            if (pending_[index] && *switches[index] != SwitchState::Unavailable) *switches[index] = SwitchState::Changing;
        return true;
    }
    bool set(Control control, uint8_t value) override
    {
        const auto index = static_cast<unsigned>(control);
        if (index >= pending_.size() || value > (index < 3 ? 1 : 100) ||
            (control == Control::Brightness && value < 10)) return false;
        pending_[index] = 0x100U | value;
        return true;
    }
    void service()
    {
        ::platform::linux_runtime::prepare_backlight();
        bool applied = false;
        for (unsigned index = 0; index < pending_.size(); ++index)
        {
            const auto intent = pending_[index].exchange(0);
            if (!intent) continue;
            errors_[index] = !apply(static_cast<Control>(index), intent & 0xffU);
            applied = true;
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!applied && now < refresh_at_) return;
        refresh_at_ = now + std::chrono::milliseconds(200);
        const auto network = wifi::status();
        Snapshot next;
        next.wifi_enabled = network.enabled;
        next.wifi = !network.supported ? SwitchState::Unavailable : network.enabled ? SwitchState::On
                                                                                    : SwitchState::Off;
        const auto radio_power = ::platform::linux_runtime::radio_power_snapshot();
        next.lora_enabled = radio_power.enabled;
        next.lora = !radio_power.available ? SwitchState::Unavailable : radio_power.changing ? SwitchState::Changing
                                                                    : radio_power.failed     ? SwitchState::Failed
                                                                    : radio_power.enabled    ? SwitchState::On
                                                                                             : SwitchState::Off;
        next.gps_enabled = device::gps_supported() && gps::is_enabled();
        next.gps = !device::gps_supported() ? SwitchState::Unavailable : next.gps_enabled ? SwitchState::On
                                                                                          : SwitchState::Off;
        if (auto* player = music_service())
        {
            if (!media_primed_)
            {
                ::platform::linux_runtime::prepare_linux_music();
                media_primed_ = true;
            }
            ::music::Snapshot music;
            player->snapshot(music);
            next.volume_supported = music.audio_supported;
            next.volume = player->volume();
        }
        const auto maximum = device::screen_brightness_max();
        next.brightness_supported = device::supports_screen_brightness() && maximum;
        next.brightness = maximum ? std::clamp<unsigned>(device::screen_brightness() * 100U / maximum, 10, 100) : 0;
        const auto backlight = ::platform::linux_runtime::backlight_snapshot();
        if (backlight.failed) std::snprintf(next.message, sizeof(next.message), "Display brightness change failed");
        else if (backlight.selection_required) std::snprintf(next.message, sizeof(next.message), "Select a display backlight in the device profile");
        constexpr const char* names[] = {"Wi-Fi", "LoRa", "GPS", "Volume", "Brightness"};
        if (radio_power.failed) std::snprintf(next.message, sizeof(next.message), "%s", radio_power.error.c_str());
        SwitchState* switches[] = {&next.wifi, &next.lora, &next.gps};
        for (unsigned index = 0; index < errors_.size(); ++index)
        {
            if (!errors_[index]) continue;
            std::snprintf(next.message, sizeof(next.message), "%s: change failed or resource in use", names[index]);
            if (index < 3 && *switches[index] != SwitchState::Unavailable) *switches[index] = SwitchState::Failed;
            break;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        published_ = next;
    }

  private:
    bool apply(Control control, uint8_t value)
    {
        switch (control)
        {
        case Control::Wifi:
            return wifi::set_user_enabled(value != 0);
        case Control::Lora:
            return ::platform::linux_runtime::request_radio_enabled(value != 0);
        case Control::Gps:
        {
            if (!device::gps_supported() || !app::hasAppFacade()) return false;
            auto edit = app::configFacade().beginConfigEdit();
            if (!edit) return false;
            edit.config().gps_enabled = value != 0;
            edit.commit(app::AppConfigChangeSet::gps());
            gps::set_enabled(value != 0);
            app::configFacade().applyPositionConfig();
            return true;
        }
        case Control::Volume:
        {
            auto* player = music_service();
            if (!player) return false;
            ::music::Request request;
            request.action = ::music::Action::Volume;
            request.value = value;
            return player->send(request);
        }
        case Control::Brightness:
        {
            const auto maximum = device::screen_brightness_max();
            if (!device::supports_screen_brightness() || !maximum) return false;
            const auto level = static_cast<uint8_t>(std::max<unsigned>(1, (value * maximum + 50U) / 100U));
            device::set_screen_brightness(level);
            settings_store::put_int("settings", "screen_brightness", level);
            return true;
        }
        }
        return false;
    }
    std::mutex mutex_;
    Snapshot published_;
    std::array<std::atomic<uint32_t>, 5> pending_{};
    std::array<bool, 5> errors_{};
    std::chrono::steady_clock::time_point refresh_at_{};
    bool media_primed_ = false;
};
Source& source()
{
    static Source value;
    return value;
}
} // namespace
::ui::controls::IQuickControls* quick_controls_source() { return &source(); }
void service_quick_controls() { source().service(); }
} // namespace platform::ui
