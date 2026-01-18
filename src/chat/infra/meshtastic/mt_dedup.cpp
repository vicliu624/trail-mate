/**
 * @file mt_dedup.cpp
 * @brief Meshtastic packet deduplication implementation
 */

#include "mt_dedup.h"

namespace chat {
namespace meshtastic {

MtDedup::MtDedup() : last_cleanup_(millis()) {
}

MtDedup::~MtDedup() {
}

bool MtDedup::isDuplicate(NodeId from_node, uint32_t packet_id) {
    cleanup();
    
    PacketKey key;
    key.from = from_node;
    key.id = packet_id;
    
    return cache_.find(key) != cache_.end();
}

void MtDedup::markSeen(NodeId from_node, uint32_t packet_id) {
    cleanup();
    
    // Remove oldest if cache is full
    if (cache_.size() >= MAX_CACHE_SIZE) {
        auto oldest = cache_.begin();
        cache_.erase(oldest);
    }
    
    PacketKey key;
    key.from = from_node;
    key.id = packet_id;
    
    PacketEntry entry;
    entry.timestamp = millis();
    
    cache_[key] = entry;
}

void MtDedup::cleanup() {
    uint32_t now = millis();
    
    // Cleanup every 30 seconds
    if (now - last_cleanup_ < 30000) {
        return;
    }
    last_cleanup_ = now;
    
    // Remove expired entries
    auto it = cache_.begin();
    while (it != cache_.end()) {
        if (now - it->second.timestamp > CACHE_TIMEOUT_MS) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace meshtastic
} // namespace chat
