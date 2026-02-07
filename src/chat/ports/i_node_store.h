/**
 * @file i_node_store.h
 * @brief Node store interface
 */

#pragma once

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
    uint8_t protocol;   // NodeProtocolType
    uint8_t role;       // NodeRoleType (Meshtastic roles)
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
     * @param node_id Node ID
     * @param short_name Short name
     * @param long_name Long name
     * @param now_secs Current timestamp (seconds)
     * @param snr Signal-to-Noise Ratio
     */
    virtual void upsert(uint32_t node_id, const char* short_name, const char* long_name,
                        uint32_t now_secs, float snr = 0.0f, uint8_t protocol = 0, uint8_t role = kNodeRoleUnknown) = 0;

    /**
     * @brief Update node protocol (without changing names)
     * @param node_id Node ID
     * @param protocol Protocol type
     * @param now_secs Current timestamp (seconds)
     */
    virtual void updateProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs) = 0;

    /**
     * @brief Get all entries (for iteration)
     * @return Reference to entries vector
     */
    virtual const std::vector<NodeEntry>& getEntries() const = 0;

    /**
     * @brief Clear all stored node entries
     */
    virtual void clear() = 0;
};

} // namespace contacts
} // namespace chat
