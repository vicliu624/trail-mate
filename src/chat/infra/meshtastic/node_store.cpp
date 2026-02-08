/**
 * @file node_store.cpp
 * @brief Lightweight persisted NodeInfo store (SD-first, Preferences fallback)
 */

#include "node_store.h"
#include "../../ports/i_node_store.h"
#include "node_persist.h"
#include <cstring>
#include <limits>
#include <cmath>
#include <esp_err.h>
#include <nvs.h>

namespace chat
{
namespace meshtastic
{

namespace
{
uint32_t crc32(const uint8_t* data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
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

void logNvsStats(const char* tag, const char* ns)
{
    nvs_stats_t stats{};
    esp_err_t err = nvs_get_stats(nullptr, &stats);
    if (err == ESP_OK)
    {
        Serial.printf("[NodeStore] NVS stats(%s): used=%u free=%u total=%u namespaces=%u\n",
                      tag ? tag : "?",
                      static_cast<unsigned>(stats.used_entries),
                      static_cast<unsigned>(stats.free_entries),
                      static_cast<unsigned>(stats.total_entries),
                      static_cast<unsigned>(stats.namespace_count));
    }
    else
    {
        Serial.printf("[NodeStore] NVS stats(%s) err=%s\n",
                      tag ? tag : "?",
                      esp_err_to_name(err));
    }
    if (ns && ns[0])
    {
        nvs_handle_t handle;
        err = nvs_open(ns, NVS_READONLY, &handle);
        if (err == ESP_OK)
        {
            Serial.printf("[NodeStore] NVS open ns=%s ok\n", ns);
            nvs_close(handle);
        }
        else
        {
            Serial.printf("[NodeStore] NVS open ns=%s err=%s\n", ns, esp_err_to_name(err));
        }
    }
}

} // namespace

void NodeStore::begin()
{
    const bool sd_available = (SD.cardType() != CARD_NONE);
    if (sd_available && loadFromSd())
    {
        use_sd_ = true;
        return;
    }

    use_sd_ = sd_available;
    if (loadFromNvs())
    {
        if (use_sd_)
        {
            saveToSd();
        }
        return;
    }

    entries_.clear();
    Serial.printf("[NodeStore] loaded=0\n");
}

void NodeStore::upsert(uint32_t node_id, const char* short_name, const char* long_name,
                       uint32_t now_secs, float snr, float rssi, uint8_t protocol, uint8_t role,
                       uint8_t hops_away)
{
    Serial.printf("[NodeStore] upsert node=%08lX ts=%lu snr=%.1f rssi=%.1f\n",
                  (unsigned long)node_id,
                  (unsigned long)now_secs,
                  snr,
                  rssi);
    // find existing
    for (auto& e : entries_)
    {
        if (e.node_id == node_id)
        {
            if (short_name && short_name[0] != '\0')
            {
                strncpy(e.short_name, short_name, sizeof(e.short_name) - 1);
                e.short_name[sizeof(e.short_name) - 1] = '\0';
            }
            if (long_name && long_name[0] != '\0')
            {
                strncpy(e.long_name, long_name, sizeof(e.long_name) - 1);
                e.long_name[sizeof(e.long_name) - 1] = '\0';
            }
            e.last_seen = now_secs;
            if (!std::isnan(snr))
            {
                e.snr = snr;
            }
            if (!std::isnan(rssi))
            {
                e.rssi = rssi;
            }
            if (hops_away != 0xFF)
            {
                e.hops_away = hops_away;
            }
            if (protocol != 0)
            {
                e.protocol = protocol;
            }
            if (role != contacts::kNodeRoleUnknown)
            {
                e.role = role;
            }
            dirty_ = true;
            maybeSave();
            return;
        }
    }

    if (entries_.size() >= kMaxNodes)
    {
        entries_.erase(entries_.begin()); // drop oldest
    }
    contacts::NodeEntry e{};
    e.node_id = node_id;
    if (short_name && short_name[0] != '\0')
    {
        strncpy(e.short_name, short_name, sizeof(e.short_name) - 1);
        e.short_name[sizeof(e.short_name) - 1] = '\0';
    }
    if (long_name && long_name[0] != '\0')
    {
        strncpy(e.long_name, long_name, sizeof(e.long_name) - 1);
        e.long_name[sizeof(e.long_name) - 1] = '\0';
    }
    e.last_seen = now_secs;
    e.snr = snr;
    e.rssi = rssi;
    e.hops_away = hops_away;
    e.protocol = protocol;
    e.role = (role != contacts::kNodeRoleUnknown) ? role : contacts::kNodeRoleUnknown;
    entries_.push_back(e);
    dirty_ = true;
    maybeSave();
}

void NodeStore::updateProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs)
{
    if (protocol == 0)
    {
        return;
    }
    for (auto& e : entries_)
    {
        if (e.node_id == node_id)
        {
            e.protocol = protocol;
            e.last_seen = now_secs;
            dirty_ = true;
            maybeSave();
            return;
        }
    }

    if (entries_.size() >= kMaxNodes)
    {
        entries_.erase(entries_.begin()); // drop oldest
    }
    contacts::NodeEntry e{};
    e.node_id = node_id;
    e.short_name[0] = '\0';
    e.long_name[0] = '\0';
    e.last_seen = now_secs;
    e.snr = std::numeric_limits<float>::quiet_NaN();
    e.rssi = std::numeric_limits<float>::quiet_NaN();
    e.hops_away = 0xFF;
    e.protocol = protocol;
    entries_.push_back(e);
    dirty_ = true;
    maybeSave();
}

void NodeStore::save()
{
    if (use_sd_)
    {
        if (saveToSd())
        {
            Serial.printf("[NodeStore] save ok (SD) count=%u\n",
                          static_cast<unsigned>(entries_.size()));
            return;
        }
    }

    Preferences prefs;
    if (!prefs.begin(kPersistNodesNs, false))
    {
        Serial.printf("[NodeStore] save failed ns=%s\n", kPersistNodesNs);
        logNvsStats("save-open", kPersistNodesNs);
        return;
    }
    if (!entries_.empty())
    {
        std::vector<PersistedNodeEntry> persisted;
        persisted.reserve(entries_.size());
        for (const auto& src : entries_)
        {
            PersistedNodeEntry dst{};
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
            persisted.push_back(dst);
        }
        size_t expected = persisted.size() * sizeof(PersistedNodeEntry);
        size_t written = prefs.putBytes(kPersistNodesKey, persisted.data(), expected);
        if (written != expected)
        {
            prefs.remove(kPersistNodesKey);
            written = prefs.putBytes(kPersistNodesKey, persisted.data(), expected);
        }
        uint32_t crc = crc32(reinterpret_cast<const uint8_t*>(persisted.data()), expected);
        prefs.putUChar(kPersistNodesKeyVer, kPersistVersion);
        prefs.putUInt(kPersistNodesKeyCrc, crc);
        if (written != expected)
        {
            Serial.printf("[NodeStore] save failed wrote=%u expected=%u\n",
                          static_cast<unsigned>(written),
                          static_cast<unsigned>(expected));
            logNvsStats("save-write", kPersistNodesNs);
        }
        else
        {
            size_t verify_len = prefs.getBytesLength(kPersistNodesKey);
            uint8_t verify_ver = prefs.getUChar(kPersistNodesKeyVer, 0);
            uint32_t verify_crc = prefs.getUInt(kPersistNodesKeyCrc, 0);
            Serial.printf("[NodeStore] save ok len=%u ver=%u crc=%08lX\n",
                          static_cast<unsigned>(verify_len),
                          static_cast<unsigned>(verify_ver),
                          static_cast<unsigned long>(verify_crc));
        }
    }
    else
    {
        prefs.remove(kPersistNodesKey);
        prefs.remove(kPersistNodesKeyVer);
        prefs.remove(kPersistNodesKeyCrc);
    }
    prefs.end();
    Serial.printf("[NodeStore] saved=%u\n", static_cast<unsigned>(entries_.size()));
}

void NodeStore::maybeSave()
{
    if (!dirty_)
    {
        return;
    }
    uint32_t now_ms = millis();
    if (last_save_ms_ == 0 || (now_ms - last_save_ms_) >= kSaveIntervalMs)
    {
        save();
        last_save_ms_ = now_ms;
        dirty_ = false;
    }
}

void NodeStore::clear()
{
    entries_.clear();
    dirty_ = false;
    last_save_ms_ = 0;
    if (SD.cardType() != CARD_NONE)
    {
        if (SD.exists(kPersistNodesFile))
        {
            SD.remove(kPersistNodesFile);
        }
    }
    Preferences prefs;
    if (!prefs.begin(kPersistNodesNs, false))
    {
        return;
    }
    prefs.remove(kPersistNodesKey);
    prefs.remove(kPersistNodesKeyVer);
    prefs.remove(kPersistNodesKeyCrc);
    prefs.end();
}

bool NodeStore::loadFromNvs()
{
    Preferences prefs;
    if (!prefs.begin(kPersistNodesNs, false))
    {
        Serial.printf("[NodeStore] begin failed ns=%s\n", kPersistNodesNs);
        logNvsStats("begin", kPersistNodesNs);
        return false;
    }
    size_t len = prefs.getBytesLength(kPersistNodesKey);
    uint8_t ver = prefs.getUChar(kPersistNodesKeyVer, 0);
    bool has_crc = prefs.isKey(kPersistNodesKeyCrc);
    uint32_t stored_crc = has_crc ? prefs.getUInt(kPersistNodesKeyCrc, 0) : 0;
    Serial.printf("[NodeStore] blob len=%u ver=%u crc=%08lX has_crc=%u\n",
                  static_cast<unsigned>(len),
                  static_cast<unsigned>(ver),
                  static_cast<unsigned long>(stored_crc),
                  has_crc ? 1U : 0U);
    if (len == 0 && has_crc)
    {
        Serial.printf("[NodeStore] stale meta detected, clearing ver/crc\n");
        prefs.remove(kPersistNodesKeyVer);
        prefs.remove(kPersistNodesKeyCrc);
    }
    if (len == 0)
    {
        entries_.clear();
        prefs.end();
        return true;
    }

    if (len > 0 && (len % sizeof(PersistedNodeEntry) == 0))
    {
        size_t count = len / sizeof(PersistedNodeEntry);
        if (count > kPersistMaxNodes)
        {
            count = kPersistMaxNodes;
        }
        std::vector<PersistedNodeEntry> persisted(count);
        prefs.getBytes(kPersistNodesKey, persisted.data(), count * sizeof(PersistedNodeEntry));
        if (!has_crc)
        {
            prefs.end();
            Serial.printf("[NodeStore] missing crc\n");
            clear();
            return false;
        }
        if (ver != kPersistVersion)
        {
            prefs.end();
            Serial.printf("[NodeStore] version mismatch stored=%u expected=%u\n",
                          static_cast<unsigned>(ver),
                          static_cast<unsigned>(kPersistVersion));
            clear();
            return false;
        }
        uint32_t calc_crc = crc32(reinterpret_cast<const uint8_t*>(persisted.data()),
                                  persisted.size() * sizeof(PersistedNodeEntry));
        if (calc_crc != stored_crc)
        {
            prefs.end();
            Serial.printf("[NodeStore] crc mismatch stored=%08lX calc=%08lX\n",
                          static_cast<unsigned long>(stored_crc),
                          static_cast<unsigned long>(calc_crc));
            clear();
            return false;
        }
        entries_.clear();
        entries_.reserve(count);
        for (const auto& src : persisted)
        {
            contacts::NodeEntry dst{};
            dst.node_id = src.node_id;
            memcpy(dst.short_name, src.short_name, sizeof(dst.short_name));
            dst.short_name[sizeof(dst.short_name) - 1] = '\0';
            memcpy(dst.long_name, src.long_name, sizeof(dst.long_name));
            dst.long_name[sizeof(dst.long_name) - 1] = '\0';
            dst.last_seen = src.last_seen;
            dst.snr = src.snr;
            dst.rssi = src.rssi;
            dst.hops_away = src.hops_away;
            dst.protocol = src.protocol;
            dst.role = src.role;
            entries_.push_back(dst);
        }
    }
    else if (len > 0)
    {
        Serial.printf("[NodeStore] invalid blob size=%u\n", static_cast<unsigned>(len));
        prefs.end();
        clear();
        return false;
    }
    prefs.end();
    Serial.printf("[NodeStore] loaded=%u\n", static_cast<unsigned>(entries_.size()));
    return true;
}

bool NodeStore::loadFromSd()
{
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }
    File f = SD.open(kPersistNodesFile, FILE_READ);
    if (!f)
    {
        return false;
    }

