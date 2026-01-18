/**
 * @file i_node_store.h
 * @brief Node store interface
 */

#pragma once

#include <cstdint>
#include <vector>

namespace chat {
namespace contacts {

/**
 * @brief Node entry structure
 */
struct NodeEntry {
    uint32_t node_id;
    char short_name[10];
    char long_name[32];
    uint32_t last_seen;  // Unix timestamp (seconds)
    float snr;           // Signal-to-Noise Ratio
};

/**
 * @brief Node store interface
 * Abstracts node information storage implementation
 */
class INodeStore {
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
                       uint32_t now_secs, float snr = 0.0f) = 0;
    
    /**
     * @brief Get all entries (for iteration)
     * @return Reference to entries vector
     */
    virtual const std::vector<NodeEntry>& getEntries() const = 0;
};

} // namespace contacts
} // namespace chat
