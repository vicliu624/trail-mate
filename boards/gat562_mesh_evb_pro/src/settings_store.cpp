#include "boards/gat562_mesh_evb_pro/settings_store.h"

#include "boards/gat562_mesh_evb_pro/gat562_board.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "platform/nrf52/arduino_common/internal_fs_utils.h"

#include <Arduino.h>
#include <InternalFileSystem.h>

#include <algorithm>
#include <cstring>

namespace boards::gat562_mesh_evb_pro::settings_store
{
namespace
{
using Adafruit_LittleFS_Namespace::FILE_O_READ;

constexpr const char* kSettingsPath = "/gat562_settings.bin";
constexpr const char* kSettingsCorruptPath = "/gat562_settings.bin.corrupt";
constexpr const char* kLogTag = "[gat562][settings]";
constexpr uint32_t kSettingsMagic = 0x53415447UL; // GTAS
constexpr uint16_t kSettingsVersion = 2;
constexpr uint8_t kDefaultToneVolume = 45;
constexpr uint32_t kDeferredSaveDebounceMs = 1500UL;
constexpr uint32_t kImmediateSaveRetryDelayMs = 20UL;

struct PersistedPayloadV1
{
    app::AppConfig config;
    uint8_t tone_volume = kDefaultToneVolume;
    uint8_t reserved[3] = {};
};

struct PersistedPayload
{
    app::AppConfig config;
    uint8_t tone_volume = kDefaultToneVolume;
    uint8_t has_meshtastic_ble_state = 0;
    uint8_t reserved[2] = {};
    meshtastic_Config_BluetoothConfig meshtastic_ble_bluetooth = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig meshtastic_ble_module = meshtastic_LocalModuleConfig_init_zero;
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
    bool has_meshtastic_ble_state = false;
    meshtastic_Config_BluetoothConfig meshtastic_ble_bluetooth = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig meshtastic_ble_module = meshtastic_LocalModuleConfig_init_zero;
};

bool s_cache_loaded = false;
CachedSettings s_cache{};
StoreStatus s_last_load_status = StoreStatus::NotFound;
StoreStatus s_last_save_status = StoreStatus::NotFound;
bool s_deferred_save_pending = false;
uint32_t s_last_dirty_ms = 0;
bool s_save_in_progress = false;
uint32_t s_last_save_attempt_ms = 0;
// Keep persistence payload scratch off the nRF52 task stack. Protocol switching
// can nest save + verify while UI/GPS code continues to run, and these payloads
// are large enough to make the stack path fragile.
FileHeader s_file_header_scratch{};
PersistedPayload s_payload_scratch{};
PersistedPayloadV1 s_payload_v1_scratch{};

class ScopedGpsSuspend
{
  public:
    ScopedGpsSuspend()
        : board_(&::boards::gat562_mesh_evb_pro::Gat562Board::instance()),
          resume_(board_->gpsEnabled())
    {
        if (resume_)
        {
            board_->suspendGps();
        }
    }

    ~ScopedGpsSuspend()
    {
        if (resume_)
        {
            board_->resumeGps();
        }
    }

    ScopedGpsSuspend(const ScopedGpsSuspend&) = delete;
    ScopedGpsSuspend& operator=(const ScopedGpsSuspend&) = delete;

  private:
    ::boards::gat562_mesh_evb_pro::Gat562Board* board_ = nullptr;
    bool resume_ = false;
};

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

