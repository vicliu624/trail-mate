#pragma once

#include "../ports/i_node_blob_store.h"
#include "../ports/i_node_store.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace chat
{
namespace contacts
{

class NodeStoreCore : public INodeStore
{
  public:
    static constexpr size_t kMaxNodes = 80;
    static constexpr size_t kLegacySerializedEntrySize = 64;
    static constexpr size_t kSerializedEntrySize = 104;
    static constexpr size_t kSerializedEntrySizeV8 = 144;
    static constexpr uint8_t kPersistVersion = 8;
    static constexpr uint32_t kSaveIntervalMs = 5000;

    explicit NodeStoreCore(INodeBlobStore& blob_store);
    void setProtectedNodeChecker(std::function<bool(uint32_t)> checker);
    void setAutoSaveEnabled(bool enabled);

    void begin() override;
    void applyUpdate(uint32_t node_id, const NodeUpdate& update) override;
    void upsert(uint32_t node_id, const char* short_name, const char* long_name,
                uint32_t now_secs, float snr = 0.0f, float rssi = 0.0f, uint8_t protocol = 0,
                uint8_t role = kNodeRoleUnknown, uint8_t hops_away = 0xFF,
                uint8_t hw_model = 0, uint8_t channel = 0xFF) override;
    void updateProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs) override;
    void updatePosition(uint32_t node_id, const NodePosition& position) override;
    bool setNextHop(uint32_t node_id, uint8_t next_hop, uint32_t now_secs);
    uint8_t getNextHop(uint32_t node_id) const;
    bool remove(uint32_t node_id) override;
    const std::vector<NodeEntry>& getEntries() const override;
    void clear() override;
    bool flush() override;

    static uint32_t computeBlobCrc(const uint8_t* data, size_t len);

  private:
    bool loadEntries();
    bool saveEntries();
    bool decodeEntries(const uint8_t* data, size_t len);
    void encodeEntries(std::vector<uint8_t>& out) const;
    void maybeSave();
    size_t selectEvictionIndex() const;

    INodeBlobStore& blob_store_;
    std::vector<NodeEntry> entries_;
    uint32_t last_save_ms_ = 0;
    bool dirty_ = false;
    bool auto_save_enabled_ = true;
    std::function<bool(uint32_t)> protected_node_checker_;
};

} // namespace contacts
} // namespace chat
