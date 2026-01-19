/**
 * @file mt_dedup.h
 * @brief Meshtastic packet deduplication
 */

#pragma once

#include "../../domain/chat_types.h"
#include <Arduino.h>
#include <map>
#include <stdint.h>

namespace chat
{
namespace meshtastic
{

/**
 * @brief Packet deduplication cache
 * Prevents processing duplicate packets
 */
class MtDedup
{
  public:
    static constexpr size_t MAX_CACHE_SIZE = 100;
    static constexpr uint32_t CACHE_TIMEOUT_MS = 300000; // 5 minutes

    MtDedup();
    ~MtDedup();

    /**
     * @brief Check if packet is duplicate
     * @param from_node Source node ID
     * @param packet_id Packet ID
     * @return true if duplicate (already seen)
     */
    bool isDuplicate(NodeId from_node, uint32_t packet_id);

    /**
     * @brief Mark packet as seen
     * @param from_node Source node ID
     * @param packet_id Packet ID
     */
    void markSeen(NodeId from_node, uint32_t packet_id);

    /**
     * @brief Clear expired entries
     */
    void cleanup();

  private:
    struct PacketKey
    {
        NodeId from;
        uint32_t id;

        bool operator<(const PacketKey& other) const
        {
            if (from != other.from)
            {
                return from < other.from;
            }
            return id < other.id;
        }
    };

    struct PacketEntry
    {
        uint32_t timestamp;
    };

    std::map<PacketKey, PacketEntry> cache_;
    uint32_t last_cleanup_;
};

} // namespace meshtastic
} // namespace chat
