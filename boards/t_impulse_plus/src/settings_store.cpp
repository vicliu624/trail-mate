#include "boards/t_impulse_plus/settings_store.h"

#include "boards/t_impulse_plus/t_impulse_plus_board.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "platform/nrf52/arduino_common/internal_fs_utils.h"
#include "platform/nrf52/arduino_common/settings_file_store.h"

#include <Arduino.h>
#include <InternalFileSystem.h>

#include <algorithm>
#include <cstring>

namespace boards::t_impulse_plus::settings_store
{
namespace
{
using Adafruit_LittleFS_Namespace::FILE_O_READ;
namespace settings_file = ::platform::nrf52::arduino_common::settings_file;
using FileHeader = settings_file::SettingsFileHeader;
using settings_file::crc32;

constexpr const char* kSettingsPath = "/t_impulse_plus_settings.bin";
constexpr const char* kSettingsTempPath = "/t_impulse_plus_settings.bin.tmp";
constexpr const char* kLogTag = "[t-impulse-plus][settings]";
constexpr uint32_t kSettingsMagic = 0x54495053UL; // TIPS
constexpr uint16_t kSettingsVersion = 1;
constexpr uint8_t kDefaultToneVolume = 0;
constexpr uint32_t kDeferredSaveDebounceMs = 1500UL;
constexpr uint32_t kImmediateSaveRetryDelayMs = 20UL;
constexpr uint32_t kMinGpsIntervalMs = 60000UL;

struct PersistedPayload
{
    app::AppConfig config;
    uint8_t tone_volume = kDefaultToneVolume;
    uint8_t reserved[3] = {};
};

struct CachedSettings
{
    app::AppConfig config;
    uint8_t tone_volume = kDefaultToneVolume;
};

bool s_cache_loaded = false;
CachedSettings s_cache{};
StoreStatus s_last_load_status = StoreStatus::NotFound;
StoreStatus s_last_save_status = StoreStatus::NotFound;
bool s_deferred_save_pending = false;
uint32_t s_last_dirty_ms = 0;
bool s_save_in_progress = false;
FileHeader s_file_header_scratch{};
PersistedPayload s_payload_scratch{};

class ScopedGpsSuspend
{
  public:
    ScopedGpsSuspend()
        : board_(&::boards::t_impulse_plus::TImpulsePlusBoard::instance()),
          resume_(board_ && board_->gpsEnabled())
    {
        if (resume_)
        {
            board_->suspendGps();
        }
    }

    ~ScopedGpsSuspend()
    {
        if (resume_ && board_)
        {
            board_->resumeGps();
        }
    }

    ScopedGpsSuspend(const ScopedGpsSuspend&) = delete;
    ScopedGpsSuspend& operator=(const ScopedGpsSuspend&) = delete;

  private:
    ::boards::t_impulse_plus::TImpulsePlusBoard* board_ = nullptr;
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

const char* statusToText(StoreStatus status)
{
    return settings_file::statusText(status);
}

bool removeInvalidSettingsFile(StoreStatus status)
{
    if (!InternalFS.exists(kSettingsPath))
    {
        return true;
    }
    if (InternalFS.remove(kSettingsPath))
    {
        Serial.printf("%s removed invalid store status=%s\n", kLogTag, statusToText(status));
        return true;
    }
    Serial.printf("%s failed to remove invalid store status=%s\n", kLogTag, statusToText(status));
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
        Serial.printf("%s open failed path=%s\n", kLogTag, kSettingsPath);
        return false;
    }

    const uint32_t actual_size = file.size();
    if (actual_size != static_cast<uint32_t>(sizeof(FileHeader) + sizeof(PersistedPayload)))
    {
        file.close();
        s_last_load_status = StoreStatus::PayloadSizeMismatch;
        (void)removeInvalidSettingsFile(s_last_load_status);
        Serial.printf("%s size mismatch actual=%lu expected=%lu\n",
                      kLogTag,
                      static_cast<unsigned long>(actual_size),
                      static_cast<unsigned long>(sizeof(FileHeader) + sizeof(PersistedPayload)));
        return false;
    }

    FileHeader header{};
    if (file.read(&header, sizeof(header)) != sizeof(header))
    {
        file.close();
        s_last_load_status = StoreStatus::ReadFailed;
        return false;
    }
    if (header.magic != kSettingsMagic || header.version != kSettingsVersion ||
        header.payload_size != sizeof(PersistedPayload))
    {
        file.close();
        s_last_load_status = StoreStatus::HeaderInvalid;
        (void)removeInvalidSettingsFile(s_last_load_status);
        Serial.printf("%s header invalid magic=0x%08lX version=%u payload=%lu\n",
                      kLogTag,
                      static_cast<unsigned long>(header.magic),
                      static_cast<unsigned>(header.version),
                      static_cast<unsigned long>(header.payload_size));
        return false;
    }

