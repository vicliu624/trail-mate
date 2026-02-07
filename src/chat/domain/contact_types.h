/**
 * @file contact_types.h
 * @brief Contact domain types
 */

#pragma once

#include <cstdint>
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
    int32_t latitude_i = 0;   // 1e-7 degrees
    int32_t longitude_i = 0;  // 1e-7 degrees
    bool has_altitude = false;
    int32_t altitude = 0;     // meters
    uint32_t timestamp = 0;   // Unix timestamp (seconds)
    uint32_t precision_bits = 0;
    uint32_t pdop = 0;        // 1/100 units
    uint32_t hdop = 0;        // 1/100 units
    uint32_t vdop = 0;        // 1/100 units
    uint32_t gps_accuracy_mm = 0;
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
    bool is_contact;          // true if user has assigned a nickname
    std::string display_name; // nickname if contact, short_name otherwise
    NodeProtocolType protocol;
    NodeRoleType role;
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
