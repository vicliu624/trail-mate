#include "platform/linux/settings_transaction.h"
#include "platform/ui/wifi_profile_snapshot.h"
#include "platform/ui/wifi_runtime.h"

#include <array>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace platform::ui::wifi
{
namespace
{
constexpr const char* kNamespace = "settings";
std::mutex profile_mutex;
struct Snapshot
{
    std::array<Config, kMaxSavedProfileCount> profiles{};
    Config primary{};
    bool enabled = false;
    size_t count = 0;
};
bool read_snapshot(Snapshot& output)
{
    output = Snapshot{};
    const bool ok = ::platform::linux_runtime::read_settings_snapshot(
        [](void* context, const ::platform::linux_runtime::SettingsReadView& reader)
        {
            auto& state = *static_cast<Snapshot*>(context);
            // Linux networking owns startup state; an absent app preference
            // retains the existing desktop connection's enabled default.
            return profile_snapshot::read_persisted_wifi_profiles(reader, &state.enabled, &state.count,
                                                                  state.profiles.data(), state.profiles.size(), true, &state.primary);
        },
        &output);
    if (!ok) output = Snapshot{};
    return ok;
}
bool valid(const Config& config, bool require_ssid)
{
    return std::memchr(config.ssid, 0, sizeof(config.ssid)) &&
           std::memchr(config.password, 0, sizeof(config.password)) && (!require_ssid || config.ssid[0]);
}
bool write_snapshot(const Snapshot& state)
{
    using Mutation = ::platform::linux_runtime::SettingsMutation;
    std::vector<Mutation> changes;
    changes.reserve(3 * kMaxSavedProfileCount + 4);
    changes.push_back({"wifi_enabled", 'b', state.enabled ? "1" : "0"});
    changes.push_back({"wifi_prof_count", 'i', std::to_string(state.count)});
    for (size_t i = 0; i < kMaxSavedProfileCount; ++i)
    {
        const auto suffix = std::to_string(i);
        const bool erase = i >= state.count;
        changes.push_back({"wifi_ssid_" + suffix, 's', erase ? "" : state.profiles[i].ssid, erase});
        changes.push_back({"wifi_password_" + suffix, 's', erase ? "" : state.profiles[i].password, erase});
        changes.push_back({"wifi_pass_" + suffix, 's', "", true});
    }
    changes.push_back({"wifi_ssid", 's', state.primary.ssid});
    changes.push_back({"wifi_password", 's', state.primary.password});
    return ::platform::linux_runtime::write_settings_batch(kNamespace, changes.data(), changes.size());
}
} // namespace

bool load_config(Config& output)
{
    output = Config{};
    Snapshot state;
    if (!read_snapshot(state)) return false;
    output = !state.primary.ssid[0] && state.count ? state.profiles[0] : state.primary;
    output.enabled = state.enabled;
    return true;
}
bool save_config(const Config& config)
{
    if (!valid(config, false)) return false;
    std::lock_guard<std::mutex> lock(profile_mutex);
    Snapshot state;
    if (!read_snapshot(state)) return false;
    if (config.ssid[0])
    {
        size_t existing = 0;
        while (existing < state.count && std::strcmp(state.profiles[existing].ssid, config.ssid)) ++existing;
        size_t last = existing;
        if (existing == state.count)
        {
            if (state.count < state.profiles.size()) ++state.count;
            last = state.count - 1;
        }
        for (size_t i = last; i > 0; --i) state.profiles[i] = state.profiles[i - 1];
        state.profiles[0] = config;
        state.profiles[0].enabled = true;
    }
    state.primary = config;
    state.enabled = config.enabled;
    return write_snapshot(state);
}
bool find_saved_config(const char* ssid, Config& output)
{
    output = Config{};
    if (!ssid || !*ssid) return false;
    Snapshot state;
    if (!read_snapshot(state)) return false;
    for (size_t i = 0; i < state.count; ++i)
        if (!std::strcmp(state.profiles[i].ssid, ssid))
        {
            output = state.profiles[i];
            output.enabled = state.enabled;
            return true;
        }
    return false;
}
bool visit_saved_profiles(bool* enabled, size_t* count, SavedProfileVisitor visitor, void* context)
{
    if (enabled) *enabled = false;
    if (count) *count = 0;
    if (!enabled || !count || !visitor) return false;
    Snapshot state;
    if (!read_snapshot(state)) return false;
    *enabled = state.enabled;
    *count = state.count;
    // External visitors run after the read transaction and store lock end.
    for (size_t i = 0; i < state.count; ++i)
        if (!visitor(context, i, state.profiles[i])) return false;
    return true;
}
bool read_saved_profiles_into(bool* enabled, size_t* count, Config* profiles, size_t capacity)
{
    const auto clear = [&]
    {
        if (enabled) *enabled = false;
        if (count) *count = 0;
        if (profiles)
            for (size_t i = 0; i < capacity && i < kMaxSavedProfileCount; ++i) profiles[i] = Config{};
    };
    clear();
    if (!enabled || !count || !profiles || capacity < kMaxSavedProfileCount) return false;
    Snapshot state;
    if (!read_snapshot(state)) return false;
    *enabled = state.enabled;
    *count = state.count;
    for (size_t i = 0; i < state.profiles.size(); ++i) profiles[i] = state.profiles[i];
    return true;
}
bool replace_saved_profiles(bool enabled, const Config* profiles, size_t count)
{
    if (count > kMaxSavedProfileCount || (count && !profiles)) return false;
    for (size_t i = 0; i < count; ++i)
    {
        if (!valid(profiles[i], true)) return false;
        for (size_t j = 0; j < i; ++j)
            if (!std::strcmp(profiles[i].ssid, profiles[j].ssid)) return false;
    }
    std::lock_guard<std::mutex> lock(profile_mutex);
    Snapshot state;
    state.enabled = enabled;
    state.count = count;
    for (size_t i = 0; i < count; ++i)
    {
        state.profiles[i] = profiles[i];
        state.profiles[i].enabled = true;
    }
    if (count) state.primary = state.profiles[0];
    state.primary.enabled = enabled;
    return write_snapshot(state);
}
} // namespace platform::ui::wifi
