/**
 * @file node_store.cpp
 * @brief Lightweight persisted NodeInfo store shell (SD-first, Preferences fallback)
 */

#include "platform/esp/arduino_common/chat/infra/meshtastic/node_store.h"
#include "../../internal/blob_store_io.h"
#include "chat/infra/node_store_blob_format.h"

#include <SD.h>
#include <cstdio>
#include <esp_err.h>
#include <nvs.h>

namespace chat
{
namespace meshtastic
{

#ifndef NODE_STORE_LOG_ENABLE
#define NODE_STORE_LOG_ENABLE 0
#endif

#if NODE_STORE_LOG_ENABLE
#define NODE_STORE_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define NODE_STORE_LOG(...)
#endif

namespace
{

void logNvsStats(const char* tag, const char* ns)
{
    nvs_stats_t stats{};
    esp_err_t err = nvs_get_stats(nullptr, &stats);
    if (err == ESP_OK)
    {
        NODE_STORE_LOG("[NodeStore] NVS stats(%s): used=%u free=%u total=%u namespaces=%u\n",
                       tag ? tag : "?",
                       static_cast<unsigned>(stats.used_entries),
                       static_cast<unsigned>(stats.free_entries),
                       static_cast<unsigned>(stats.total_entries),
                       static_cast<unsigned>(stats.namespace_count));
    }
    else
    {
        NODE_STORE_LOG("[NodeStore] NVS stats(%s) err=%s\n",
                       tag ? tag : "?",
                       esp_err_to_name(err));
    }

    if (ns && ns[0])
    {
        nvs_handle_t handle;
        err = nvs_open(ns, NVS_READONLY, &handle);
        if (err == ESP_OK)
        {
            NODE_STORE_LOG("[NodeStore] NVS open ns=%s ok\n", ns);
            nvs_close(handle);
        }
        else
        {
            NODE_STORE_LOG("[NodeStore] NVS open ns=%s err=%s\n", ns, esp_err_to_name(err));
        }
    }
}

} // namespace

NodeStore::NodeStore()
    : core_(*this)
{
}

void NodeStore::begin()
{
    core_.begin();
}

void NodeStore::applyUpdate(uint32_t node_id, const contacts::NodeUpdate& update)
{
    core_.applyUpdate(node_id, update);
}

void NodeStore::upsert(uint32_t node_id, const char* short_name, const char* long_name,
                       uint32_t now_secs, float snr, float rssi, uint8_t protocol,
                       uint8_t role, uint8_t hops_away, uint8_t hw_model, uint8_t channel)
{
    NODE_STORE_LOG("[NodeStore] upsert node=%08lX ts=%lu snr=%.1f rssi=%.1f\n",
                   static_cast<unsigned long>(node_id),
                   static_cast<unsigned long>(now_secs),
                   snr,
                   rssi);
    core_.upsert(node_id, short_name, long_name, now_secs, snr, rssi,
                 protocol, role, hops_away, hw_model, channel);
}

void NodeStore::updateProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs)
{
    core_.updateProtocol(node_id, protocol, now_secs);
}

void NodeStore::updatePosition(uint32_t node_id, const contacts::NodePosition& position)
{
    core_.updatePosition(node_id, position);
}

bool NodeStore::remove(uint32_t node_id)
{
    const bool removed = core_.remove(node_id);
    if (removed)
    {
        NODE_STORE_LOG("[NodeStore] remove node=%08lX remaining=%u\n",
                       static_cast<unsigned long>(node_id),
                       static_cast<unsigned>(core_.getEntries().size()));
    }
    return removed;
}

const std::vector<contacts::NodeEntry>& NodeStore::getEntries() const
{
    return core_.getEntries();
}

void NodeStore::clear()
{
    core_.clear();
}

bool NodeStore::flush()
{
    return core_.flush();
}

bool NodeStore::loadBlob(std::vector<uint8_t>& out)
{
    out.clear();

    const bool sd_available = (SD.cardType() != CARD_NONE);
    if (sd_available && loadFromSd(out))
    {
        return true;
    }

    if (loadFromNvs(out))
    {
        if (sd_available)
        {
            saveToSd(out.data(), out.size());
        }
        return true;
    }

    return false;
}

bool NodeStore::saveBlob(const uint8_t* data, size_t len)
{
    const bool sd_available = (SD.cardType() != CARD_NONE);
    if (sd_available && saveToSd(data, len))
    {
        NODE_STORE_LOG("[NodeStore] save ok (SD) count=%u\n",
                       static_cast<unsigned>(contacts::nodeBlobEntryCount(len)));
        return true;
    }

    const bool ok = saveToNvs(data, len);
    if (ok)
    {
        NODE_STORE_LOG("[NodeStore] saved=%u\n",
                       static_cast<unsigned>(contacts::nodeBlobEntryCount(len)));
    }
    return ok;
}

void NodeStore::clearBlob()
{
    if (SD.cardType() != CARD_NONE && SD.exists(kPersistNodesFile))
    {
        SD.remove(kPersistNodesFile);
    }
    clearNvs();
}

bool NodeStore::loadFromNvs(std::vector<uint8_t>& out)
{
    out.clear();

    chat::infra::PreferencesBlobMetadata meta;
    if (!chat::infra::loadRawBlobFromPreferencesWithMetadata(kPersistNodesNs,
                                                             kPersistNodesKey,
                                                             kPersistNodesKeyVer,
                                                             kPersistNodesKeyCrc,
                                                             out,
                                                             &meta))
    {
        NODE_STORE_LOG("[NodeStore] begin failed ns=%s\n", kPersistNodesNs);
        logNvsStats("begin", kPersistNodesNs);
        return false;
    }

    NODE_STORE_LOG("[NodeStore] blob len=%u ver=%u crc=%08lX has_crc=%u\n",
                   static_cast<unsigned>(meta.len),
                   static_cast<unsigned>(meta.version),
                   static_cast<unsigned long>(meta.crc),
                   meta.has_crc ? 1U : 0U);

    if (meta.len == 0)
    {
        if (meta.has_crc || meta.has_version)
        {
            NODE_STORE_LOG("[NodeStore] stale meta detected, clearing ver/crc\n");
            chat::infra::clearPreferencesKeys(kPersistNodesNs,
                                              kPersistNodesKeyVer,
                                              kPersistNodesKeyCrc);
        }
        return true;
    }

    if (!contacts::isValidNodeBlobSize(meta.len) ||
        contacts::nodeBlobEntryCount(meta.len) > contacts::NodeStoreCore::kMaxNodes)
    {
        NODE_STORE_LOG("[NodeStore] invalid blob size=%u\n", static_cast<unsigned>(meta.len));
        out.clear();
        clearBlob();
        return false;
    }

    const contacts::NodeBlobValidation validation =
        contacts::validateNodeBlobMetadata(meta.len,
                                           meta.has_version ? meta.version : 0,
                                           meta.has_crc,
                                           meta.crc,
                                           out.data());
    if (validation != contacts::NodeBlobValidation::Ok)
    {
        if (validation == contacts::NodeBlobValidation::MissingCrc)
        {
            NODE_STORE_LOG("[NodeStore] missing crc\n");
        }
        else if (validation == contacts::NodeBlobValidation::VersionMismatch)
        {
            NODE_STORE_LOG("[NodeStore] version mismatch stored=%u expected=%u\n",
                           static_cast<unsigned>(meta.version),
                           static_cast<unsigned>(contacts::NodeStoreCore::kPersistVersion));
        }
        else if (validation == contacts::NodeBlobValidation::CrcMismatch)
        {
            const uint32_t calc_crc = contacts::NodeStoreCore::computeBlobCrc(out.data(), out.size());
            NODE_STORE_LOG("[NodeStore] crc mismatch stored=%08lX calc=%08lX\n",
                           static_cast<unsigned long>(meta.crc),
                           static_cast<unsigned long>(calc_crc));
        }
        out.clear();
        clearBlob();
        return false;
    }

    NODE_STORE_LOG("[NodeStore] loaded=%u\n",
                   static_cast<unsigned>(contacts::nodeBlobEntryCount(out.size())));
    return true;
}

bool NodeStore::loadFromSd(std::vector<uint8_t>& out) const
{
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }

