// Desktop-only NVS and runtime fixtures. Configuration emit/consume/apply,
// settings_store, Wi-Fi persistence and module codecs are production code.
#include "app/tms_config_codec.h"
#include "chat/domain/reticulum_network_validation.h"
#include "platform/ui/reticulum_network_config_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include <cstdio>
#include <cstring>

// Keep production diagnostic output outside the binary test bridge stream.
#define vprintf(fmt, args) vfprintf(stderr, fmt, args)
#include "platform/esp/common/settings_store_impl.h"
#undef vprintf
#include "platform/esp/common/wifi_profile_snapshot.h"

namespace platform::ui::screen
{
uint32_t timeout_ms() { return 30000; }
} // namespace platform::ui::screen
namespace platform::ui::device
{
uint8_t screen_brightness() { return 8; }
} // namespace platform::ui::device
namespace platform::ui::time
{
int timezone_profile_id() { return 0; }
int timezone_offset_min() { return 0; }
} // namespace platform::ui::time
namespace platform::ui::wifi
{
namespace
{
constexpr const char* kSettingsNs = "settings";
constexpr const char* kWifiEnabledKey = "wifi_enabled";
constexpr const char* kWifiSsidKey = "wifi_ssid";
constexpr const char* kWifiPasswordKey = "wifi_password";
constexpr const char* kWifiProfileCountKey = "wifi_prof_count";
constexpr auto kWifiProfileCapacity = kMaxSavedProfileCount;
struct Runtime
{
    Config profiles[kWifiProfileCapacity]{};
    Config config{};
    std::size_t profile_count = 0, next_profile_index = 0;
    bool config_cached = false, profiles_cached = false;
} s_runtime;
void profile_key(char* out, std::size_t size, const char* field, std::size_t index)
{
    std::snprintf(out, size, "wifi_%s_%u", field, static_cast<unsigned>(index));
}
bool has_saved_credentials(const Config& config) { return config.ssid[0] != 0; }
bool same_ssid(const Config& a, const Config& b) { return !std::strcmp(a.ssid, b.ssid); }
void clear_profiles()
{
    for (auto& profile : s_runtime.profiles) profile = {};
    s_runtime.profile_count = 0;
    s_runtime.profiles_cached = true;
}
void cache_config(const Config& config)
{
    s_runtime.config = config;
    s_runtime.config_cached = true;
}
void refresh_runtime_status_message() {}
#include "wifi_common_persist_actual.inc"
} // namespace
#include "wifi_common_replace_actual.inc"
#include "wifi_common_snapshot_actual.inc"
} // namespace platform::ui::wifi

namespace platform::ui::reticulum_network_config
{
namespace
{
using NetworkConfig = chat::reticulum::ReticulumNetworkConfig;
using InterfaceConfig = chat::reticulum::NetworkInterfaceConfig;
NetworkConfig fixture;
#include "complete_network_helpers_actual.inc"
#include "complete_network_validate_actual.inc"
} // namespace
bool validateForTms(const NetworkConfig& config) { return validate_config(config); }
bool setFromTms(const NetworkConfig& config)
{
    if (!validate_config(config)) return false;
    fixture = config;
    return true;
}
bool snapshotForTms(const chat::MeshConfig&, NetworkConfig* out)
{
    if (!out || !validate_config(fixture)) return false;
    *out = fixture;
    return true;
}
} // namespace platform::ui::reticulum_network_config

void resetCompleteConfigFixture()
{
    fake_nvs::values.clear();
    fake_nvs::handles.clear();
    fake_nvs::next = 1;
    fake_nvs::fail_commit = false;
    fake_nvs::open_error = 0;
    using namespace platform::ui::reticulum_network_config;
    fixture = NetworkConfig{};
    fixture.interface_count = 5;
    for (std::size_t i = 0; i < fixture.interface_count; ++i)
    {
        auto& entry = fixture.interfaces[i];
        std::snprintf(entry.id, sizeof(entry.id), "fixture-%u", static_cast<unsigned>(i));
        entry.type = i == 0 ? chat::reticulum::NetworkInterfaceType::IntegratedLoRa : i == 1 ? chat::reticulum::NetworkInterfaceType::Auto
                                                                                             : chat::reticulum::NetworkInterfaceType::TcpClient;
        entry.enabled = false;
        if (i >= 2) std::snprintf(entry.target_host, sizeof(entry.target_host), "fixture-%u.invalid", static_cast<unsigned>(i));
    }
    fake_nvs::values["settings"]["wifi_prof_count"] = int32_t{2};
    fake_nvs::values["settings"]["wifi_enabled"] = uint8_t{0};
    for (unsigned i = 0; i < 2; ++i)
    {
        fake_nvs::values["settings"]["wifi_ssid_" + std::to_string(i)] = "fixture-wifi-" + std::to_string(i);
        fake_nvs::values["settings"]["wifi_password_" + std::to_string(i)] = std::string("fixture-password");
    }
}
