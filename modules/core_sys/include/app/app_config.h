/**
 * @file app_config.h
 * @brief Application configuration
 */

#pragma once

#include "chat/domain/chat_policy.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "gps/domain/motion_config.h"
#include <cstdint>
#include <cstring>

namespace app
{

struct AprsConfig
{
    bool enabled;
    char igate_callsign[16];
    uint8_t igate_ssid;
    char tocall[16];
    char path[64];
    uint16_t tx_min_interval_s;
    uint16_t dedupe_window_s;
    char symbol_table;
    char symbol_code;
    uint16_t position_interval_s;
    uint8_t node_map_len;
    uint8_t node_map[255];
    bool self_enable;
    char self_callsign[16];

    AprsConfig()
        : enabled(false),
          igate_ssid(0),
          tx_min_interval_s(0),
          dedupe_window_s(0),
          symbol_table(0),
          symbol_code(0),
          position_interval_s(0),
          node_map_len(0),
          self_enable(false)
    {
        igate_callsign[0] = '\0';
        tocall[0] = '\0';
        path[0] = '\0';
        self_callsign[0] = '\0';
        memset(node_map, 0, sizeof(node_map));
    }
};

/**
 * @brief Application configuration
 */
struct AppConfig
{
    static constexpr uint8_t kDefaultRegionCode = 4; // Meshtastic CN
    static constexpr float kMeshCoreDefaultFreqMHz = 869.525f;
    static constexpr float kMeshCoreDefaultBwKHz = 250.0f;
    static constexpr uint8_t kMeshCoreDefaultSf = 11;
    static constexpr uint8_t kMeshCoreDefaultCr = 5;
    static constexpr int8_t kMeshCoreDefaultTxPowerDbm = 20;
    static constexpr float kRNodeDefaultFreqMHz = 869.525f;
    static constexpr float kRNodeDefaultBwKHz = 125.0f;
    static constexpr uint8_t kRNodeDefaultSf = 9;
    static constexpr uint8_t kRNodeDefaultCr = 5;
    static constexpr int8_t kRNodeDefaultTxPowerDbm = 17;
    static constexpr int8_t kTxPowerMinDbm = -9;
#if defined(TRAIL_MATE_LORA_TX_POWER_MAX_DBM)
    // Board/module capability must be declared per build target.
    // Examples: SX1262 22S => 22 dBm, 30S => 30 dBm, 33S => 33 dBm.
    static constexpr int8_t kTxPowerMaxDbm = TRAIL_MATE_LORA_TX_POWER_MAX_DBM;
#elif defined(__INTELLISENSE__)
    // Keep IDE parsing usable when build flags are not loaded.
    static constexpr int8_t kTxPowerMaxDbm = 22;
#else
#error "Define TRAIL_MATE_LORA_TX_POWER_MAX_DBM for this build target."
#endif
    // Chat settings
    chat::ChatPolicy chat_policy;
    chat::MeshConfig meshtastic_config;
    chat::MeshConfig meshcore_config;
    chat::MeshConfig rnode_config;
    chat::MeshProtocol mesh_protocol;

    // Device settings
    char node_name[32];
    char short_name[16];
    bool ble_enabled;

    // Channel settings
    bool primary_enabled;
    bool secondary_enabled;
    bool primary_uplink_enabled;
    bool primary_downlink_enabled;
    bool secondary_uplink_enabled;
    bool secondary_downlink_enabled;
    uint8_t secondary_key[16]; // PSK for secondary channel

    // GPS settings
    uint32_t gps_interval_ms;
    uint8_t gps_mode;
    uint8_t gps_sat_mask;
    uint8_t gps_strategy;
    uint8_t gps_alt_ref;
    uint8_t gps_coord_format;
    gps::MotionConfig motion_config;

    // Map settings
    uint8_t map_coord_system;
    uint8_t map_source;
    bool map_contour_enabled;
    bool map_track_enabled;
    uint8_t map_track_interval;
    uint8_t map_track_format;

    // Chat settings (UI defaults)
    uint8_t chat_channel;

    // Network settings
    bool net_duty_cycle;
    uint8_t net_channel_util;

    // Privacy settings
    uint8_t privacy_encrypt_mode;
    uint8_t privacy_nmea_output;
    uint8_t privacy_nmea_sentence;

    // Tracker route settings
    bool route_enabled;
    char route_path[96];

    // APRS/iGate settings (optional persistence for host)
    AprsConfig aprs;

