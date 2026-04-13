/**
 * @file contact_types.h
 * @brief Contact domain types
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace chat
{
namespace contacts
{

/**
 * @brief Node protocol type
 */
enum class NodeProtocolType : uint8_t
{
    Unknown = 0,
    Meshtastic = 1,
    MeshCore = 2
};

/**
 * @brief Node role type (aligned with Meshtastic roles)
 */
enum class NodeRoleType : uint8_t
{
    Unknown = 0xFF,
    Client = 0,
    ClientMute = 1,
    Router = 2,
    RouterClient = 3,
    Repeater = 4,
    Tracker = 5,
    Sensor = 6,
    Tak = 7,
    ClientHidden = 8,
    LostAndFound = 9,
    TakTracker = 10,
    RouterLate = 11,
    ClientBase = 12
};

/**
 * @brief Node position data (from POSITION messages)
 */
struct NodePosition
{
    bool valid = false;
    int32_t latitude_i = 0;  // 1e-7 degrees
    int32_t longitude_i = 0; // 1e-7 degrees
    bool has_altitude = false;
    int32_t altitude = 0;   // meters
    uint32_t timestamp = 0; // Unix timestamp (seconds)
    uint32_t precision_bits = 0;
    uint32_t pdop = 0; // 1/100 units
    uint32_t hdop = 0; // 1/100 units
    uint32_t vdop = 0; // 1/100 units
    uint32_t gps_accuracy_mm = 0;
};

struct NodeDeviceMetrics
{
    bool has_battery_level = false;
    uint32_t battery_level = 0;
    bool has_voltage = false;
    float voltage = 0.0f;
    bool has_channel_utilization = false;
    float channel_utilization = 0.0f;
    bool has_air_util_tx = false;
    float air_util_tx = 0.0f;
    bool has_uptime_seconds = false;
    uint32_t uptime_seconds = 0;
};

struct NodeUpdate
{
    const char* short_name = nullptr;
    const char* long_name = nullptr;

    bool has_last_seen = false;
    uint32_t last_seen = 0;

    bool has_snr = false;
    float snr = 0.0f;

    bool has_rssi = false;
    float rssi = 0.0f;

    bool has_hops_away = false;
    uint8_t hops_away = 0xFF;

    bool has_channel = false;
    uint8_t channel = 0xFF;

    bool has_next_hop = false;
    uint8_t next_hop = 0;

    bool has_protocol = false;
    uint8_t protocol = 0;

    bool has_role = false;
    uint8_t role = 0xFF;

    bool has_hw_model = false;
    uint8_t hw_model = 0;

    bool has_macaddr = false;
    uint8_t macaddr[6] = {};

    bool has_via_mqtt = false;
    bool via_mqtt = false;

    bool has_is_ignored = false;
    bool is_ignored = false;

    bool has_public_key = false;
    bool public_key_present = false;

    bool has_key_manually_verified = false;
    bool key_manually_verified = false;

    bool has_device_metrics = false;
    NodeDeviceMetrics device_metrics{};

    bool has_position = false;
    NodePosition position{};
};

/**
 * @brief Base node information
 */
struct NodeInfoBase
{
    uint32_t node_id;
    char short_name[10];
    char long_name[32];
    uint32_t last_seen;       // Unix timestamp (seconds)
    float snr;                // Signal-to-Noise Ratio
    float rssi;               // RSSI in dBm
    uint8_t hops_away = 0xFF; // 0xFF = unknown
    uint8_t channel = 0xFF;   // Meshtastic channel index, 0xFF = unknown
    bool is_contact;          // true if user has assigned a nickname
    std::string display_name; // nickname if contact, short_name otherwise
    NodeProtocolType protocol;
    NodeRoleType role;
    uint8_t hw_model = 0;
    uint8_t next_hop = 0;
    bool has_macaddr = false;
    uint8_t macaddr[6] = {};
    bool via_mqtt = false;
    bool is_ignored = false;
    bool has_public_key = false;
    bool key_manually_verified = false;
    bool has_device_metrics = false;
    NodeDeviceMetrics device_metrics;
    NodePosition position;
};

/**
 * @brief Meshtastic-specific node info (reserved for future extensions)
 */
struct MeshtasticNodeInfo : public NodeInfoBase
{
};

/**
 * @brief MeshCore-specific node info (reserved for future extensions)
 */
struct MeshCoreNodeInfo : public NodeInfoBase
{
};

/**
 * @brief Node information (from mesh network)
 */
using NodeInfo = NodeInfoBase;

} // namespace contacts
} // namespace chat
