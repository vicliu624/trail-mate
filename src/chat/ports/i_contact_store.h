/**
 * @file i_contact_store.h
 * @brief Contact store interface
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chat
{
namespace contacts
{

/**
 * @brief Contact store interface
 * Abstracts contact nickname storage implementation
 */
class IContactStore
{
  public:
    virtual ~IContactStore() = default;

    /**
     * @brief Initialize store (load from persistent storage)
     */
    virtual void begin() = 0;

    /**
     * @brief Get nickname for a node_id
     * @param node_id Node ID
     * @return Nickname if found, empty string otherwise
     */
    virtual std::string getNickname(uint32_t node_id) const = 0;

    /**
     * @brief Set nickname for a node_id
     * @param node_id Node ID
     * @param nickname Nickname (max 12 bytes)
     * @return true if successful, false if duplicate name or storage full
     */
    virtual bool setNickname(uint32_t node_id, const char* nickname) = 0;

    /**
     * @brief Remove nickname for a node_id
     * @param node_id Node ID
     * @return true if removed, false if not found
     */
    virtual bool removeNickname(uint32_t node_id) = 0;

    /**
     * @brief Check if a nickname already exists
     * @param nickname Nickname to check
     * @return true if duplicate
     */
    virtual bool hasNickname(const char* nickname) const = 0;

    /**
     * @brief Get all contact node IDs
     * @return Vector of node IDs
     */
    virtual std::vector<uint32_t> getAllContactIds() const = 0;

    /**
     * @brief Get number of contacts
     */
    virtual size_t getCount() const = 0;
};

} // namespace contacts
} // namespace chat
