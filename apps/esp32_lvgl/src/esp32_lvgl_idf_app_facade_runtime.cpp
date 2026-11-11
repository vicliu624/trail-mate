#include "esp32_lvgl_idf_app_facade_runtime.h"

#if defined(ESP_PLATFORM)
#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "app/app_facades.h"
#include "app/app_system_tick.h"
#include "app/config_persistence_runtime.h"
#include "board/BoardBase.h"
#include "chat/delivery/chat_delivery_event_port.h"
#include "chat/delivery/chat_delivery_event_projector.h"
#include "chat/delivery/chat_delivery_read_model.h"
#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/ports/i_mesh_peer_directory_blob_store.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/chachapoly.h"
#include "mbedtls/sha256.h"
#include "platform/esp/arduino_common/app_config_sd_tms_runtime.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/arduino_common/chat/infra/mesh_adapter_router.h"
#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_adapter.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_adapter.h"
#include "platform/esp/arduino_common/chat/infra/store/sd_store.h"
#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/esp/idf_common/storage_runtime.h"
#include "platform/esp/radio/meshtastic_radio_adapter.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_group_config_runtime.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "sys/event_bus.h"
#include "team/ports/i_team_crypto.h"
#include "team/ports/i_team_event_sink.h"
#include "team/ports/i_team_runtime.h"
#include "team/ports/i_team_track_source.h"
#include "team/protocol/team_position.h"
#include "team/usecase/team_controller.h"
#include "team/usecase/team_pairing_service.h"
#include "team/usecase/team_service.h"
#include "team/usecase/team_track_sampler.h"
#include "ui/chat_ui_runtime.h"
#include "ui/screens/team/team_page_shell.h"
#include "ui/widgets/reticulum_call_overlay.h"
#include "ui/widgets/top_bar_power_presenter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>
#include <string>
#include <vector>
#endif

namespace trailmate::apps::esp32_lvgl::idf_app_facade_runtime
{
namespace
{

#if defined(ESP_PLATFORM)
constexpr const char* kIdfStoreTag = "idf-app-store";
constexpr const char* kIdfConfigTag = "idf-app-cfg";
constexpr const char* kIdfSettingsNs = "idf_app";
constexpr const char* kIdfConfigKey = "app_cfg";
constexpr const char* kIdfMeshPeersDir = "/mesh";
constexpr const char* kIdfMeshPeersFile = "/mesh/peers.bin";
constexpr size_t kIdfReadChunkBytes = 256;
constexpr uint32_t kIdfAppConfigMagic = 0x50344346UL; // P4CF
constexpr uint16_t kIdfAppConfigVersion = 1;
constexpr size_t kIdfMaxMeshPeerBlobBytes = 768U * 1024U;
constexpr const char* kIdfTeamTag = "idf-team";
constexpr size_t kTeamAeadTagBytes = 16;
constexpr size_t kTeamAeadKeyBytes = 32;
constexpr size_t kTeamAeadNonceBytes = 12;

struct IdfPersistedAppConfig
{
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t payload_size = 0;
    uint32_t checksum = 0;
    app::AppConfig config{};
};

IdfPersistedAppConfig* s_config_blob_scratch = nullptr;

IdfPersistedAppConfig* ensureConfigBlobScratch()
{
    if (s_config_blob_scratch)
    {
        return s_config_blob_scratch;
    }

    void* psram_storage =
        heap_caps_malloc(sizeof(IdfPersistedAppConfig),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (psram_storage)
    {
        s_config_blob_scratch = new (psram_storage) IdfPersistedAppConfig();
    }
    else
    {
        s_config_blob_scratch =
            new (std::nothrow) IdfPersistedAppConfig();
    }
    return s_config_blob_scratch;
}

uint32_t fnv1a32(const void* data, size_t len)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < len; ++i)
    {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

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

bool idfSupportsMeshProtocol(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::Reticulum ||
           protocol == chat::MeshProtocol::MeshCore ||
           protocol == chat::MeshProtocol::Meshtastic;
}

void syncReticulumGroupConfig(app::AppConfig& config)
{
    // Current TMS documents contain groups. The legacy TSV is read only while
    // establishing the migration baseline, never over a bound working config.
    if (app::sd_tms::workingConfigIsBound())
    {
        return;
    }
    if (!chat::infra::isReticulumMeshProtocol(
            chat::infra::normalizeMeshProtocol(config.mesh_protocol)))
    {
        return;
    }

    if (!platform::esp::idf_common::bsp_runtime::sdcard_ready())
    {
        ESP_LOGI(kIdfConfigTag, "reticulum group sync deferred: SD card not ready");
        return;
    }

    const auto status = platform::ui::reticulum_groups::load(
        config.reticulumConfig().reticulum_groups,
        chat::kReticulumGroupDestinationMaxCount);
    ESP_LOGI(kIdfConfigTag,
             "reticulum group sync sd=%d loaded=%d file=%d message=%s",
             status.sd_present ? 1 : 0,
             status.loaded ? 1 : 0,
             status.file_present ? 1 : 0,
             status.message);
}

void normalizeIdfAppConfig(app::AppConfig& config)
{
    if (!chat::infra::isValidMeshProtocol(config.mesh_protocol) ||
        !idfSupportsMeshProtocol(config.mesh_protocol))
    {
        ESP_LOGW(kIdfConfigTag,
                 "unsupported mesh protocol=%u; falling back to Reticulum",
                 static_cast<unsigned>(config.mesh_protocol));
        config.mesh_protocol = chat::MeshProtocol::Reticulum;
    }

    if (chat::meshtastic::findRegion(
            static_cast<meshtastic_Config_LoRaConfig_RegionCode>(
                config.meshtastic_config.region)) == nullptr)
    {
        config.meshtastic_config.region = app::AppConfig::kDefaultRegionCode;
    }

    config.meshtastic_config.tx_power = clampTxPower(config.meshtastic_config.tx_power);
    config.meshcore_config.tx_power = clampTxPower(config.meshcore_config.tx_power);
    config.reticulumConfig().tx_power = clampTxPower(config.reticulumConfig().tx_power);
    if (!chat::meshcore::isValidRegionPresetId(
            config.meshcore_config.meshcore_region_preset))
    {
        config.meshcore_config.meshcore_region_preset = 0;
    }
    if (config.gps_interval_ms == 0)
    {
        config.gps_interval_ms = 60000;
    }
    if (config.chat_channel > 1)
    {
        config.chat_channel = 0;
    }
}

bool loadIdfAppConfig(app::AppConfig& out)
{
    IdfPersistedAppConfig* scratch = ensureConfigBlobScratch();
    if (!scratch)
    {
        ESP_LOGE(kIdfConfigTag, "config scratch allocation failed");
        return false;
    }

    std::size_t blob_size = 0U;
    if (!platform::ui::settings_store::get_blob_into(
            kIdfSettingsNs,
            kIdfConfigKey,
            scratch,
            sizeof(*scratch),
            &blob_size))
    {
        return false;
    }
    if (blob_size != sizeof(IdfPersistedAppConfig))
    {
        ESP_LOGW(kIdfConfigTag,
                 "load rejected size=%u expected=%u",
                 static_cast<unsigned>(blob_size),
                 static_cast<unsigned>(sizeof(IdfPersistedAppConfig)));
        return false;
    }

    if (scratch->magic != kIdfAppConfigMagic ||
        scratch->version != kIdfAppConfigVersion ||
        scratch->payload_size != sizeof(app::AppConfig))
    {
        ESP_LOGW(kIdfConfigTag,
                 "load rejected magic=%08lx version=%u payload=%u",
                 static_cast<unsigned long>(scratch->magic),
                 static_cast<unsigned>(scratch->version),
                 static_cast<unsigned>(scratch->payload_size));
        return false;
    }

    const uint32_t checksum =
        fnv1a32(&scratch->config, sizeof(scratch->config));
    if (checksum != scratch->checksum)
    {
        ESP_LOGW(kIdfConfigTag,
                 "load rejected checksum stored=%08lx actual=%08lx",
                 static_cast<unsigned long>(scratch->checksum),
                 static_cast<unsigned long>(checksum));
        return false;
    }

    out = scratch->config;
    normalizeIdfAppConfig(out);
    ESP_LOGI(kIdfConfigTag,
             "loaded app config proto=%u region=%u tx=%d",
             static_cast<unsigned>(out.mesh_protocol),
             static_cast<unsigned>(out.meshtastic_config.region),
             static_cast<int>(out.meshtastic_config.tx_power));
    return true;
}

bool saveIdfAppConfig(const app::AppConfig& config)
{
    IdfPersistedAppConfig* scratch = ensureConfigBlobScratch();
    if (!scratch)
    {
        ESP_LOGE(kIdfConfigTag, "config scratch allocation failed");
        return false;
    }

    *scratch = IdfPersistedAppConfig{};
    scratch->magic = kIdfAppConfigMagic;
    scratch->version = kIdfAppConfigVersion;
    scratch->payload_size = static_cast<uint16_t>(sizeof(app::AppConfig));
    scratch->config = config;
    scratch->checksum =
        fnv1a32(&scratch->config, sizeof(scratch->config));

    const bool ok = platform::ui::settings_store::put_blob(
        kIdfSettingsNs,
        kIdfConfigKey,
        scratch,
        sizeof(*scratch));
    ESP_LOGI(kIdfConfigTag,
             "save app config proto=%u region=%u tx=%d ok=%u",
             static_cast<unsigned>(config.mesh_protocol),
             static_cast<unsigned>(config.meshtastic_config.region),
             static_cast<int>(config.meshtastic_config.tx_power),
             ok ? 1U : 0U);
    return ok;
}

std::string makeSdPath(const char* relative)
{
    if (!relative || !relative[0])
    {
        return "/";
    }
    std::string path = relative;
    if (path.size() >= 2 && (path[0] == 'A' || path[0] == 'a') && path[1] == ':')
    {
        path.erase(0, 2);
    }
    if (path.empty())
    {
        return "/";
    }
    if (path.front() != '/')
    {
        path.insert(path.begin(), '/');
    }
    return path;
}

bool readSdFile(const char* relative, std::vector<uint8_t>& out, size_t max_len)
{
    out.clear();
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        ESP_LOGW(kIdfStoreTag, "load skipped: sd not ready path=%s", relative);
        return false;
    }

    const std::string path = makeSdPath(relative);
    platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(path.c_str(), "rb"))
    {
        return false;
    }

