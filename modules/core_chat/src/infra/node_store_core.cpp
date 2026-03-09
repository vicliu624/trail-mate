#include "chat/infra/node_store_core.h"

#include "sys/clock.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace chat
{
namespace contacts
{

namespace
{
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
    uint8_t hw_model;
    uint8_t channel;
} __attribute__((packed));

static_assert(sizeof(PersistedNodeEntry) == NodeStoreCore::kSerializedEntrySize,
              "PersistedNodeEntry size changed");

void copyIntoPersisted(PersistedNodeEntry& dst, const NodeEntry& src)
{
    dst.node_id = src.node_id;
    memcpy(dst.short_name, src.short_name, sizeof(dst.short_name));
    dst.short_name[sizeof(dst.short_name) - 1] = '\0';
    memcpy(dst.long_name, src.long_name, sizeof(dst.long_name));
    dst.long_name[sizeof(dst.long_name) - 1] = '\0';
    dst.last_seen = src.last_seen;
    dst.snr = src.snr;
    dst.rssi = src.rssi;
    dst.protocol = src.protocol;
    dst.role = src.role;
    dst.hops_away = src.hops_away;
    dst.hw_model = src.hw_model;
    dst.channel = src.channel;
}

void copyFromPersisted(NodeEntry& dst, const PersistedNodeEntry& src)
{
    dst = {};
    dst.node_id = src.node_id;
    memcpy(dst.short_name, src.short_name, sizeof(dst.short_name));
    dst.short_name[sizeof(dst.short_name) - 1] = '\0';
    memcpy(dst.long_name, src.long_name, sizeof(dst.long_name));
    dst.long_name[sizeof(dst.long_name) - 1] = '\0';
    dst.last_seen = src.last_seen;
    dst.snr = src.snr;
    dst.rssi = src.rssi;
    dst.protocol = src.protocol;
    dst.role = src.role;
    dst.hops_away = src.hops_away;
    dst.hw_model = src.hw_model;
    dst.channel = src.channel;
}

} // namespace

NodeStoreCore::NodeStoreCore(INodeBlobStore& blob_store)
    : blob_store_(blob_store)
{
}

void NodeStoreCore::begin()
{
    if (!loadEntries())
    {
        entries_.clear();
        dirty_ = false;
        last_save_ms_ = 0;
    }
}

void NodeStoreCore::upsert(uint32_t node_id, const char* short_name, const char* long_name,
                           uint32_t now_secs, float snr, float rssi, uint8_t protocol,
                           uint8_t role, uint8_t hops_away, uint8_t hw_model, uint8_t channel)
{
    for (auto& entry : entries_)
    {
        if (entry.node_id == node_id)
        {
            if (short_name && short_name[0] != '\0')
            {
                strncpy(entry.short_name, short_name, sizeof(entry.short_name) - 1);
                entry.short_name[sizeof(entry.short_name) - 1] = '\0';
            }
            if (long_name && long_name[0] != '\0')
            {
                strncpy(entry.long_name, long_name, sizeof(entry.long_name) - 1);
                entry.long_name[sizeof(entry.long_name) - 1] = '\0';
            }
            entry.last_seen = now_secs;
            if (!std::isnan(snr))
            {
                entry.snr = snr;
            }
            if (!std::isnan(rssi))
            {
                entry.rssi = rssi;
            }
            if (hops_away != 0xFF)
            {
                entry.hops_away = hops_away;
            }
            if (protocol != 0)
            {
                entry.protocol = protocol;
            }
            if (role != kNodeRoleUnknown)
            {
                entry.role = role;
            }
            if (hw_model != 0)
            {
                entry.hw_model = hw_model;
            }
            if (channel != 0xFF)
            {
                entry.channel = channel;
            }
            dirty_ = true;
            maybeSave();
            return;
        }
    }

    if (entries_.size() >= kMaxNodes)
    {
        entries_.erase(entries_.begin());
    }

    NodeEntry entry{};
    entry.node_id = node_id;
    if (short_name && short_name[0] != '\0')
    {
        strncpy(entry.short_name, short_name, sizeof(entry.short_name) - 1);
        entry.short_name[sizeof(entry.short_name) - 1] = '\0';
    }
    if (long_name && long_name[0] != '\0')
    {
        strncpy(entry.long_name, long_name, sizeof(entry.long_name) - 1);
        entry.long_name[sizeof(entry.long_name) - 1] = '\0';
    }
    entry.last_seen = now_secs;
    entry.snr = snr;
    entry.rssi = rssi;
    entry.hops_away = hops_away;
    entry.protocol = protocol;
    entry.role = (role != kNodeRoleUnknown) ? role : kNodeRoleUnknown;
    entry.hw_model = hw_model;
    entry.channel = channel;
    entries_.push_back(entry);

    dirty_ = true;
    maybeSave();
}

void NodeStoreCore::updateProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs)
{
    if (protocol == 0)
    {
        return;
    }

    for (auto& entry : entries_)
    {
        if (entry.node_id == node_id)
        {
            entry.protocol = protocol;
            entry.last_seen = now_secs;
            dirty_ = true;
            maybeSave();
            return;
        }
    }

    if (entries_.size() >= kMaxNodes)
    {
        entries_.erase(entries_.begin());
    }

    NodeEntry entry{};
    entry.node_id = node_id;
    entry.short_name[0] = '\0';
    entry.long_name[0] = '\0';
    entry.last_seen = now_secs;
    entry.snr = std::numeric_limits<float>::quiet_NaN();
    entry.rssi = std::numeric_limits<float>::quiet_NaN();
    entry.hops_away = 0xFF;
    entry.channel = 0xFF;
    entry.protocol = protocol;
    entries_.push_back(entry);

    dirty_ = true;
    maybeSave();
}

