/**
 * @file contact_store.h
 * @brief Contact nickname storage (node_id → local nickname mapping)
 *
 * Stores user-defined nicknames for mesh nodes.
 * Storage: SD card (preferred) or Flash (fallback)
 * Format: key-value pairs (node_id → nickname)
 * Capacity: up to 100 contacts
 */

#pragma once

#include "../ports/i_contact_store.h"
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <string>
#include <vector>

namespace chat
{
namespace contacts
{

class ContactStore : public IContactStore
{
  public:
    ContactStore() = default;

    struct Entry
    {
        uint32_t node_id;
        char nickname[13]; // 12 bytes + null terminator
    };

    /**
     * @brief Initialize storage (load from SD card or Flash)
     */
    void begin() override;

    /**
     * @brief Get nickname for a node_id
     * @param node_id Node ID
     * @return Nickname if found, empty string otherwise
     */
    std::string getNickname(uint32_t node_id) const override;

    /**
     * @brief Set nickname for a node_id
     * @param node_id Node ID
     * @param nickname Nickname (max 12 bytes)
     * @return true if successful, false if duplicate name or storage full
     */
    bool setNickname(uint32_t node_id, const char* nickname) override;

    /**
     * @brief Remove nickname for a node_id
     * @param node_id Node ID
     * @return true if removed, false if not found
     */
    bool removeNickname(uint32_t node_id) override;

    /**
     * @brief Check if a nickname already exists
     * @param nickname Nickname to check
     * @return true if duplicate
     */
    bool hasNickname(const char* nickname) const override;

    /**
     * @brief Get all contact node IDs
     * @return Vector of node IDs
     */
    std::vector<uint32_t> getAllContactIds() const override;

    /**
     * @brief Get number of contacts
     */
    size_t getCount() const override
    {
        return entries_.size();
    }

  private:
    static constexpr size_t kMaxContacts = 100;
    static constexpr const char* kSdPath = "/sd/contacts.dat";
    static constexpr const char* kPrefNs = "contacts";
    static constexpr const char* kPrefKey = "contact_blob";

    std::vector<Entry> entries_;
    bool use_sd_ = false;

    bool loadFromSD();
    bool saveToSD();
    bool loadFromFlash();
    bool saveToFlash();
    void save();
};

} // namespace contacts
} // namespace chat
