/**
 * @file i_node_store.h
 * @brief Node store interface
 */

#pragma once

#include "chat/domain/contact_types.h"
#include <cstdint>
#include <vector>

namespace chat
{
namespace contacts
{

/**
 * @brief Node entry structure
 */
struct NodeEntry
{
    uint32_t node_id;
    char short_name[10];
    char long_name[32];
    uint32_t last_seen; // Unix timestamp (seconds)
    float snr;          // Signal-to-Noise Ratio
    float rssi;         // RSSI in dBm
    uint8_t hops_away = 0xFF;
    uint8_t channel = 0xFF; // Meshtastic channel index; 0xFF = unknown
    uint8_t next_hop = 0;   // Meshtastic learned next-hop relay hint; 0 = unknown/flooding
    uint8_t protocol;       // NodeProtocolType
    uint8_t role;           // NodeRoleType (Meshtastic roles)
    uint8_t hw_model;       // Meshtastic_HardwareModel (0 = UNSET)
    bool has_macaddr = false;
    uint8_t macaddr[6] = {};
    bool via_mqtt = false;
    bool is_ignored = false;
    bool has_public_key = false;
    bool key_manually_verified = false;
    bool has_device_metrics = false;
    NodeDeviceMetrics device_metrics{};
    bool position_valid = false;
    int32_t position_latitude_i = 0;
    int32_t position_longitude_i = 0;
    bool position_has_altitude = false;
    int32_t position_altitude = 0;
    uint32_t position_timestamp = 0;
    uint32_t position_precision_bits = 0;
    uint32_t position_pdop = 0;
    uint32_t position_hdop = 0;
    uint32_t position_vdop = 0;
    uint32_t position_gps_accuracy_mm = 0;
};

static constexpr uint8_t kNodeRoleUnknown = 0xFF;

/**
 * @brief Node store interface
 * Abstracts node information storage implementation
 */
class INodeStore
{
  public:
    virtual ~INodeStore() = default;

    /**
     * @brief Initialize store (load from persistent storage)
     */
    virtual void begin() = 0;

    /**
     * @brief Update or insert a node entry
     */
    virtual void applyUpdate(uint32_t node_id, const NodeUpdate& update) = 0;

    virtual void upsert(uint32_t node_id, const char* short_name, const char* long_name,
                        uint32_t now_secs, float snr = 0.0f, float rssi = 0.0f, uint8_t protocol = 0,
                        uint8_t role = kNodeRoleUnknown, uint8_t hops_away = 0xFF,
                        uint8_t hw_model = 0, uint8_t channel = 0xFF) = 0;

    /**
     * @brief Update node protocol (without changing names)
     * @param node_id Node ID
     * @param protocol Protocol type
     * @param now_secs Current timestamp (seconds)
     */
    virtual void updateProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs) = 0;

    /**
     * @brief Update node position info
     * @param node_id Node ID
     * @param position Latest known position
     */
    virtual void updatePosition(uint32_t node_id, const NodePosition& position) = 0;

    /**
     * @brief Remove a node entry by node ID
     * @param node_id Node ID
     * @return true if an entry was removed
     */
    virtual bool remove(uint32_t node_id) = 0;

    /**
     * @brief Get all entries (for iteration)
     * @return Reference to entries vector
     */
    virtual const std::vector<NodeEntry>& getEntries() const = 0;

    /**
     * @brief Clear all stored node entries
     */
    virtual void clear() = 0;

    /**
     * @brief Flush any pending dirty state to persistent storage immediately
     * @return true if storage is synced or there was nothing pending
     */
    virtual bool flush() = 0;
};

} // namespace contacts
} // namespace chat