    ::platform::nrf52::arduino_common::internal_fs::removeIfExists(kSettingsCorruptPath);
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
    s_cache.has_meshtastic_ble_state = false;
    s_cache.meshtastic_ble_bluetooth = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig zero_module = meshtastic_LocalModuleConfig_init_zero;
    s_cache.meshtastic_ble_module = zero_module;
    normalizeConfig(s_cache.config);
}

bool verifySavedFile()
{
    auto file = InternalFS.open(kSettingsPath, FILE_O_READ);
    if (!file)
    {
        Serial.printf("[gat562][settings] verify open failed path=%s\n", kSettingsPath);
        return false;
    }

    const uint32_t actual_size = file.size();
    const uint32_t expected_size =
        static_cast<uint32_t>(sizeof(FileHeader) + sizeof(PersistedPayload));

    if (actual_size != expected_size)
    {
        file.close();
        Serial.printf("[gat562][settings] verify size mismatch actual=%lu expected=%lu\n",
                      static_cast<unsigned long>(actual_size),
                      static_cast<unsigned long>(expected_size));
        return false;
    }

    auto& header = s_file_header_scratch;
    std::memset(&header, 0, sizeof(header));
    if (file.read(&header, sizeof(header)) != sizeof(header))
    {
        file.close();
        Serial.printf("[gat562][settings] verify header read failed\n");
        return false;
    }

    if (header.magic != kSettingsMagic)
    {
        file.close();
        Serial.printf("[gat562][settings] verify magic mismatch got=0x%08lX expected=0x%08lX\n",
                      static_cast<unsigned long>(header.magic),
                      static_cast<unsigned long>(kSettingsMagic));
        return false;
    }

    if (header.version != kSettingsVersion)
    {
        file.close();
        Serial.printf("[gat562][settings] verify version mismatch got=%u expected=%u\n",
                      static_cast<unsigned>(header.version),
                      static_cast<unsigned>(kSettingsVersion));
        return false;
    }

    if (header.payload_size != sizeof(PersistedPayload))
    {
        file.close();
        Serial.printf("[gat562][settings] verify payload size mismatch got=%lu expected=%lu\n",
                      static_cast<unsigned long>(header.payload_size),
                      static_cast<unsigned long>(sizeof(PersistedPayload)));
        return false;
    }

    auto& payload = s_payload_scratch;
    std::memset(&payload, 0, sizeof(payload));
    if (file.read(&payload, sizeof(payload)) != sizeof(payload))
    {
        file.close();
        Serial.printf("[gat562][settings] verify payload read failed\n");
        return false;
    }

    file.close();

    const uint32_t actual_crc =
        crc32(reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));

    if (actual_crc != header.crc32)
    {
        Serial.printf("[gat562][settings] verify crc mismatch got=0x%08lX expected=0x%08lX\n",
                      static_cast<unsigned long>(actual_crc),
                      static_cast<unsigned long>(header.crc32));
        return false;
    }

    Serial.printf("[gat562][settings] verify ok size=%lu crc=0x%08lX mt_ble=%u\n",
                  static_cast<unsigned long>(actual_size),
                  static_cast<unsigned long>(actual_crc),
                  payload.has_meshtastic_ble_state ? 1U : 0U);
    return true;
}

