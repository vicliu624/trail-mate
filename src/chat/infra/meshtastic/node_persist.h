/**
 * @file node_persist.h
 * @brief Shared node persistence layout for Preferences.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace chat
{
namespace meshtastic
{

static constexpr size_t kPersistMaxNodes = 16;
static constexpr const char* kPersistNodesNs = "nodes";
static constexpr const char* kPersistNodesKey = "node_blob";
static constexpr const char* kPersistNodesKeyVer = "ver";
static constexpr const char* kPersistNodesKeyCrc = "crc";
static constexpr uint8_t kPersistVersion = 4;

struct PersistedNodeEntry
{
    uint32_t node_id;
    char short_name[10];
    char long_name[32];
    uint32_t last_seen;
    float snr;
    float rssi;
    uint8_t protocol;
    uint8_t role;
    uint8_t hops_away;
} __attribute__((packed));

static_assert(sizeof(PersistedNodeEntry) == 61, "PersistedNodeEntry size changed");

} // namespace meshtastic
} // namespace chat
