#pragma once

#include "chat/infra/node_store_core.h"
#include "platform/nrf52/arduino_common/chat/infra/blob_file_store.h"

#include <functional>

namespace platform::nrf52::arduino_common::chat::meshtastic
{

class NodeStore final : public ::chat::contacts::INodeStore
{
  public:
    NodeStore();

    void begin() override;
    void applyUpdate(uint32_t node_id, const ::chat::contacts::NodeUpdate& update) override;
    void upsert(uint32_t node_id, const char* short_name, const char* long_name,
                uint32_t now_secs, float snr = 0.0f, float rssi = 0.0f, uint8_t protocol = 0,
                uint8_t role = ::chat::contacts::kNodeRoleUnknown, uint8_t hops_away = 0xFF,
                uint8_t hw_model = 0, uint8_t channel = 0xFF) override;
    void updateProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs) override;
    void updatePosition(uint32_t node_id, const ::chat::contacts::NodePosition& position) override;
    bool setNextHop(uint32_t node_id, uint8_t next_hop, uint32_t now_secs);
    uint8_t getNextHop(uint32_t node_id) const;
    bool remove(uint32_t node_id) override;
    const std::vector<::chat::contacts::NodeEntry>& getEntries() const override;
    void clear() override;
    bool flush() override;
    void setProtectedNodeChecker(std::function<bool(uint32_t)> checker);

  private:
    ::platform::nrf52::arduino_common::chat::infra::NodeBlobFileStore blob_store_;
    ::chat::contacts::NodeStoreCore core_;
};

} // namespace platform::nrf52::arduino_common::chat::meshtastic