    AppConfig()
    {
        chat_policy = chat::ChatPolicy::outdoor();
        meshtastic_config = chat::MeshConfig();
        meshtastic_config.region = kDefaultRegionCode;
        meshtastic_config.use_preset = true;
        meshtastic_config.modem_preset = 0;
        meshtastic_config.bandwidth_khz = 250.0f;
        meshtastic_config.spread_factor = 11;
        meshtastic_config.coding_rate = 5;
        meshtastic_config.tx_power = 14;
        meshtastic_config.hop_limit = 2;
        meshtastic_config.tx_enabled = true;
        meshtastic_config.override_duty_cycle = false;
        meshtastic_config.channel_num = 0;
        meshtastic_config.frequency_offset_mhz = 0.0f;
        meshtastic_config.override_frequency_mhz = 0.0f;

        meshcore_config = chat::MeshConfig();
        applyMeshCoreFactoryDefaults();
        rnode_config = chat::MeshConfig();
        applyRNodeFactoryDefaults();
        mesh_protocol = chat::MeshProtocol::Meshtastic;
        node_name[0] = '\0';
        short_name[0] = '\0';
        ble_enabled = true;
        primary_enabled = true;
        secondary_enabled = false;
        primary_uplink_enabled = false;
        primary_downlink_enabled = false;
        secondary_uplink_enabled = false;
        secondary_downlink_enabled = false;
        memset(secondary_key, 0, 16);
        gps_interval_ms = 60000;
        gps_mode = 0;
        gps_sat_mask = 0x1 | 0x8 | 0x4;
        gps_strategy = 0;
        gps_alt_ref = 0;
        gps_coord_format = 0;
        motion_config = gps::MotionConfig();

        map_coord_system = 0;
        map_source = 0;
        map_contour_enabled = false;
        map_track_enabled = false;
        map_track_interval = 1;
        map_track_format = 0;

        chat_channel = 0;

        net_duty_cycle = true;
        net_channel_util = 0;

        privacy_encrypt_mode = 1;
        privacy_nmea_output = 0;
        privacy_nmea_sentence = 0;
        route_enabled = false;
        route_path[0] = '\0';
        aprs = AprsConfig();
    }

    void applyMeshCoreFactoryDefaults()
    {
        meshcore_config.meshcore_region_preset = 0;
        meshcore_config.meshcore_freq_mhz = kMeshCoreDefaultFreqMHz;
        meshcore_config.meshcore_bw_khz = kMeshCoreDefaultBwKHz;
        meshcore_config.meshcore_sf = kMeshCoreDefaultSf;
        meshcore_config.meshcore_cr = kMeshCoreDefaultCr;
        meshcore_config.tx_power = kMeshCoreDefaultTxPowerDbm;
        meshcore_config.meshcore_client_repeat = false;
        meshcore_config.meshcore_rx_delay_base = 0.0f;
        meshcore_config.meshcore_airtime_factor = 1.0f;
        meshcore_config.meshcore_flood_max = 16;
        meshcore_config.meshcore_multi_acks = false;
        meshcore_config.meshcore_channel_slot = 0;
        strncpy(meshcore_config.meshcore_channel_name, "Public",
                sizeof(meshcore_config.meshcore_channel_name) - 1);
        meshcore_config.meshcore_channel_name[sizeof(meshcore_config.meshcore_channel_name) - 1] = '\0';
    }

    void applyRNodeFactoryDefaults()
    {
        rnode_config.use_preset = false;
        rnode_config.bandwidth_khz = kRNodeDefaultBwKHz;
        rnode_config.spread_factor = kRNodeDefaultSf;
        rnode_config.coding_rate = kRNodeDefaultCr;
        rnode_config.tx_power = kRNodeDefaultTxPowerDbm;
        rnode_config.tx_enabled = true;
        rnode_config.override_duty_cycle = false;
        rnode_config.override_frequency_mhz = kRNodeDefaultFreqMHz;
    }

    chat::MeshConfig& activeMeshConfig()
    {
        switch (mesh_protocol)
        {
        case chat::MeshProtocol::MeshCore:
            return meshcore_config;
        case chat::MeshProtocol::LXMF:
        case chat::MeshProtocol::RNode:
            return rnode_config;
        case chat::MeshProtocol::Meshtastic:
        default:
            return meshtastic_config;
        }
    }

    const chat::MeshConfig& activeMeshConfig() const
    {
        switch (mesh_protocol)
        {
        case chat::MeshProtocol::MeshCore:
            return meshcore_config;
        case chat::MeshProtocol::LXMF:
        case chat::MeshProtocol::RNode:
            return rnode_config;
        case chat::MeshProtocol::Meshtastic:
        default:
            return meshtastic_config;
        }
    }
};

} // namespace app
