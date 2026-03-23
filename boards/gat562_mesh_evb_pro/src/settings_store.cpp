#include "boards/gat562_mesh_evb_pro/settings_store.h"

#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"

#include <Arduino.h>
#include <InternalFileSystem.h>

#include <algorithm>
#include <cstring>

namespace boards::gat562_mesh_evb_pro::settings_store
{
namespace
{
using Adafruit_LittleFS_Namespace::FILE_O_READ;
using Adafruit_LittleFS_Namespace::FILE_O_WRITE;

constexpr const char* kSettingsPath = "/gat562_settings.bin";
constexpr const char* kSettingsTempPath = "/gat562_settings.bin.tmp";
constexpr const char* kSettingsBackupPath = "/gat562_settings.bin.bak";
constexpr const char* kSettingsCorruptPath = "/gat562_settings.bin.corrupt";
constexpr uint32_t kSettingsMagic = 0x53415447UL; // GTAS
constexpr uint16_t kSettingsVersion = 1;
constexpr uint8_t kDefaultToneVolume = 45;

struct PersistedPayload
{
    app::AppConfig config;
    uint8_t tone_volume = kDefaultToneVolume;
    uint8_t reserved[3] = {};
};

struct FileHeader
{
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t reserved = 0;
    uint32_t payload_size = 0;
    uint32_t crc32 = 0;
} __attribute__((packed));

struct CachedSettings
{
    app::AppConfig config;
    uint8_t tone_volume = kDefaultToneVolume;
};

bool s_cache_loaded = false;
CachedSettings s_cache{};
StoreStatus s_last_load_status = StoreStatus::NotFound;
StoreStatus s_last_save_status = StoreStatus::NotFound;

int8_t clampTxPower(int8_t value)
{
    if (value < app::AppConfig::kTxPowerMinDbm)
    {
        return app::AppConfig::kTxPowerMinDbm;
    }
    if (value > app::AppConfig::kTxPowerMaxDbm)
    {
        return app::AppConfig::kTxPowerMaxDbm;
    }
    return value;
}

uint8_t clampToneVolume(uint8_t volume)
{
    return static_cast<uint8_t>(std::min<unsigned>(volume, 100U));
}

const char* statusText(StoreStatus status)
{
    switch (status)
    {
    case StoreStatus::Ok:
        return "ok";
    case StoreStatus::NotFound:
        return "not_found";
    case StoreStatus::FsInitFailed:
        return "fs_init_failed";
    case StoreStatus::OpenFailed:
        return "open_failed";
    case StoreStatus::ReadFailed:
        return "read_failed";
    case StoreStatus::WriteFailed:
        return "write_failed";
    case StoreStatus::FlushFailed:
        return "flush_failed";
    case StoreStatus::HeaderInvalid:
        return "header_invalid";
    case StoreStatus::VersionMismatch:
        return "version_mismatch";
    case StoreStatus::PayloadSizeMismatch:
        return "payload_size_mismatch";
    case StoreStatus::CrcMismatch:
        return "crc_mismatch";
    case StoreStatus::RenameFailed:
        return "rename_failed";
    case StoreStatus::BackupFailed:
        return "backup_failed";
    default:
        return "unknown";
    }
}

bool ensureFs()
{
    if (InternalFS.begin())
    {
        return true;
    }
    Serial.printf("[gat562][settings] fs init failed\n");
    return false;
}

void removeIfExists(const char* path)
{
    if (path && InternalFS.exists(path))
    {
        InternalFS.remove(path);
    }
}

uint32_t crc32(const uint8_t* data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return ~crc;
}

bool quarantineCorruptFile(StoreStatus status)
{
    if (!InternalFS.exists(kSettingsPath))
    {
        return true;
    }

    removeIfExists(kSettingsCorruptPath);
    if (InternalFS.rename(kSettingsPath, kSettingsCorruptPath))
    {
        Serial.printf("[gat562][settings] quarantined corrupt store status=%s path=%s\n",
                      statusText(status),
                      kSettingsCorruptPath);
        return true;
    }

    Serial.printf("[gat562][settings] failed to quarantine corrupt store status=%s\n",
                  statusText(status));
    return false;
}

void resetCacheToDefaults()
{
    s_cache = CachedSettings{};
    s_cache.tone_volume = kDefaultToneVolume;
    normalizeConfig(s_cache.config);
}

bool loadFromFs()
{
    if (!ensureFs())
    {
        s_last_load_status = StoreStatus::FsInitFailed;
        return false;
    }

    if (!InternalFS.exists(kSettingsPath))
    {
        s_last_load_status = StoreStatus::NotFound;
        return false;
    }

    auto file = InternalFS.open(kSettingsPath, FILE_O_READ);
    if (!file)
    {
        s_last_load_status = StoreStatus::OpenFailed;
        Serial.printf("[gat562][settings] open failed path=%s\n", kSettingsPath);
        return false;
    }

    const uint32_t expected_size = static_cast<uint32_t>(sizeof(FileHeader) + sizeof(PersistedPayload));
    const uint32_t actual_size = file.size();
    if (actual_size != expected_size)
    {
        file.close();
        s_last_load_status = StoreStatus::PayloadSizeMismatch;
        (void)quarantineCorruptFile(s_last_load_status);
        Serial.printf("[gat562][settings] size mismatch actual=%lu expected=%lu\n",
                      static_cast<unsigned long>(actual_size),
                      static_cast<unsigned long>(expected_size));
        return false;
    }

    FileHeader header{};
    if (file.read(&header, sizeof(header)) != sizeof(header))
    {
        file.close();
        s_last_load_status = StoreStatus::ReadFailed;
        Serial.printf("[gat562][settings] header read failed\n");
        return false;
    }

    if (header.magic != kSettingsMagic)
    {
        file.close();
        s_last_load_status = StoreStatus::HeaderInvalid;
        (void)quarantineCorruptFile(s_last_load_status);
        Serial.printf("[gat562][settings] magic mismatch got=0x%08lX expected=0x%08lX\n",
                      static_cast<unsigned long>(header.magic),
                      static_cast<unsigned long>(kSettingsMagic));
        return false;
    }

    if (header.version != kSettingsVersion)
    {
        file.close();
        s_last_load_status = StoreStatus::VersionMismatch;
        (void)quarantineCorruptFile(s_last_load_status);
        Serial.printf("[gat562][settings] version mismatch got=%u expected=%u\n",
                      static_cast<unsigned>(header.version),
                      static_cast<unsigned>(kSettingsVersion));
        return false;
    }

    if (header.payload_size != sizeof(PersistedPayload))
    {
        file.close();
        s_last_load_status = StoreStatus::PayloadSizeMismatch;
        (void)quarantineCorruptFile(s_last_load_status);
        Serial.printf("[gat562][settings] payload size mismatch got=%lu expected=%lu\n",
                      static_cast<unsigned long>(header.payload_size),
                      static_cast<unsigned long>(sizeof(PersistedPayload)));
        return false;
    }

    PersistedPayload payload{};
    if (file.read(&payload, sizeof(payload)) != sizeof(payload))
    {
        file.close();
        s_last_load_status = StoreStatus::ReadFailed;
        Serial.printf("[gat562][settings] payload read failed\n");
        return false;
    }
    file.close();

    const uint32_t actual_crc = crc32(reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
    if (actual_crc != header.crc32)
    {
        s_last_load_status = StoreStatus::CrcMismatch;
        (void)quarantineCorruptFile(s_last_load_status);
        Serial.printf("[gat562][settings] crc mismatch got=0x%08lX expected=0x%08lX\n",
                      static_cast<unsigned long>(actual_crc),
                      static_cast<unsigned long>(header.crc32));
        return false;
    }

    s_cache.config = payload.config;
    normalizeConfig(s_cache.config);
    s_cache.tone_volume = clampToneVolume(payload.tone_volume);
    s_last_load_status = StoreStatus::Ok;
    Serial.printf("[gat562][settings] load ok tone=%u ble=%u proto=%u\n",
                  static_cast<unsigned>(s_cache.tone_volume),
                  static_cast<unsigned>(s_cache.config.ble_enabled ? 1 : 0),
                  static_cast<unsigned>(s_cache.config.mesh_protocol));
    return true;
}

bool saveToFs()
{
    if (!ensureFs())
    {
        s_last_save_status = StoreStatus::FsInitFailed;
        return false;
    }

    removeIfExists(kSettingsTempPath);

    auto file = InternalFS.open(kSettingsTempPath, FILE_O_WRITE);
    if (!file)
    {
        s_last_save_status = StoreStatus::OpenFailed;
        Serial.printf("[gat562][settings] temp open failed path=%s\n", kSettingsTempPath);
        return false;
    }

    PersistedPayload payload{};
    payload.config = s_cache.config;
    payload.tone_volume = clampToneVolume(s_cache.tone_volume);

    FileHeader header{};
    header.magic = kSettingsMagic;
    header.version = kSettingsVersion;
    header.payload_size = sizeof(payload);
    header.crc32 = crc32(reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));

    const size_t header_written = file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
    const size_t payload_written = header_written == sizeof(header)
                                       ? file.write(reinterpret_cast<const uint8_t*>(&payload), sizeof(payload))
                                       : 0U;
    file.flush();
    const bool write_ok = (header_written == sizeof(header) && payload_written == sizeof(payload));
    file.close();

    if (!write_ok)
    {
        removeIfExists(kSettingsTempPath);
        s_last_save_status = StoreStatus::WriteFailed;
        Serial.printf("[gat562][settings] write failed header=%lu payload=%lu\n",
                      static_cast<unsigned long>(header_written),
                      static_cast<unsigned long>(payload_written));
        return false;
    }

    removeIfExists(kSettingsBackupPath);
    if (InternalFS.exists(kSettingsPath) && !InternalFS.rename(kSettingsPath, kSettingsBackupPath))
    {
        removeIfExists(kSettingsTempPath);
        s_last_save_status = StoreStatus::BackupFailed;
        Serial.printf("[gat562][settings] backup rename failed main=%s backup=%s\n",
                      kSettingsPath,
                      kSettingsBackupPath);
        return false;
    }

    if (!InternalFS.rename(kSettingsTempPath, kSettingsPath))
    {
        if (InternalFS.exists(kSettingsBackupPath))
        {
            (void)InternalFS.rename(kSettingsBackupPath, kSettingsPath);
        }
        removeIfExists(kSettingsTempPath);
        s_last_save_status = StoreStatus::RenameFailed;
        Serial.printf("[gat562][settings] commit rename failed temp=%s main=%s\n",
                      kSettingsTempPath,
                      kSettingsPath);
        return false;
    }

    removeIfExists(kSettingsBackupPath);
    s_last_save_status = StoreStatus::Ok;
    Serial.printf("[gat562][settings] save ok tone=%u ble=%u proto=%u\n",
                  static_cast<unsigned>(payload.tone_volume),
                  static_cast<unsigned>(payload.config.ble_enabled ? 1 : 0),
                  static_cast<unsigned>(payload.config.mesh_protocol));
    return true;
}

void ensureCacheLoaded()
{
    if (s_cache_loaded)
    {
        return;
    }

    resetCacheToDefaults();
    (void)loadFromFs();
    s_cache_loaded = true;
}

} // namespace

void normalizeConfig(app::AppConfig& config)
{
    Serial.printf("[gat562][settings] normalize start proto=%u mt_region=%u ok_to_mqtt=%u ignore_mqtt=%u gps_ms=%lu\n",
                  static_cast<unsigned>(config.mesh_protocol),
                  static_cast<unsigned>(config.meshtastic_config.region),
                  config.meshtastic_config.config_ok_to_mqtt ? 1U : 0U,
                  config.meshtastic_config.ignore_mqtt ? 1U : 0U,
                  static_cast<unsigned long>(config.gps_interval_ms));
    Serial2.printf("[gat562][settings] normalize start proto=%u mt_region=%u ok_to_mqtt=%u ignore_mqtt=%u gps_ms=%lu\n",
                   static_cast<unsigned>(config.mesh_protocol),
                   static_cast<unsigned>(config.meshtastic_config.region),
                   config.meshtastic_config.config_ok_to_mqtt ? 1U : 0U,
                   config.meshtastic_config.ignore_mqtt ? 1U : 0U,
                   static_cast<unsigned long>(config.gps_interval_ms));
    if (!chat::infra::isValidMeshProtocol(config.mesh_protocol))
    {
        config.mesh_protocol = chat::MeshProtocol::Meshtastic;
    }
    Serial.printf("[gat562][settings] normalize post-proto proto=%u\n",
                  static_cast<unsigned>(config.mesh_protocol));
    Serial2.printf("[gat562][settings] normalize post-proto proto=%u\n",
                   static_cast<unsigned>(config.mesh_protocol));

    if (chat::meshtastic::findRegion(
            static_cast<meshtastic_Config_LoRaConfig_RegionCode>(config.meshtastic_config.region)) == nullptr)
    {
        config.meshtastic_config.region = app::AppConfig::kDefaultRegionCode;
    }
    Serial.printf("[gat562][settings] normalize post-region mt_region=%u\n",
                  static_cast<unsigned>(config.meshtastic_config.region));
    Serial2.printf("[gat562][settings] normalize post-region mt_region=%u\n",
                   static_cast<unsigned>(config.meshtastic_config.region));

    config.meshtastic_config.tx_power = clampTxPower(config.meshtastic_config.tx_power);
    config.meshcore_config.tx_power = clampTxPower(config.meshcore_config.tx_power);
    Serial.printf("[gat562][settings] normalize post-tx mt_tx=%d mc_tx=%d\n",
                  static_cast<int>(config.meshtastic_config.tx_power),
                  static_cast<int>(config.meshcore_config.tx_power));
    Serial2.printf("[gat562][settings] normalize post-tx mt_tx=%d mc_tx=%d\n",
                   static_cast<int>(config.meshtastic_config.tx_power),
                   static_cast<int>(config.meshcore_config.tx_power));

    if (chat::meshcore::isValidRegionPresetId(config.meshcore_config.meshcore_region_preset) &&
        config.meshcore_config.meshcore_region_preset > 0)
    {
        if (const chat::meshcore::RegionPreset* preset =
                chat::meshcore::findRegionPresetById(config.meshcore_config.meshcore_region_preset))
        {
            config.meshcore_config.meshcore_freq_mhz = preset->freq_mhz;
            config.meshcore_config.meshcore_bw_khz = preset->bw_khz;
            config.meshcore_config.meshcore_sf = preset->sf;
            config.meshcore_config.meshcore_cr = preset->cr;
        }
    }
    else
    {
        config.meshcore_config.meshcore_region_preset = 0;
    }
    Serial.printf("[gat562][settings] normalize post-mc-preset preset=%u\n",
                  static_cast<unsigned>(config.meshcore_config.meshcore_region_preset));
    Serial2.printf("[gat562][settings] normalize post-mc-preset preset=%u\n",
                   static_cast<unsigned>(config.meshcore_config.meshcore_region_preset));

    if (config.gps_interval_ms == 0)
    {
        config.gps_interval_ms = 60000UL;
    }
    Serial.printf("[gat562][settings] normalize done gps_ms=%lu\n",
                  static_cast<unsigned long>(config.gps_interval_ms));
    Serial2.printf("[gat562][settings] normalize done gps_ms=%lu\n",
                   static_cast<unsigned long>(config.gps_interval_ms));
}

bool loadAppConfig(app::AppConfig& config)
{
    ensureCacheLoaded();
    config = s_cache.config;
    normalizeConfig(config);
    s_cache.config = config;
    return s_last_load_status == StoreStatus::Ok;
}

bool saveAppConfig(const app::AppConfig& config)
{
    ensureCacheLoaded();
    s_cache.config = config;
    normalizeConfig(s_cache.config);
    return saveToFs();
}

uint8_t loadMessageToneVolume()
{
    ensureCacheLoaded();
    return s_cache.tone_volume;
}

bool saveMessageToneVolume(uint8_t volume)
{
    ensureCacheLoaded();
    s_cache.tone_volume = clampToneVolume(volume);
    return saveToFs();
}

StoreStatus lastLoadStatus()
{
    return s_last_load_status;
}

StoreStatus lastSaveStatus()
{
    return s_last_save_status;
}

const char* statusLabel(StoreStatus status)
{
    return statusText(status);
}

} // namespace boards::gat562_mesh_evb_pro::settings_store
