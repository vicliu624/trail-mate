#include "platform/ui/auto_reply_settings.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <string>

namespace platform::ui::settings_store
{
static std::map<std::string, int64_t> values;
static unsigned writes = 0;
static unsigned reads = 0;
static unsigned fail_write = 0;
static unsigned fail_read = 0;
static unsigned other_writes = 0;
static unsigned fail_other_write = 0;
static std::string storage_key(const char* ns, const char* key)
{
    return std::string(ns) + "/" + key;
}
static void write(const char* ns, const char* key, int64_t value)
{
    if (++writes != fail_write) values[storage_key(ns, key)] = value;
}
static int64_t read(const char* ns, const char* key, int64_t fallback)
{
    const auto found = values.find(storage_key(ns, key));
    return ++reads == fail_read || found == values.end() ? fallback : found->second;
}
void put_int(const char* ns, const char* key, int value) { write(ns, key, value); }
void put_bool(const char* ns, const char* key, bool value) { write(ns, key, value); }
void put_uint(const char* ns, const char* key, uint32_t value) { write(ns, key, value); }
int get_int(const char* ns, const char* key, int fallback) { return static_cast<int>(read(ns, key, fallback)); }
bool get_bool(const char* ns, const char* key, bool fallback) { return read(ns, key, fallback) != 0; }
uint32_t get_uint(const char* ns, const char* key, uint32_t fallback) { return static_cast<uint32_t>(read(ns, key, fallback)); }
bool put_string(const char*, const char*, const char*)
{
    return ++other_writes != fail_other_write;
}
bool put_blob(const char*, const char*, const void*, std::size_t)
{
    return ++other_writes != fail_other_write;
}
} // namespace platform::ui::settings_store

namespace platform::ui::wifi
{
bool replace_saved_profiles(bool, const int*, uint8_t)
{
    return ++settings_store::other_writes != settings_store::fail_other_write;
}
} // namespace platform::ui::wifi
namespace platform::ui::reticulum_network_config
{
bool setFromTms(int)
{
    return ++settings_store::other_writes != settings_store::fail_other_write;
}
} // namespace platform::ui::reticulum_network_config
namespace ble_extension
{
static bool accepted = true;
bool finishDocument(bool, uint16_t) { return accepted; }
} // namespace ble_extension

constexpr const char* kSettingsNs = "settings";
constexpr const char* kPowerNs = "power";
constexpr uint32_t kRequiredBase = 3;
constexpr uint32_t SeenRadioEnabled = 1UL << 18U;
struct ScalarState
{
    bool valid() const { return (seen & kRequiredBase) == kRequiredBase; }
    uint32_t seen = kRequiredBase;
    uint64_t network_seen = 0;
    uint32_t cellular_seen = 0;
    uint32_t screen_timeout_ms = 30000;
    int32_t screen_brightness = 0;
    int32_t speaker_volume = 45;
    bool vibration_enabled = true;
    int32_t timezone_profile = -1;
    int32_t timezone_offset_minutes = -420;
    int32_t message_alerts = 0;
    int32_t contact_alerts = 2;
    bool auto_reply_enabled = false;
    bool debug_logs_enabled = false;
    uint32_t gauge_design_mah = 1500;
    uint32_t gauge_full_mah = 1400;
    char locale[48] = "en";
    char enabled_imes[192] = {};
    std::size_t enabled_imes_len = 0;
    char auto_reply_text[256] = {};
    std::size_t auto_reply_text_len = 0;
    uint8_t timezone_tzdef[192] = {};
    std::size_t timezone_tzdef_len = 0;
    bool wifi_enabled = false;
    bool radio_enabled = true;
    int wifi_profiles[1] = {};
    uint8_t wifi_profile_count = 0;
    int network = 0;
} s_state;
static ScalarState* s_state_storage = &s_state;
// Admission dependencies are accepted here; the test exercises the actual
// finish/apply control flow, not their independent validation implementations.
bool valid_screen_timeout(uint32_t) { return true; }
bool valid_timezone_offset_minutes(int32_t) { return true; }
bool valid_text_blob(const char*, std::size_t) { return true; }
bool valid_wifi_profile_set() { return true; }
bool valid_network_records(uint16_t) { return true; }

// Exact production function, extracted by CMake. The fake represents NVS
// write/read failures, not an alternate copy of the implementation.
#include "tms_scalar_storage_actual.inc"

#include "tms_finish_actual.inc"

