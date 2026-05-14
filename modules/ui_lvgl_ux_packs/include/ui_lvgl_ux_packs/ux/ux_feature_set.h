#pragma once

namespace ui_lvgl_ux
{

struct UxFeatureSet
{
    constexpr UxFeatureSet() = default;

    constexpr UxFeatureSet(bool chat_enabled,
                           bool contacts_enabled,
                           bool map_enabled,
                           bool gps_enabled,
                           bool team_enabled,
                           bool tracker_enabled,
                           bool settings_enabled,
                           bool walkie_enabled,
                           bool sstv_enabled,
                           bool extensions_enabled)
        : chat(chat_enabled),
          contacts(contacts_enabled),
          map(map_enabled),
          gps(gps_enabled),
          team(team_enabled),
          tracker(tracker_enabled),
          settings(settings_enabled),
          walkie(walkie_enabled),
          sstv(sstv_enabled),
          extensions(extensions_enabled)
    {
    }

    bool chat = true;
    bool contacts = true;
    bool map = true;
    bool gps = true;
    bool team = true;
    bool tracker = true;
    bool settings = true;
    bool walkie = false;
    bool sstv = false;
    bool extensions = false;
};

} // namespace ui_lvgl_ux