    struct Header
    {
        uint8_t ver = 0;
        uint8_t reserved[3] = {0, 0, 0};
        uint32_t crc = 0;
        uint32_t count = 0;
    } header{};

    if (f.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header))
    {
        f.close();
        return false;
    }
    if (header.ver != kPersistVersion || header.count > kPersistMaxNodes)
    {
        f.close();
        return false;
    }

    size_t expected_bytes = header.count * sizeof(PersistedNodeEntry);
    std::vector<PersistedNodeEntry> persisted(header.count);
    size_t read_bytes = f.read(reinterpret_cast<uint8_t*>(persisted.data()), expected_bytes);
    f.close();
    if (read_bytes != expected_bytes)
    {
        return false;
    }

    uint32_t calc_crc = crc32(reinterpret_cast<const uint8_t*>(persisted.data()), expected_bytes);
    if (calc_crc != header.crc)
    {
        return false;
    }

    entries_.clear();
    entries_.reserve(header.count);
    for (const auto& src : persisted)
    {
        contacts::NodeEntry dst{};
        dst.node_id = src.node_id;
        memcpy(dst.short_name, src.short_name, sizeof(dst.short_name));
        dst.short_name[sizeof(dst.short_name) - 1] = '\0';
        memcpy(dst.long_name, src.long_name, sizeof(dst.long_name));
        dst.long_name[sizeof(dst.long_name) - 1] = '\0';
        dst.last_seen = src.last_seen;
        dst.snr = src.snr;
        dst.rssi = src.rssi;
        dst.hops_away = src.hops_away;
        dst.protocol = src.protocol;
        dst.role = src.role;
        entries_.push_back(dst);
    }
    Serial.printf("[NodeStore] loaded=%u (SD)\n", static_cast<unsigned>(entries_.size()));
    return true;
}

