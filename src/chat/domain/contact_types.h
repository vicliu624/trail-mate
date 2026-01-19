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
 * @brief Base node information
 */
struct NodeInfoBase
{
    uint32_t node_id;
    char short_name[10];
    char long_name[32];
    uint32_t last_seen;       // Unix timestamp (seconds)
    float snr;                // Signal-to-Noise Ratio
    bool is_contact;          // true if user has assigned a nickname
    std::string display_name; // nickname if contact, short_name otherwise
    NodeProtocolType protocol;
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
