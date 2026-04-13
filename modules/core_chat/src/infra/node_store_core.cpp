#include "chat/infra/node_store_core.h"

#include "chat/domain/contact_types.h"
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
struct PersistedNodeEntryV6
{
    uint32_t node_id;
    char short_name[10];
    char long_name[32];
    uint32_t last_seen;
    float snr;
    float rssi;
    uint8_t hops_away;
    uint8_t channel;
    uint8_t next_hop;
    uint8_t protocol;
    uint8_t role;
    uint8_t hw_model;
} __attribute__((packed));

static_assert(sizeof(PersistedNodeEntryV6) == NodeStoreCore::kLegacySerializedEntrySize,
              "PersistedNodeEntryV6 size changed");

struct PersistedNodeEntryV7
{
    uint32_t node_id;
    char short_name[10];
    char long_name[32];
    uint32_t last_seen;
    float snr;
    float rssi;
    uint8_t hops_away;
    uint8_t channel;
    uint8_t next_hop;
    uint8_t protocol;
    uint8_t role;
    uint8_t hw_model;
    uint8_t position_valid;
    uint8_t position_has_altitude;
    uint8_t reserved[2];
    int32_t position_latitude_i;
    int32_t position_longitude_i;
    int32_t position_altitude;
    uint32_t position_timestamp;
    uint32_t position_precision_bits;
    uint32_t position_pdop;
    uint32_t position_hdop;
    uint32_t position_vdop;
    uint32_t position_gps_accuracy_mm;
} __attribute__((packed));

static_assert(sizeof(PersistedNodeEntryV7) == NodeStoreCore::kSerializedEntrySize,
              "PersistedNodeEntryV7 size changed");

struct PersistedNodeEntryV8
{
    uint32_t node_id;
    char short_name[10];
    char long_name[32];
    uint32_t last_seen;
    float snr;
    float rssi;
    uint8_t hops_away;
    uint8_t channel;
    uint8_t next_hop;
    uint8_t protocol;
    uint8_t role;
    uint8_t hw_model;
    uint8_t has_macaddr;
    uint8_t via_mqtt;
    uint8_t is_ignored;
    uint8_t has_public_key;
    uint8_t key_manually_verified;
    uint8_t has_device_metrics;
    uint8_t position_valid;
    uint8_t position_has_altitude;
    uint8_t metrics_has_battery_level;
    uint8_t metrics_has_voltage;
    uint8_t metrics_has_channel_utilization;
    uint8_t metrics_has_air_util_tx;
    uint8_t metrics_has_uptime_seconds;
    uint8_t reserved[3];
    uint8_t macaddr[6];
    uint8_t reserved_mac[2];
    int32_t position_latitude_i;
    int32_t position_longitude_i;
    int32_t position_altitude;
    uint32_t position_timestamp;
    uint32_t position_precision_bits;
    uint32_t position_pdop;
    uint32_t position_hdop;
    uint32_t position_vdop;
    uint32_t position_gps_accuracy_mm;
    uint32_t metrics_battery_level;
    float metrics_voltage;
    float metrics_channel_utilization;
    float metrics_air_util_tx;
    uint32_t metrics_uptime_seconds;
} __attribute__((packed));

static_assert(sizeof(PersistedNodeEntryV8) == NodeStoreCore::kSerializedEntrySizeV8,
              "PersistedNodeEntryV8 size changed");

void copyCommonFields(NodeEntry& dst,
                      uint32_t node_id,
                      const char short_name[10],
                      const char long_name[32],
                      uint32_t last_seen,
                      float snr,
                      float rssi,
                      uint8_t hops_away,
                      uint8_t channel,
                      uint8_t next_hop,
                      uint8_t protocol,
                      uint8_t role,
                      uint8_t hw_model)
{
    dst = {};
    dst.node_id = node_id;
    memcpy(dst.short_name, short_name, sizeof(dst.short_name));
    dst.short_name[sizeof(dst.short_name) - 1] = '\0';
    memcpy(dst.long_name, long_name, sizeof(dst.long_name));
    dst.long_name[sizeof(dst.long_name) - 1] = '\0';
    dst.last_seen = last_seen;
    dst.snr = snr;
    dst.rssi = rssi;
    dst.hops_away = hops_away;
    dst.channel = channel;
    dst.next_hop = next_hop;
    dst.protocol = protocol;
    dst.role = role;
    dst.hw_model = hw_model;
}

