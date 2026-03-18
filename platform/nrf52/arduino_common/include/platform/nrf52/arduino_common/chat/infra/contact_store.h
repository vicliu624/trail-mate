#pragma once

#include "chat/infra/contact_store_core.h"
#include "platform/nrf52/arduino_common/chat/infra/blob_file_store.h"

namespace platform::nrf52::arduino_common::chat::infra
{

class ContactStore final : public ::chat::contacts::IContactStore
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
    ContactBlobFileStore blob_store_;
    ::chat::contacts::ContactStoreCore core_;
};

} // namespace platform::nrf52::arduino_common::chat::infra
