#include "platform/esp/idf_common/sd_card_runtime_sdfat_adapter.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <FsLib/FsLib.h>
#include <SdFatConfig.h>
#include <common/FsBlockDeviceInterface.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#if SDFAT_FILE_TYPE != 3
#error "TrailMate IDF SD runtime requires SdFat with FAT/FAT32/exFAT support."
#endif

#if !USE_BLOCK_DEVICE_INTERFACE
#error "TrailMate IDF SD runtime requires SdFat FsBlockDeviceInterface support."
#endif

namespace platform::esp::arduino_common::storage
{
namespace detail
{

namespace sdmmc_host_runtime = ::platform::esp::idf_common::sdmmc_host_runtime;

constexpr const char* kTag = "idf-sdfat";
constexpr uint8_t kRuntimeCardNone = 0;
constexpr uint8_t kRuntimeCardSdhc = 3;
constexpr uint8_t kRuntimeCardUnknown = 4;
constexpr uint32_t kSdSectorSize = 512;
constexpr TickType_t kSdRuntimeLockWait = pdMS_TO_TICKS(25);

#ifndef TRAIL_MATE_SD_IO_LOG_ENABLE
#define TRAIL_MATE_SD_IO_LOG_ENABLE 1
#endif

#ifndef TRAIL_MATE_SD_IO_TRACE_LOG
#define TRAIL_MATE_SD_IO_TRACE_LOG 0
#endif

#ifndef TRAIL_MATE_SD_IO_SLOW_MS
#define TRAIL_MATE_SD_IO_SLOW_MS 20
#endif

#ifndef TRAIL_MATE_SD_IO_LOG_INTERVAL_MS
#define TRAIL_MATE_SD_IO_LOG_INTERVAL_MS 1000
#endif

FsVolume s_volume;
SdCardInfo s_info{};
sdmmc_card_t* s_card = nullptr;
sdmmc_host_t s_host = SDMMC_HOST_DEFAULT();
sdmmc_host_runtime::SlotOwner s_owner = sdmmc_host_runtime::SlotOwner::None;
bool s_mounted = false;
volatile bool s_external_block_owner_active = false;
SemaphoreHandle_t s_storage_mutex = nullptr;
uint32_t s_last_sd_io_log_ms = 0;
uint32_t s_suppressed_sd_io_logs = 0;
alignas(4) DRAM_ATTR uint8_t s_dma_sector[kSdSectorSize];

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool ensure_storage_mutex()
{
    if (s_storage_mutex)
    {
        return true;
    }
    s_storage_mutex = xSemaphoreCreateRecursiveMutex();
    return s_storage_mutex != nullptr;
}

class SdRuntimeBusGuard
{
  public:
    explicit SdRuntimeBusGuard(const char* owner = "sd_runtime")
        : owner_(owner)
    {
        if (ensure_storage_mutex())
        {
            locked_ = xSemaphoreTakeRecursive(s_storage_mutex, kSdRuntimeLockWait) == pdTRUE;
        }
        if (!locked_)
        {
            ESP_LOGW(kTag, "storage lock timeout owner=%s", owner_ ? owner_ : "unknown");
        }
    }

    ~SdRuntimeBusGuard()
    {
        if (locked_)
        {
            xSemaphoreGiveRecursive(s_storage_mutex);
        }
    }

    bool locked() const { return locked_; }

    SdRuntimeBusGuard(const SdRuntimeBusGuard&) = delete;
    SdRuntimeBusGuard& operator=(const SdRuntimeBusGuard&) = delete;