bool loadFromFs()
{
    if (!::platform::nrf52::arduino_common::internal_fs::ensureMounted(true, kLogTag))
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

    const uint32_t actual_size = file.size();
    if (actual_size < sizeof(FileHeader))
    {
        file.close();
        s_last_load_status = StoreStatus::PayloadSizeMismatch;
        (void)quarantineCorruptFile(s_last_load_status);
        Serial.printf("[gat562][settings] size mismatch actual=%lu expected_at_least=%lu\n",
                      static_cast<unsigned long>(actual_size),
                      static_cast<unsigned long>(sizeof(FileHeader)));
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
        if (header.version == 1)
        {
            const uint32_t expected_size_v1 =
                static_cast<uint32_t>(sizeof(FileHeader) + sizeof(PersistedPayloadV1));
            if (header.payload_size != sizeof(PersistedPayloadV1) || actual_size != expected_size_v1)
            {
                file.close();
                s_last_load_status = StoreStatus::PayloadSizeMismatch;
                (void)quarantineCorruptFile(s_last_load_status);
                Serial.printf("[gat562][settings] v1 payload size mismatch got=%lu expected=%lu actual=%lu\n",
                              static_cast<unsigned long>(header.payload_size),
                              static_cast<unsigned long>(sizeof(PersistedPayloadV1)),
                              static_cast<unsigned long>(actual_size));
                return false;
            }

            auto& payload_v1 = s_payload_v1_scratch;
            std::memset(&payload_v1, 0, sizeof(payload_v1));
            if (file.read(&payload_v1, sizeof(payload_v1)) != sizeof(payload_v1))
            {
                file.close();
                s_last_load_status = StoreStatus::ReadFailed;
                Serial.printf("[gat562][settings] v1 payload read failed\n");
                return false;
            }
            file.close();

            const uint32_t actual_crc = crc32(reinterpret_cast<const uint8_t*>(&payload_v1), sizeof(payload_v1));
            if (actual_crc != header.crc32)
            {
                s_last_load_status = StoreStatus::CrcMismatch;
                (void)quarantineCorruptFile(s_last_load_status);
                Serial.printf("[gat562][settings] v1 crc mismatch got=0x%08lX expected=0x%08lX\n",
                              static_cast<unsigned long>(actual_crc),
                              static_cast<unsigned long>(header.crc32));
                return false;
            }

            s_cache.config = payload_v1.config;
            normalizeConfig(s_cache.config);
            s_cache.tone_volume = clampToneVolume(payload_v1.tone_volume);
            s_cache.has_meshtastic_ble_state = false;
            s_cache.meshtastic_ble_bluetooth = meshtastic_Config_BluetoothConfig_init_zero;
            meshtastic_LocalModuleConfig zero_module = meshtastic_LocalModuleConfig_init_zero;
            s_cache.meshtastic_ble_module = zero_module;
            s_last_load_status = StoreStatus::Ok;
            Serial.printf("[gat562][settings] load ok tone=%u ble=%u proto=%u mt_ble=0 version=1\n",
                          static_cast<unsigned>(s_cache.tone_volume),
                          static_cast<unsigned>(s_cache.config.ble_enabled ? 1 : 0),
                          static_cast<unsigned>(s_cache.config.mesh_protocol));
            return true;
        }

        file.close();
        s_last_load_status = StoreStatus::VersionMismatch;
        (void)quarantineCorruptFile(s_last_load_status);
        Serial.printf("[gat562][settings] version mismatch got=%u expected=%u\n",
                      static_cast<unsigned>(header.version),
                      static_cast<unsigned>(kSettingsVersion));
        return false;
    }

    if (header.payload_size != sizeof(PersistedPayload) ||
        actual_size != static_cast<uint32_t>(sizeof(FileHeader) + sizeof(PersistedPayload)))
    {
        file.close();
        s_last_load_status = StoreStatus::PayloadSizeMismatch;
        (void)quarantineCorruptFile(s_last_load_status);
        Serial.printf("[gat562][settings] payload size mismatch got=%lu expected=%lu actual=%lu\n",
                      static_cast<unsigned long>(header.payload_size),
                      static_cast<unsigned long>(sizeof(PersistedPayload)),
                      static_cast<unsigned long>(actual_size));
        return false;
    }

    auto& payload = s_payload_scratch;
    std::memset(&payload, 0, sizeof(payload));
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
    s_cache.has_meshtastic_ble_state = (payload.has_meshtastic_ble_state != 0);
    s_cache.meshtastic_ble_bluetooth = payload.meshtastic_ble_bluetooth;
    s_cache.meshtastic_ble_module = payload.meshtastic_ble_module;
    s_last_load_status = StoreStatus::Ok;
    Serial.printf("[gat562][settings] load ok tone=%u ble=%u proto=%u mt_ble=%u version=%u\n",
                  static_cast<unsigned>(s_cache.tone_volume),
                  static_cast<unsigned>(s_cache.config.ble_enabled ? 1 : 0),
                  static_cast<unsigned>(s_cache.config.mesh_protocol),
                  s_cache.has_meshtastic_ble_state ? 1U : 0U,
                  static_cast<unsigned>(header.version));
    return true;
}