void copyIntoPersisted(PersistedNodeEntryV8& dst, const NodeEntry& src)
{
    memset(&dst, 0, sizeof(dst));
    dst.node_id = src.node_id;
    memcpy(dst.short_name, src.short_name, sizeof(dst.short_name));
    dst.short_name[sizeof(dst.short_name) - 1] = '\0';
    memcpy(dst.long_name, src.long_name, sizeof(dst.long_name));
    dst.long_name[sizeof(dst.long_name) - 1] = '\0';
    dst.last_seen = src.last_seen;
    dst.snr = src.snr;
    dst.rssi = src.rssi;
    dst.hops_away = src.hops_away;
    dst.channel = src.channel;
    dst.next_hop = src.next_hop;
    dst.protocol = src.protocol;
    dst.role = src.role;
    dst.hw_model = src.hw_model;
    dst.has_macaddr = src.has_macaddr ? 1U : 0U;
    memcpy(dst.macaddr, src.macaddr, sizeof(dst.macaddr));
    dst.via_mqtt = src.via_mqtt ? 1U : 0U;
    dst.is_ignored = src.is_ignored ? 1U : 0U;
    dst.has_public_key = src.has_public_key ? 1U : 0U;
    dst.key_manually_verified = src.key_manually_verified ? 1U : 0U;
    dst.has_device_metrics = src.has_device_metrics ? 1U : 0U;
    dst.position_valid = src.position_valid ? 1U : 0U;
    dst.position_has_altitude = src.position_has_altitude ? 1U : 0U;
    dst.position_latitude_i = src.position_latitude_i;
    dst.position_longitude_i = src.position_longitude_i;
    dst.position_altitude = src.position_altitude;
    dst.position_timestamp = src.position_timestamp;
    dst.position_precision_bits = src.position_precision_bits;
    dst.position_pdop = src.position_pdop;
    dst.position_hdop = src.position_hdop;
    dst.position_vdop = src.position_vdop;
    dst.position_gps_accuracy_mm = src.position_gps_accuracy_mm;
    dst.metrics_has_battery_level = src.device_metrics.has_battery_level ? 1U : 0U;
    dst.metrics_has_voltage = src.device_metrics.has_voltage ? 1U : 0U;
    dst.metrics_has_channel_utilization = src.device_metrics.has_channel_utilization ? 1U : 0U;
    dst.metrics_has_air_util_tx = src.device_metrics.has_air_util_tx ? 1U : 0U;
    dst.metrics_has_uptime_seconds = src.device_metrics.has_uptime_seconds ? 1U : 0U;
    dst.metrics_battery_level = src.device_metrics.battery_level;
    dst.metrics_voltage = src.device_metrics.voltage;
    dst.metrics_channel_utilization = src.device_metrics.channel_utilization;
    dst.metrics_air_util_tx = src.device_metrics.air_util_tx;
    dst.metrics_uptime_seconds = src.device_metrics.uptime_seconds;
}

void copyFromPersisted(NodeEntry& dst, const PersistedNodeEntryV6& src)
{
    copyCommonFields(dst,
                     src.node_id,
                     src.short_name,
                     src.long_name,
                     src.last_seen,
                     src.snr,
                     src.rssi,
                     src.hops_away,
                     src.channel,
                     src.next_hop,
                     src.protocol,
                     src.role,
                     src.hw_model);
}

void copyFromPersisted(NodeEntry& dst, const PersistedNodeEntryV7& src)
{
    copyCommonFields(dst,
                     src.node_id,
                     src.short_name,
                     src.long_name,
                     src.last_seen,
                     src.snr,
                     src.rssi,
                     src.hops_away,
                     src.channel,
                     src.next_hop,
                     src.protocol,
                     src.role,
                     src.hw_model);
    dst.position_valid = src.position_valid != 0;
    dst.position_has_altitude = src.position_has_altitude != 0;
    dst.position_latitude_i = src.position_latitude_i;
    dst.position_longitude_i = src.position_longitude_i;
    dst.position_altitude = src.position_altitude;
    dst.position_timestamp = src.position_timestamp;
    dst.position_precision_bits = src.position_precision_bits;
    dst.position_pdop = src.position_pdop;
    dst.position_hdop = src.position_hdop;
    dst.position_vdop = src.position_vdop;
    dst.position_gps_accuracy_mm = src.position_gps_accuracy_mm;
}

