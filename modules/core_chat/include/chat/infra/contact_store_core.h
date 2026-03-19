#pragma once

#include "../ports/i_contact_blob_store.h"
#include "../ports/i_contact_store.h"
#include "chat/infra/node_store_core.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chat
{
namespace contacts
{

class ContactStoreCore : public IContactStore
{
  public:
    struct Entry
    {
        uint32_t node_id = 0;
        char nickname[13] = {0};
    };

    static constexpr size_t kMaxContacts = NodeStoreCore::kMaxNodes;
    static constexpr size_t kSerializedEntrySize = sizeof(Entry);

    explicit ContactStoreCore(IContactBlobStore& blob_store);

    void begin() override;
    std::string getNickname(uint32_t node_id) const override;
    bool setNickname(uint32_t node_id, const char* nickname) override;
    bool removeNickname(uint32_t node_id) override;
    bool hasNickname(const char* nickname) const override;
    std::vector<uint32_t> getAllContactIds() const override;
    size_t getCount() const override;

  private:
    bool loadEntries();
    bool saveEntries();
    bool decodeEntries(const uint8_t* data, size_t len);
    void encodeEntries(std::vector<uint8_t>& out) const;

    IContactBlobStore& blob_store_;
    std::vector<Entry> entries_;
};

} // namespace contacts
} // namespace chat