    const uint64_t file_size = file.size();
    if (file_size == 0 || file_size > max_len)
    {
        file.close();
        return false;
    }
    if (!file.seek(0))
    {
        file.close();
        return false;
    }

    const size_t size = static_cast<size_t>(file_size);
    out.reserve(size);
    uint8_t buffer[kIdfReadChunkBytes];
    size_t total_read = 0;
    while (total_read < size)
    {
        const size_t chunk = std::min(kIdfReadChunkBytes, size - total_read);
        const int read = file.read(buffer, chunk);
        if (read < 0 || static_cast<size_t>(read) != chunk)
        {
            file.close();
            out.clear();
            return false;
        }
        out.insert(out.end(), buffer, buffer + static_cast<size_t>(read));
        total_read += static_cast<size_t>(read);
    }
    file.close();
    return true;
}

bool removeSdFile(const char* relative)
{
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        return false;
    }
    const std::string path = makeSdPath(relative);
    const std::string temp_path = path + ".tmp";
    (void)platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
    (void)platform::esp::arduino_common::storage::sd_remove(path.c_str());
    return true;
}

bool writeSdFileAtomic(const char* relative, const uint8_t* data, size_t len)
{
    if (len == 0)
    {
        return removeSdFile(relative);
    }
    if (!data)
    {
        return false;
    }
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        ESP_LOGW(kIdfStoreTag, "save skipped: sd not ready path=%s len=%u",
                 relative,
                 static_cast<unsigned>(len));
        return false;
    }

    const std::string path = makeSdPath(relative);
    const std::string temp_path = path + ".tmp";
    (void)platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());

    platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(temp_path.c_str(), "wb"))
    {
        ESP_LOGW(kIdfStoreTag, "save open failed path=%s", temp_path.c_str());
        return false;
    }

    const size_t written = file.write(data, len);
    const bool flushed = file.flush();
    file.close();
    if (written != len || !flushed)
    {
        (void)platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        ESP_LOGW(kIdfStoreTag, "save write failed path=%s want=%u got=%u flush=%u",
                 temp_path.c_str(),
                 static_cast<unsigned>(len),
                 static_cast<unsigned>(written),
                 flushed ? 1U : 0U);
        return false;
    }

    (void)platform::esp::arduino_common::storage::sd_remove(path.c_str());
    if (!platform::esp::arduino_common::storage::sd_rename(temp_path.c_str(), path.c_str()))
    {
        (void)platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        ESP_LOGW(kIdfStoreTag, "save rename failed tmp=%s path=%s",
                 temp_path.c_str(),
                 path.c_str());
        return false;
    }
    return true;
}

class IdfSdMeshPeerDirectoryBlobStore final
    : public chat::IMeshPeerDirectoryBlobStore
{
  public:
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(
        std::vector<uint8_t>& out) override
    {
        out.clear();
        if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
        {
            return chat::MeshPeerDirectoryBlobLoadResult::Unavailable;
        }

        const std::string path = makeSdPath(kIdfMeshPeersFile);
        if (!platform::esp::arduino_common::storage::sd_exists(path.c_str()))
        {
            return chat::MeshPeerDirectoryBlobLoadResult::Missing;
        }
        if (!readSdFile(kIdfMeshPeersFile, out, kIdfMaxMeshPeerBlobBytes))
        {
            return chat::MeshPeerDirectoryBlobLoadResult::IoError;
        }

        ESP_LOGI(kIdfStoreTag,
                 "mesh peer load path=%s len=%u",
                 kIdfMeshPeersFile,
                 static_cast<unsigned>(out.size()));
        return chat::MeshPeerDirectoryBlobLoadResult::Loaded;
    }

    chat::MeshPeerDirectoryBlobLoadResult loadBlobTo(
        chat::IMeshPeerDirectoryBlobSink& sink) override
    {
        if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
        {
            return chat::MeshPeerDirectoryBlobLoadResult::Unavailable;
        }

        const std::string path = makeSdPath(kIdfMeshPeersFile);
        if (!platform::esp::arduino_common::storage::sd_exists(path.c_str()))
        {
            return chat::MeshPeerDirectoryBlobLoadResult::Missing;
        }

        platform::esp::arduino_common::storage::SdRuntimeFile file;
        if (!file.open(path.c_str(), "rb"))
        {
            return chat::MeshPeerDirectoryBlobLoadResult::IoError;
        }
        const uint64_t file_size = file.size();
        if (file_size == 0U || file_size > kIdfMaxMeshPeerBlobBytes ||
            !file.seek(0))
        {
            file.close();
            return chat::MeshPeerDirectoryBlobLoadResult::IoError;
        }

        const std::size_t size = static_cast<std::size_t>(file_size);
        if (!sink.begin(size))
        {
            file.close();
            return chat::MeshPeerDirectoryBlobLoadResult::IoError;
        }

        uint8_t buffer[kIdfReadChunkBytes];
        std::size_t total_read = 0U;
        while (total_read < size)
        {
            const std::size_t chunk =
                std::min(kIdfReadChunkBytes, size - total_read);
            const int read = file.read(buffer, chunk);
            if (read < 0 || static_cast<std::size_t>(read) != chunk ||
                !sink.write(buffer, chunk))
            {
                file.close();
                return chat::MeshPeerDirectoryBlobLoadResult::IoError;
            }
            total_read += chunk;
        }
        file.close();
        return sink.finish() ? chat::MeshPeerDirectoryBlobLoadResult::Loaded
                             : chat::MeshPeerDirectoryBlobLoadResult::IoError;
    }

    bool saveBlob(const uint8_t* data, size_t len) override
    {
        if ((!data && len != 0) || len > kIdfMaxMeshPeerBlobBytes ||
            !ensureDirectory())
        {
            return false;
        }
        const bool ok = writeSdFileAtomic(kIdfMeshPeersFile, data, len);
        ESP_LOGI(kIdfStoreTag,
                 "mesh peer save path=%s len=%u ok=%u",
                 kIdfMeshPeersFile,
                 static_cast<unsigned>(len),
                 ok ? 1U : 0U);
        return ok;
    }

    void clearBlob() override
    {
        (void)removeSdFile(kIdfMeshPeersFile);
    }

  private:
    static bool ensureDirectory()
    {
        if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
        {
            return false;
        }

        const std::string path = makeSdPath(kIdfMeshPeersDir);
        return platform::esp::arduino_common::storage::sd_is_directory(path.c_str()) ||
               platform::esp::arduino_common::storage::sd_mkdir(path.c_str());
    }
};

class IdfNullMeshAdapter final : public chat::IMeshAdapter
{
  public:
    bool sendText(chat::ChannelId channel,
                  const std::string& text,
                  chat::MessageId* out_msg_id,
                  chat::NodeId peer = 0) override
    {
        (void)channel;
        (void)text;
        (void)peer;
        if (out_msg_id)
        {
            *out_msg_id = 0;
        }
        return false;
    }

    chat::MeshSendResult sendTextDetailed(chat::ChannelId channel,
                                          const std::string& text,
                                          chat::MessageId forced_msg_id = 0,
                                          chat::NodeId peer = 0) override
    {
        (void)channel;
        (void)text;
        (void)peer;
        const chat::MessageId msg_id = forced_msg_id != 0 ? forced_msg_id : next_msg_id_++;
        return chat::MeshSendResult::fail(chat::MeshOperationFailure::NotReady, msg_id);
    }

    bool pollIncomingText(chat::MeshIncomingText* out) override
    {
        (void)out;
        return false;
    }

    bool sendAppData(chat::ChannelId channel,
                     uint32_t portnum,
                     const uint8_t* payload,
                     size_t len,
                     chat::NodeId dest = 0,
                     bool want_ack = false,
                     chat::MessageId packet_id = 0,
                     bool want_response = false) override
    {
        (void)channel;
        (void)portnum;
        (void)payload;
        (void)len;
        (void)dest;
        (void)want_ack;
        (void)packet_id;
        (void)want_response;
        return false;
    }

    bool pollIncomingData(chat::MeshIncomingData* out) override
    {
        (void)out;
        return false;
    }

