/**
 * @file node_store.h
 * @brief Lightweight persisted NodeInfo store (best-effort, Preferences-backed)
 *
 * This is a trimmed down replacement for the full Meshtastic NodeDB: we only keep
 * node_id + short/long names + last_seen, capped to a small fixed set, and persist
 * to ESP32 Preferences as a blob. It is intentionally minimal for stability.
 */

#pragma once

#include "../../ports/i_node_store.h"
#include "node_persist.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string>
#include <vector>

namespace chat
{
namespace meshtastic
{

class NodeStore : public contacts::INodeStore
{
  public:
    NodeStore() = default;

    /**
     * @brief Load from Preferences (best effort)
     */
    void begin() override;

    /**
     * @brief Update or insert a node entry and persist (best effort)
     */
    void upsert(uint32_t node_id, const char* short_name, const char* long_name,
                uint32_t now_secs, float snr = 0.0f, uint8_t protocol = 0,
                uint8_t role = contacts::kNodeRoleUnknown) override;

    /**
     * @brief Update protocol for an existing node (best effort)
     */
    void updateProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs) override;

    /**
     * @brief Get all entries (for iteration)
     */
    const std::vector<contacts::NodeEntry>& getEntries() const override
    {
        return entries_;
    }

    /**
     * @brief Clear all entries and persisted data
     */
    void clear() override;

  private:
    static constexpr size_t kMaxNodes = kPersistMaxNodes;
    static constexpr uint32_t kSaveIntervalMs = 5000;

    std::vector<contacts::NodeEntry> entries_;
    uint32_t last_save_ms_ = 0;
    bool dirty_ = false;

    void save();
    void maybeSave();
};

} // namespace meshtastic
} // namespace chat