bool saveToFsOnce()
{
    Serial.printf("[gat562][settings] save begin path=%s\n", kSettingsPath);

    if (!::platform::nrf52::arduino_common::internal_fs::ensureMounted(true, kLogTag))
    {
        s_last_save_status = StoreStatus::FsInitFailed;
        Serial.printf("%s ensureMounted failed\n", kLogTag);
        return false;
    }

    auto& payload = s_payload_scratch;
    std::memset(&payload, 0, sizeof(payload));
    payload.config = s_cache.config;
    payload.tone_volume = clampToneVolume(s_cache.tone_volume);
    payload.has_meshtastic_ble_state = s_cache.has_meshtastic_ble_state ? 1U : 0U;
    payload.reserved[0] = 0;
    payload.reserved[1] = 0;
    payload.meshtastic_ble_bluetooth = s_cache.meshtastic_ble_bluetooth;
    payload.meshtastic_ble_module = s_cache.meshtastic_ble_module;

    auto& header = s_file_header_scratch;
    std::memset(&header, 0, sizeof(header));
    header.magic = kSettingsMagic;
    header.version = kSettingsVersion;
    header.reserved = 0;
    header.payload_size = sizeof(PersistedPayload);
    header.crc32 = crc32(reinterpret_cast<const uint8_t*>(&payload), sizeof(PersistedPayload));

    // 先尝试正常打开已有文件
    Adafruit_LittleFS_Namespace::File file(InternalFS);
    if (!::platform::nrf52::arduino_common::internal_fs::openForOverwrite(kSettingsPath, &file, true, kLogTag))
    {
        s_last_save_status = StoreStatus::OpenFailed;
        Serial.printf("%s open failed path=%s\n", kLogTag, kSettingsPath);
        return false;
    }

    {
        auto oldf = InternalFS.open(kSettingsPath, FILE_O_READ);
        if (oldf)
        {
            Serial.printf("[gat562][settings] old size before overwrite=%lu\n",
                          static_cast<unsigned long>(oldf.size()));
            oldf.close();
        }
    }

    const bool seek_ok = ::platform::nrf52::arduino_common::internal_fs::rewindForOverwrite(file);
    Serial.printf("[gat562][settings] seek0 ok=%u\n", seek_ok ? 1U : 0U);
    if (!seek_ok)
    {
        file.close();
        s_last_save_status = StoreStatus::WriteFailed;
        Serial.printf("[gat562][settings] seek failed path=%s\n", kSettingsPath);
        return false;
    }

    const size_t header_written =
        file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(FileHeader));

    const size_t payload_written =
        (header_written == sizeof(FileHeader))
            ? file.write(reinterpret_cast<const uint8_t*>(&payload), sizeof(PersistedPayload))
            : 0U;

    const uint32_t final_size =
        static_cast<uint32_t>(sizeof(FileHeader) + sizeof(PersistedPayload));

    bool trunc_ok = false;
    if (header_written == sizeof(FileHeader) &&
        payload_written == sizeof(PersistedPayload))
    {
        trunc_ok = ::platform::nrf52::arduino_common::internal_fs::truncateAfterWrite(file, final_size);
    }

    file.flush();
    file.close();

    if (header_written != sizeof(FileHeader) ||
        payload_written != sizeof(PersistedPayload))
    {
        s_last_save_status = StoreStatus::WriteFailed;
        Serial.printf("[gat562][settings] write failed header=%lu payload=%lu expected_header=%lu expected_payload=%lu crc=0x%08lx exists=%u\n",
                      static_cast<unsigned long>(header_written),
                      static_cast<unsigned long>(payload_written),
                      static_cast<unsigned long>(sizeof(FileHeader)),
                      static_cast<unsigned long>(sizeof(PersistedPayload)),
                      static_cast<unsigned long>(header.crc32),
                      InternalFS.exists(kSettingsPath) ? 1U : 0U);
        return false;
    }

    if (!trunc_ok)
    {
        s_last_save_status = StoreStatus::WriteFailed;
        Serial.printf("[gat562][settings] truncate failed target=%lu\n",
                      static_cast<unsigned long>(final_size));
        return false;
    }

    if (!verifySavedFile())
    {
        s_last_save_status = StoreStatus::WriteFailed;
        Serial.printf("[gat562][settings] verify after save failed\n");
        return false;
    }

    s_last_save_status = StoreStatus::Ok;
    Serial.printf("[gat562][settings] save ok size=%lu crc=0x%08lx tone=%u mt_ble=%u\n",
                  static_cast<unsigned long>(sizeof(PersistedPayload)),
                  static_cast<unsigned long>(header.crc32),
                  static_cast<unsigned>(payload.tone_volume),
                  static_cast<unsigned>(payload.has_meshtastic_ble_state));
    return true;
}