bool NodeStoreCore::remove(uint32_t node_id)
{
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
    {
        if (it->node_id == node_id)
        {
            entries_.erase(it);
            dirty_ = true;
            return saveEntries();
        }
    }
    return false;
}

const std::vector<NodeEntry>& NodeStoreCore::getEntries() const
{
    return entries_;
}

void NodeStoreCore::clear()
{
    entries_.clear();
    dirty_ = false;
    last_save_ms_ = 0;
    blob_store_.clearBlob();
}

uint32_t NodeStoreCore::computeBlobCrc(const uint8_t* data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            if (crc & 1U)
            {
                crc = (crc >> 1) ^ 0xEDB88320U;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

bool NodeStoreCore::loadEntries()
{
    std::vector<uint8_t> blob;
    if (!blob_store_.loadBlob(blob))
    {
        return false;
    }
    const bool ok = decodeEntries(blob.data(), blob.size());
    if (ok)
    {
        dirty_ = false;
        last_save_ms_ = 0;
    }
    return ok;
}

bool NodeStoreCore::saveEntries()
{
    std::vector<uint8_t> blob;
    encodeEntries(blob);
    if (!blob_store_.saveBlob(blob.data(), blob.size()))
    {
        return false;
    }
    last_save_ms_ = sys::millis_now();
    dirty_ = false;
    return true;
}

bool NodeStoreCore::decodeEntries(const uint8_t* data, size_t len)
{
    if (!data)
    {
        return len == 0;
    }

    if (len == 0)
    {
        entries_.clear();
        return true;
    }

    if ((len % kSerializedEntrySize) != 0)
    {
        return false;
    }

    size_t count = len / kSerializedEntrySize;
    if (count > kMaxNodes)
    {
        count = kMaxNodes;
    }

    entries_.clear();
    entries_.reserve(count);
    auto* persisted = reinterpret_cast<const PersistedNodeEntry*>(data);
    for (size_t index = 0; index < count; ++index)
    {
        NodeEntry entry{};
        copyFromPersisted(entry, persisted[index]);
        entries_.push_back(entry);
    }
    return true;
}

void NodeStoreCore::encodeEntries(std::vector<uint8_t>& out) const
{
    out.clear();
    if (entries_.empty())
    {
        return;
    }

    std::vector<PersistedNodeEntry> persisted(entries_.size());
    for (size_t index = 0; index < entries_.size(); ++index)
    {
        copyIntoPersisted(persisted[index], entries_[index]);
    }

    out.resize(persisted.size() * sizeof(PersistedNodeEntry));
    memcpy(out.data(), persisted.data(), out.size());
}

void NodeStoreCore::maybeSave()
{
    if (!dirty_)
    {
        return;
    }

    const uint32_t now_ms = sys::millis_now();
    if (last_save_ms_ == 0 || (now_ms - last_save_ms_) >= kSaveIntervalMs)
    {
        saveEntries();
    }
}

} // namespace contacts
} // namespace chat