bool NodeStore::saveToSd() const
{
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }

    if (SD.exists(kPersistNodesFile))
    {
        SD.remove(kPersistNodesFile);
    }

    File f = SD.open(kPersistNodesFile, FILE_WRITE);
    if (!f)
    {
        return false;
    }

    struct Header
    {
        uint8_t ver = kPersistVersion;
        uint8_t reserved[3] = {0, 0, 0};
        uint32_t crc = 0;
        uint32_t count = 0;
    } header{};

    std::vector<PersistedNodeEntry> persisted;
    persisted.reserve(entries_.size());
    for (const auto& src : entries_)
    {
        PersistedNodeEntry dst{};
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
        persisted.push_back(dst);
    }

    header.count = static_cast<uint32_t>(persisted.size());
    header.crc = crc32(reinterpret_cast<const uint8_t*>(persisted.data()),
                       persisted.size() * sizeof(PersistedNodeEntry));

    bool ok = true;
    ok = (f.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header));
    if (ok && !persisted.empty())
    {
        const size_t bytes = persisted.size() * sizeof(PersistedNodeEntry);
        ok = (f.write(reinterpret_cast<const uint8_t*>(persisted.data()), bytes) == bytes);
    }
    f.close();
    return ok;
}

} // namespace meshtastic
} // namespace chat
