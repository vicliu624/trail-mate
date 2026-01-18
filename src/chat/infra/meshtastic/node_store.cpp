/**
 * @file node_store.cpp
 * @brief Lightweight persisted NodeInfo store
 */

#include "node_store.h"
#include "../../ports/i_node_store.h"
#include <cstring>

namespace chat {
namespace meshtastic {

void NodeStore::begin() {
    Preferences prefs;
    if (!prefs.begin(kNs, true)) {
        return;
    }
    size_t len = prefs.getBytesLength(kKey);
    if (len > 0 && (len % sizeof(contacts::NodeEntry) == 0)) {
        size_t count = len / sizeof(contacts::NodeEntry);
        if (count > kMaxNodes) {
            count = kMaxNodes;
        }
        entries_.resize(count);
        prefs.getBytes(kKey, entries_.data(), count * sizeof(contacts::NodeEntry));
    }
    prefs.end();
}

void NodeStore::upsert(uint32_t node_id, const char* short_name, const char* long_name, uint32_t now_secs, float snr) {
    // find existing
    for (auto& e : entries_) {
        if (e.node_id == node_id) {
            if (short_name) {
                strncpy(e.short_name, short_name, sizeof(e.short_name) - 1);
                e.short_name[sizeof(e.short_name) - 1] = '\0';
            }
            if (long_name) {
                strncpy(e.long_name, long_name, sizeof(e.long_name) - 1);
                e.long_name[sizeof(e.long_name) - 1] = '\0';
            }
            e.last_seen = now_secs;
            e.snr = snr;
            save();
            return;
        }
    }

    if (entries_.size() >= kMaxNodes) {
        entries_.erase(entries_.begin()); // drop oldest
    }
    contacts::NodeEntry e{};
    e.node_id = node_id;
    if (short_name) {
        strncpy(e.short_name, short_name, sizeof(e.short_name) - 1);
        e.short_name[sizeof(e.short_name) - 1] = '\0';
    }
    if (long_name) {
        strncpy(e.long_name, long_name, sizeof(e.long_name) - 1);
        e.long_name[sizeof(e.long_name) - 1] = '\0';
    }
    e.last_seen = now_secs;
    e.snr = snr;
    entries_.push_back(e);
    save();
}

void NodeStore::save() {
    Preferences prefs;
    if (!prefs.begin(kNs, false)) {
        return;
    }
    if (!entries_.empty()) {
        prefs.putBytes(kKey, entries_.data(), entries_.size() * sizeof(contacts::NodeEntry));
    } else {
        prefs.remove(kKey);
    }
    prefs.end();
}

} // namespace meshtastic
} // namespace chat