    File file = SD.open(kPersistNodesFile, FILE_READ);
    if (!file)
    {
        return false;
    }

    contacts::NodeStoreSdHeader header{};
    if (file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header))
    {
        file.close();
        return false;
    }

    if (contacts::validateNodeStoreSdHeader(header) != contacts::NodeBlobValidation::Ok)
    {
        file.close();
        return false;
    }

    const size_t entry_size = (header.ver == contacts::NodeStoreCore::kPersistVersion)
                                  ? contacts::NodeStoreCore::kSerializedEntrySize
                                  : contacts::NodeStoreCore::kLegacySerializedEntrySize;
    const size_t expected_bytes = header.count * entry_size;
    out.resize(expected_bytes);
    const size_t read_bytes = expected_bytes > 0 ? file.read(out.data(), expected_bytes) : 0;
    file.close();

    if (read_bytes != expected_bytes)
    {
        out.clear();
        return false;
    }

    if (contacts::validateNodeStoreSdBlob(header, out.data(), out.size()) != contacts::NodeBlobValidation::Ok)
    {
        out.clear();
        return false;
    }

    NODE_STORE_LOG("[NodeStore] loaded=%u (SD)\n", static_cast<unsigned>(header.count));
    return true;
}

bool NodeStore::saveToNvs(const uint8_t* data, size_t len) const
{
    chat::infra::PreferencesBlobMetadata meta;
    if (data && len > 0)
    {
        meta.len = len;
        meta.has_version = true;
        meta.version = contacts::NodeStoreCore::kPersistVersion;
        meta.has_crc = true;
        meta.crc = contacts::NodeStoreCore::computeBlobCrc(data, len);
    }

    const bool ok = chat::infra::saveRawBlobToPreferencesWithMetadata(kPersistNodesNs,
                                                                      kPersistNodesKey,
                                                                      kPersistNodesKeyVer,
                                                                      kPersistNodesKeyCrc,
                                                                      data,
                                                                      len,
                                                                      &meta,
                                                                      true);
    if (!ok)
    {
        NODE_STORE_LOG("[NodeStore] save failed ns=%s\n", kPersistNodesNs);
        logNvsStats("save-write", kPersistNodesNs);
    }
    return ok;
}

bool NodeStore::saveToSd(const uint8_t* data, size_t len) const
{
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }
    if (!contacts::isValidNodeBlobSize(len) ||
        contacts::nodeBlobEntryCount(len) > contacts::NodeStoreCore::kMaxNodes)
    {
        return false;
    }

    if (SD.exists(kPersistNodesFile))
    {
        SD.remove(kPersistNodesFile);
    }

    File file = SD.open(kPersistNodesFile, FILE_WRITE);
    if (!file)
    {
        return false;
    }

    contacts::NodeStoreSdHeader header = contacts::makeNodeStoreSdHeader(data, len);

    bool ok = (file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header));
    if (ok && data && len > 0)
    {
        ok = (file.write(data, len) == len);
    }

    file.close();
    return ok;
}

void NodeStore::clearNvs() const
{
    chat::infra::clearPreferencesKeys(kPersistNodesNs,
                                      kPersistNodesKey,
                                      kPersistNodesKeyVer,
                                      kPersistNodesKeyCrc);
}

} // namespace meshtastic
} // namespace chat