void copyFromPersisted(NodeEntry& dst, const PersistedNodeEntryV8& src)
{
    copyCommonFields(dst,
                     src.node_id,
                     src.short_name,
                     src.long_name,
                     src.last_seen,
                     src.snr,
                     src.rssi,
                     src.hops_away,
                     src.channel,
                     src.next_hop,
                     src.protocol,
                     src.role,
                     src.hw_model);
    dst.has_macaddr = src.has_macaddr != 0;
    memcpy(dst.macaddr, src.macaddr, sizeof(dst.macaddr));
    dst.via_mqtt = src.via_mqtt != 0;
    dst.is_ignored = src.is_ignored != 0;
    dst.has_public_key = src.has_public_key != 0;
    dst.key_manually_verified = src.key_manually_verified != 0;
    dst.has_device_metrics = src.has_device_metrics != 0;
    dst.position_valid = src.position_valid != 0;
    dst.position_has_altitude = src.position_has_altitude != 0;
    dst.position_latitude_i = src.position_latitude_i;
    dst.position_longitude_i = src.position_longitude_i;
    dst.position_altitude = src.position_altitude;
    dst.position_timestamp = src.position_timestamp;
    dst.position_precision_bits = src.position_precision_bits;
    dst.position_pdop = src.position_pdop;
    dst.position_hdop = src.position_hdop;
    dst.position_vdop = src.position_vdop;
    dst.position_gps_accuracy_mm = src.position_gps_accuracy_mm;
    dst.device_metrics.has_battery_level = src.metrics_has_battery_level != 0;
    dst.device_metrics.battery_level = src.metrics_battery_level;
    dst.device_metrics.has_voltage = src.metrics_has_voltage != 0;
    dst.device_metrics.voltage = src.metrics_voltage;
    dst.device_metrics.has_channel_utilization = src.metrics_has_channel_utilization != 0;
    dst.device_metrics.channel_utilization = src.metrics_channel_utilization;
    dst.device_metrics.has_air_util_tx = src.metrics_has_air_util_tx != 0;
    dst.device_metrics.air_util_tx = src.metrics_air_util_tx;
    dst.device_metrics.has_uptime_seconds = src.metrics_has_uptime_seconds != 0;
    dst.device_metrics.uptime_seconds = src.metrics_uptime_seconds;
}

} // namespace

NodeStoreCore::NodeStoreCore(INodeBlobStore& blob_store)
    : blob_store_(blob_store)
{
}

void NodeStoreCore::setProtectedNodeChecker(std::function<bool(uint32_t)> checker)
{
    protected_node_checker_ = std::move(checker);
}