    auto& payload = s_payload_scratch;
    std::memset(&payload, 0, sizeof(payload));
    if (file.read(&payload, sizeof(payload)) != sizeof(payload))
    {
        file.close();
        s_last_load_status = StoreStatus::ReadFailed;
        return false;
    }
    file.close();

    const uint32_t actual_crc = crc32(reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
    if (actual_crc != header.crc32)
    {
        s_last_load_status = StoreStatus::CrcMismatch;
        (void)removeInvalidSettingsFile(s_last_load_status);
        Serial.printf("%s crc mismatch got=0x%08lX expected=0x%08lX\n",
                      kLogTag,
                      static_cast<unsigned long>(actual_crc),
                      static_cast<unsigned long>(header.crc32));
        return false;
    }

    s_cache.config = payload.config;
    normalizeConfig(s_cache.config);
    s_cache.tone_volume = clampToneVolume(payload.tone_volume);
    s_last_load_status = StoreStatus::Ok;
    Serial.printf("%s load ok proto=%u ble=%u gps=%u tone=%u\n",
                  kLogTag,
                  static_cast<unsigned>(s_cache.config.mesh_protocol),
                  s_cache.config.ble_enabled ? 1U : 0U,
                  s_cache.config.gps_enabled ? 1U : 0U,
                  static_cast<unsigned>(s_cache.tone_volume));
    return true;
}

bool saveToFsOnce()
{
    if (!::platform::nrf52::arduino_common::internal_fs::ensureMounted(true, kLogTag))
    {
        s_last_save_status = StoreStatus::FsInitFailed;
        return false;
    }

    auto& payload = s_payload_scratch;
    std::memset(&payload, 0, sizeof(payload));
    payload.config = s_cache.config;
    payload.tone_volume = clampToneVolume(s_cache.tone_volume);

    settings_file::ReplaceRequest request{};
    request.path = kSettingsPath;
    request.temp_path = kSettingsTempPath;
    request.fs_log_tag = kLogTag;
    request.log_prefix = kLogTag;
    request.magic = kSettingsMagic;
    request.version = kSettingsVersion;
    request.payload = &payload;
    request.payload_size = sizeof(PersistedPayload);
    request.header_scratch = &s_file_header_scratch;
    request.verify_payload_scratch = &s_payload_scratch;
    request.allow_format_recovery = true;

    settings_file::ReplaceResult result{};
    s_last_save_status = settings_file::replaceSettingsFile(request, &result);
    if (s_last_save_status != StoreStatus::Ok)
    {
        Serial.printf("%s save failed status=%s\n", kLogTag, statusToText(s_last_save_status));
        return false;
    }

    Serial.printf("%s save ok size=%lu crc=0x%08lX\n",
                  kLogTag,
                  static_cast<unsigned long>(sizeof(PersistedPayload)),
                  static_cast<unsigned long>(result.crc32));
    return true;
}

bool saveToFs()
{
    if (s_save_in_progress)
    {
        return false;
    }

    s_save_in_progress = true;
    ScopedGpsSuspend suspend_gps;
    bool ok = saveToFsOnce();
    if (!ok)
    {
        delay(kImmediateSaveRetryDelayMs);
        ok = saveToFsOnce();
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
    if (config.mesh_protocol != chat::MeshProtocol::Meshtastic &&
        config.mesh_protocol != chat::MeshProtocol::MeshCore)
    {
        config.mesh_protocol = chat::MeshProtocol::Meshtastic;
    }

    if (chat::meshtastic::findRegion(
            static_cast<meshtastic_Config_LoRaConfig_RegionCode>(config.meshtastic_config.region)) == nullptr)
    {
        config.meshtastic_config.region = app::AppConfig::kDefaultRegionCode;
    }
    if (!chat::meshcore::isValidRegionPresetId(config.meshcore_config.meshcore_region_preset))
    {
        config.meshcore_config.meshcore_region_preset = 0;
    }

    config.meshtastic_config.tx_power = clampTxPower(config.meshtastic_config.tx_power);
    config.meshcore_config.tx_power = clampTxPower(config.meshcore_config.tx_power);
    config.ble_enabled = true;
    config.gps_enabled = true;
    if (config.gps_interval_ms < kMinGpsIntervalMs)
    {
        config.gps_interval_ms = kMinGpsIntervalMs;
    }
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

bool tickDeferredSave()
{
    ensureCacheLoaded();
    if (!s_deferred_save_pending || s_save_in_progress)
    {
        return false;
    }

    if ((millis() - s_last_dirty_ms) < kDeferredSaveDebounceMs)
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
    return statusToText(status);
}

} // namespace boards::t_impulse_plus::settings_store
