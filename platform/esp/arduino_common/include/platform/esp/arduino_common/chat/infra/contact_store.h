/**
 * @file contact_store.h
 * @brief Contact nickname storage shell backed by SD/Flash persistence
 */

#pragma once

#include "chat/infra/contact_store_core.h"
#include "chat/ports/i_contact_blob_store.h"

namespace chat
{
namespace contacts
{

class ContactStore : public IContactStore,
                     private chat::IContactBlobStore
{
  public:
    ContactStore();

    void begin() override;
    std::string getNickname(uint32_t node_id) const override;
    bool setNickname(uint32_t node_id, const char* nickname) override;
    bool removeNickname(uint32_t node_id) override;
    bool hasNickname(const char* nickname) const override;
    std::vector<uint32_t> getAllContactIds() const override;
    size_t getCount() const override;

  private:
    static constexpr const char* kSdPath = "/sd/contacts.dat";
    static constexpr const char* kPrefNs = "contacts";
    static constexpr const char* kPrefKey = "contact_blob";

    bool loadBlob(std::vector<uint8_t>& out) override;
    bool saveBlob(const uint8_t* data, size_t len) override;

    bool loadFromSD(std::vector<uint8_t>& out) const;
    bool saveToSD(const uint8_t* data, size_t len) const;
    bool loadFromFlash(std::vector<uint8_t>& out) const;
    bool saveToFlash(const uint8_t* data, size_t len) const;
    void clearFlashBackup() const;

    ContactStoreCore core_;
};

} // namespace contacts
} // namespace chat
