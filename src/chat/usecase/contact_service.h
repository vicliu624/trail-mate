/**
 * @file contact_service.h
 * @brief Contact service (use case layer)
 *
 * Responsibilities:
 * - Coordinate node info and contact nickname management
 * - Provide unified interface to get display names (nickname or short_name)
 */

#pragma once

#include "../domain/contact_types.h"
#include "../ports/i_node_store.h"
#include "../ports/i_contact_store.h"
#include <string>
#include <vector>

namespace chat {
namespace contacts {

/**
 * @brief Contact service
 * Use case layer: coordinates domain model and storage adapters
 */
class ContactService {
public:
    /**
     * @brief Constructor with dependency injection
     * @param node_store Node store implementation
     * @param contact_store Contact store implementation
     */
    ContactService(INodeStore& node_store, IContactStore& contact_store);
    ~ContactService() = default;

    /**
     * @brief Initialize service (load stores)
     */
    void begin();

    /**
     * @brief Update node info from NodeInfo packet
     * @param node_id Node ID
     * @param short_name Short name
     * @param long_name Long name
     * @param snr Signal-to-Noise Ratio
     * @param now_secs Current timestamp (seconds)
     */
    void updateNodeInfo(uint32_t node_id, const char* short_name, const char* long_name, 
                       float snr, uint32_t now_secs);

    /**
     * @brief Get display name for a node (nickname if contact, short_name otherwise)
     * @param node_id Node ID
     * @return Display name
     */
    std::string getContactName(uint32_t node_id) const;

    /**
     * @brief Get all contacts (nodes with nicknames)
     */
    std::vector<NodeInfo> getContacts() const;

    /**
     * @brief Get all nearby nodes (nodes without nicknames, visible within 6 days)
     */
    std::vector<NodeInfo> getNearby() const;

    /**
     * @brief Add contact (set nickname)
     * @param node_id Node ID
     * @param nickname Nickname (max 12 bytes)
     * @return true if successful
     */
    bool addContact(uint32_t node_id, const char* nickname);

    /**
     * @brief Edit contact nickname
     * @param node_id Node ID
     * @param nickname New nickname (max 12 bytes)
     * @return true if successful
     */
    bool editContact(uint32_t node_id, const char* nickname);

    /**
     * @brief Remove contact (delete nickname)
     * @param node_id Node ID
     * @return true if removed
     */
    bool removeContact(uint32_t node_id);

    /**
     * @brief Get node info by node_id
     * @param node_id Node ID
     * @return NodeInfo if found, nullptr otherwise
     */
    const NodeInfo* getNodeInfo(uint32_t node_id) const;

private:
    INodeStore& node_store_;
    IContactStore& contact_store_;
    mutable std::vector<NodeInfo> cached_nodes_;  // Cache for getContacts/getNearby
    mutable uint32_t cache_timestamp_;
    static constexpr uint32_t kCacheTimeoutMs = 1000;  // 1 second cache

    void invalidateCache() const;
    void buildCache() const;
    bool isNodeVisible(uint32_t last_seen) const;
    std::string formatTimeStatus(uint32_t last_seen) const;
};

} // namespace contacts
} // namespace chat
