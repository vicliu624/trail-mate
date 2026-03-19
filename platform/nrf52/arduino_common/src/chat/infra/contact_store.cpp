#include "platform/nrf52/arduino_common/chat/infra/contact_store.h"

namespace platform::nrf52::arduino_common::chat::infra
{

ContactStore::ContactStore()
    : blob_store_("/chat_contacts.bin"),
      core_(blob_store_)
{
}

void ContactStore::begin()
{
    core_.begin();
}

std::string ContactStore::getNickname(uint32_t node_id) const
{
    return core_.getNickname(node_id);
}

bool ContactStore::setNickname(uint32_t node_id, const char* nickname)
{
    return core_.setNickname(node_id, nickname);
}

bool ContactStore::removeNickname(uint32_t node_id)
{
    return core_.removeNickname(node_id);
}

bool ContactStore::hasNickname(const char* nickname) const
{
    return core_.hasNickname(nickname);
}

std::vector<uint32_t> ContactStore::getAllContactIds() const
{
    return core_.getAllContactIds();
}

size_t ContactStore::getCount() const
{
    return core_.getCount();
}

bool ContactStore::hasContactNode(uint32_t node_id) const
{
    return !core_.getNickname(node_id).empty();
}

} // namespace platform::nrf52::arduino_common::chat::infra
