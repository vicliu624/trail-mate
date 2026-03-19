#pragma once

#include <Preferences.h>
#include <array>
#include <cstddef>
#include <cstdint>

#include "chat/domain/chat_types.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif
#if __has_include("esp_efuse.h")
#include "esp_efuse.h"
#endif

namespace platform::esp::arduino_common::device_identity
{

inline bool loadBleEnabledPreference()
{
    Preferences prefs;
    if (!prefs.begin("settings", true))
    {
        return true;
    }
    const bool enabled = prefs.getBool("ble_enabled", true);
    prefs.end();
    return enabled;
}

inline chat::NodeId getSelfNodeId()
{
    static chat::NodeId cached_id = 0;
    if (cached_id != 0)
    {
        return cached_id;
    }

    uint8_t mac_bytes[6] = {};
#if __has_include(<Arduino.h>)
    const uint64_t raw = ESP.getEfuseMac();
    const uint8_t* raw_bytes = reinterpret_cast<const uint8_t*>(&raw);
    for (size_t i = 0; i < 6; ++i)
    {
        mac_bytes[i] = raw_bytes[i];
    }
#else
    (void)esp_efuse_mac_get_default(mac_bytes);
#endif

    uint32_t node_id = (static_cast<uint32_t>(mac_bytes[2]) << 24) |
                       (static_cast<uint32_t>(mac_bytes[3]) << 16) |
                       (static_cast<uint32_t>(mac_bytes[4]) << 8) |
                       static_cast<uint32_t>(mac_bytes[5]);
    if (node_id == 0)
    {
        node_id = 1;
    }

    Preferences prefs;
    if (prefs.begin("chat", false))
    {
        if (prefs.getUInt("node_id", 0) != node_id)
        {
            prefs.putUInt("node_id", node_id);
        }
        prefs.end();
    }

    cached_id = node_id;
    return cached_id;
}

inline std::array<uint8_t, 6> getSelfMacAddress()
{
    std::array<uint8_t, 6> mac_bytes{};
#if __has_include(<Arduino.h>)
    const uint64_t raw = ESP.getEfuseMac();
    const uint8_t* raw_bytes = reinterpret_cast<const uint8_t*>(&raw);
    for (size_t i = 0; i < mac_bytes.size(); ++i)
    {
        mac_bytes[i] = raw_bytes[i];
    }
#else
    (void)esp_efuse_mac_get_default(mac_bytes.data());
#endif
    return mac_bytes;
}

} // namespace platform::esp::arduino_common::device_identity