    chat::NodeId getNodeId() const override
    {
        return self_node_id_;
    }

    void applyConfig(const chat::MeshConfig& config) override
    {
        active_config_ = config;
    }

    void setUserInfo(const char* long_name, const char* short_name) override
    {
        copyString(long_name_, sizeof(long_name_), long_name);
        copyString(short_name_, sizeof(short_name_), short_name);
    }

    void setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent) override
    {
        duty_cycle_enabled_ = duty_cycle_enabled;
        util_percent_ = util_percent;
    }

    void setPrivacyConfig(uint8_t encrypt_mode) override
    {
        encrypt_mode_ = encrypt_mode;
    }

    bool isReady() const override
    {
        return false;
    }

    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override
    {
        (void)out_data;
        (void)max_len;
        out_len = 0;
        return false;
    }

    void setSelfNodeId(chat::NodeId node_id)
    {
        self_node_id_ = node_id;
    }

  private:
    static void copyString(char* dst, size_t dst_len, const char* src)
    {
        if (!dst || dst_len == 0)
        {
            return;
        }
        if (!src)
        {
            dst[0] = '\0';
            return;
        }
        std::size_t copy_len = std::strlen(src);
        if (copy_len >= dst_len)
        {
            copy_len = dst_len - 1;
        }
        std::memmove(dst, src, copy_len);
        dst[copy_len] = '\0';
    }

    chat::MessageId next_msg_id_ = 1;
    chat::NodeId self_node_id_ = 0;
    chat::MeshConfig active_config_{};
    char long_name_[32] = {};
    char short_name_[16] = {};
    bool duty_cycle_enabled_ = true;
    uint8_t util_percent_ = 0;
    uint8_t encrypt_mode_ = 1;
};

class IdfTeamRuntime final : public team::ITeamRuntime
{
  public:
    uint32_t nowMillis() override
    {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    }

    uint32_t nowUnixSeconds() override
    {
        const std::time_t now = std::time(nullptr);
        if (now > 0)
        {
            return static_cast<uint32_t>(now);
        }
        return nowMillis() / 1000U;
    }

    void fillRandomBytes(uint8_t* out, size_t len) override
    {
        if (out == nullptr || len == 0)
        {
            return;
        }
        esp_fill_random(out, len);
    }
};

class IdfTeamCrypto final : public team::ITeamCrypto
{
  public:
    bool deriveKey(const uint8_t* key,
                   size_t key_len,
                   const char* info,
                   uint8_t* out,
                   size_t out_len) override
    {
        if (key == nullptr || info == nullptr || out == nullptr || out_len > 32)
        {
            return false;
        }

        uint8_t digest[32] = {};
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        const bool ok = mbedtls_sha256_starts(&ctx, 0) == 0 &&
                        mbedtls_sha256_update(&ctx, key, key_len) == 0 &&
                        mbedtls_sha256_update(
                            &ctx,
                            reinterpret_cast<const unsigned char*>(info),
                            std::strlen(info)) == 0 &&
                        mbedtls_sha256_finish(&ctx, digest) == 0;
        mbedtls_sha256_free(&ctx);
        if (!ok)
        {
            return false;
        }
        std::memcpy(out, digest, out_len);
        return true;
    }

    bool aeadEncrypt(const uint8_t* key,
                     size_t key_len,
                     const uint8_t* nonce,
                     size_t nonce_len,
                     const uint8_t* aad,
                     size_t aad_len,
                     const uint8_t* plain,
                     size_t plain_len,
                     std::vector<uint8_t>& out_cipher) override
    {
        if (!validAeadInput(key, key_len, nonce, nonce_len, plain, plain_len))
        {
            return false;
        }

        out_cipher.assign(plain_len + kTeamAeadTagBytes, 0);
        mbedtls_chachapoly_context ctx;
        mbedtls_chachapoly_init(&ctx);
        const bool ok =
            mbedtls_chachapoly_setkey(&ctx, key) == 0 &&
            mbedtls_chachapoly_encrypt_and_tag(
                &ctx,
                plain_len,
                nonce,
                aad_len > 0 ? aad : nullptr,
                aad_len,
                plain_len > 0 ? plain : nullptr,
                plain_len > 0 ? out_cipher.data() : nullptr,
                out_cipher.data() + plain_len) == 0;
        mbedtls_chachapoly_free(&ctx);
        if (!ok)
        {
            out_cipher.clear();
            return false;
        }
        return true;
    }

    bool aeadDecrypt(const uint8_t* key,
                     size_t key_len,
                     const uint8_t* nonce,
                     size_t nonce_len,
                     const uint8_t* aad,
                     size_t aad_len,
                     const uint8_t* cipher,
                     size_t cipher_len,
                     std::vector<uint8_t>& out_plain) override
    {
        if (!validAeadInput(key, key_len, nonce, nonce_len, cipher, cipher_len) ||
            cipher_len < kTeamAeadTagBytes)
        {
            return false;
        }

        const size_t plain_len = cipher_len - kTeamAeadTagBytes;
        out_plain.assign(plain_len, 0);
        mbedtls_chachapoly_context ctx;
        mbedtls_chachapoly_init(&ctx);
        const bool ok =
            mbedtls_chachapoly_setkey(&ctx, key) == 0 &&
            mbedtls_chachapoly_auth_decrypt(
                &ctx,
                plain_len,
                nonce,
                aad_len > 0 ? aad : nullptr,
                aad_len,
                cipher + plain_len,
                plain_len > 0 ? cipher : nullptr,
                plain_len > 0 ? out_plain.data() : nullptr) == 0;
        mbedtls_chachapoly_free(&ctx);
        if (!ok)
        {
            out_plain.clear();
            return false;
        }
        return true;
    }

  private:
    static bool validAeadInput(const uint8_t* key,
                               size_t key_len,
                               const uint8_t* nonce,
                               size_t nonce_len,
                               const uint8_t* payload,
                               size_t payload_len)
    {
        return key != nullptr &&
               key_len == kTeamAeadKeyBytes &&
               nonce != nullptr &&
               nonce_len == kTeamAeadNonceBytes &&
               (payload != nullptr || payload_len == 0);
    }
};

class IdfTeamTrackSourceGps final : public team::ITeamTrackSource
{
  public:
    bool readTrackPoint(team::proto::TeamTrackPoint* out_point) override
    {
        if (out_point == nullptr)
        {
            return false;
        }

        const platform::ui::gps::GpsState state = platform::ui::gps::get_data();
        if (!state.valid)
        {
            out_point->lat_e7 = 0;
            out_point->lon_e7 = 0;
            return false;
        }

        out_point->lat_e7 = static_cast<int32_t>(std::lround(state.lat * 10000000.0));
        out_point->lon_e7 = static_cast<int32_t>(std::lround(state.lng * 10000000.0));
        return true;
    }
};

class IdfTeamAppDataEventBusBridge final
    : public team::TeamService::UnhandledAppDataObserver
{
  public:
    void onUnhandledAppData(const chat::MeshIncomingData& msg) override
    {
        sys::EventBus::publish(
            new sys::AppDataEvent(
                msg.portnum,
                msg.from,
                msg.to,
                msg.packet_id,
                static_cast<uint8_t>(msg.channel),
                msg.channel_hash,
                msg.want_response,
                msg.payload,
                &msg.rx_meta),
            0);
    }
};

class IdfTeamEventBusSink final : public team::ITeamEventSink
{
  public:
    void onTeamKick(const team::TeamKickEvent& event) override
    {
        sys::EventBus::publish(new sys::TeamKickEvent(event), 0);
    }

    void onTeamTransferLeader(const team::TeamTransferLeaderEvent& event) override
    {
        sys::EventBus::publish(new sys::TeamTransferLeaderEvent(event), 0);
    }

    void onTeamKeyDist(const team::TeamKeyDistEvent& event) override
    {
        sys::EventBus::publish(new sys::TeamKeyDistEvent(event), 0);
    }

    void onTeamKeyRequest(const team::TeamKeyRequestEvent& event) override
    {
        sys::EventBus::publish(new sys::TeamKeyRequestEvent(event), 0);
    }

    void onTeamStatus(const team::TeamStatusEvent& event) override
    {
        sys::EventBus::publish(new sys::TeamStatusEvent(event), 0);
    }

    void onTeamPosition(const team::TeamPositionEvent& event) override
    {
        team::proto::TeamPositionMessage msg;
        if (event.ctx.from != 0 &&
            team::proto::decodeTeamPositionMessage(event.payload.data(),
                                                   event.payload.size(),
                                                   &msg))
        {
            const uint32_t timestamp = (msg.ts != 0) ? msg.ts : event.ctx.timestamp;
            sys::EventBus::publish(
                new sys::NodePositionUpdateEvent(
                    event.ctx.from,
                    msg.lat_e7,
                    msg.lon_e7,
                    team::proto::teamPositionHasAltitude(msg),
                    team::proto::teamPositionHasAltitude(msg) ? msg.alt_m : 0,
                    timestamp,
                    0,
                    0,
                    0,
                    0,
                    0),
                0);
        }

        sys::EventBus::publish(new sys::TeamPositionEvent(event), 0);
    }

    void onTeamWaypoint(const team::TeamWaypointEvent& event) override
    {
        sys::EventBus::publish(new sys::TeamWaypointEvent(event), 0);
    }

