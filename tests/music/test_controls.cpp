#include "app/app_facade_access.h"
#include "board/LoraBoard.h"
#include "music/service.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/idf_common/selected_radio.h"
#include "platform/radio/radio_power.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/quick_controls_runtime.h"
#include "platform/ui/wifi_access_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "ui_presentation/controls/quick_controls.h"
#include <cassert>
#include <cstring>
#include <map>
#include <string>
namespace fake
{
bool display_available = true, gps_enabled = true, call = false;
unsigned hardware_calls = 0, wifi_calls = 0, writes = 0;
uint8_t brightness = 7;
std::map<std::string, int> settings;
platform::ui::wifi::Status wifi;
platform::ui::wifi_access::RuntimeStatus access;
class Radio : public LoraBoard
{
    bool isRadioOnline() const override { return true; }
} radio;
class Music : public music::IService
{
  public:
    uint8_t level = 45;
    uint8_t volume() const override { return level; }
    void attach(uint8_t) override { assert(false); }
    void detach() override { assert(false); }
    bool snapshot(music::Snapshot&) override
    {
        assert(false);
        return false;
    }
    bool send(const music::Request& request) override
    {
        assert(request.action == music::Action::Volume);
        level = request.value;
        return true;
    }
    void request_artwork(uint16_t) override { assert(false); }
    music::Artwork* acquire_artwork() override
    {
        assert(false);
        return nullptr;
    }
    void request_lyrics(bool) override { assert(false); }
} music;
} // namespace fake
namespace platform::esp::boards
{
bool tryResolveAppContextInitHandles(AppContextInitHandles* out)
{
    out->lora_board = &fake::radio;
    return true;
}
bool lockDisplay(uint32_t) { return fake::display_available; }
void unlockDisplay() {}
} // namespace platform::esp::boards
namespace platform::ui
{
music::IService* music_service() { return &fake::music; }
namespace wifi
{
Status status() { return fake::wifi; }
bool set_user_enabled(bool enabled)
{
    ++fake::wifi_calls;
    ++fake::hardware_calls;
    fake::wifi.enabled = enabled;
    return true;
}
} // namespace wifi
namespace wifi_access
{
RuntimeStatus status() { return fake::access; }
} // namespace wifi_access
namespace reticulum_call
{
bool realtime_mode_active() { return fake::call; }
} // namespace reticulum_call
namespace gps
{
bool is_enabled() { return fake::gps_enabled; }
void set_enabled(bool enabled)
{
    ++fake::hardware_calls;
    fake::gps_enabled = enabled;
}
} // namespace gps
namespace device
{
bool gps_supported() { return true; }
bool supports_screen_brightness() { return true; }
uint8_t screen_brightness_max() { return 10; }
uint8_t screen_brightness() { return fake::brightness; }
void set_screen_brightness(uint8_t level)
{
    ++fake::hardware_calls;
    fake::brightness = level;
}
} // namespace device
namespace settings_store
{
void put_int(const char*, const char* key, int value)
{
    ++fake::writes;
    fake::settings[key] = value;
}
int get_int(const char*, const char* key, int fallback)
{
    const auto found = fake::settings.find(key);
    return found == fake::settings.end() ? fallback : found->second;
}
void put_bool(const char* ns, const char* key, bool value) { put_int(ns, key, value); }
bool get_bool(const char* ns, const char* key, bool fallback) { return get_int(ns, key, fallback) != 0; }
} // namespace settings_store
} // namespace platform::ui
int main()
{
    using namespace ui::controls;
    auto* source = platform::ui::quick_controls_source();
    Snapshot out;
    fake::wifi.supported = true;
    for (unsigned i = 0; i < 100; i++) assert(source->snapshot(out));
    assert(fake::hardware_calls == 0 && fake::writes == 0 && app::edits == 0);
    assert(out.lora == SwitchState::On && out.volume == 45);
    fake::access.ota_active = true;
    assert(source->set(Control::Wifi, 0));
    source->snapshot(out);
    assert(out.wifi == SwitchState::Changing);
    platform::ui::service_quick_controls();
    assert(fake::wifi_calls == 0);
    source->snapshot(out);
    assert(std::strstr(out.message, "in use"));
    fake::access.ota_active = false;
    assert(source->set(Control::Wifi, 1));
    platform::ui::service_quick_controls();
    assert(fake::wifi_calls == 1 && fake::wifi.enabled);
    auto& radio = platform::radio::power_policy();
    assert(radio.begin_exclusive());
    source->set(Control::Lora, 0);
    platform::ui::service_quick_controls();
    assert(radio.enabled() && !fake::settings.count("radio_enabled"));
    radio.end_exclusive();
    source->set(Control::Lora, 0);
    platform::ui::service_quick_controls();
    source->snapshot(out);
    assert(out.lora == SwitchState::Changing && out.lora_enabled);
    radio.acknowledge(false);
    // A disabled radio may be uninitialized after reboot. Enabling uses capability,
    // rather than requiring the very hardware initialization it is meant to start.
    platform::esp::idf_common::SelectedRadio::instance().online = false;
    source->snapshot(out);
    assert(out.lora == SwitchState::Off);
    source->set(Control::Lora, 1);
    platform::ui::service_quick_controls();
    assert(radio.enabled());
    fake::display_available = false;
    source->set(Control::Gps, 0);
    platform::ui::service_quick_controls();
    assert(fake::gps_enabled && app::edits == 0);
    fake::display_available = true;
    source->set(Control::Gps, 0);
    platform::ui::service_quick_controls();
    assert(!fake::gps_enabled && !app::config.gps_enabled && app::commits == 1 && app::projections == 1);
    const auto writes = fake::writes;
    source->set(Control::Brightness, 20);
    source->set(Control::Brightness, 50);
    source->set(Control::Brightness, 90);
    platform::ui::service_quick_controls();
    assert(fake::brightness == 9 && fake::writes == writes + 1);
    source->set(Control::Volume, 27);
    platform::ui::service_quick_controls();
    assert(fake::music.level == 27);
    assert(!source->set(Control::Brightness, 0) && !source->set(Control::Gps, 2));
}