int main()
{
    namespace store = platform::ui::settings_store;
    assert(write_base_scalar_settings());
    assert(store::writes == 12 && store::reads == 12 && store::values.size() == 12);
    assert(store::values.at("settings/timezone_profile") == -1);
    assert(store::values.at("settings/timezone_offset") == -420);
    assert(store::values.at("power/gauge_full_mah") == 1400);
    for (unsigned index = 1; index <= 12; ++index)
    {
        store::values.clear();
        store::writes = store::reads = store::fail_read = 0;
        store::fail_write = index;
        assert(!write_base_scalar_settings());
        assert(store::writes == index && store::reads == index);
        assert(store::values.size() == index - 1); // Stop at failure.

        store::values.clear();
        store::writes = store::reads = store::fail_write = 0;
        store::fail_read = index;
        assert(!write_base_scalar_settings());
        assert(store::writes == index && store::reads == index);
    }
    store::writes = store::reads = store::fail_read = store::fail_write = 0;
    assert(write_base_scalar_settings());
    // A dropped write is acceptable only if durable storage already matches.
    for (unsigned index = 1; index <= 12; ++index)
    {
        store::writes = store::reads = 0;
        store::fail_write = index;
        assert(write_base_scalar_settings());
        assert(store::writes == 12 && store::reads == 12);
    }
    store::fail_write = 0;
    // Unequal fallback remains valid at scalar extrema, zero and both bools.
    s_state.screen_timeout_ms = UINT32_MAX;
    s_state.screen_brightness = INT32_MIN;
    s_state.speaker_volume = INT32_MAX;
    s_state.timezone_profile = 0;
    s_state.timezone_offset_minutes = 0;
    s_state.vibration_enabled = false;
    s_state.auto_reply_enabled = true;
    s_state.debug_logs_enabled = true;
    assert(write_base_scalar_settings());
    store::values.clear();
    store::writes = store::reads = 0;
    store::fail_write = 1;
    assert(!write_base_scalar_settings());

    s_state = ScalarState{};
    store::values.clear();
    store::writes = store::reads = store::fail_write = store::other_writes = 0;
    assert(finishDocument(nullptr, false, 7));
    assert(store::writes == 0 && store::reads == 0 && store::other_writes == 0);
    for (unsigned index = 1; index <= 12; ++index)
    {
        for (bool failed_read : {false, true})
        {
            store::values.clear();
            store::writes = store::reads = store::other_writes = 0;
            store::fail_write = failed_read ? 0 : index;
            store::fail_read = failed_read ? index : 0;
            assert(!finishDocument(nullptr, true, 7));
            assert(store::writes == index && store::reads == index);
            assert(store::other_writes == 0); // No later strings/profiles/network.
        }
    }
    store::values.clear();
    store::writes = store::reads = store::fail_write = store::fail_read = store::other_writes = 0;
    s_state.network_seen = 1;
    assert(finishDocument(nullptr, true, 7));
    assert(store::writes == 12 && store::reads == 12 && store::other_writes == 6);
    for (unsigned index = 1; index <= 6; ++index)
    {
        store::writes = store::reads = store::other_writes = 0;
        store::fail_other_write = index;
        assert(!finishDocument(nullptr, true, 7));
        assert(store::other_writes == index); // Wi-Fi failure cannot apply Reticulum settings.
    }
    store::fail_other_write = 0;
    store::writes = store::reads = store::other_writes = 0;
    ble_extension::accepted = false;
    assert(!finishDocument(nullptr, true, 7));
    assert(store::writes == 0 && store::reads == 0 && store::other_writes == 0);
    ble_extension::accepted = true;
    s_state.seen = 0;
    assert(!finishDocument(nullptr, true, 7));
    assert(store::writes == 0 && store::reads == 0 && store::other_writes == 0);
    assert(finishDocument(nullptr, false, 2) == false); // Network record makes it non-legacy.
    s_state.network_seen = 0;
    assert(finishDocument(nullptr, true, 2)); // Legacy omission retains existing settings.
    assert(store::writes == 0 && store::reads == 0 && store::other_writes == 0);
    s_state = ScalarState{};
    s_state.seen |= SeenRadioEnabled;
    s_state.radio_enabled = false;
    store::values.clear();
    store::writes = store::reads = 0;
    assert(write_base_scalar_settings());
    assert(store::values.at("settings/radio_enabled") == 0);
    assert(store::writes == 13 && store::reads == 13);
    s_state.seen &= ~SeenRadioEnabled;
    s_state.radio_enabled = true;
    assert(write_base_scalar_settings());
    assert(store::values.at("settings/radio_enabled") == 0); // Old documents preserve the current preference.
    s_state_storage = nullptr;
    assert(!finishDocument(nullptr, true, 7));
}
