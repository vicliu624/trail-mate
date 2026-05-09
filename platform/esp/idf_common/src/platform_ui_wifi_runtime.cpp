#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(TRAIL_MATE_ESP_BOARD_TAB5)

#include "platform/ui/wifi_runtime.h"

#include <cstdio>
#include <cstring>

#include "platform/ui/settings_store.h"

namespace platform::ui::wifi
{
namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kWifiEnabledKey = "wifi_enabled";
constexpr const char* kWifiSsidKey = "wifi_ssid";
constexpr const char* kWifiPasswordKey = "wifi_password";
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
constexpr const char* kUnsupportedMessage =
    "Wi-Fi is handled by the external C6 firmware on T-Display-P4";
#else
constexpr const char* kUnsupportedMessage =
    "Wi-Fi is not wired into the current Tab5 ESP-IDF target";
#endif

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

} // namespace

bool is_supported()
{
    return false;
}

bool load_config(Config& out)
{
    out = Config{};
    out.enabled = ::platform::ui::settings_store::get_bool(kSettingsNs, kWifiEnabledKey, false);

    std::string value;
    if (::platform::ui::settings_store::get_string(kSettingsNs, kWifiSsidKey, value))
    {
        copy_text(out.ssid, sizeof(out.ssid), value.c_str());
    }

    value.clear();
    if (::platform::ui::settings_store::get_string(kSettingsNs, kWifiPasswordKey, value))
    {
        copy_text(out.password, sizeof(out.password), value.c_str());
    }

    return true;
}

bool save_config(const Config& config)
{
    const bool ssid_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiSsidKey, config.ssid);
    const bool password_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiPasswordKey, config.password);
    ::platform::ui::settings_store::put_bool(kSettingsNs, kWifiEnabledKey, config.enabled);
    return ssid_ok && password_ok;
}

bool apply_enabled(bool enabled)
{
    return !enabled;
}

bool connect(const Config* override_config)
{
    (void)override_config;
    return false;
}

void disconnect() {}

bool scan(std::vector<ScanResult>& out_results)
{
    out_results.clear();
    return false;
}

Status status()
{
    Status out{};
    out.supported = false;
    out.enabled = false;
    out.connected = false;
    out.scanning = false;
    out.has_credentials = false;
    out.rssi = -127;
    out.state = ConnectionState::Unsupported;
    copy_text(out.message, sizeof(out.message), kUnsupportedMessage);
    return out;
}

} // namespace platform::ui::wifi

#else

#include "platform/esp/common/wifi_runtime_impl.h"

#endif
