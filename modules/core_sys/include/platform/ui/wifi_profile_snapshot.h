#pragma once
#include "platform/ui/settings_store.h"
#include "platform/ui/wifi_runtime.h"
#include <cstdio>
#include <cstring>

namespace platform::ui::wifi::profile_snapshot
{
// Platform-neutral persistent-set admission. Reader supplies strict settings
// methods; platforms own snapshot consistency, allocation and persistence.
template <class Reader>
inline bool read_persisted_wifi_profiles(Reader& reader, bool* enabled, std::size_t* count,
                                         ::platform::ui::wifi::Config* profiles,
                                         std::size_t capacity, bool default_enabled = false,
                                         Config* primary_out = nullptr)
{
    namespace store = ::platform::ui::settings_store;
    using Scalar = store::ScalarReadResult;
    using Text = store::StringReadResult;
    constexpr auto max_count = ::platform::ui::wifi::kMaxSavedProfileCount;
    static_assert(sizeof(::platform::ui::wifi::Config) <= 100U,
                  "Wi-Fi snapshot primary scratch must stay small");
    if (primary_out) *primary_out = {};
    if (enabled) *enabled = false;
    if (count) *count = 0;
    if (profiles)
        for (std::size_t i = 0; i < capacity && i < max_count; ++i) profiles[i] = {};
    if (!enabled || !count || !profiles || capacity < max_count) return false;
    const auto fail = [&]()
    {
        if (primary_out) *primary_out = {};
        *enabled = false;
        *count = 0;
        for (std::size_t i = 0; i < max_count; ++i) profiles[i] = {};
        return false;
    };
    bool stored_enabled = false;
    auto scalar_result = reader.read_bool_into("settings", "wifi_enabled", &stored_enabled);
    if (scalar_result != Scalar::Ok && scalar_result != Scalar::Missing) return fail();
    if (scalar_result == Scalar::Missing) stored_enabled = default_enabled;
    int32_t stored_count = 0;
    scalar_result = reader.read_int_into("settings", "wifi_prof_count", &stored_count);
    if ((scalar_result != Scalar::Ok && scalar_result != Scalar::Missing) ||
        stored_count < 0 || stored_count > static_cast<int32_t>(max_count)) return fail();

    const auto read_text = [&reader](const char* key, char* out, std::size_t bytes)
    {
        std::size_t size = 0;
        return reader.read_string_into("settings", key, out, bytes, &size);
    };
    const bool recovering = stored_count == 0;
    const auto slots = recovering ? max_count : static_cast<std::size_t>(stored_count);
    std::size_t loaded = 0;
    for (std::size_t slot = 0; slot < slots; ++slot)
    {
        auto& profile = profiles[loaded];
        profile = {};
        char key[25]{};
        std::snprintf(key, sizeof(key), "wifi_ssid_%u", static_cast<unsigned>(slot));
        const auto ssid_result = read_text(key, profile.ssid, sizeof(profile.ssid));
        if (ssid_result != Text::Ok && ssid_result != Text::Missing) return fail();
        if (ssid_result == Text::Missing || profile.ssid[0] == '\0')
        {
            if (!recovering) return fail();
            continue;
        }
        bool duplicate = false;
        for (std::size_t previous = 0; previous < loaded; ++previous)
        {
            // An explicit count promises a complete set. Duplicate slots cannot
            // silently disappear into a shorter exported list.
            if (!std::strcmp(profiles[previous].ssid, profile.ssid)) duplicate = true;
        }
        if (duplicate && !recovering) return fail();
        std::snprintf(key, sizeof(key), "wifi_password_%u", static_cast<unsigned>(slot));
        auto password_result = read_text(key, profile.password, sizeof(profile.password));
        if (password_result == Text::Missing)
        {
            std::snprintf(key, sizeof(key), "wifi_pass_%u", static_cast<unsigned>(slot));
            password_result = read_text(key, profile.password, sizeof(profile.password));
        }
        if (password_result != Text::Ok && password_result != Text::Missing) return fail();
        if (duplicate) continue; // Legacy recovery retains the first priority.
        profile.enabled = true;
        ++loaded;
    }

    // Older firmware also used one primary pair. Preserve its fallback and
    // appended-profile semantics without letting its mirror override a slot.
    ::platform::ui::wifi::Config primary{};
    const auto ssid_result = read_text("wifi_ssid", primary.ssid, sizeof(primary.ssid));
    const auto password_result = read_text("wifi_password", primary.password, sizeof(primary.password));
    if ((ssid_result != Text::Ok && ssid_result != Text::Missing) ||
        (password_result != Text::Ok && password_result != Text::Missing)) return fail();
    if (primary.ssid[0] != '\0')
    {
        bool found = false;
        for (std::size_t i = 0; i < loaded; ++i)
            if (!std::strcmp(profiles[i].ssid, primary.ssid)) found = true;
        if (!found)
        {
            if (loaded == max_count) return fail();
            primary.enabled = true;
            profiles[loaded++] = primary;
        }
    }
    *enabled = stored_enabled;
    *count = loaded;
    if (primary_out)
    {
        *primary_out = primary;
        primary_out->enabled = stored_enabled;
    }
    for (std::size_t i = loaded; i < max_count; ++i) profiles[i] = {};
    return true;
}

} // namespace platform::ui::wifi::profile_snapshot