void NodeStoreCore::setAutoSaveEnabled(bool enabled)
{
    auto_save_enabled_ = enabled;
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

void NodeStoreCore::applyUpdate(uint32_t node_id, const NodeUpdate& update)
{
    if (node_id == 0)
    {
        return;
    }

    auto apply_to_entry = [&](NodeEntry& entry)
    {
        if (update.short_name && update.short_name[0] != '\0')
        {
            strncpy(entry.short_name, update.short_name, sizeof(entry.short_name) - 1);
            entry.short_name[sizeof(entry.short_name) - 1] = '\0';
        }
        if (update.long_name && update.long_name[0] != '\0')
        {
            strncpy(entry.long_name, update.long_name, sizeof(entry.long_name) - 1);
            entry.long_name[sizeof(entry.long_name) - 1] = '\0';
        }
        if (update.has_last_seen)
        {
            entry.last_seen = update.last_seen;
        }
        if (update.has_snr)
        {
            entry.snr = update.snr;
        }
        if (update.has_rssi)
        {
            entry.rssi = update.rssi;
        }
        if (update.has_hops_away)
        {
            entry.hops_away = update.hops_away;
        }
        if (update.has_channel)
        {
            entry.channel = update.channel;
        }
        if (update.has_next_hop)
        {
            entry.next_hop = update.next_hop;
        }
        if (update.has_protocol)
        {
            entry.protocol = update.protocol;
        }
        if (update.has_role)
        {
            entry.role = update.role;
        }
        if (update.has_hw_model)
        {
            entry.hw_model = update.hw_model;
        }
        if (update.has_macaddr)
        {
            entry.has_macaddr = true;
            memcpy(entry.macaddr, update.macaddr, sizeof(entry.macaddr));
        }
        if (update.has_via_mqtt)
        {
            entry.via_mqtt = update.via_mqtt;
        }
        if (update.has_is_ignored)
        {
            entry.is_ignored = update.is_ignored;
        }
        if (update.has_public_key)
        {
            entry.has_public_key = update.public_key_present;
        }
        if (update.has_key_manually_verified)
        {
            entry.key_manually_verified = update.key_manually_verified;
        }
        if (update.has_device_metrics)
        {
            entry.has_device_metrics = true;
            entry.device_metrics = update.device_metrics;
        }
        if (update.has_position)
        {
            entry.position_valid = update.position.valid;
            entry.position_latitude_i = update.position.latitude_i;
            entry.position_longitude_i = update.position.longitude_i;
            entry.position_has_altitude = update.position.has_altitude;
            entry.position_altitude = update.position.altitude;
            entry.position_timestamp = update.position.timestamp;
            entry.position_precision_bits = update.position.precision_bits;
            entry.position_pdop = update.position.pdop;
            entry.position_hdop = update.position.hdop;
            entry.position_vdop = update.position.vdop;
            entry.position_gps_accuracy_mm = update.position.gps_accuracy_mm;
        }
    };

    for (auto& entry : entries_)
    {
        if (entry.node_id != node_id)
        {
            continue;
        }
        apply_to_entry(entry);
        dirty_ = true;
        maybeSave();
        return;
    }

    if (entries_.size() >= kMaxNodes)
    {
        const size_t eviction_index = selectEvictionIndex();
        if (eviction_index < entries_.size())
        {
            entries_.erase(entries_.begin() + static_cast<long>(eviction_index));
        }
    }

    NodeEntry entry{};
    entry.node_id = node_id;
    entry.snr = std::numeric_limits<float>::quiet_NaN();
    entry.rssi = std::numeric_limits<float>::quiet_NaN();
    entry.hops_away = 0xFF;
    entry.channel = 0xFF;
    entry.role = kNodeRoleUnknown;
    apply_to_entry(entry);
    entries_.push_back(entry);
    dirty_ = true;
    maybeSave();
}

void NodeStoreCore::upsert(uint32_t node_id, const char* short_name, const char* long_name,
                           uint32_t now_secs, float snr, float rssi, uint8_t protocol,
                           uint8_t role, uint8_t hops_away, uint8_t hw_model, uint8_t channel)
{
    NodeUpdate update{};
    update.short_name = short_name;
    update.long_name = long_name;
    update.has_last_seen = true;
    update.last_seen = now_secs;
    update.has_snr = !std::isnan(snr);
    update.snr = snr;
    update.has_rssi = !std::isnan(rssi);
    update.rssi = rssi;
    update.has_protocol = (protocol != 0);
    update.protocol = protocol;
    update.has_role = (role != kNodeRoleUnknown);
    update.role = role;
    update.has_hops_away = (hops_away != 0xFF);
    update.hops_away = hops_away;
    update.has_hw_model = (hw_model != 0);
    update.hw_model = hw_model;
    update.has_channel = (channel != 0xFF);
    update.channel = channel;
    applyUpdate(node_id, update);
}

void NodeStoreCore::updateProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs)
{
    if (protocol == 0)
    {
        return;
    }
    NodeUpdate update{};
    update.has_protocol = true;
    update.protocol = protocol;
    update.has_last_seen = true;
    update.last_seen = now_secs;
    applyUpdate(node_id, update);
}

void NodeStoreCore::updatePosition(uint32_t node_id, const NodePosition& position)
{
    NodeUpdate update{};
    update.has_position = true;
    update.position = position;
    applyUpdate(node_id, update);
}