  private:
    const char* owner_ = nullptr;
    bool locked_ = false;
};

const char* backend_name_from_info()
{
    switch (s_info.backend)
    {
    case SdCardBackend::SdFat:
        return "sdfat";
    case SdCardBackend::None:
    default:
        return "none";
    }
}

const char* safe_path(const char* path)
{
    return path ? path : "";
}

void copy_path(char* out, std::size_t out_size, const char* path)
{
    if (!out || out_size == 0)
    {
        return;
    }
    std::snprintf(out, out_size, "%s", safe_path(path));
}

bool path_empty(const char* path)
{
    return path == nullptr || path[0] == '\0';
}

const char* normalize_sd_path(const char* path)
{
    if (path_empty(path))
    {
        return "/";
    }

    if ((path[0] == 'A' || path[0] == 'a') && path[1] == ':')
    {
        path += 2;
    }

    if (path[0] == '\0')
    {
        return "/";
    }
    return path;
}

bool open_mode_mutates(const char* mode)
{
    if (mode == nullptr)
    {
        return false;
    }
    return std::strchr(mode, 'w') != nullptr || std::strchr(mode, 'a') != nullptr ||
           std::strchr(mode, '+') != nullptr;
}

uint32_t sd_io_begin(const char* op, const char* path, std::size_t bytes = 0)
{
    const uint32_t start_ms = now_ms();
#if TRAIL_MATE_SD_IO_LOG_ENABLE && TRAIL_MATE_SD_IO_TRACE_LOG
    ESP_LOGI(kTag,
             "io begin op=%s backend=%s path=%s bytes=%u t=%lu",
             op ? op : "unknown",
             backend_name_from_info(),
             safe_path(path),
             static_cast<unsigned>(bytes),
             static_cast<unsigned long>(start_ms));
#else
    (void)op;
    (void)path;
    (void)bytes;
#endif
    return start_ms;
}

void sd_io_end(const char* op,
               const char* path,
               uint32_t start_ms,
               bool ok,
               std::size_t bytes = 0,
               int32_t result = 0)
{
    const uint32_t end_ms = now_ms();
    const uint32_t elapsed_ms = end_ms - start_ms;
#if TRAIL_MATE_SD_IO_LOG_ENABLE
    if (TRAIL_MATE_SD_IO_TRACE_LOG || !ok || elapsed_ms >= TRAIL_MATE_SD_IO_SLOW_MS)
    {
        ++s_suppressed_sd_io_logs;
        if (TRAIL_MATE_SD_IO_TRACE_LOG || s_last_sd_io_log_ms == 0 ||
            end_ms - s_last_sd_io_log_ms >= TRAIL_MATE_SD_IO_LOG_INTERVAL_MS)
        {
            ESP_LOGI(kTag,
                     "io end op=%s backend=%s path=%s ok=%d bytes=%u result=%ld elapsed_ms=%lu suppressed=%lu",
                     op ? op : "unknown",
                     backend_name_from_info(),
                     safe_path(path),
                     ok ? 1 : 0,
                     static_cast<unsigned>(bytes),
                     static_cast<long>(result),
                     static_cast<unsigned long>(elapsed_ms),
                     static_cast<unsigned long>(s_suppressed_sd_io_logs - 1));
            s_suppressed_sd_io_logs = 0;
            s_last_sd_io_log_ms = end_ms;
        }
    }
#else
    (void)op;
    (void)path;
    (void)ok;
    (void)bytes;
    (void)result;
    (void)elapsed_ms;
#endif
}

bool sd_mutation_blocked_by_external_owner(const char* op,
                                           const char* path,
                                           uint32_t start_ms,
                                           std::size_t bytes = 0)
{
    if (!s_external_block_owner_active)
    {
        return false;
    }
    sd_io_end(op, path, start_ms, false, bytes, -4);
    return true;
}

oflag_t sdfat_open_flags(const char* mode)
{
    if (mode == nullptr || std::strcmp(mode, "r") == 0 || std::strcmp(mode, "rb") == 0)
    {
        return O_RDONLY;
    }
    if (std::strcmp(mode, "w") == 0 || std::strcmp(mode, "wb") == 0)
    {
        return O_WRONLY | O_CREAT | O_TRUNC;
    }
    if (std::strcmp(mode, "a") == 0 || std::strcmp(mode, "ab") == 0)
    {
        return O_WRONLY | O_CREAT | O_APPEND;
    }
    if (std::strcmp(mode, "r+") == 0 || std::strcmp(mode, "rb+") == 0 ||
        std::strcmp(mode, "r+b") == 0)
    {
        return O_RDWR;
    }
    if (std::strcmp(mode, "w+") == 0 || std::strcmp(mode, "wb+") == 0 ||
        std::strcmp(mode, "w+b") == 0)
    {
        return O_RDWR | O_CREAT | O_TRUNC;
    }
    if (std::strcmp(mode, "a+") == 0 || std::strcmp(mode, "ab+") == 0 ||
        std::strcmp(mode, "a+b") == 0)
    {
        return O_RDWR | O_CREAT | O_APPEND;
    }
    return O_RDONLY;
}

class SdmmcBlockDevice final : public FsBlockDeviceInterface
{
  public:
    void setCard(sdmmc_card_t* card) { card_ = card; }

    void end() override { card_ = nullptr; }

    bool isBusy() override { return false; }

    bool readSector(Sector_t sector, uint8_t* dst) override
    {
        if (!card_ || !dst)
        {
            return false;
        }
        const esp_err_t err = sdmmc_read_sectors(card_, s_dma_sector, sector, 1);
        if (err != ESP_OK)
        {
            ESP_LOGW(kTag,
                     "raw read sector=%lu failed: %s",
                     static_cast<unsigned long>(sector),
                     esp_err_to_name(err));
            return false;
        }
        std::memcpy(dst, s_dma_sector, kSdSectorSize);
        return true;
    }

    bool readSectors(Sector_t sector, uint8_t* dst, size_t ns) override
    {
        if (!card_ || (!dst && ns > 0))
        {
            return false;
        }
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || \
    defined(TRAIL_MATE_ESP_BOARD_TAB5)
        if (canUseMultiSectorDma(dst, ns))
        {
            const esp_err_t err = sdmmc_read_sectors(card_, dst, sector, ns);
            if (err != ESP_OK)
            {
                ESP_LOGW(kTag,
                         "raw multi-read sector=%lu count=%u failed: %s",
                         static_cast<unsigned long>(sector),
                         static_cast<unsigned>(ns),
                         esp_err_to_name(err));
                return false;
            }
            return true;
        }
#endif
        for (size_t i = 0; i < ns; ++i)
        {
            if (!readSector(sector + static_cast<Sector_t>(i),
                            dst + (i * kSdSectorSize)))
            {
                return false;
            }
        }
        return true;
    }