    void onTeamTrack(const team::TeamTrackEvent& event) override
    {
        sys::EventBus::publish(new sys::TeamTrackEvent(event), 0);
    }

    void onTeamChat(const team::TeamChatEvent& event) override
    {
        sys::EventBus::publish(new sys::TeamChatEvent(event), 0);
    }

    void onTeamError(const team::TeamErrorEvent& event) override
    {
        sys::EventBus::publish(new sys::TeamErrorEvent(event), 0);
    }
};

std::unique_ptr<chat::IChatStore> createIdfChatStore(
    chat::SdStore** deferred_store)
{
    if (deferred_store)
    {
        *deferred_store = nullptr;
    }
    std::unique_ptr<chat::SdStore> persistent_store(new chat::SdStore());
    if (persistent_store)
    {
        if (deferred_store)
        {
            *deferred_store = persistent_store.get();
        }
        ESP_LOGI(kIdfStoreTag,
                 "chat store=SdStore backend=deferred layout=/data/v2");
        return std::unique_ptr<chat::IChatStore>(persistent_store.release());
    }

    ESP_LOGW(kIdfStoreTag, "chat store=RamStore reason=ffat_store_unavailable");
    return std::unique_ptr<chat::IChatStore>(new chat::RamStore());
}

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || \
    defined(TRAIL_MATE_ESP_BOARD_TAB5)
constexpr std::size_t kDeferredRadioApplySlotCount = 3U;
constexpr uint8_t kDeferredRadioApplyMesh = 1U << 0;
constexpr uint8_t kDeferredRadioApplyUserInfo = 1U << 1;
constexpr uint8_t kDeferredRadioApplyNetworkLimits = 1U << 2;
constexpr uint8_t kDeferredRadioApplyPrivacy = 1U << 3;

// This object is embedded in IdfAppFacadeRuntime, which is allocated in PSRAM
// by createRuntime().  Keeping three fixed slots avoids a heap-backed queue,
// avoids a large task-stack copy, and lets the UI writer, pending request, and
// app-loop consumer remain disjoint while a radio apply is in progress.
struct DeferredRadioApplySnapshot
{
    chat::MeshProtocol protocol{};
    chat::MeshConfig mesh_config{};
    char long_name[32] = {};
    char short_name[16] = {};
    uint8_t net_duty_cycle = 0;
    uint8_t net_channel_util = 0;
    uint8_t privacy_encrypt_mode = 0;
    uint8_t flags = 0;
};
#endif

