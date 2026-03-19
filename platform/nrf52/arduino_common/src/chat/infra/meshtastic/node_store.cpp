#include "platform/nrf52/arduino_common/chat/infra/meshtastic/node_store.h"

namespace platform::nrf52::arduino_common::chat::meshtastic
{

NodeStore::NodeStore()
    : blob_store_("/chat_nodes.bin"),
      core_(blob_store_)
{
}

void NodeStore::begin()
{
    core_.begin();
}

void NodeStore::upsert(uint32_t node_id, const char* short_name, const char* long_name,
                       uint32_t now_secs, float snr, float rssi, uint8_t protocol,
                       uint8_t role, uint8_t hops_away, uint8_t hw_model, uint8_t channel)
{
    core_.upsert(node_id, short_name, long_name, now_secs, snr, rssi,
                 protocol, role, hops_away, hw_model, channel);
}

void NodeStore::updateProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs)
{
    core_.updateProtocol(node_id, protocol, now_secs);
}

bool NodeStore::setNextHop(uint32_t node_id, uint8_t next_hop, uint32_t now_secs)
{
    return core_.setNextHop(node_id, next_hop, now_secs);
}

uint8_t NodeStore::getNextHop(uint32_t node_id) const
{
    return core_.getNextHop(node_id);
}

bool NodeStore::remove(uint32_t node_id)
{
    return core_.remove(node_id);
}

const std::vector<::chat::contacts::NodeEntry>& NodeStore::getEntries() const
{
    return core_.getEntries();
}

void NodeStore::clear()
{
    core_.clear();
}

void NodeStore::setProtectedNodeChecker(std::function<bool(uint32_t)> checker)
{
    core_.setProtectedNodeChecker(std::move(checker));
}

} // namespace platform::nrf52::arduino_common::chat::meshtastic