bool NodeStoreCore::setNextHop(uint32_t node_id, uint8_t next_hop, uint32_t now_secs)
{
    if (node_id == 0)
    {
        return false;
    }

    if (getNextHop(node_id) == next_hop)
    {
        return true;
    }

    NodeUpdate update{};
    update.has_next_hop = true;
    update.next_hop = next_hop;
    if (now_secs != 0)
    {
        update.has_last_seen = true;
        update.last_seen = now_secs;
    }
    applyUpdate(node_id, update);
    return true;
}

uint8_t NodeStoreCore::getNextHop(uint32_t node_id) const
{
    for (const auto& entry : entries_)
    {
        if (entry.node_id == node_id)
        {
            return entry.next_hop;
        }
    }
    return 0;
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

bool NodeStoreCore::flush()
{
    if (!dirty_)
    {
        return true;
    }
    return saveEntries();
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

    const bool is_v8_blob = (len % sizeof(PersistedNodeEntryV8)) == 0;
    const bool is_v7_blob = (len % kSerializedEntrySize) == 0;
    const bool is_v6_blob = (len % kLegacySerializedEntrySize) == 0;
    if (!is_v8_blob && !is_v7_blob && !is_v6_blob)
    {
        return false;
    }

    const size_t entry_size = is_v8_blob ? sizeof(PersistedNodeEntryV8)
                                         : (is_v7_blob ? kSerializedEntrySize : kLegacySerializedEntrySize);
    size_t count = len / entry_size;
    if (count > kMaxNodes)
    {
        count = kMaxNodes;
    }

    entries_.clear();
    entries_.reserve(count);
    if (is_v8_blob)
    {
        auto* persisted = reinterpret_cast<const PersistedNodeEntryV8*>(data);
        for (size_t index = 0; index < count; ++index)
        {
            NodeEntry entry{};
            copyFromPersisted(entry, persisted[index]);
            entries_.push_back(entry);
        }
    }
    else if (is_v7_blob)
    {
        auto* persisted = reinterpret_cast<const PersistedNodeEntryV7*>(data);
        for (size_t index = 0; index < count; ++index)
        {
            NodeEntry entry{};
            copyFromPersisted(entry, persisted[index]);
            entries_.push_back(entry);
        }
    }
    else
    {
        auto* persisted = reinterpret_cast<const PersistedNodeEntryV6*>(data);
        for (size_t index = 0; index < count; ++index)
        {
            NodeEntry entry{};
            copyFromPersisted(entry, persisted[index]);
            entries_.push_back(entry);
        }
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

    std::vector<PersistedNodeEntryV8> persisted(entries_.size());
    for (size_t index = 0; index < entries_.size(); ++index)
    {
        copyIntoPersisted(persisted[index], entries_[index]);
    }

    out.resize(persisted.size() * sizeof(PersistedNodeEntryV8));
    memcpy(out.data(), persisted.data(), out.size());
}

void NodeStoreCore::maybeSave()
{
    if (!dirty_ || !auto_save_enabled_)
    {
        return;
    }

    const uint32_t now_ms = sys::millis_now();
    if (last_save_ms_ == 0 || (now_ms - last_save_ms_) >= kSaveIntervalMs)
    {
        saveEntries();
    }
}

size_t NodeStoreCore::selectEvictionIndex() const
{
    if (entries_.empty())
    {
        return 0;
    }

    auto is_protected = [this](uint32_t node_id) -> bool
    {
        return protected_node_checker_ ? protected_node_checker_(node_id) : false;
    };

    size_t oldest_unprotected_index = entries_.size();
    uint32_t oldest_unprotected_seen = std::numeric_limits<uint32_t>::max();
    size_t oldest_any_index = 0;
    uint32_t oldest_any_seen = std::numeric_limits<uint32_t>::max();

    for (size_t index = 0; index < entries_.size(); ++index)
    {
        const NodeEntry& entry = entries_[index];
        if (entry.last_seen < oldest_any_seen)
        {
            oldest_any_seen = entry.last_seen;
            oldest_any_index = index;
        }

        if (is_protected(entry.node_id))
        {
            continue;
        }

        if (entry.last_seen < oldest_unprotected_seen)
        {
            oldest_unprotected_seen = entry.last_seen;
            oldest_unprotected_index = index;
        }
    }

    return (oldest_unprotected_index < entries_.size()) ? oldest_unprotected_index : oldest_any_index;
}

} // namespace contacts
} // namespace chat