    Sector_t sectorCount() override
    {
        return card_ ? static_cast<Sector_t>(card_->csd.capacity) : 0;
    }

    bool syncDevice() override { return card_ != nullptr; }

    bool writeSector(Sector_t sector, const uint8_t* src) override
    {
        if (!card_ || !src)
        {
            return false;
        }
        std::memcpy(s_dma_sector, src, kSdSectorSize);
        const esp_err_t err = sdmmc_write_sectors(card_, s_dma_sector, sector, 1);
        if (err != ESP_OK)
        {
            ESP_LOGW(kTag,
                     "raw write sector=%lu failed: %s",
                     static_cast<unsigned long>(sector),
                     esp_err_to_name(err));
            return false;
        }
        return true;
    }

    bool writeSectors(Sector_t sector, const uint8_t* src, size_t ns) override
    {
        if (!card_ || (!src && ns > 0))
        {
            return false;
        }
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || \
    defined(TRAIL_MATE_ESP_BOARD_TAB5)
        if (canUseMultiSectorDma(src, ns))
        {
            const esp_err_t err = sdmmc_write_sectors(card_, src, sector, ns);
            if (err != ESP_OK)
            {
                ESP_LOGW(kTag,
                         "raw multi-write sector=%lu count=%u failed: %s",
                         static_cast<unsigned long>(sector),
                         static_cast<unsigned>(ns),
                         esp_err_to_name(err));
                return false;
            }
            return true;
        }
#endif
        for (size_t i = 0; i < ns; ++i)
        {
            if (!writeSector(sector + static_cast<Sector_t>(i),
                             src + (i * kSdSectorSize)))
            {
                return false;
            }
        }
        return true;
    }

  private:
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || \
    defined(TRAIL_MATE_ESP_BOARD_TAB5)
    bool canUseMultiSectorDma(const void* buffer, size_t sector_count) const
    {
        if (!card_ || !buffer || sector_count < 2 ||
            sector_count > (SIZE_MAX / kSdSectorSize) ||
            card_->host.check_buffer_alignment == nullptr)
        {
            return false;
        }

        return card_->host.check_buffer_alignment(
            card_->host.slot,
            buffer,
            sector_count * kSdSectorSize);
    }
#endif

