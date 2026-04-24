#pragma once

#include "app/app_config.h"
#include "chat/ble/phone_runtime_context.h"

#include <cstddef>
#include <cstring>

namespace platform
{
namespace shared
{
namespace ble_bridge
{

inline void copyBounded(char* dst, std::size_t dst_len, const char* src)
{
    if (!dst || dst_len == 0)
    {
        return;
    }
    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    std::strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

inline ::ble::MeshtasticPhoneConfigSnapshot makeMeshtasticPhoneConfigSnapshot(const app::AppConfig& cfg)
{
    ::ble::MeshtasticPhoneConfigSnapshot snapshot{};
    snapshot.mesh = cfg.meshtastic_config;
    snapshot.relay_enabled = cfg.chat_policy.enable_relay;
    copyBounded(snapshot.node_name, sizeof(snapshot.node_name), cfg.node_name);
    copyBounded(snapshot.short_name, sizeof(snapshot.short_name), cfg.short_name);
    snapshot.primary_enabled = cfg.primary_enabled;
    snapshot.secondary_enabled = cfg.secondary_enabled;
    snapshot.primary_uplink_enabled = cfg.primary_uplink_enabled;
    snapshot.primary_downlink_enabled = cfg.primary_downlink_enabled;
    snapshot.secondary_uplink_enabled = cfg.secondary_uplink_enabled;
    snapshot.secondary_downlink_enabled = cfg.secondary_downlink_enabled;
    snapshot.gps_mode = cfg.gps_mode;
    snapshot.gps_interval_ms = cfg.gps_interval_ms;
    snapshot.gps_strategy = cfg.gps_strategy;
    return snapshot;
}

inline void applyMeshtasticPhoneConfigSnapshot(app::AppConfig& cfg,
                                               const ::ble::MeshtasticPhoneConfigSnapshot& snapshot)
{
    cfg.meshtastic_config = snapshot.mesh;
    cfg.chat_policy.enable_relay = snapshot.relay_enabled;
    copyBounded(cfg.node_name, sizeof(cfg.node_name), snapshot.node_name);
    copyBounded(cfg.short_name, sizeof(cfg.short_name), snapshot.short_name);
    cfg.primary_enabled = snapshot.primary_enabled;
    cfg.secondary_enabled = snapshot.secondary_enabled;
    cfg.primary_uplink_enabled = snapshot.primary_uplink_enabled;
    cfg.primary_downlink_enabled = snapshot.primary_downlink_enabled;
    cfg.secondary_uplink_enabled = snapshot.secondary_uplink_enabled;
    cfg.secondary_downlink_enabled = snapshot.secondary_downlink_enabled;
    cfg.gps_mode = snapshot.gps_mode;
    cfg.gps_interval_ms = snapshot.gps_interval_ms;
    cfg.gps_strategy = snapshot.gps_strategy;
}

inline ::ble::MeshCorePhoneConfigSnapshot makeMeshCorePhoneConfigSnapshot(const app::AppConfig& cfg)
{
    ::ble::MeshCorePhoneConfigSnapshot snapshot{};
    snapshot.active_protocol = cfg.mesh_protocol;
    snapshot.mesh = cfg.meshcore_config;
    copyBounded(snapshot.node_name, sizeof(snapshot.node_name), cfg.node_name);
    copyBounded(snapshot.short_name, sizeof(snapshot.short_name), cfg.short_name);
    snapshot.tx_power_min_dbm = app::AppConfig::kTxPowerMinDbm;
    snapshot.tx_power_max_dbm = app::AppConfig::kTxPowerMaxDbm;
    return snapshot;
}

inline void applyMeshCorePhoneConfigSnapshot(app::AppConfig& cfg,
                                             const ::ble::MeshCorePhoneConfigSnapshot& snapshot)
{
    cfg.mesh_protocol = snapshot.active_protocol;
    cfg.meshcore_config = snapshot.mesh;
    copyBounded(cfg.node_name, sizeof(cfg.node_name), snapshot.node_name);
    copyBounded(cfg.short_name, sizeof(cfg.short_name), snapshot.short_name);
}

} // namespace ble_bridge
} // namespace shared
} // namespace platform
