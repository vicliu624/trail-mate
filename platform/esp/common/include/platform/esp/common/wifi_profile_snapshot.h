#pragma once
#include "platform/ui/wifi_profile_snapshot.h"

namespace platform::esp::common
{
namespace wifi_snapshot_detail
{
struct Reader
{
    ::platform::ui::settings_store::ScalarReadResult read_bool_into(const char* ns, const char* key, bool* out)
    {
        return ::platform::ui::settings_store::read_bool_into(ns, key, out);
    }
    ::platform::ui::settings_store::ScalarReadResult read_int_into(const char* ns, const char* key, int32_t* out)
    {
        return ::platform::ui::settings_store::read_int_into(ns, key, out);
    }
    ::platform::ui::settings_store::StringReadResult read_string_into(const char* ns, const char* key, char* out, std::size_t capacity, std::size_t* length)
    {
        return ::platform::ui::settings_store::read_string_into(ns, key, out, capacity, length);
    }
};
} // namespace wifi_snapshot_detail
inline bool read_persisted_wifi_profiles(bool* enabled, std::size_t* count,
                                         ::platform::ui::wifi::Config* profiles,
                                         std::size_t capacity)
{
    wifi_snapshot_detail::Reader reader;
    return ::platform::ui::wifi::profile_snapshot::read_persisted_wifi_profiles(reader, enabled, count, profiles, capacity);
}
} // namespace platform::esp::common