bool saveToFs()
{
    if (s_save_in_progress)
    {
        Serial.printf("[gat562][settings] save skipped: already in progress\n");
        return false;
    }

    s_save_in_progress = true;
    s_last_save_attempt_ms = millis();
    ScopedGpsSuspend suspend_gps;

    bool ok = saveToFsOnce();
    if (!ok)
    {
        Serial.printf("[gat562][settings] save first attempt failed status=%s retry_delay_ms=%lu\n",
                      statusText(s_last_save_status),
                      static_cast<unsigned long>(kImmediateSaveRetryDelayMs));

        delay(kImmediateSaveRetryDelayMs);

        ok = saveToFsOnce();
        if (!ok)
        {
            Serial.printf("[gat562][settings] save retry failed status=%s\n",
                          statusText(s_last_save_status));
        }
    }

    s_save_in_progress = false;
    return ok;
}

void markDeferredSaveDirty()
{
    s_deferred_save_pending = true;
    s_last_dirty_ms = millis();
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

void cacheAppConfig(const app::AppConfig& config)
{
    ensureCacheLoaded();
    s_cache.config = config;
    normalizeConfig(s_cache.config);
}

bool saveAppConfig(const app::AppConfig& config)
{
    ensureCacheLoaded();
    s_cache.config = config;
    normalizeConfig(s_cache.config);
    s_deferred_save_pending = false;
    return saveToFs();
}

void queueSaveAppConfig(const app::AppConfig& config)
{
    ensureCacheLoaded();
    s_cache.config = config;
    normalizeConfig(s_cache.config);
    markDeferredSaveDirty();
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
    s_deferred_save_pending = false;
    return saveToFs();
}

void queueSaveMessageToneVolume(uint8_t volume)
{
    ensureCacheLoaded();
    s_cache.tone_volume = clampToneVolume(volume);
    markDeferredSaveDirty();
}

bool loadMeshtasticBleState(meshtastic_Config_BluetoothConfig* bluetooth,
                            meshtastic_LocalModuleConfig* module)
{
    ensureCacheLoaded();

    Serial.printf("[gat562][settings][mt] load request has_state=%u last_load=%s\n",
                  s_cache.has_meshtastic_ble_state ? 1U : 0U,
                  statusText(s_last_load_status));

    if (!bluetooth || !module || !s_cache.has_meshtastic_ble_state)
    {
        return false;
    }

    *bluetooth = s_cache.meshtastic_ble_bluetooth;
    *module = s_cache.meshtastic_ble_module;

    Serial.printf("[gat562][settings][mt] load ok mode=%u mqtt=%u proxy=%u root=%s\n",
                  static_cast<unsigned>(bluetooth->mode),
                  module->has_mqtt && module->mqtt.enabled ? 1U : 0U,
                  module->has_mqtt && module->mqtt.proxy_to_client_enabled ? 1U : 0U,
                  module->mqtt.root);
    return true;
}

bool saveMeshtasticBleState(const meshtastic_Config_BluetoothConfig& bluetooth,
                            const meshtastic_LocalModuleConfig& module)
{
    ensureCacheLoaded();
    s_cache.has_meshtastic_ble_state = true;
    s_cache.meshtastic_ble_bluetooth = bluetooth;
    s_cache.meshtastic_ble_module = module;

    s_deferred_save_pending = false;
    const bool ok = saveToFs();
    if (!ok)
    {
        s_deferred_save_pending = true;
        s_last_dirty_ms = millis();
        Serial.printf("[gat562][settings][mt] save failed status=%s\n", statusText(s_last_save_status));
        return false;
    }

    Serial.printf("[gat562][settings][mt] save ok mode=%u mqtt=%u proxy=%u root=%s\n",
                  static_cast<unsigned>(bluetooth.mode),
                  module.has_mqtt && module.mqtt.enabled ? 1U : 0U,
                  module.has_mqtt && module.mqtt.proxy_to_client_enabled ? 1U : 0U,
                  module.mqtt.root);
    return true;
}

bool tickDeferredSave()
{
    ensureCacheLoaded();
    if (!s_deferred_save_pending)
    {
        return false;
    }

    if (s_save_in_progress)
    {
        return false;
    }

    const uint32_t now_ms = millis();
    if ((now_ms - s_last_dirty_ms) < kDeferredSaveDebounceMs)
    {
        return false;
    }

    s_deferred_save_pending = false;
    if (saveToFs())
    {
        return true;
    }

    s_deferred_save_pending = true;
    s_last_dirty_ms = millis();
    return false;
}

bool hasDeferredSavePending()
{
    ensureCacheLoaded();
    return s_deferred_save_pending;
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