class IdfAppFacadeRuntime final : public app::IAppFacade
{
  public:
    bool begin(BoardBase& board, LoraBoard* lora_board)
    {
        if (initialized_)
        {
            return true;
        }

        board_ = &board;
        lora_board_ = lora_board;
        (void)platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();
        const auto working = app::sd_tms::loadWorkingConfig(config_);
        if (working == app::sd_tms::LoadResult::Invalid)
        {
            ESP_LOGE(kIdfConfigTag, "working TMS requires repair; refusing stale NVS fallback");
            return false;
        }
        if (working != app::sd_tms::LoadResult::Applied && !loadIdfAppConfig(config_))
        {
            config_ = app::AppConfig{};
            normalizeIdfAppConfig(config_);
            ESP_LOGI(kIdfConfigTag, "using default app config");
        }
        // The existing blob scratch is needed only for migration/no-SD NVS
        // operations. Do not retain a second AppConfig for web management.
        if (s_config_blob_scratch)
        {
            s_config_blob_scratch->~IdfPersistedAppConfig();
            heap_caps_free(s_config_blob_scratch);
            s_config_blob_scratch = nullptr;
        }
        if (working != app::sd_tms::LoadResult::Applied)
        {
            syncReticulumGroupConfig(config_);
            if (working == app::sd_tms::LoadResult::Legacy &&
                !app::sd_tms::applyLegacyWorkingConfig(config_))
            {
                app::sd_tms::requireWorkingConfigRepair();
                return false;
            }
        }
        if (working != app::sd_tms::LoadResult::Unavailable)
        {
            if (working != app::sd_tms::LoadResult::Applied &&
                !app::sd_tms::syncWorkingConfig(config_))
            {
                ESP_LOGE(kIdfConfigTag, "cannot persist canonical TMS migration");
                return false;
            }
            app::sd_tms::bindWorkingConfig(config_);
        }

        mesh_peer_directory_.setAutoSaveEnabled(false);
        const chat::MeshPeerDirectoryStatus peer_directory_status =
            mesh_peer_directory_.beginEmpty();
        mesh_peer_directory_ready_ = peer_directory_status.succeeded();
        platform::ui::reticulum_directory::bind_mesh_peer_directory(
            mesh_peer_directory_ready_ ? &mesh_peer_directory_ : nullptr);
        ESP_LOGI(kIdfStoreTag,
                 "mesh peer directory path=%s status=%u hydration=deferred",
                 kIdfMeshPeersFile,
                 static_cast<unsigned>(peer_directory_status.code));

        if (!sys::EventBus::init())
        {
            return false;
        }

        chat_store_ = createIdfChatStore(&deferred_chat_store_);
        if (!chat_store_)
        {
            ESP_LOGE(kIdfStoreTag, "chat store allocation failed");
            return false;
        }

        const auto identity = platform::esp::boards::defaultIdentity();
        if (config_.node_name[0] == '\0')
        {
            copyString(config_.node_name, sizeof(config_.node_name), identity.long_name);
        }
        if (config_.short_name[0] == '\0')
        {
            copyString(config_.short_name, sizeof(config_.short_name), identity.short_name);
        }

        if (!installMeshAdapter())
        {
            ESP_LOGE(kIdfConfigTag,
                     "mesh backend install failed proto=%u",
                     static_cast<unsigned>(config_.mesh_protocol));
            return false;
        }
        applyMeshConfig();
        ESP_LOGI(kIdfConfigTag,
                 "mesh config applied stack_high_water=%u",
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        applyUserInfo();
        applyNetworkLimits();
        applyPrivacyConfig();
        config_persistence_runtime_.initialize();

        contact_service_.begin();
        chat_service_.reset(new chat::ChatService(chat_model_, meshAdapter(), *chat_store_));
        chat_service_->setDeliveryEventPort(&delivery_event_port_);
        chat_service_->setActiveProtocol(config_.mesh_protocol);
        chat_service_->switchChannel(config_.chat_channel == 1 ? chat::ChannelId::SECONDARY
                                                               : chat::ChannelId::PRIMARY);
        initTeamServices();
        restoreTeamKeysFromSnapshot();
        setTeamModeActive(team_service_ && team_service_->hasKeys());
        if (lora_board_)
        {
            platform::esp::arduino_common::geocaching::browse_runtime::configure(mesh_router_, *lora_board_);
        }
        initialized_ = true;
        ESP_LOGI(kIdfConfigTag,
                 "app facade ready stack_high_water=%u",
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        return true;
    }

    void startDeferredStorage()
    {
        if (deferred_storage_started_ || !deferred_chat_store_)
        {
            return;
        }
        deferred_storage_started_ = true;
        platform::esp::idf_common::storage::start_deferred_storage(
            deferred_chat_store_,
            mesh_peer_directory_ready_ ? &mesh_peer_directory_ : nullptr);
    }

    bool startBackgroundTasks()
    {
        if (background_tasks_started_)
        {
            return true;
        }

        background_tasks_started_ =
            lora_board_ != nullptr && app::AppTasks::init(*lora_board_, &meshAdapter());
        ESP_LOGI(kIdfConfigTag,
                 "shared ESP background tasks ready=%u",
                 background_tasks_started_ ? 1U : 0U);
        return background_tasks_started_;
    }

    bool waitForInitialStorageHydration(uint32_t timeout_ms = 15000U)
    {
        const uint32_t started_ms = persistenceNowMs();
        bool ready = platform::esp::idf_common::storage::
            consume_hydration_ready();
        while (!ready &&
               platform::esp::idf_common::storage::hydration_active())
        {
            vTaskDelay(pdMS_TO_TICKS(10U));
            ready = platform::esp::idf_common::storage::
                consume_hydration_ready();
            if (static_cast<uint32_t>(persistenceNowMs() - started_ms) >=
                timeout_ms)
            {
                return false;
            }
        }
        if (!ready)
        {
            ready = platform::esp::idf_common::storage::
                consume_hydration_ready();
        }
        mesh_peer_directory_hydrated_ = ready;
        return ready;
    }

  protected:
    const app::AppConfig& getConfig() const override { return config_; }

  public:
    app::AppConfigEdit beginConfigEdit() override
    {
        return app::AppConfigEdit(&config_,
                                  this,
                                  &IdfAppFacadeRuntime::commitConfigEdit,
                                  &IdfAppFacadeRuntime::cancelConfigEdit);
    }

    void saveConfig() override
    {
        saveConfig(app::AppConfigChangeSet::allPersisted());
    }

    void saveConfig(app::AppConfigChangeSet changes) override
    {
        normalizeIdfAppConfig(config_);
        if (chat_service_)
        {
            chat_service_->setActiveProtocol(config_.mesh_protocol);
        }

        if (app::sd_tms::workingConfigIsBound())
        {
            app::sd_tms::requestWorkingConfigSync();
            return;
        }

        const uint32_t now_ms = persistenceNowMs();
        const auto submission = config_persistence_runtime_.submit(
            changes,
            now_ms,
            app::ConfigPersistenceUrgency::Debounced);
        ESP_LOGI(kIdfConfigTag,
                 "config intent queued=%u generation=%lu changes=0x%08lx",
                 submission.queued ? 1U : 0U,
                 static_cast<unsigned long>(submission.generation),
                 static_cast<unsigned long>(submission.changes.bits()));
    }

    void applyMeshConfig() override
    {
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || \
    defined(TRAIL_MATE_ESP_BOARD_TAB5)
        if (initialized_)
        {
            queueDeferredRadioApply(kDeferredRadioApplyMesh);
            return;
        }
        applyMeshConfigNow(config_.mesh_protocol, config_.activeMeshConfig());
#else
        normalizeIdfAppConfig(config_);
        syncReticulumGroupConfig(config_);
        if (!mesh_router_.hasBackend() ||
            mesh_router_.backendProtocol() != config_.mesh_protocol)
        {
            if (!switchMeshProtocol(config_.mesh_protocol, false))
            {
                ESP_LOGE(kIdfConfigTag,
                         "mesh backend switch failed proto=%u",
                         static_cast<unsigned>(config_.mesh_protocol));
                return;
            }
        }
        else
        {
            meshAdapter().applyConfig(config_.activeMeshConfig());
        }
        if (board_)
        {
            board_->applyRadioConfig(config_.mesh_protocol, config_.activeMeshConfig());
        }
#endif
    }

    void applyUserInfo() override
    {
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || \
    defined(TRAIL_MATE_ESP_BOARD_TAB5)
        if (initialized_)
        {
            queueDeferredRadioApply(kDeferredRadioApplyUserInfo);
            return;
        }
#endif
        char long_name[sizeof(config_.node_name)] = {};
        char short_name[sizeof(config_.short_name)] = {};
        getEffectiveUserInfo(long_name, sizeof(long_name), short_name, sizeof(short_name));
        meshAdapter().setUserInfo(long_name, short_name);
    }

    void applyPositionConfig() override {}

    void applyNetworkLimits() override
    {
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || \
    defined(TRAIL_MATE_ESP_BOARD_TAB5)
        if (initialized_)
        {
            queueDeferredRadioApply(kDeferredRadioApplyNetworkLimits);
            return;
        }
#endif
        meshAdapter().setNetworkLimits(config_.net_duty_cycle, config_.net_channel_util);
    }

    void applyPrivacyConfig() override
    {
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || \
    defined(TRAIL_MATE_ESP_BOARD_TAB5)
        if (initialized_)
        {
            queueDeferredRadioApply(kDeferredRadioApplyPrivacy);
            return;
        }
#endif
        meshAdapter().setPrivacyConfig(config_.privacy_encrypt_mode);
    }

    void applyChatDefaults() override
    {
        if (chat_service_)
        {
            chat_service_->switchChannel(config_.chat_channel == 1 ? chat::ChannelId::SECONDARY
                                                                   : chat::ChannelId::PRIMARY);
        }
    }

    chat::MeshProtocol getMeshProtocol() const override
    {
        return config_.mesh_protocol;
    }

    void getEffectiveUserInfo(char* out_long,
                              std::size_t long_len,
                              char* out_short,
                              std::size_t short_len) const override
    {
        const auto identity = platform::esp::boards::defaultIdentity();
        copyString(out_long,
                   long_len,
                   config_.node_name[0] != '\0' ? config_.node_name : identity.long_name);
        copyString(out_short,
                   short_len,
                   config_.short_name[0] != '\0' ? config_.short_name : identity.short_name);
    }

    bool switchMeshProtocol(chat::MeshProtocol protocol, bool persist = true) override
    {
        const chat::MeshProtocol normalized =
            chat::infra::normalizeMeshProtocol(protocol);
        if (!chat::infra::isValidMeshProtocol(normalized) ||
            !idfSupportsMeshProtocol(normalized))
        {
            ESP_LOGW(kIdfConfigTag,
                     "reject mesh protocol switch proto=%u",
                     static_cast<unsigned>(protocol));
            return false;
        }

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || \
    defined(TRAIL_MATE_ESP_BOARD_TAB5)
        if (initialized_)
        {
            if (lora_board_ == nullptr)
            {
                return false;
            }
            config_.mesh_protocol = normalized;
            syncReticulumGroupConfig(config_);
            queueDeferredRadioApply(kDeferredRadioApplyMesh);
            if (persist)
            {
                saveConfig();
            }
            return true;
        }
#endif

        std::unique_ptr<chat::IMeshAdapter> backend =
            createMeshBackend(normalized);
        if (!backend)
        {
            return false;
        }

        const chat::MeshProtocol previous_protocol = config_.mesh_protocol;
        config_.mesh_protocol = normalized;
        syncReticulumGroupConfig(config_);
        backend->applyConfig(config_.activeMeshConfig());

        char long_name[sizeof(config_.node_name)] = {};
        char short_name[sizeof(config_.short_name)] = {};
        getEffectiveUserInfo(long_name,
                             sizeof(long_name),
                             short_name,
                             sizeof(short_name));
        backend->setUserInfo(long_name, short_name);
        backend->setNetworkLimits(config_.net_duty_cycle, config_.net_channel_util);
        backend->setPrivacyConfig(config_.privacy_encrypt_mode);

        if (!mesh_router_.installBackend(normalized, std::move(backend)))
        {
            config_.mesh_protocol = previous_protocol;
            return false;
        }
        mesh_adapter_ = &mesh_router_;

        if (board_)
        {
            board_->applyRadioConfig(normalized, config_.activeMeshConfig());
        }
        if (chat_service_)
        {
            chat_service_->setActiveProtocol(normalized);
        }
        if (persist)
        {
            saveConfig();
        }
        return true;
    }

    chat::ChatService& getChatService() override { return *chat_service_; }
    chat::contacts::ContactService& getContactService() override { return contact_service_; }
    chat::IMeshAdapter* getMeshAdapter() override { return &meshAdapter(); }
    const chat::IMeshAdapter* getMeshAdapter() const override { return &meshAdapter(); }
    chat::NodeId getSelfNodeId() const override { return meshAdapter().getNodeId(); }
    chat::delivery::ChatDeliveryReadModel* getChatDeliveryReadModel() override
    {
        return &delivery_read_model_;
    }
    const chat::delivery::ChatDeliveryReadModel*
    getChatDeliveryReadModel() const override
    {
        return &delivery_read_model_;
    }
    chat::delivery::IChatDeliveryEventPort* getChatDeliveryEventPort() override
    {
        return &delivery_event_port_;
    }

    team::TeamController* getTeamController() override { return team_controller_.get(); }
    team::TeamPairingService* getTeamPairing() override { return team_pairing_service_.get(); }
    team::TeamService* getTeamService() override { return team_service_.get(); }
    const team::TeamService* getTeamService() const override { return team_service_.get(); }
    team::TeamTrackSampler* getTeamTrackSampler() override { return team_track_sampler_.get(); }
    void setTeamModeActive(bool active) override { team_mode_active_ = active; }

    void broadcastNodeInfo() override
    {
        (void)meshAdapter().triggerDiscoveryAction(chat::MeshDiscoveryAction::SendIdBroadcast);
    }

    void clearNodeDb() override
    {
        (void)mesh_peer_directory_.clearProtocol(config_.mesh_protocol);
    }

    void clearMessageDb() override
    {
        if (chat_service_)
        {
            chat_service_->clearAllMessages();
        }
    }

    ble::BleManager* getBleManager() override { return nullptr; }
    const ble::BleManager* getBleManager() const override { return nullptr; }
    bool isBleEnabled() const override { return config_.ble_enabled; }
    void setBleEnabled(bool enabled) override { config_.ble_enabled = enabled; }

    void restartDevice() override
    {
        esp_restart();
    }

    chat::ui::IChatUiRuntime* getChatUiRuntime() override
    {
        return chat_ui_runtime_;
    }

    void setChatUiRuntime(chat::ui::IChatUiRuntime* runtime) override
    {
        chat_ui_runtime_ = runtime;
    }

    BoardBase* getBoard() override { return board_; }
    const BoardBase* getBoard() const override { return board_; }

    void updateCoreServices() override
    {
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || \
    defined(TRAIL_MATE_ESP_BOARD_TAB5)
        flushDeferredRadioApplies();
#endif
        flushConfigPersistence(persistenceNowMs());
        if (!mesh_peer_directory_hydrated_ &&
            platform::esp::idf_common::storage::consume_hydration_ready())
        {
            mesh_peer_directory_hydrated_ = true;
            ESP_LOGI(kIdfStoreTag, "mesh peer directory hydration ready");
        }
        if (::platform::ui::reticulum_groups::hasPending())
        {
            (void)::platform::ui::reticulum_groups::flushPending();
        }
        platform::ui::tracker::poll();
        chat_service_->processIncoming();
        chat_service_->flushStore();
        if (team_service_)
        {
            team_service_->processIncoming();
        }
        if (team_pairing_service_)
        {
            team_pairing_service_->update();
        }

        const bool team_active = team_service_ && team_service_->hasKeys();
        setTeamModeActive(team_active);
        if (team_track_sampler_)
        {
            team_track_sampler_->update(team_controller_.get(), team_active);
        }
    }

    void tickEventRuntime() override
    {
        app::publishSystemTick(sys::millis_now());
        // The IDF app loop runs on a dedicated FreeRTOS task. Keep every LVGL
        // projection inside tickBoundLifecycle()'s display lock instead of
        // allowing the loop task to update the call overlay independently.
        ::ui::widgets::reticulum_call_overlay::tick();
        chat::ui::IChatUiRuntime* runtime = getChatUiRuntime();
        if (runtime != nullptr)
        {
            runtime->update();
        }
        ::ui::widgets::top_bar_power::tick();
    }

    void dispatchPendingEvents(std::size_t max_events = 32) override
    {
        sys::Event* event = nullptr;
        for (std::size_t processed = 0;
             processed < max_events && sys::EventBus::subscribe(&event, 0);)
        {
            if (event == nullptr)
            {
                continue;
            }
            ++processed;

            if (dispatchRuntimeEvent(event))
            {
                continue;
            }

            if (dispatchTeamUiEvent(event))
            {
                continue;
            }

            chat::ui::IChatUiRuntime* runtime = getChatUiRuntime();
            if (runtime != nullptr)
            {
                runtime->onChatEvent(event);
                continue;
            }

            delete event;
        }
    }

    bool initialized() const { return initialized_; }

  private:
    static uint32_t persistenceNowMs()
    {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    }

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || \
    defined(TRAIL_MATE_ESP_BOARD_TAB5)
    void captureDeferredRadioApply(DeferredRadioApplySnapshot& snapshot,
                                   uint8_t flags)
    {
        normalizeIdfAppConfig(config_);
        syncReticulumGroupConfig(config_);
        snapshot.protocol = config_.mesh_protocol;
        snapshot.mesh_config = config_.activeMeshConfig();
        snapshot.net_duty_cycle = config_.net_duty_cycle ? 1U : 0U;
        snapshot.net_channel_util = config_.net_channel_util;
        snapshot.privacy_encrypt_mode = config_.privacy_encrypt_mode;
        getEffectiveUserInfo(snapshot.long_name,
                             sizeof(snapshot.long_name),
                             snapshot.short_name,
                             sizeof(snapshot.short_name));
        snapshot.flags = flags;
    }

    void queueDeferredRadioApply(uint8_t flags)
    {
        int8_t write_slot = -1;
        uint8_t coalesced_flags = flags;

        portENTER_CRITICAL(&deferred_radio_apply_mux_);
        if (deferred_radio_pending_slot_ >= 0)
        {
            coalesced_flags = static_cast<uint8_t>(
                coalesced_flags |
                deferred_radio_apply_slots_[deferred_radio_pending_slot_].flags);
        }
        for (std::size_t index = 0; index < kDeferredRadioApplySlotCount; ++index)
        {
            const int8_t candidate = static_cast<int8_t>(index);
            if (candidate != deferred_radio_active_slot_ &&
                candidate != deferred_radio_pending_slot_ &&
                candidate != deferred_radio_writing_slot_)
            {
                deferred_radio_writing_slot_ = candidate;
                write_slot = candidate;
                break;
            }
        }
        portEXIT_CRITICAL(&deferred_radio_apply_mux_);

        if (write_slot < 0)
        {
            ESP_LOGE(kIdfConfigTag,
                     "radio apply request dropped flags=0x%02x slots exhausted",
                     static_cast<unsigned>(flags));
            return;
        }

        DeferredRadioApplySnapshot& snapshot =
            deferred_radio_apply_slots_[write_slot];
        captureDeferredRadioApply(snapshot, coalesced_flags);

        portENTER_CRITICAL(&deferred_radio_apply_mux_);
        if (deferred_radio_pending_slot_ >= 0)
        {
            snapshot.flags = static_cast<uint8_t>(
                snapshot.flags |
                deferred_radio_apply_slots_[deferred_radio_pending_slot_].flags);
        }
        deferred_radio_pending_slot_ = write_slot;
        deferred_radio_writing_slot_ = -1;
        portEXIT_CRITICAL(&deferred_radio_apply_mux_);

        ESP_LOGI(kIdfConfigTag,
                 "radio apply queued flags=0x%02x proto=%u slot=%d",
                 static_cast<unsigned>(snapshot.flags),
                 static_cast<unsigned>(snapshot.protocol),
                 static_cast<int>(write_slot));
    }

    int8_t takeDeferredRadioApply()
    {
        portENTER_CRITICAL(&deferred_radio_apply_mux_);
        if (deferred_radio_active_slot_ >= 0 ||
            deferred_radio_pending_slot_ < 0)
        {
            portEXIT_CRITICAL(&deferred_radio_apply_mux_);
            return -1;
        }

        const int8_t slot = deferred_radio_pending_slot_;
        deferred_radio_pending_slot_ = -1;
        deferred_radio_active_slot_ = slot;
        portEXIT_CRITICAL(&deferred_radio_apply_mux_);
        return slot;
    }

    void releaseDeferredRadioApply(int8_t slot)
    {
        portENTER_CRITICAL(&deferred_radio_apply_mux_);
        if (deferred_radio_active_slot_ == slot)
        {
            deferred_radio_active_slot_ = -1;
        }
        portEXIT_CRITICAL(&deferred_radio_apply_mux_);
    }

    void retryDeferredRadioApply(int8_t slot)
    {
        portENTER_CRITICAL(&deferred_radio_apply_mux_);
        if (deferred_radio_active_slot_ == slot)
        {
            if (deferred_radio_pending_slot_ >= 0)
            {
                deferred_radio_apply_slots_[deferred_radio_pending_slot_].flags =
                    static_cast<uint8_t>(
                        deferred_radio_apply_slots_[deferred_radio_pending_slot_].flags |
                        deferred_radio_apply_slots_[slot].flags);
                deferred_radio_active_slot_ = -1;
            }
            else
            {
                deferred_radio_pending_slot_ = slot;
                deferred_radio_active_slot_ = -1;
            }
        }
        portEXIT_CRITICAL(&deferred_radio_apply_mux_);
    }

    bool switchMeshBackendNow(const DeferredRadioApplySnapshot& snapshot)
    {
        const chat::MeshProtocol normalized =
            chat::infra::normalizeMeshProtocol(snapshot.protocol);
        if (!chat::infra::isValidMeshProtocol(normalized) ||
            !idfSupportsMeshProtocol(normalized))
        {
            ESP_LOGE(kIdfConfigTag,
                     "deferred mesh backend rejected proto=%u",
                     static_cast<unsigned>(snapshot.protocol));
            return false;
        }

        std::unique_ptr<chat::IMeshAdapter> backend =
            createMeshBackend(normalized);
        if (!backend)
        {
            ESP_LOGE(kIdfConfigTag,
                     "deferred mesh backend unavailable proto=%u",
                     static_cast<unsigned>(normalized));
            return false;
        }

        backend->applyConfig(snapshot.mesh_config);
        backend->setUserInfo(snapshot.long_name, snapshot.short_name);
        backend->setNetworkLimits(snapshot.net_duty_cycle != 0,
                                  snapshot.net_channel_util);
        backend->setPrivacyConfig(snapshot.privacy_encrypt_mode);

        if (!mesh_router_.installBackend(normalized, std::move(backend)))
        {
            ESP_LOGE(kIdfConfigTag,
                     "deferred mesh backend install failed proto=%u",
                     static_cast<unsigned>(normalized));
            return false;
        }

        mesh_adapter_ = &mesh_router_;
        if (chat_service_)
        {
            chat_service_->setActiveProtocol(normalized);
        }
        return true;
    }

    void applyDeferredRadioApply(const DeferredRadioApplySnapshot& snapshot)
    {
        const bool backend_switch_needed =
            !mesh_router_.hasBackend() ||
            mesh_router_.backendProtocol() != snapshot.protocol;
        bool backend_switched = false;
        if (backend_switch_needed)
        {
            if (!switchMeshBackendNow(snapshot))
            {
                return;
            }
            backend_switched = true;
        }

        if ((snapshot.flags & kDeferredRadioApplyMesh) != 0U &&
            !backend_switched)
        {
            meshAdapter().applyConfig(snapshot.mesh_config);
        }
        if (board_ &&
            (backend_switched ||
             (snapshot.flags & kDeferredRadioApplyMesh) != 0U))
        {
            board_->applyRadioConfig(snapshot.protocol, snapshot.mesh_config);
        }
        if ((snapshot.flags & kDeferredRadioApplyUserInfo) != 0U &&
            !backend_switched)
        {
            meshAdapter().setUserInfo(snapshot.long_name, snapshot.short_name);
        }
        if ((snapshot.flags & kDeferredRadioApplyNetworkLimits) != 0U &&
            !backend_switched)
        {
            meshAdapter().setNetworkLimits(snapshot.net_duty_cycle != 0,
                                           snapshot.net_channel_util);
        }
        if ((snapshot.flags & kDeferredRadioApplyPrivacy) != 0U &&
            !backend_switched)
        {
            meshAdapter().setPrivacyConfig(snapshot.privacy_encrypt_mode);
        }
    }

    void flushDeferredRadioApplies()
    {
        const int8_t slot = takeDeferredRadioApply();
        if (slot < 0)
        {
            return;
        }

        DeferredRadioApplySnapshot& snapshot =
            deferred_radio_apply_slots_[slot];
        const bool must_pause_radio_tasks = background_tasks_started_;
        const bool radio_tasks_paused =
            !must_pause_radio_tasks || app::AppTasks::pauseRadioTasks();
        if (!radio_tasks_paused)
        {
            ESP_LOGW(kIdfConfigTag,
                     "radio apply deferred again flags=0x%02x proto=%u reason=task_quiesce",
                     static_cast<unsigned>(snapshot.flags),
                     static_cast<unsigned>(snapshot.protocol));
            retryDeferredRadioApply(slot);
            return;
        }

        ESP_LOGI(kIdfConfigTag,
                 "radio apply begin flags=0x%02x proto=%u paused=%u",
                 static_cast<unsigned>(snapshot.flags),
                 static_cast<unsigned>(snapshot.protocol),
                 must_pause_radio_tasks ? 1U : 0U);
        applyDeferredRadioApply(snapshot);
        if (must_pause_radio_tasks)
        {
            app::AppTasks::resumeRadioTasks();
        }
        ESP_LOGI(kIdfConfigTag,
                 "radio apply complete flags=0x%02x proto=%u",
                 static_cast<unsigned>(snapshot.flags),
                 static_cast<unsigned>(snapshot.protocol));
        releaseDeferredRadioApply(slot);
    }

    void applyMeshConfigNow(chat::MeshProtocol protocol,
                            const chat::MeshConfig& mesh_config)
    {
        normalizeIdfAppConfig(config_);
        syncReticulumGroupConfig(config_);
        if (!mesh_router_.hasBackend() ||
            mesh_router_.backendProtocol() != protocol)
        {
            if (!switchMeshProtocol(protocol, false))
            {
                ESP_LOGE(kIdfConfigTag,
                         "mesh backend switch failed proto=%u",
                         static_cast<unsigned>(protocol));
                return;
            }
            return;
        }
        meshAdapter().applyConfig(mesh_config);
        if (board_)
        {
            board_->applyRadioConfig(protocol, mesh_config);
        }
    }
#endif

    void flushConfigPersistence(uint32_t now_ms)
    {
        app::ConfigPersistenceWork work{};
        if (!config_persistence_runtime_.takeDue(now_ms, work))
        {
            return;
        }

        const bool ok = app::sd_tms::workingConfigIsBound()
                            ? app::sd_tms::syncPendingWorkingConfig() ==
                                  app::sd_tms::WorkingConfigSyncResult::Synchronized
                            : saveIdfAppConfig(config_);
        if (s_config_blob_scratch)
        {
            s_config_blob_scratch->~IdfPersistedAppConfig();
            heap_caps_free(s_config_blob_scratch);
            s_config_blob_scratch = nullptr;
        }
        config_persistence_runtime_.complete(
            work.generation,
            ok ? app::ConfigPersistenceResultKind::Completed
               : app::ConfigPersistenceResultKind::IoError,
            persistenceNowMs());
    }

    static void commitConfigEdit(void* context, app::AppConfigChangeSet changes)
    {
        auto* self = static_cast<IdfAppFacadeRuntime*>(context);
        if (self)
        {
            self->saveConfig(changes);
        }
    }

    static void cancelConfigEdit(void*) {}

    static void copyString(char* dst, size_t dst_len, const char* src)
    {
        if (!dst || dst_len == 0)
        {
            return;
        }
        if (!src)
        {
            dst[0] = '\0';
            return;
        }
        std::size_t copy_len = std::strlen(src);
        if (copy_len >= dst_len)
        {
            copy_len = dst_len - 1;
        }
        std::memmove(dst, src, copy_len);
        dst[copy_len] = '\0';
    }

    static chat::NodeId resolveSelfNodeId()
    {
        uint8_t mac[6] = {};
        if (esp_efuse_mac_get_default(mac) == ESP_OK)
        {
            return (static_cast<chat::NodeId>(mac[2]) << 24) |
                   (static_cast<chat::NodeId>(mac[3]) << 16) |
                   (static_cast<chat::NodeId>(mac[4]) << 8) |
                   static_cast<chat::NodeId>(mac[5]);
        }
        return 0x544D5034UL; // "TMP4"
    }

    void initTeamServices()
    {
        team_crypto_.reset(new IdfTeamCrypto());
        team_event_sink_.reset(new IdfTeamEventBusSink());
        team_app_data_bridge_.reset(new IdfTeamAppDataEventBusBridge());
        team_runtime_.reset(new IdfTeamRuntime());
        team_track_source_.reset(new IdfTeamTrackSourceGps());

        if (!team_crypto_ ||
            !team_event_sink_ ||
            !team_app_data_bridge_ ||
            !team_runtime_ ||
            !team_track_source_)
        {
            ESP_LOGW(kIdfTeamTag, "team service dependencies unavailable");
            return;
        }

        team_service_.reset(
            new team::TeamService(
                *team_crypto_,
                meshAdapter(),
                *team_event_sink_,
                *team_runtime_));
        if (team_service_)
        {
            team_service_->setUnhandledAppDataObserver(team_app_data_bridge_.get());
        }
        team_controller_.reset(new team::TeamController(*team_service_));
        team_track_sampler_.reset(
            new team::TeamTrackSampler(*team_runtime_, *team_track_source_));
    }

    void restoreTeamKeysFromSnapshot()
    {
        if (!team_controller_)
        {
            return;
        }

        team::ui::TeamUiSnapshot snapshot{};
        if (!team::ui::team_ui_snapshot_store().load(snapshot) ||
            !snapshot.has_team_id ||
            !snapshot.has_team_psk ||
            snapshot.security_round == 0)
        {
            return;
        }

        if (team_controller_->setKeysFromPsk(snapshot.team_id,
                                             snapshot.security_round,
                                             snapshot.team_psk.data(),
                                             snapshot.team_psk.size()))
        {
            ESP_LOGI(kIdfTeamTag,
                     "keys restored from Team UI store key_id=%lu",
                     static_cast<unsigned long>(snapshot.security_round));
        }
        else
        {
            ESP_LOGW(kIdfTeamTag,
                     "keys restore failed key_id=%lu",
                     static_cast<unsigned long>(snapshot.security_round));
        }
    }

    std::unique_ptr<chat::IMeshAdapter> createMeshBackend(
        chat::MeshProtocol protocol)
    {
        if (lora_board_ == nullptr)
        {
            return nullptr;
        }

        if (protocol == chat::MeshProtocol::Reticulum)
        {
            return std::unique_ptr<chat::IMeshAdapter>(
                new chat::reticulum::ReticulumAdapter(
                    *lora_board_,
                    mesh_peer_directory_ready_ ? &mesh_peer_directory_ : nullptr));
        }
        if (protocol == chat::MeshProtocol::Meshtastic)
        {
            return std::unique_ptr<chat::IMeshAdapter>(
                new platform::esp::radio::MeshtasticRadioAdapter(*lora_board_));
        }
        if (protocol == chat::MeshProtocol::MeshCore)
        {
            return std::unique_ptr<chat::IMeshAdapter>(
                new chat::meshcore::MeshCoreAdapter(
                    *lora_board_,
                    mesh_peer_directory_ready_ ? &mesh_peer_directory_ : nullptr));
        }
        return nullptr;
    }

    bool installMeshAdapter()
    {
        null_mesh_adapter_.setSelfNodeId(resolveSelfNodeId());
        mesh_adapter_ = &null_mesh_adapter_;

        std::unique_ptr<chat::IMeshAdapter> backend =
            createMeshBackend(config_.mesh_protocol);
        if (!backend ||
            !mesh_router_.installBackend(config_.mesh_protocol, std::move(backend)))
        {
            return false;
        }
        mesh_adapter_ = &mesh_router_;
        return true;
    }

    chat::IMeshAdapter& meshAdapter()
    {
        return mesh_adapter_ != nullptr ? *mesh_adapter_ : null_mesh_adapter_;
    }

    const chat::IMeshAdapter& meshAdapter() const
    {
        if (mesh_adapter_ != nullptr)
        {
            return *mesh_adapter_;
        }
        return null_mesh_adapter_;
    }

    static bool isTeamRuntimeEvent(sys::EventType type)
    {
        return type == sys::EventType::TeamKick ||
               type == sys::EventType::TeamTransferLeader ||
               type == sys::EventType::TeamKeyDist ||
               type == sys::EventType::TeamKeyRequest ||
               type == sys::EventType::TeamStatus ||
               type == sys::EventType::TeamPosition ||
               type == sys::EventType::TeamWaypoint ||
               type == sys::EventType::TeamTrack ||
               type == sys::EventType::TeamChat ||
               type == sys::EventType::TeamPairing ||
               type == sys::EventType::TeamError;
    }

    bool dispatchTeamUiEvent(sys::Event* event)
    {
        if (event == nullptr)
        {
            return true;
        }
        if (!isTeamRuntimeEvent(event->type) &&
            event->type != sys::EventType::SystemTick)
        {
            return false;
        }

        team::ui::shell::handle_event(nullptr, event);
        delete event;
        return true;
    }

    bool dispatchRuntimeEvent(sys::Event* event)
    {
        if (event == nullptr)
        {
            return true;
        }

        switch (event->type)
        {
        case sys::EventType::ChatSendResult:
        {
            auto* result_event = static_cast<sys::ChatSendResultEvent*>(event);
            if (result_event->has_protocol)
            {
                chat_service_->handleSendResultForProtocol(
                    result_event->msg_id,
                    result_event->protocol,
                    result_event->status,
                    result_event->timestamp,
                    result_event->failure);
            }
            else
            {
                chat_service_->handleSendResult(result_event->msg_id,
                                                result_event->status,
                                                result_event->timestamp,
                                                result_event->failure);
            }
            return false;
        }
        case sys::EventType::NodeInfoUpdate:
        {
            auto* node_event = static_cast<sys::NodeInfoUpdateEvent*>(event);
            chat::contacts::NodeUpdate update{};
            update.short_name = node_event->short_name;
            update.long_name = node_event->long_name;
            update.has_last_seen = true;
            update.last_seen = node_event->timestamp;
            update.has_snr = true;
            update.snr = node_event->snr;
            update.has_rssi = true;
            update.rssi = node_event->rssi;
            update.has_protocol = true;
            update.protocol = node_event->protocol;
            update.has_role = true;
            update.role = node_event->role;
            update.has_hops_away = true;
            update.hops_away = node_event->hops_away;
            update.has_hw_model = true;
            update.hw_model = node_event->hw_model;
            update.has_channel = true;
            update.channel = node_event->channel;
            update.has_macaddr = node_event->has_macaddr;
            if (node_event->has_macaddr)
            {
                std::memcpy(update.macaddr, node_event->macaddr, sizeof(update.macaddr));
            }
            update.has_via_mqtt = true;
            update.via_mqtt = node_event->via_mqtt;
            update.has_is_ignored = true;
            update.is_ignored = node_event->is_ignored;
            update.has_public_key = node_event->has_public_key_state;
            update.public_key_present = node_event->has_public_key;
            update.has_key_manually_verified = node_event->has_key_manually_verified_state;
            update.key_manually_verified = node_event->key_manually_verified;
            update.has_device_metrics = node_event->has_device_metrics;
            if (node_event->has_device_metrics)
            {
                update.device_metrics = node_event->device_metrics;
            }
            contact_service_.applyNodeUpdate(node_event->node_id, update);
            delete event;
            return true;
        }
        case sys::EventType::NodeProtocolUpdate:
        {
            auto* protocol_event = static_cast<sys::NodeProtocolUpdateEvent*>(event);
            contact_service_.updateNodeProtocol(protocol_event->node_id,
                                                protocol_event->protocol,
                                                protocol_event->timestamp);
            delete event;
            return true;
        }
        case sys::EventType::NodePositionUpdate:
        {
            auto* pos_event = static_cast<sys::NodePositionUpdateEvent*>(event);
            chat::contacts::NodePosition pos{};
            pos.valid = true;
            pos.latitude_i = pos_event->latitude_i;
            pos.longitude_i = pos_event->longitude_i;
            pos.has_altitude = pos_event->has_altitude;
            pos.altitude = pos_event->altitude;
            pos.timestamp = pos_event->timestamp;
            pos.precision_bits = pos_event->precision_bits;
            pos.pdop = pos_event->pdop;
            pos.hdop = pos_event->hdop;
            pos.vdop = pos_event->vdop;
            pos.gps_accuracy_mm = pos_event->gps_accuracy_mm;
            contact_service_.updateNodePosition(pos_event->node_id, pos);
            delete event;
            return true;
        }
        default:
            return false;
        }
    }

    bool initialized_ = false;
    bool team_mode_active_ = false;
    BoardBase* board_ = nullptr;
    LoraBoard* lora_board_ = nullptr;
    app::AppConfig config_{};
    app::ConfigPersistenceRuntime config_persistence_runtime_{};
    IdfSdMeshPeerDirectoryBlobStore mesh_peer_directory_blob_store_{};
    chat::MeshPeerDirectoryCore mesh_peer_directory_{mesh_peer_directory_blob_store_};
    chat::contacts::ContactService contact_service_{mesh_peer_directory_};
    chat::ChatModel chat_model_{};
    std::unique_ptr<chat::IChatStore> chat_store_{};
    chat::SdStore* deferred_chat_store_ = nullptr;
    bool deferred_storage_started_ = false;
    bool mesh_peer_directory_hydrated_ = false;
    IdfNullMeshAdapter null_mesh_adapter_{};
    chat::MeshAdapterRouter mesh_router_{};
    chat::IMeshAdapter* mesh_adapter_ = &null_mesh_adapter_;
    chat::delivery::ChatDeliveryReadModel delivery_read_model_{};
    chat::delivery::ChatDeliveryEventProjector delivery_projector_{
        delivery_read_model_};
    chat::delivery::ProjectingChatDeliveryEventPort delivery_event_port_{
        delivery_projector_};
    std::unique_ptr<chat::ChatService> chat_service_{};
    std::unique_ptr<team::ITeamCrypto> team_crypto_{};
    std::unique_ptr<team::ITeamEventSink> team_event_sink_{};
    std::unique_ptr<team::TeamService::UnhandledAppDataObserver> team_app_data_bridge_{};
    std::unique_ptr<team::ITeamRuntime> team_runtime_{};
    std::unique_ptr<team::ITeamTrackSource> team_track_source_{};
    std::unique_ptr<team::TeamPairingService> team_pairing_service_{};
    std::unique_ptr<team::TeamService> team_service_{};
    std::unique_ptr<team::TeamController> team_controller_{};
    std::unique_ptr<team::TeamTrackSampler> team_track_sampler_{};
    chat::ui::IChatUiRuntime* chat_ui_runtime_ = nullptr;
    bool mesh_peer_directory_ready_ = false;
    bool background_tasks_started_ = false;
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || \
    defined(TRAIL_MATE_ESP_BOARD_TAB5)
    portMUX_TYPE deferred_radio_apply_mux_ = portMUX_INITIALIZER_UNLOCKED;
    DeferredRadioApplySnapshot
        deferred_radio_apply_slots_[kDeferredRadioApplySlotCount]{};
    int8_t deferred_radio_active_slot_ = -1;
    int8_t deferred_radio_pending_slot_ = -1;
    int8_t deferred_radio_writing_slot_ = -1;
#endif
};

IdfAppFacadeRuntime* s_runtime = nullptr;

IdfAppFacadeRuntime* createRuntime()
{
    void* psram_storage =
        heap_caps_malloc(sizeof(IdfAppFacadeRuntime),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (psram_storage)
    {
        ESP_LOGI(kIdfConfigTag,
                 "app facade runtime allocated in PSRAM bytes=%u",
                 static_cast<unsigned>(sizeof(IdfAppFacadeRuntime)));
        return new (psram_storage) IdfAppFacadeRuntime();
    }

    ESP_LOGW(kIdfConfigTag,
             "app facade runtime PSRAM allocation failed; using internal heap");
    return new (std::nothrow) IdfAppFacadeRuntime();
}
#endif

} // namespace

bool initialize(const platform::esp::boards::AppContextInitHandles& handles,
                const Esp32LvglRuntimeConfig& config)
{
#if defined(ESP_PLATFORM)
    if (!handles.isValid())
    {
        ESP_LOGE(config.log_tag, "IDF AppFacade runtime cannot start without board handles");
        return false;
    }

    if (app::hasAppFacade())
    {
        return true;
    }

    if (s_runtime == nullptr)
    {
        s_runtime = createRuntime();
    }
    if (s_runtime == nullptr ||
        !s_runtime->begin(*handles.board, handles.lora_board))
    {
        ESP_LOGE(config.log_tag, "IDF AppFacade runtime initialization failed for %s", config.target_name);
        return false;
    }

    app::bindAppFacade(*s_runtime);
    s_runtime->startDeferredStorage();
    if (!s_runtime->startBackgroundTasks())
    {
        ESP_LOGE(config.log_tag,
                 "IDF shared ESP background tasks unavailable for %s; radio TX/RX remains disabled",
                 config.target_name);
    }
    if (!s_runtime->waitForInitialStorageHydration())
    {
        ESP_LOGE(config.log_tag,
                 "initial storage hydration did not reach a ready state for %s; keeping AppFacade and radio tasks active while storage-backed data remains unavailable",
                 config.target_name);
    }
    ESP_LOGI(config.log_tag,
             "IDF AppFacade runtime bound for %s self=%08lX mesh_backend=%s",
             config.target_name,
             static_cast<unsigned long>(s_runtime->getSelfNodeId()),
             s_runtime->getMeshAdapter() != nullptr &&
                     s_runtime->getMeshAdapter()->isReady()
                 ? chat::infra::meshProtocolName(s_runtime->getMeshProtocol())
                 : "not_ready");
    return true;
#else
    (void)handles;
    (void)config;
    return false;
#endif
}

bool isInitialized()
{
#if defined(ESP_PLATFORM)
    return s_runtime != nullptr && s_runtime->initialized();
#else
    return false;
#endif
}

void startDeferredStorage()
{
#if defined(ESP_PLATFORM)
    if (s_runtime != nullptr)
    {
        s_runtime->startDeferredStorage();
    }
#endif
}

} // namespace trailmate::apps::esp32_lvgl::idf_app_facade_runtime