    sdmmc_card_t* card_ = nullptr;
};

SdmmcBlockDevice s_block_device;

void reset_info_locked()
{
    s_info = SdCardInfo{};
}

uint8_t card_type_from_idf(const sdmmc_card_t* card)
{
    if (!card)
    {
        return kRuntimeCardNone;
    }
    return card->csd.capacity > 0 ? kRuntimeCardSdhc : kRuntimeCardUnknown;
}

void clear_mounted_locked()
{
    if (s_mounted)
    {
        s_volume.end();
    }
    s_block_device.end();
    if (s_card)
    {
        const int slot = s_host.slot;
        (void)sdmmc_host_runtime::release_slot(s_owner, slot);
        std::free(s_card);
        s_card = nullptr;
    }
    s_owner = sdmmc_host_runtime::SlotOwner::None;
    s_mounted = false;
    s_external_block_owner_active = false;
    reset_info_locked();
}

void record_sdfat_info_locked()
{
    const uint32_t info_start_ms = now_ms();
    s_info = SdCardInfo{};
    s_info.backend = SdCardBackend::SdFat;
    s_info.card_type = card_type_from_idf(s_card);
    s_info.fat_type = s_volume.fatType();
    s_info.sector_size = kSdSectorSize;
    s_info.sector_count = static_cast<uint32_t>(s_block_device.sectorCount());
    s_info.card_size_bytes = static_cast<uint64_t>(s_info.sector_count) * kSdSectorSize;

    const uint64_t cluster_count = s_volume.clusterCount();
    const uint64_t bytes_per_cluster = s_volume.bytesPerCluster();
    if (cluster_count > 0 && bytes_per_cluster > 0)
    {
        s_info.total_bytes = cluster_count * bytes_per_cluster;
    }

    ESP_LOGI(kTag,
             "info card_type=%u fat=%u sectors=%lu card_mb=%llu total_mb=%llu elapsed_ms=%lu",
             static_cast<unsigned>(s_info.card_type),
             static_cast<unsigned>(s_info.fat_type),
             static_cast<unsigned long>(s_info.sector_count),
             static_cast<unsigned long long>(s_info.card_size_bytes / (1024ULL * 1024ULL)),
             static_cast<unsigned long long>(s_info.total_bytes / (1024ULL * 1024ULL)),
             static_cast<unsigned long>(now_ms() - info_start_ms));
}

bool mount_sdmmc_locked(sdmmc_host_runtime::SlotOwner owner,
                        const sdmmc_host_t& host,
                        const sdmmc_slot_config_t& slot_config,
                        const char* mount_point,
                        uint8_t max_files)
{
    (void)mount_point;
    (void)max_files;

    if (s_mounted)
    {
        return true;
    }

    s_host = host;
    s_owner = owner;
    s_card = static_cast<sdmmc_card_t*>(std::calloc(1, sizeof(sdmmc_card_t)));
    if (!s_card)
    {
        clear_mounted_locked();
        return false;
    }

    esp_err_t err = sdmmc_host_runtime::initialize_slot(owner, s_host, slot_config);
    if (err != ESP_OK)
    {
        ESP_LOGW(kTag,
                 "SDMMC slot init failed owner=%s slot=%d err=%s",
                 sdmmc_host_runtime::owner_name(owner),
                 s_host.slot,
                 esp_err_to_name(err));
        clear_mounted_locked();
        return false;
    }

    err = sdmmc_card_init(&s_host, s_card);
    if (err != ESP_OK)
    {
        ESP_LOGW(kTag,
                 "SDMMC card init failed owner=%s slot=%d err=%s",
                 sdmmc_host_runtime::owner_name(owner),
                 s_host.slot,
                 esp_err_to_name(err));
        clear_mounted_locked();
        return false;
    }

    s_block_device.setCard(s_card);
    bool volume_ok = s_volume.begin(&s_block_device, true, 1);
    if (!volume_ok)
    {
        ESP_LOGW(kTag, "SdFat partition mount failed; retrying as superfloppy");
        volume_ok = s_volume.begin(&s_block_device, true, 0);
    }
    if (!volume_ok || s_volume.fatType() == 0)
    {
        ESP_LOGW(kTag, "SdFat volume mount failed fat=%u", static_cast<unsigned>(s_volume.fatType()));
        clear_mounted_locked();
        return false;
    }

    s_mounted = true;
    record_sdfat_info_locked();
    ESP_LOGI(kTag,
             "backend=sdfat host=sdmmc slot=%d owner=%s fs=%s card=%llu MB total=%llu MB sectors=%lu",
             s_host.slot,
             sdmmc_host_runtime::owner_name(owner),
             sd_card_filesystem_name(),
             static_cast<unsigned long long>(s_info.card_size_bytes / (1024ULL * 1024ULL)),
             static_cast<unsigned long long>(s_info.total_bytes / (1024ULL * 1024ULL)),
             static_cast<unsigned long>(s_info.sector_count));
    return true;
}

} // namespace detail

using namespace detail;

bool mount_sd_card(int, SPIClass&, uint32_t, const char*, uint8_t)
{
    ESP_LOGW(kTag, "SPI SdFat mount is unavailable in ESP-IDF build; use native SDMMC mount");
    return false;
}

bool mount_sd_card(int, const SdSpiBusConfig&, uint32_t, const char*, uint8_t)
{
    ESP_LOGW(kTag, "SPI SdFat mount is unavailable in ESP-IDF build; use native SDMMC mount");
    return false;
}

void unmount_sd_card()
{
    SdRuntimeBusGuard guard("sd_unmount");
    if (!guard.locked())
    {
        return;
    }
    clear_mounted_locked();
}

bool sd_card_ready()
{
    SdRuntimeBusGuard guard("sd_ready");
    return guard.locked() && s_mounted && s_info.backend == SdCardBackend::SdFat &&
           s_info.sector_size != 0 && s_info.card_type != kRuntimeCardNone;
}

bool sd_card_uses_sdfat()
{
    SdRuntimeBusGuard guard("sd_backend");
    return guard.locked() && s_info.backend == SdCardBackend::SdFat;
}

bool sd_card_is_exfat()
{
    SdRuntimeBusGuard guard("sd_exfat");
    return guard.locked() && s_info.backend == SdCardBackend::SdFat &&
           s_info.fat_type == FAT_TYPE_EXFAT;
}

SdCardBackend sd_card_backend()
{
    SdRuntimeBusGuard guard("sd_backend");
    return guard.locked() ? s_info.backend : SdCardBackend::None;
}

SdCardInfo sd_card_info()
{
    SdRuntimeBusGuard guard("sd_info");
    return guard.locked() ? s_info : SdCardInfo{};
}

const char* sd_card_backend_name()
{
    return backend_name_from_info();
}

const char* sd_card_filesystem_name()
{
    SdRuntimeBusGuard guard("sd_fs_name");
    if (!guard.locked())
    {
        return "none";
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        switch (s_info.fat_type)
        {
        case FAT_TYPE_EXFAT:
            return "exfat";
        case FAT_TYPE_FAT32:
            return "fat32";
        case FAT_TYPE_FAT16:
            return "fat16";
        case FAT_TYPE_FAT12:
            return "fat12";
        default:
            return "fat";
        }
    }
    return "none";
}

bool sd_external_block_owner_active()
{
    return s_external_block_owner_active;
}

bool sd_set_external_block_owner_active(bool active)
{
    SdRuntimeBusGuard guard("sd_external_owner_transition");
    if (!guard.locked())
    {
        return false;
    }
    s_external_block_owner_active = active;
    return true;
}

bool sd_exists(const char* path)
{
    const char* normalized = normalize_sd_path(path);
    const uint32_t start_ms = sd_io_begin("exists", normalized);
    bool result = false;
    SdRuntimeBusGuard guard("sd_exists");
    if (!guard.locked())
    {
        sd_io_end("exists", normalized, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_volume.exists(normalized);
        sd_io_end("exists", normalized, start_ms, true, 0, result ? 1 : 0);
        return result;
    }
    sd_io_end("exists", normalized, start_ms, false, 0, -1);
    return false;
}

SdFileReadResult sd_read_file(const char* path,
                              uint8_t* buffer,
                              std::size_t capacity)
{
    const char* normalized = normalize_sd_path(path);
    const uint32_t start_ms = sd_io_begin("map_file_read", normalized, capacity);
    SdFileReadResult result{};

    auto finish = [&](SdFileReadStatus status,
                      std::size_t bytes_read,
                      uint64_t file_size,
                      int32_t error)
    {
        result.status = status;
        result.bytes_read = bytes_read;
        result.file_size = file_size;
        result.error = error;
        sd_io_end("map_file_read",
                  normalized,
                  start_ms,
                  status == SdFileReadStatus::Ready,
                  bytes_read,
                  error);
        return result;
    };

    if (path_empty(path) || buffer == nullptr || capacity == 0)
    {
        return finish(SdFileReadStatus::Invalid, 0, 0, -4);
    }
    if (!sd_card_ready() || s_info.backend != SdCardBackend::SdFat)
    {
        return finish(SdFileReadStatus::Unavailable, 0, 0, -3);
    }

    FsFile file;
    uint64_t file_size = 0;
    {
        SdRuntimeBusGuard guard("sd_map_file_open");
        if (!guard.locked())
        {
            return finish(SdFileReadStatus::Busy, 0, 0, -2);
        }

        file = s_volume.open(normalized, O_RDONLY);
        if (!file)
        {
            return finish(SdFileReadStatus::Missing, 0, 0, -1);
        }

        file_size = file.fileSize();
        if (file_size == 0 || file_size > capacity)
        {
            file.close();
            return finish(SdFileReadStatus::Invalid, 0, file_size, -5);
        }
    }

    const std::size_t target_size = static_cast<std::size_t>(file_size);
    std::size_t total_read = 0;
    while (total_read < target_size)
    {
        const std::size_t chunk_size =
            std::min<std::size_t>(2048U, target_size - total_read);
        int bytes_read = -1;
        {
            SdRuntimeBusGuard guard("sd_map_file_read_chunk");
            if (!guard.locked())
            {
                return finish(SdFileReadStatus::Busy, total_read, file_size, -2);
            }
            bytes_read = file.read(buffer + total_read, chunk_size);
            if (bytes_read <= 0)
            {
                file.close();
                return finish(SdFileReadStatus::IoError, total_read, file_size, -6);
            }
        }

        total_read += static_cast<std::size_t>(bytes_read);
    }

    {
        SdRuntimeBusGuard guard("sd_map_file_close");
        if (!guard.locked())
        {
            return finish(SdFileReadStatus::Busy, total_read, file_size, -2);
        }
        file.close();
    }

    return finish(SdFileReadStatus::Ready, total_read, file_size, 0);
}

bool sd_is_directory(const char* path)
{
    const char* normalized = normalize_sd_path(path);
    const uint32_t start_ms = sd_io_begin("is_dir", normalized);
    bool result = false;
    SdRuntimeBusGuard guard("sd_is_dir");
    if (!guard.locked())
    {
        sd_io_end("is_dir", normalized, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        FsFile dir = s_volume.open(normalized, O_RDONLY);
        result = dir && dir.isDir();
        dir.close();
        sd_io_end("is_dir", normalized, start_ms, true, 0, result ? 1 : 0);
        return result;
    }
    sd_io_end("is_dir", normalized, start_ms, false, 0, -1);
    return false;
}

bool sd_mkdir(const char* path)
{
    const char* normalized = normalize_sd_path(path);
    const uint32_t start_ms = sd_io_begin("mkdir", normalized);
    if (sd_mutation_blocked_by_external_owner("mkdir", normalized, start_ms))
    {
        return false;
    }
    bool result = false;
    SdRuntimeBusGuard guard("sd_mkdir");
    if (!guard.locked())
    {
        sd_io_end("mkdir", normalized, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_volume.mkdir(normalized, true) || s_volume.exists(normalized);
        sd_io_end("mkdir", normalized, start_ms, result);
        return result;
    }
    sd_io_end("mkdir", normalized, start_ms, false, 0, -1);
    return false;
}

bool sd_rmdir(const char* path)
{
    const char* normalized = normalize_sd_path(path);
    const uint32_t start_ms = sd_io_begin("rmdir", normalized);
    if (sd_mutation_blocked_by_external_owner("rmdir", normalized, start_ms))
    {
        return false;
    }
    bool result = false;
    SdRuntimeBusGuard guard("sd_rmdir");
    if (!guard.locked())
    {
        sd_io_end("rmdir", normalized, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_volume.rmdir(normalized);
        sd_io_end("rmdir", normalized, start_ms, result);
        return result;
    }
    sd_io_end("rmdir", normalized, start_ms, false, 0, -1);
    return false;
}

bool sd_remove(const char* path)
{
    const char* normalized = normalize_sd_path(path);
    const uint32_t start_ms = sd_io_begin("remove", normalized);
    if (sd_mutation_blocked_by_external_owner("remove", normalized, start_ms))
    {
        return false;
    }
    bool result = false;
    SdRuntimeBusGuard guard("sd_remove");
    if (!guard.locked())
    {
        sd_io_end("remove", normalized, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_volume.remove(normalized);
        sd_io_end("remove", normalized, start_ms, result);
        return result;
    }
    sd_io_end("remove", normalized, start_ms, false, 0, -1);
    return false;
}

bool sd_rename(const char* old_path, const char* new_path)
{
    const char* normalized_old = normalize_sd_path(old_path);
    const char* normalized_new = normalize_sd_path(new_path);
    const uint32_t start_ms = sd_io_begin("rename", normalized_old);
    if (sd_mutation_blocked_by_external_owner("rename", normalized_old, start_ms))
    {
        return false;
    }
    bool result = false;
    SdRuntimeBusGuard guard("sd_rename");
    if (!guard.locked())
    {
        sd_io_end("rename", normalized_old, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_volume.rename(normalized_old, normalized_new);
        sd_io_end("rename", normalized_old, start_ms, result, 0, result ? 0 : -1);
        return result;
    }
    sd_io_end("rename", normalized_old, start_ms, false, 0, -1);
    return false;
}

class SdRuntimeFile::Impl
{
  public:
    FsFile sdfat_file;
    SdCardBackend backend = SdCardBackend::None;
    char path[128]{};
    char mode[8]{};
};

SdRuntimeFile::SdRuntimeFile()
    : impl_(new (std::nothrow) Impl())
{
}

SdRuntimeFile::~SdRuntimeFile()
{
    close();
    delete impl_;
}

bool SdRuntimeFile::open(const char* path, const char* mode)
{
    close();
    read_busy_ = false;
    if (impl_ == nullptr || path_empty(path))
    {
        return false;
    }

    const char* normalized = normalize_sd_path(path);
    copy_path(impl_->path, sizeof(impl_->path), normalized);
    copy_path(impl_->mode, sizeof(impl_->mode), mode ? mode : "r");
    const uint32_t start_ms = sd_io_begin("file_open", impl_->path);
    if (open_mode_mutates(mode) &&
        sd_mutation_blocked_by_external_owner("file_open", impl_->path, start_ms))
    {
        return false;
    }
    SdRuntimeBusGuard guard("sd_file_open");
    if (!guard.locked())
    {
        read_busy_ = s_storage_mutex != nullptr;
        sd_io_end("file_open", impl_->path, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        impl_->sdfat_file = s_volume.open(normalized, sdfat_open_flags(mode));
        impl_->backend = impl_->sdfat_file ? SdCardBackend::SdFat : SdCardBackend::None;
        sd_io_end("file_open", impl_->path, start_ms, impl_->backend == SdCardBackend::SdFat);
        return impl_->backend == SdCardBackend::SdFat;
    }

    sd_io_end("file_open", impl_->path, start_ms, false, 0, -1);
    return false;
}

void SdRuntimeFile::close()
{
    if (impl_ == nullptr)
    {
        return;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("file_close", impl_->path);
        SdRuntimeBusGuard guard("sd_file_close");
        if (guard.locked())
        {
            impl_->sdfat_file.close();
            sd_io_end("file_close", impl_->path, start_ms, true);
        }
        else
        {
            sd_io_end("file_close", impl_->path, start_ms, false, 0, -2);
        }
    }
    impl_->backend = SdCardBackend::None;
    impl_->path[0] = '\0';
    impl_->mode[0] = '\0';
}

bool SdRuntimeFile::is_open() const
{
    return impl_ != nullptr && impl_->backend != SdCardBackend::None;
}

int SdRuntimeFile::available() const
{
    if (!is_open())
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        SdRuntimeBusGuard guard("sd_file_available");
        if (!guard.locked())
        {
            return 0;
        }
        return impl_->sdfat_file.available();
    }
    return 0;
}

int SdRuntimeFile::read(void* buffer, std::size_t bytes_to_read)
{
    read_busy_ = false;
    if (!is_open() || buffer == nullptr || bytes_to_read == 0)
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("file_read", impl_->path, bytes_to_read);
        SdRuntimeBusGuard guard("sd_file_read");
        if (!guard.locked())
        {
            read_busy_ = s_storage_mutex != nullptr;
            sd_io_end("file_read", impl_->path, start_ms, false, bytes_to_read, -2);
            return -1;
        }
        const int result = impl_->sdfat_file.read(buffer, bytes_to_read);
        sd_io_end("file_read", impl_->path, start_ms, result >= 0, bytes_to_read, result);
        return result;
    }
    return -1;
}

int SdRuntimeFile::read_byte()
{
    if (!is_open())
    {
        return -1;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        SdRuntimeBusGuard guard("sd_file_read_byte");
        if (!guard.locked())
        {
            return -1;
        }
        return impl_->sdfat_file.read();
    }
    return -1;
}

std::size_t SdRuntimeFile::read_bytes(char* buffer, std::size_t bytes_to_read)
{
    if (!is_open() || buffer == nullptr || bytes_to_read == 0)
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("file_read_bytes", impl_->path, bytes_to_read);
        SdRuntimeBusGuard guard("sd_file_read_bytes");
        if (!guard.locked())
        {
            sd_io_end("file_read_bytes", impl_->path, start_ms, false, bytes_to_read, -2);
            return 0;
        }
        int result = impl_->sdfat_file.read(buffer, bytes_to_read);
        sd_io_end("file_read_bytes", impl_->path, start_ms, result >= 0, bytes_to_read, result);
        return result > 0 ? static_cast<std::size_t>(result) : 0;
    }
    return 0;
}

std::size_t SdRuntimeFile::write(const void* buffer, std::size_t bytes_to_write)
{
    if (!is_open() || buffer == nullptr || bytes_to_write == 0)
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("file_write", impl_->path, bytes_to_write);
        if (sd_mutation_blocked_by_external_owner(
                "file_write", impl_->path, start_ms, bytes_to_write))
        {
            return 0;
        }
        SdRuntimeBusGuard guard("sd_file_write");
        if (!guard.locked())
        {
            sd_io_end("file_write", impl_->path, start_ms, false, bytes_to_write, -2);
            return 0;
        }
        const std::size_t result = impl_->sdfat_file.write(buffer, bytes_to_write);
        sd_io_end("file_write", impl_->path, start_ms, result == bytes_to_write, bytes_to_write, result);
        return result;
    }
    return 0;
}

std::size_t SdRuntimeFile::write_byte(uint8_t value)
{
    return write(&value, 1);
}

std::size_t SdRuntimeFile::print(const char* text)
{
    return text ? write(text, std::strlen(text)) : 0;
}

std::size_t SdRuntimeFile::print(double value, int digits)
{
    char text[48] = {};
    const int count = std::snprintf(text, sizeof(text), "%.*f", digits < 0 ? 0 : digits, value);
    return count > 0 ? write(text, static_cast<std::size_t>(count)) : 0;
}

std::size_t SdRuntimeFile::printf(const char* format, ...)
{
    if (!is_open() || format == nullptr)
    {
        return 0;
    }

    va_list args;
    va_start(args, format);
    va_list args_copy;
    va_copy(args_copy, args);
    int len = std::vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);
    if (len <= 0)
    {
        va_end(args);
        return 0;
    }

    std::vector<char> buffer(static_cast<std::size_t>(len) + 1U);
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    return write(buffer.data(), static_cast<std::size_t>(len));
}

bool SdRuntimeFile::seek(uint64_t offset)
{
    read_busy_ = false;
    if (!is_open())
    {
        return false;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        SdRuntimeBusGuard guard("sd_file_seek");
        if (!guard.locked())
        {
            read_busy_ = s_storage_mutex != nullptr;
            return false;
        }
        return impl_->sdfat_file.seekSet(offset);
    }
    return false;
}

uint64_t SdRuntimeFile::position() const
{
    if (!is_open())
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        SdRuntimeBusGuard guard("sd_file_position");
        if (!guard.locked())
        {
            return 0;
        }
        return impl_->sdfat_file.curPosition();
    }
    return 0;
}

uint64_t SdRuntimeFile::size() const
{
    read_busy_ = false;
    if (!is_open())
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        SdRuntimeBusGuard guard("sd_file_size");
        if (!guard.locked())
        {
            read_busy_ = s_storage_mutex != nullptr;
            return 0;
        }
        return impl_->sdfat_file.fileSize();
    }
    return 0;
}

bool SdRuntimeFile::flush()
{
    if (!is_open())
    {
        return false;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("file_flush", impl_->path);
        if (sd_mutation_blocked_by_external_owner("file_flush", impl_->path, start_ms))
        {
            return false;
        }
        SdRuntimeBusGuard guard("sd_file_flush");
        if (!guard.locked())
        {
            sd_io_end("file_flush", impl_->path, start_ms, false, 0, -2);
            return false;
        }
        const bool result = impl_->sdfat_file.sync();
        sd_io_end("file_flush", impl_->path, start_ms, result);
        return result;
    }
    return false;
}

class SdRuntimeDir::Impl
{
  public:
    FsFile sdfat_dir;
    SdCardBackend backend = SdCardBackend::None;
    char path[128]{};
};

SdRuntimeDir::SdRuntimeDir()
    : impl_(new (std::nothrow) Impl())
{
}

SdRuntimeDir::~SdRuntimeDir()
{
    close();
    delete impl_;
}

bool SdRuntimeDir::open(const char* path)
{
    close();
    if (impl_ == nullptr)
    {
        return false;
    }
    const char* normalized = normalize_sd_path(path);
    copy_path(impl_->path, sizeof(impl_->path), normalized);
    const uint32_t start_ms = sd_io_begin("dir_open", impl_->path);
    SdRuntimeBusGuard guard("sd_dir_open");
    if (!guard.locked())
    {
        sd_io_end("dir_open", impl_->path, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        impl_->sdfat_dir = s_volume.open(normalized, O_RDONLY);
        impl_->backend =
            (impl_->sdfat_dir && impl_->sdfat_dir.isDir()) ? SdCardBackend::SdFat
                                                           : SdCardBackend::None;
        sd_io_end("dir_open", impl_->path, start_ms, impl_->backend == SdCardBackend::SdFat);
        return impl_->backend == SdCardBackend::SdFat;
    }
    sd_io_end("dir_open", impl_->path, start_ms, false, 0, -1);
    return false;
}

void SdRuntimeDir::close()
{
    if (impl_ == nullptr)
    {
        return;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("dir_close", impl_->path);
        SdRuntimeBusGuard guard("sd_dir_close");
        if (guard.locked())
        {
            impl_->sdfat_dir.close();
            sd_io_end("dir_close", impl_->path, start_ms, true);
        }
        else
        {
            sd_io_end("dir_close", impl_->path, start_ms, false, 0, -2);
        }
    }
    impl_->backend = SdCardBackend::None;
    impl_->path[0] = '\0';
}

bool SdRuntimeDir::is_open() const
{
    return impl_ != nullptr && impl_->backend != SdCardBackend::None;
}

bool SdRuntimeDir::read_next(char* name, std::size_t name_size, bool* is_dir)
{
    if (!is_open() || name == nullptr || name_size == 0)
    {
        return false;
    }
    name[0] = '\0';
    if (is_dir != nullptr)
    {
        *is_dir = false;
    }

    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("dir_read", impl_->path);
        SdRuntimeBusGuard guard("sd_dir_read");
        if (!guard.locked())
        {
            sd_io_end("dir_read", impl_->path, start_ms, false, 0, -2);
            return false;
        }
        FsFile entry = impl_->sdfat_dir.openNextFile(O_RDONLY);
        if (!entry)
        {
            sd_io_end("dir_read", impl_->path, start_ms, true, 0, 0);
            return false;
        }
        entry.getName(name, name_size);
        if (is_dir != nullptr)
        {
            *is_dir = entry.isDir();
        }
        entry.close();
        sd_io_end("dir_read", impl_->path, start_ms, true, 0, name[0] != '\0' ? 1 : 0);
        return name[0] != '\0';
    }

    return false;
}

bool sd_read_raw(uint32_t lba, uint8_t* buffer)
{
    char path[32];
    std::snprintf(path, sizeof(path), "raw:%lu", static_cast<unsigned long>(lba));
    const uint32_t start_ms = sd_io_begin("raw_read", path, kSdSectorSize);
    bool result = false;
    SdRuntimeBusGuard guard("sd_raw_read");
    if (!guard.locked())
    {
        sd_io_end("raw_read", path, start_ms, false, kSdSectorSize, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_block_device.readSector(lba, buffer);
        sd_io_end("raw_read", path, start_ms, result, kSdSectorSize);
        return result;
    }
    sd_io_end("raw_read", path, start_ms, false, kSdSectorSize, -1);
    return false;
}

bool sd_write_raw(uint32_t lba, const uint8_t* buffer)
{
    char path[32];
    std::snprintf(path, sizeof(path), "raw:%lu", static_cast<unsigned long>(lba));
    const uint32_t start_ms = sd_io_begin("raw_write", path, kSdSectorSize);
    bool result = false;
    SdRuntimeBusGuard guard("sd_raw_write");
    if (!guard.locked())
    {
        sd_io_end("raw_write", path, start_ms, false, kSdSectorSize, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_block_device.writeSector(lba, buffer);
        sd_io_end("raw_write", path, start_ms, result, kSdSectorSize);
        return result;
    }
    sd_io_end("raw_write", path, start_ms, false, kSdSectorSize, -1);
    return false;
}

} // namespace platform::esp::arduino_common::storage

namespace platform::esp::idf_common::sd_card_runtime
{

bool mount_sdmmc(sdmmc_host_runtime::SlotOwner owner,
                 const sdmmc_host_t& host,
                 const sdmmc_slot_config_t& slot_config,
                 const char* mount_point,
                 uint8_t max_files)
{
    namespace storage_detail = ::platform::esp::arduino_common::storage::detail;
    storage_detail::SdRuntimeBusGuard guard("sd_mount");
    if (!guard.locked())
    {
        return false;
    }
    return storage_detail::mount_sdmmc_locked(
        owner, host, slot_config, mount_point, max_files);
}

void unmount_sdmmc(sdmmc_host_runtime::SlotOwner owner)
{
    namespace storage_detail = ::platform::esp::arduino_common::storage::detail;
    storage_detail::SdRuntimeBusGuard guard("sd_unmount_owner");
    if (!guard.locked())
    {
        return;
    }
    if (storage_detail::s_owner == owner)
    {
        storage_detail::clear_mounted_locked();
    }
}

sdmmc_card_t* mounted_card()
{
    return ::platform::esp::arduino_common::storage::detail::s_card;
}

} // namespace platform::esp::idf_common::sd_card_runtime
