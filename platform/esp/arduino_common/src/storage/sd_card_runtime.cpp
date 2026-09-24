#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/arduino_common/storage/sd_operation_scope.h"
#include "platform/esp/arduino_common/storage/sd_transfer_policy.h"

#include "platform/esp/arduino_common/storage/sd_spi_bus_hooks.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/common/shared_spi_coordinator.h"
#include "sys/bus_access_scope.h"
#include "sys/clock.h"

#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <Arduino.h>
#include <SPI.h>
#ifndef DISABLE_FS_H_WARNING
#define DISABLE_FS_H_WARNING 1
#endif
#include "platform/esp/arduino_common/storage/sd_file_lifetime.h"
#include "platform/esp/arduino_common/storage/sd_file_probe.h"
#include "platform/esp/arduino_common/storage/sdmmc_block_device.h"
#include <SdFat.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

#if SDFAT_FILE_TYPE != 3
#error "TrailMate requires SdFs with FAT/FAT32/exFAT support (SDFAT_FILE_TYPE=3)."
#endif

namespace platform::esp::arduino_common::storage
{
namespace
{

constexpr uint8_t kRuntimeCardNone = 0;
constexpr uint8_t kRuntimeCardSd = 2;
constexpr uint8_t kRuntimeCardSdhc = 3;
constexpr uint8_t kRuntimeCardUnknown = 4;
constexpr uint32_t kSdSectorSize = 512;
constexpr uint32_t kDefaultSharedSpiSdHz = 4000000U;
constexpr uint32_t kMaxSharedSpiSdHz = 10000000U;
constexpr uint32_t kSdInitHz = 400000U;
constexpr uint8_t kSdR1IdleState = 0x01U;
constexpr uint32_t kSdRuntimeLockWaitMs = 25U;
constexpr uint32_t kSdInteractiveReadLockWaitMs = 200U;
constexpr uint32_t kSdDurableLockWaitMs = 250U;
constexpr uint32_t kSdExternalTransferLockWaitMs = 500U;
constexpr uint32_t kSdExternalOwnerTransitionLockWaitMs = 2000U;
constexpr std::size_t kSdTransferSliceBytes = ActiveSdTransferPolicy::file_slice_bytes;

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

#if defined(TRAIL_MATE_SDFAT_SDMMC)
FsVolume s_sdfat;
ArduinoSdmmcBlockDevice s_sdmmc;
#else
SdFs s_sdfat;
#endif
SdCardInfo s_info{};
std::atomic<uint32_t> s_media_session{0};
std::atomic<uint32_t> s_faulted_media_session{0};
bool s_sdfat_mounted = false;
volatile bool s_external_block_owner_active = false;
uint32_t s_last_sd_io_log_ms = 0;
uint32_t s_suppressed_sd_io_logs = 0;
StaticSemaphore_t s_filesystem_mutex_storage{};
SemaphoreHandle_t s_filesystem_mutex = nullptr;
FsFile* s_transient_file = nullptr;
SdPathProbeScratch* s_path_probe = nullptr;

// Created only inside the filesystem operation guard. A previous operation's
// sticky card error must not turn a genuinely absent path into a permanent
// I/O failure. Keep the transport choice here, not in map callers.
class SdIoErrorScope
{
  public:
    SdIoErrorScope()
    {
#if defined(TRAIL_MATE_SDFAT_SDMMC)
        s_sdmmc.clearIoError();
        s_sdmmc.resetReadMetrics();
#else
        if (s_sdfat.card()) s_sdfat.card()->sdError(0);
#endif
    }
    int32_t error() const
    {
#if defined(TRAIL_MATE_SDFAT_SDMMC)
        return s_sdmmc.ioError();
#else
        return s_sdfat.card() ? s_sdfat.card()->errorCode() : -3;
#endif
    }
    static void captureReadMetrics(SdFileReadResult& result)
    {
#if defined(TRAIL_MATE_SDFAT_SDMMC)
        const auto& metrics = s_sdmmc.readMetrics();
        result.block_calls = metrics.calls;
        result.block_sectors = metrics.sectors;
        result.block_max_sectors = metrics.max_sectors;
        result.block_us = metrics.elapsed_us;
        result.block_timing_available = true;
#else
        (void)result; // No fabricated physical-call evidence for SPI.
#endif
    }
};

// Persistent scalar description of the last mounted shared SPI wiring. SdFat
// may call SPIClass::end() internally, so cleanup must restore the same board
// mapping that was used for the mount rather than assuming Arduino defaults.
struct ActiveSdSpiBus
{
    SPIClass* spi = nullptr;
    int sck = -1;
    int miso = -1;
    int mosi = -1;
};

ActiveSdSpiBus s_active_spi_bus{};

enum class SdAccessOwner : uint8_t
{
    Application,
    ExternalStorageRaw,
};

bool ensure_filesystem_mutex()
{
    if (s_filesystem_mutex == nullptr)
    {
        s_filesystem_mutex =
            xSemaphoreCreateRecursiveMutexStatic(&s_filesystem_mutex_storage);
    }
    return s_filesystem_mutex != nullptr;
}

template <typename T>
T* psram_preferred_object()
{
    void* storage = heap_caps_malloc_prefer(sizeof(T),
                                            2,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return storage != nullptr ? new (storage) T() : nullptr;
}

void* psram_preferred_bytes(std::size_t bytes)
{
    return heap_caps_malloc_prefer(bytes,
                                   2,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

template <typename T>
void destroy_psram_preferred_object(T*& object)
{
    if (object == nullptr)
    {
        return;
    }
    object->~T();
    heap_caps_free(object);
    object = nullptr;
}

bool ensure_transient_file()
{
    if (s_transient_file == nullptr)
    {
        s_transient_file = psram_preferred_object<FsFile>();
    }
    return s_transient_file != nullptr;
}

class SdRuntimeOperationGuard
{
  public:
    explicit SdRuntimeOperationGuard(
        const char* owner = "sd_runtime",
        sys::runtime::BusAccessPolicy policy =
            sys::runtime::BusAccessPolicy::BackgroundWorkerBounded,
        uint32_t wait_ms = kSdRuntimeLockWaitMs,
        SdAccessOwner access_owner = SdAccessOwner::Application)
    {
        if (s_external_block_owner_active &&
            access_owner != SdAccessOwner::ExternalStorageRaw)
        {
            status_ = sys::runtime::BusAcquireStatus::Busy;
            return;
        }

        if (!ensure_filesystem_mutex())
        {
            status_ = sys::runtime::BusAcquireStatus::Unavailable;
            return;
        }

        const TickType_t wait_ticks =
            wait_ms == 0U ? 0 : std::max<TickType_t>(1, pdMS_TO_TICKS(wait_ms));
        if (xSemaphoreTakeRecursive(s_filesystem_mutex, wait_ticks) != pdTRUE)
        {
            status_ = wait_ms == 0U ? sys::runtime::BusAcquireStatus::Busy
                                    : sys::runtime::BusAcquireStatus::TimedOut;
            return;
        }

        // The owner can change after the optimistic check above while this
        // operation waits for the filesystem lock. Recheck under that same
        // lock so USB MSC's ownership transition cannot race a new app I/O.
        if (s_external_block_owner_active &&
            access_owner != SdAccessOwner::ExternalStorageRaw)
        {
            xSemaphoreGiveRecursive(s_filesystem_mutex);
            status_ = sys::runtime::BusAcquireStatus::Busy;
            return;
        }

        transport_scope_.enter(owner, policy, wait_ms);
        status_ = sys::runtime::BusAcquireStatus::Acquired;
        locked_ = true;
    }

    ~SdRuntimeOperationGuard()
    {
        if (locked_)
        {
            transport_scope_.leave();
            xSemaphoreGiveRecursive(s_filesystem_mutex);
        }
    }

    bool locked() const { return locked_; }
    sys::runtime::BusAcquireStatus status() const { return status_; }
    sys::runtime::BusAcquireStatus busStatus() const
    {
        return locked_ ? transport_scope_.status() : status_;
    }

  private:
    ActiveSdOperationScope transport_scope_{};
    sys::runtime::BusAcquireStatus status_ =
        sys::runtime::BusAcquireStatus::Unavailable;
    bool locked_ = false;
};

int32_t bus_acquire_error(sys::runtime::BusAcquireStatus status)
{
    switch (status)
    {
    case sys::runtime::BusAcquireStatus::Busy:
        return -4;
    case sys::runtime::BusAcquireStatus::Unavailable:
        return -3;
    case sys::runtime::BusAcquireStatus::TimedOut:
    default:
        return -2;
    }
}

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

uint32_t sd_io_begin(const char* op, const char* path, std::size_t bytes = 0)
{
    const uint32_t start_ms = millis();
#if TRAIL_MATE_SD_IO_LOG_ENABLE && TRAIL_MATE_SD_IO_TRACE_LOG
    Serial.printf("[SD][IO] begin op=%s backend=%s path=%s bytes=%u t=%lu\n",
                  op,
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
    const uint32_t end_ms = millis();
    const uint32_t elapsed_ms = end_ms - start_ms;
#if TRAIL_MATE_SD_IO_LOG_ENABLE
    if (TRAIL_MATE_SD_IO_TRACE_LOG || !ok || elapsed_ms >= TRAIL_MATE_SD_IO_SLOW_MS)
    {
        ++s_suppressed_sd_io_logs;
        if (TRAIL_MATE_SD_IO_TRACE_LOG || s_last_sd_io_log_ms == 0 ||
            end_ms - s_last_sd_io_log_ms >= TRAIL_MATE_SD_IO_LOG_INTERVAL_MS)
        {
            Serial.printf("[SD][IO] end op=%s backend=%s path=%s ok=%d bytes=%u result=%ld elapsed_ms=%lu suppressed=%lu\n",
                          op,
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

uint32_t sanitize_sd_spi_hz(uint32_t requested_hz)
{
    if (requested_hz == 0 || requested_hz > kMaxSharedSpiSdHz)
    {
        return kDefaultSharedSpiSdHz;
    }
    return requested_hz;
}

uint8_t card_type_from_sdfat(SdFs& fs)
{
    SdCard* card = fs.card();
    if (card == nullptr)
    {
        return kRuntimeCardNone;
    }

    const uint8_t type = card->type();
    if (type == SD_CARD_TYPE_SD1)
    {
        return kRuntimeCardSd;
    }
    if (type == SD_CARD_TYPE_SD2)
    {
        return kRuntimeCardSd;
    }
    if (type == SD_CARD_TYPE_SDHC)
    {
        return kRuntimeCardSdhc;
    }
    return kRuntimeCardUnknown;
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

// SdFat::end() ultimately calls SPIClass::end(). Unlike regular filesystem
// operations, that cleanup does not pass through SdFat's activate/deactivate
// hook, so the adapter must explicitly fence this physical bus transition.
// Returning false preserves the mounted state when the recovery fence cannot
// be acquired; clearing bookkeeping after an unfenced SPIClass::end() would
// turn a recoverable Busy condition into a silent bus-ownership violation.
bool clear_sdfat(bool discard_files = false)
{
    if (!s_sdfat_mounted)
    {
        return true;
    }

    sys::runtime::BusAccessToken bus_token{};
    if (!sd_spi_bus_acquire(bus_token))
    {
        Serial.println("[SD] SdFat cleanup deferred: shared SPI unavailable");
        return false;
    }

    if (s_transient_file != nullptr && *s_transient_file)
    {
        if (discard_files) abandon_sd_file(*s_transient_file);
        else (void)s_transient_file->close();
    }
    s_sdfat.end();
#if defined(TRAIL_MATE_SDFAT_SDMMC)
    s_sdmmc.end();
#endif
    // SdFat's Arduino driver ends the shared SPIClass. Restore the exact
    // board mapping recorded for this mount so display/radio users keep a
    // valid bus even when it differs from Arduino's global defaults.
    if (s_active_spi_bus.spi != nullptr && s_active_spi_bus.sck >= 0 &&
        s_active_spi_bus.miso >= 0 && s_active_spi_bus.mosi >= 0)
    {
        s_active_spi_bus.spi->begin(s_active_spi_bus.sck,
                                    s_active_spi_bus.miso,
                                    s_active_spi_bus.mosi);
    }
    s_sdfat_mounted = false;
    sd_spi_bus_release(bus_token);
    return true;
}

void reset_info()
{
    ++s_media_session;
    s_info = SdCardInfo{};
}

void sd_clock_bytes(SPIClass& spi, uint8_t count)
{
    for (uint8_t i = 0; i < count; ++i)
    {
        spi.transfer(0xFF);
    }
}

bool sd_wait_not_busy(SPIClass& spi, uint32_t timeout_ms)
{
    const uint32_t start_ms = millis();
    do
    {
        if (spi.transfer(0xFF) != 0x00)
        {
            return true;
        }
        delay(1);
    } while (static_cast<uint32_t>(millis() - start_ms) < timeout_ms);
    return false;
}

uint8_t sd_send_cmd0(SPIClass& spi)
{
    static constexpr uint8_t kCmd0Packet[] = {0x40, 0, 0, 0, 0, 0x95};
    for (uint8_t byte : kCmd0Packet)
    {
        spi.transfer(byte);
    }

    for (uint8_t i = 0; i < 16; ++i)
    {
        const uint8_t token = spi.transfer(0xFF);
        if ((token & 0x80U) == 0)
        {
            return token;
        }
    }
    return 0xFF;
}

bool sd_preflight_go_idle(int sd_cs, SPIClass& spi)
{
    sys::runtime::BusAccessToken bus_token{};
    if (!sd_spi_bus_acquire(bus_token))
    {
        Serial.println("[SD] SdFat preflight skipped: shared SPI unavailable");
        return false;
    }

    uint8_t last_token = 0xFF;
    bool ok = false;
    uint8_t attempt = 0;
    uint8_t attempts_used = 0;

    spi.beginTransaction(SPISettings(kSdInitHz, MSBFIRST, SPI_MODE0));
    digitalWrite(sd_cs, HIGH);
    sd_clock_bytes(spi, 20);

    for (attempt = 1; attempt <= 4; ++attempt)
    {
        attempts_used = attempt;
        digitalWrite(sd_cs, LOW);
        const bool ready = sd_wait_not_busy(spi, 500);
        last_token = sd_send_cmd0(spi);
        digitalWrite(sd_cs, HIGH);
        sd_clock_bytes(spi, 2);

        if (last_token == kSdR1IdleState)
        {
            ok = true;
            break;
        }

        Serial.printf("[SD] SdFat preflight CMD0 retry=%u ready=%d token=0x%02X\n",
                      static_cast<unsigned>(attempt),
                      ready ? 1 : 0,
                      static_cast<unsigned>(last_token));
        delay(25);
    }

    spi.endTransaction();
    sd_spi_bus_release(bus_token);
    Serial.printf("[SD] SdFat preflight CMD0 -> %d token=0x%02X attempts=%u\n",
                  ok ? 1 : 0,
                  static_cast<unsigned>(last_token),
                  static_cast<unsigned>(attempts_used));
    delay(2);
    return ok;
}

void record_sdfat_info(uint32_t initialized_spi_hz)
{
    ++s_media_session;
    const uint32_t info_start_ms = millis();
    Serial.println("[SD][mount] info begin");
    s_info = SdCardInfo{};
    s_info.backend = SdCardBackend::SdFat;
#if defined(TRAIL_MATE_SDFAT_SDMMC)
    s_info.card_type = s_sdmmc.highCapacity() ? kRuntimeCardSdhc : kRuntimeCardSd;
#else
    s_info.card_type = card_type_from_sdfat(s_sdfat);
#endif
    s_info.fat_type = s_sdfat.fatType();
    s_info.initialized_spi_hz = initialized_spi_hz;
    s_info.sector_size = kSdSectorSize;
#if defined(TRAIL_MATE_SDFAT_SDMMC)
    auto* card = &s_sdmmc;
#else
    SdCard* card = s_sdfat.card();
#endif
    if (card != nullptr)
    {
        s_info.sector_count = static_cast<uint32_t>(card->sectorCount());
        s_info.card_size_bytes = static_cast<uint64_t>(s_info.sector_count) * kSdSectorSize;
    }
    Serial.printf("[SD][mount] card type=%u fat=%u sectors=%lu elapsed_ms=%lu\n",
                  static_cast<unsigned>(s_info.card_type),
                  static_cast<unsigned>(s_info.fat_type),
                  static_cast<unsigned long>(s_info.sector_count),
                  static_cast<unsigned long>(millis() - info_start_ms));
    const uint32_t cluster_start_ms = millis();
    const uint64_t cluster_count = s_sdfat.clusterCount();
    const uint64_t bytes_per_cluster = s_sdfat.bytesPerCluster();
    Serial.printf("[SD][mount] cluster count=%llu bytes_per_cluster=%llu elapsed_ms=%lu\n",
                  static_cast<unsigned long long>(cluster_count),
                  static_cast<unsigned long long>(bytes_per_cluster),
                  static_cast<unsigned long>(millis() - cluster_start_ms));
    if (cluster_count > 0 && bytes_per_cluster > 0)
    {
        s_info.total_bytes = cluster_count * bytes_per_cluster;
    }
    Serial.println("[SD][mount] free_cluster_count skipped");
    Serial.printf("[SD][mount] info total elapsed_ms=%lu\n",
                  static_cast<unsigned long>(millis() - info_start_ms));
}

} // namespace

bool sd_spi_bus_acquire(sys::runtime::BusAccessToken& token)
{
#if defined(TRAIL_MATE_SDFAT_SHARED_SPI)
    const uint32_t now_ms = sys::millis_now();
    SdSpiOperationProfile& profile = SharedSpiSdOperationScope::profile();
    sys::runtime::BusAcquireRequest request{};
    request.resource =
        ::platform::esp::common::SharedSpiCoordinator::kSharedBusResource;
    request.policy = profile.active
                         ? profile.policy
                         : sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;
    request.command_id = 0x53445049U;
    request.origin = request.command_id;
    const uint32_t wait_ms =
        profile.active ? profile.wait_ms : kSdRuntimeLockWaitMs;
    request.deadline_ms = now_ms + wait_ms;
    request.owner_label =
        profile.active ? profile.owner : "sd_spi_unscoped";

    const sys::runtime::BusAcquireResult result =
        ::platform::esp::common::shared_spi_coordinator().acquire(request);
    if (result.status != sys::runtime::BusAcquireStatus::Acquired)
    {
        profile.last_bus_status = result.status;
    }
    token = result.token;
    return result.status == sys::runtime::BusAcquireStatus::Acquired &&
           token.valid;
#else
    token = {};
    return true;
#endif
}

void sd_spi_bus_release(const sys::runtime::BusAccessToken& token)
{
#if defined(TRAIL_MATE_SDFAT_SHARED_SPI)
    if (token.valid)
    {
        ::platform::esp::common::shared_spi_coordinator().release(token);
    }
#else
    (void)token;
#endif
}

bool mount_sd_card(int sd_cs,
                   SPIClass& spi,
                   uint32_t spi_hz,
                   const char* mount_point,
                   uint8_t max_files)
{
    return mount_sd_card(
        sd_cs, SdSpiBusConfig{spi, SCK, MISO, MOSI}, spi_hz, mount_point, max_files);
}

bool mount_sd_card(int sd_cs,
                   const SdSpiBusConfig& spi_bus,
                   uint32_t spi_hz,
                   const char* mount_point,
                   uint8_t max_files)
{
#if defined(TRAIL_MATE_SDFAT_SDMMC)
    (void)sd_cs;
    (void)spi_bus;
    (void)spi_hz;
    (void)mount_point;
    (void)max_files;
    Serial.println("[SD] SPI mount rejected: this target uses SDMMC");
    return false;
#else
    (void)mount_point;
    (void)max_files;
    if (!::platform::esp::boards::storageStartupGateSatisfied())
    {
        Serial.println("[SD] mount deferred: first shared-SPI display transaction incomplete");
        return false;
    }
    SdRuntimeOperationGuard operation(
        "sd_mount",
        sys::runtime::BusAccessPolicy::RecoveryExclusive,
        500U);
    if (!operation.locked())
    {
        Serial.println("[SD] mount skipped: filesystem session unavailable");
        return false;
    }

    s_external_block_owner_active = false;
    if (!clear_sdfat())
    {
        Serial.println("[SD] mount deferred: previous shared SPI session still owns cleanup");
        return false;
    }
    reset_info();

    if (spi_bus.sck < 0 || spi_bus.miso < 0 || spi_bus.mosi < 0)
    {
        Serial.println("[SD] mount skipped: incomplete shared SPI pin mapping");
        return false;
    }
    s_active_spi_bus.spi = &spi_bus.spi;
    s_active_spi_bus.sck = spi_bus.sck;
    s_active_spi_bus.miso = spi_bus.miso;
    s_active_spi_bus.mosi = spi_bus.mosi;

    const uint32_t effective_hz = sanitize_sd_spi_hz(spi_hz);
    if (effective_hz != spi_hz)
    {
        Serial.printf("[SD] clamp shared SPI hz requested=%lu effective=%lu\n",
                      static_cast<unsigned long>(spi_hz),
                      static_cast<unsigned long>(effective_hz));
    }

    const bool preflight_ok = sd_preflight_go_idle(sd_cs, spi_bus.spi);
    if (!preflight_ok)
    {
        Serial.println("[SD] SdFat preflight failed; continuing to SdFat.begin for detailed error");
    }

    constexpr uint8_t kSdFatSpiOptions = SHARED_SPI | USER_SPI_BEGIN;
    const uint32_t begin_start = millis();
    Serial.printf("[SD] SdFat.begin config file_type=%d dedicated=%d options=0x%02X hz=%lu\n",
                  static_cast<int>(SDFAT_FILE_TYPE),
                  static_cast<int>(ENABLE_DEDICATED_SPI),
                  static_cast<unsigned>(kSdFatSpiOptions),
                  static_cast<unsigned long>(effective_hz));

    const bool sdfat_ok = s_sdfat.begin(
        SdSpiConfig(sd_cs, kSdFatSpiOptions, effective_hz, &spi_bus.spi));
    Serial.printf("[SD] SdFat.begin hz=%lu -> %d fat=%u elapsed_ms=%lu err=0x%02X data=0x%02X\n",
                  static_cast<unsigned long>(effective_hz),
                  sdfat_ok ? 1 : 0,
                  static_cast<unsigned>(s_sdfat.fatType()),
                  static_cast<unsigned long>(millis() - begin_start),
                  static_cast<unsigned>(s_sdfat.sdErrorCode()),
                  static_cast<unsigned>(s_sdfat.sdErrorData()));
    // A successful begin can still yield an unusable filesystem type. Mark it
    // before the validation branch so clear_sdfat() restores the configured
    // SPI mapping on every partially initialized exit.
    s_sdfat_mounted = sdfat_ok;
    if (!sdfat_ok || s_sdfat.fatType() == 0)
    {
        s_sdfat.printSdError(&Serial);
        if (!clear_sdfat())
        {
            Serial.println("[SD] failed mount cleanup deferred: shared SPI unavailable");
            return false;
        }
        reset_info();
        return false;
    }

    record_sdfat_info(effective_hz);
    Serial.printf("[SD] backend=sdfat fs=%s init_hz=%lu card=%llu MB total=%llu MB sectors=%lu sector_size=%lu\n",
                  sd_card_filesystem_name(),
                  static_cast<unsigned long>(s_info.initialized_spi_hz),
                  static_cast<unsigned long long>(s_info.card_size_bytes / (1024ULL * 1024ULL)),
                  static_cast<unsigned long long>(s_info.total_bytes / (1024ULL * 1024ULL)),
                  static_cast<unsigned long>(s_info.sector_count),
                  static_cast<unsigned long>(s_info.sector_size));
    return true;
#endif
}

#if defined(TRAIL_MATE_SDFAT_SDMMC)
bool mount_sdmmc_card(int clock, int command, int data0)
{
    SdmmcSdConfig config;
    config.clock = clock;
    config.command = command;
    config.data0 = data0;
    return mount_sd_card(config);
}

bool mount_sd_card(const SdmmcSdConfig& config)
{
    SdRuntimeOperationGuard operation("sdmmc_mount", sys::runtime::BusAccessPolicy::RecoveryExclusive, 500U);
    if (!operation.locked() || s_external_block_owner_active) return false;
    if (s_sdfat_mounted) return true;
    if (!s_sdmmc.begin(config)) return false;
    bool mounted = s_sdfat.begin(&s_sdmmc, true, 1);
    if (!mounted) mounted = s_sdfat.begin(&s_sdmmc, true, 0);
    if (!mounted || s_sdfat.fatType() == 0)
    {
        s_sdfat.end();
        s_sdmmc.end();
        reset_info();
        return false;
    }
    s_sdfat_mounted = true;
    // SPI frequency metadata is inapplicable to the SDMMC transport.
    record_sdfat_info(0);
    Serial.printf("[SD] backend=sdfat bus=sdmmc width=%u max_khz=%lu fs=%s\n",
                  config.width, static_cast<unsigned long>(config.max_frequency_khz),
                  sd_card_filesystem_name());
    return true;
}
#endif

void unmount_sd_card()
{
    s_external_block_owner_active = false;
    SdRuntimeOperationGuard guard(
        "sd_unmount",
        sys::runtime::BusAccessPolicy::RecoveryExclusive,
        500U);
    if (!guard.locked())
    {
        Serial.println("[SD] unmount skipped: filesystem session unavailable");
        return;
    }
    if (!clear_sdfat())
    {
        Serial.println("[SD] unmount deferred: shared SPI cleanup unavailable");
        return;
    }
    reset_info();
}

bool sd_card_ready()
{
    return s_info.backend != SdCardBackend::None && s_info.sector_size != 0 &&
           s_info.card_type != kRuntimeCardNone;
}

uint32_t sd_media_session()
{
    return s_media_session.load();
}

SdMediaStatus sd_probe_media()
{
    if (!sd_card_ready() || sd_external_block_owner_active()) return SdMediaStatus::Unavailable;
    if (s_faulted_media_session.load() == sd_media_session()) return SdMediaStatus::IoError;
    SdRuntimeOperationGuard guard("sd_media_probe", sys::runtime::BusAccessPolicy::BackgroundWorkerBounded, 0);
    if (!guard.locked()) return SdMediaStatus::Busy;
    if (!sd_card_ready() || sd_external_block_owner_active()) return SdMediaStatus::Unavailable;
    // Access these scalars only under the filesystem mutex. No sector buffer
    // is permanently resident or placed on an ESP task stack.
    static uint32_t checked_session = 0;
    static uint32_t checked_at = 0;
    static SdMediaStatus previous = SdMediaStatus::Unavailable;
    const auto session = sd_media_session();
    const auto now = sys::millis_now();
    if (checked_session == session)
    {
        if (previous == SdMediaStatus::IoError) return previous;
        if (previous == SdMediaStatus::Ready && static_cast<uint32_t>(now - checked_at) < 1000U) return previous;
    }
    auto* scratch = static_cast<uint8_t*>(heap_caps_malloc(kSdSectorSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (!scratch) return SdMediaStatus::Busy;
    SdIoErrorScope errors;
#if defined(TRAIL_MATE_SDFAT_SDMMC)
    const bool read = s_sdmmc.readSector(0, scratch);
#else
    const bool read = s_sdfat.card() && s_sdfat.card()->readSector(0, scratch);
#endif
    heap_caps_free(scratch);
    // The shared-SPI hook acquires its bus lazily during readSector(). A failed
    // acquisition is not evidence that the physical card is missing.
    if (guard.busStatus() != sys::runtime::BusAcquireStatus::Acquired) return SdMediaStatus::Busy;
    checked_session = session;
    checked_at = now;
    previous = read && errors.error() == 0 ? SdMediaStatus::Ready : SdMediaStatus::IoError;
    if (previous == SdMediaStatus::IoError) s_faulted_media_session.store(session);
    return previous;
}

bool sd_recover_media()
{
    if (sd_external_block_owner_active()) return false;
    {
        SdRuntimeOperationGuard guard("sd_media_recovery", sys::runtime::BusAccessPolicy::RecoveryExclusive, 0);
        if (!guard.locked() || sd_external_block_owner_active()) return false;
        if (sd_card_ready() && s_faulted_media_session.load() != sd_media_session()) return true;
        static uint32_t last_attempt = 0;
        static bool attempted = false;
        const auto now = sys::millis_now();
        if (attempted && static_cast<uint32_t>(now - last_attempt) < 3000U) return false;
        attempted = true;
        last_attempt = now;
        // Do not sync old cached metadata against a replacement card. The
        // cleanup still uses the existing exclusive shared-SPI bus fence.
        if (!clear_sdfat(true)) return false;
        reset_info();
    }
    // Board policy supplies wiring, power and startup gating. Its mount guard
    // rechecks ownership if USB acquired the device after cleanup released it.
    return platform::esp::boards::initializeStorage();
}

bool sd_card_uses_sdfat()
{
    return s_info.backend == SdCardBackend::SdFat;
}

bool sd_card_is_exfat()
{
    return s_info.backend == SdCardBackend::SdFat && s_info.fat_type == FAT_TYPE_EXFAT;
}

SdCardBackend sd_card_backend()
{
    return s_info.backend;
}

SdCardInfo sd_card_info()
{
    return s_info;
}

void record_sd_card_mount_success(uint32_t configured_spi_hz,
                                  uint8_t initialization_attempts)
{
    if (!sd_card_ready())
    {
        return;
    }
    s_info.configured_spi_hz = configured_spi_hz;
    s_info.initialization_attempts =
        initialization_attempts == 0U ? 1U : initialization_attempts;
}

const char* sd_card_backend_name()
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

const char* sd_card_filesystem_name()
{
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
    if (!ensure_filesystem_mutex())
    {
        return false;
    }

    const TickType_t wait_ticks =
        std::max<TickType_t>(1, pdMS_TO_TICKS(kSdExternalOwnerTransitionLockWaitMs));
    if (xSemaphoreTakeRecursive(s_filesystem_mutex, wait_ticks) != pdTRUE)
    {
        return false;
    }
    if (s_external_block_owner_active != active) ++s_media_session;
    s_external_block_owner_active = active;
    xSemaphoreGiveRecursive(s_filesystem_mutex);
    return true;
}

bool sd_exists(const char* path)
{
    const char* normalized = normalize_sd_path(path);
    const uint32_t start_ms = sd_io_begin("exists", normalized);
    bool result = false;
    SdRuntimeOperationGuard guard("sd_exists");
    if (!guard.locked())
    {
        sd_io_end("exists", normalized, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_sdfat.exists(normalized);
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
    bool io_evidence_active = false;

    auto finish = [&](SdFileReadStatus status,
                      std::size_t bytes_read,
                      uint64_t file_size,
                      int32_t error)
    {
        result.status = status;
        result.bytes_read = bytes_read;
        result.file_size = file_size;
        result.error = error;
        if (io_evidence_active) SdIoErrorScope::captureReadMetrics(result);
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

    const uint32_t lock_start_ms = sys::millis_now();
    SdRuntimeOperationGuard operation(
        "sd_map_file",
        sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
        kSdInteractiveReadLockWaitMs);
    result.lock_wait_ms = sys::millis_now() - lock_start_ms;
    if (!operation.locked())
    {
        return finish(SdFileReadStatus::Busy,
                      0,
                      0,
                      bus_acquire_error(operation.status()));
    }
    if (!ensure_transient_file())
    {
        return finish(SdFileReadStatus::IoError, 0, 0, -8);
    }

    FsFile& file = *s_transient_file;
    if (file && !file.close())
    {
        const sys::runtime::BusAcquireStatus status = operation.busStatus();
        return finish(status == sys::runtime::BusAcquireStatus::Acquired
                          ? SdFileReadStatus::IoError
                          : SdFileReadStatus::Busy,
                      0,
                      0,
                      status == sys::runtime::BusAcquireStatus::Acquired
                          ? -7
                          : bus_acquire_error(status));
    }

    SdIoErrorScope io_errors;
    io_evidence_active = true;
    uint64_t file_size = 0;
    const uint32_t open_start_ms = sys::millis_now();
    file = s_sdfat.open(normalized, O_RDONLY);
    result.open_ms = sys::millis_now() - open_start_ms;
    if (!file)
    {
        if (operation.busStatus() != sys::runtime::BusAcquireStatus::Acquired)
            return finish(SdFileReadStatus::Busy, 0, 0, bus_acquire_error(operation.busStatus()));
        if (io_errors.error() != 0)
            return finish(SdFileReadStatus::IoError, 0, 0, io_errors.error());
        if (!s_path_probe) s_path_probe = psram_preferred_object<SdPathProbeScratch>();
        if (!s_path_probe) return finish(SdFileReadStatus::IoError, 0, 0, -8);
        const auto evidence = probe_sd_path(s_sdfat, normalized, *s_path_probe);
        result.open_ms = sys::millis_now() - open_start_ms;
        if (operation.busStatus() != sys::runtime::BusAcquireStatus::Acquired)
            return finish(SdFileReadStatus::Busy, 0, 0, bus_acquire_error(operation.busStatus()));
        if (io_errors.error() != 0)
            return finish(SdFileReadStatus::IoError, 0, 0, io_errors.error());
        // Even Present is an error here: the requested open already failed.
        return finish(evidence == SdPathEvidence::Absent ? SdFileReadStatus::Missing
                                                         : SdFileReadStatus::IoError,
                      0, 0, evidence == SdPathEvidence::Absent ? -1 : -6);
    }

    file_size = file.fileSize();
    if (file_size == 0 || file_size > capacity)
    {
        (void)file.close();
        return finish(SdFileReadStatus::Invalid, 0, file_size, -5);
    }

    const std::size_t target_size = static_cast<std::size_t>(file_size);
    std::size_t total_read = 0;
    while (total_read < target_size)
    {
        const std::size_t chunk_size =
            std::min<std::size_t>(kSdTransferSliceBytes,
                                  target_size - total_read);
        const uint32_t chunk_start_ms =
            sd_io_begin("map_file_read_chunk", normalized, chunk_size);
        const int bytes_read = file.read(buffer + total_read, chunk_size);
        result.read_ms += sys::millis_now() - chunk_start_ms;
        if (bytes_read <= 0)
        {
            const sys::runtime::BusAcquireStatus status = operation.busStatus();
            const int32_t error =
                status == sys::runtime::BusAcquireStatus::Acquired
                    ? -6
                    : bus_acquire_error(status);
            sd_io_end("map_file_read_chunk",
                      normalized,
                      chunk_start_ms,
                      false,
                      0,
                      error);
            (void)file.close();
            return finish(status == sys::runtime::BusAcquireStatus::Acquired
                              ? SdFileReadStatus::IoError
                              : SdFileReadStatus::Busy,
                          total_read,
                          file_size,
                          error);
        }
        sd_io_end("map_file_read_chunk",
                  normalized,
                  chunk_start_ms,
                  true,
                  static_cast<std::size_t>(bytes_read),
                  bytes_read);
        total_read += static_cast<std::size_t>(bytes_read);
    }

    if (!file.close())
    {
        const sys::runtime::BusAcquireStatus status = operation.busStatus();
        return finish(status == sys::runtime::BusAcquireStatus::Acquired
                          ? SdFileReadStatus::IoError
                          : SdFileReadStatus::Busy,
                      total_read,
                      file_size,
                      status == sys::runtime::BusAcquireStatus::Acquired
                          ? -7
                          : bus_acquire_error(status));
    }

    return finish(SdFileReadStatus::Ready, total_read, file_size, 0);
}

bool sd_is_directory(const char* path)
{
    const char* normalized = normalize_sd_path(path);
    const uint32_t start_ms = sd_io_begin("is_dir", normalized);
    bool result = false;
    SdRuntimeOperationGuard guard("sd_is_dir");
    if (!guard.locked())
    {
        sd_io_end("is_dir", normalized, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        if (!ensure_transient_file())
        {
            sd_io_end("is_dir", normalized, start_ms, false, 0, -8);
            return false;
        }
        FsFile& dir = *s_transient_file;
        if (dir)
        {
            (void)dir.close();
        }
        dir = s_sdfat.open(normalized, O_RDONLY);
        result = dir && dir.isDir();
        (void)dir.close();
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
    SdRuntimeOperationGuard guard(
        "sd_mkdir",
        sys::runtime::BusAccessPolicy::DurableCommit,
        kSdDurableLockWaitMs);
    if (!guard.locked())
    {
        sd_io_end("mkdir", normalized, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_sdfat.mkdir(normalized, true);
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
    SdRuntimeOperationGuard guard(
        "sd_rmdir",
        sys::runtime::BusAccessPolicy::DurableCommit,
        kSdDurableLockWaitMs);
    if (!guard.locked())
    {
        sd_io_end("rmdir", normalized, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_sdfat.rmdir(normalized);
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
    SdRuntimeOperationGuard guard(
        "sd_remove",
        sys::runtime::BusAccessPolicy::DurableCommit,
        kSdDurableLockWaitMs);
    if (!guard.locked())
    {
        sd_io_end("remove", normalized, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_sdfat.remove(normalized);
        sd_io_end("remove", normalized, start_ms, result);
        return result;
    }
    sd_io_end("remove", normalized, start_ms, false, 0, -1);
    return false;
}

bool sd_rename(const char* old_path, const char* new_path)
{
    return sd_rename(old_path, new_path, sd_media_session());
}

bool sd_rename(const char* old_path, const char* new_path, uint32_t expected_session)
{
    const char* normalized_old = normalize_sd_path(old_path);
    const char* normalized_new = normalize_sd_path(new_path);
    const uint32_t start_ms = sd_io_begin("rename", normalized_old);
    if (sd_mutation_blocked_by_external_owner("rename", normalized_old, start_ms))
    {
        return false;
    }
    bool result = false;
    SdRuntimeOperationGuard guard(
        "sd_rename",
        sys::runtime::BusAccessPolicy::DurableCommit,
        kSdDurableLockWaitMs);
    if (!guard.locked())
    {
        sd_io_end("rename", normalized_old, start_ms, false, 0, -2);
        return false;
    }
    if (expected_session != sd_media_session() || expected_session == s_faulted_media_session.load() ||
        sd_external_block_owner_active())
    {
        sd_io_end("rename", normalized_old, start_ms, false, 0, -1);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_sdfat.rename(normalized_old, normalized_new);
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
    uint32_t session = 0;
    SdCardBackend backend = SdCardBackend::None;
    char path[128]{};
    char mode[8]{};
};

SdRuntimeFile::SdRuntimeFile()
    : impl_(psram_preferred_object<Impl>())
{
}

SdRuntimeFile::~SdRuntimeFile()
{
    close();
    destroy_psram_preferred_object(impl_);
}

bool SdRuntimeFile::open(const char* path, const char* mode)
{
    return open(path, mode, sd_media_session());
}

bool SdRuntimeFile::open(const char* path, const char* mode, uint32_t expected_session)
{
    close();
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
    const bool mutating = open_mode_mutates(mode);
    SdRuntimeOperationGuard guard(
        "sd_file_open",
        mutating ? sys::runtime::BusAccessPolicy::DurableCommit
                 : sys::runtime::BusAccessPolicy::BackgroundWorkerBounded,
        mutating ? kSdDurableLockWaitMs : kSdRuntimeLockWaitMs);
    if (!guard.locked())
    {
        sd_io_end("file_open", impl_->path, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        if (expected_session != sd_media_session() || expected_session == s_faulted_media_session.load() ||
            sd_external_block_owner_active())
        {
            sd_io_end("file_open", impl_->path, start_ms, false, 0, -1);
            return false;
        }
        impl_->session = expected_session;
        impl_->sdfat_file = s_sdfat.open(normalized, sdfat_open_flags(mode));
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
        const bool mutating = open_mode_mutates(impl_->mode);
        SdRuntimeOperationGuard guard(
            "sd_file_close",
            mutating ? sys::runtime::BusAccessPolicy::DurableCommit
                     : sys::runtime::BusAccessPolicy::BackgroundWorkerBounded,
            mutating ? kSdDurableLockWaitMs : kSdRuntimeLockWaitMs);
        if (guard.locked() && is_open())
        {
            impl_->sdfat_file.close();
            sd_io_end("file_close", impl_->path, start_ms, true);
        }
        else
        {
            sd_io_end("file_close", impl_->path, start_ms, false, 0, -2);
        }
    }
    // A stale/contended handle must not flush from its later destructor or open.
    // This SdFat configuration owns file state inline and has a non-I/O destructor.
    abandon_sd_file(impl_->sdfat_file);
    impl_->backend = SdCardBackend::None;
    impl_->path[0] = '\0';
    impl_->mode[0] = '\0';
}

bool SdRuntimeFile::is_open() const
{
    return impl_ != nullptr && impl_->backend != SdCardBackend::None &&
           impl_->session == sd_media_session() &&
           impl_->session != s_faulted_media_session.load() && !sd_external_block_owner_active();
}

int SdRuntimeFile::available() const
{
    if (!is_open())
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        SdRuntimeOperationGuard guard("sd_file_available");
        if (!guard.locked() || !is_open())
        {
            return 0;
        }
        return impl_->sdfat_file.available();
    }
    return 0;
}

int SdRuntimeFile::read(void* buffer, std::size_t bytes_to_read)
{
    if (!is_open() || buffer == nullptr || bytes_to_read == 0)
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("file_read", impl_->path, bytes_to_read);
        SdRuntimeOperationGuard guard("sd_file_read");
        if (!guard.locked() || !is_open())
        {
            sd_io_end("file_read", impl_->path, start_ms, false, bytes_to_read, -2);
            return -1;
        }
        std::size_t total_read = 0;
        auto* out = static_cast<uint8_t*>(buffer);
        while (total_read < bytes_to_read)
        {
            const std::size_t slice =
                std::min(kSdTransferSliceBytes, bytes_to_read - total_read);
            const int current = impl_->sdfat_file.read(out + total_read, slice);
            if (current <= 0)
            {
                const int result =
                    total_read > 0 ? static_cast<int>(total_read) : current;
                sd_io_end("file_read",
                          impl_->path,
                          start_ms,
                          current == 0,
                          total_read,
                          result);
                return result;
            }
            total_read += static_cast<std::size_t>(current);
            if (static_cast<std::size_t>(current) < slice)
            {
                break;
            }
        }
        const int result = static_cast<int>(total_read);
        sd_io_end("file_read",
                  impl_->path,
                  start_ms,
                  true,
                  total_read,
                  result);
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
        SdRuntimeOperationGuard guard("sd_file_read_byte");
        if (!guard.locked() || !is_open())
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
        SdRuntimeOperationGuard guard("sd_file_read_bytes");
        if (!guard.locked() || !is_open())
        {
            sd_io_end("file_read_bytes", impl_->path, start_ms, false, bytes_to_read, -2);
            return 0;
        }
        std::size_t total_read = 0;
        while (total_read < bytes_to_read)
        {
            const std::size_t slice =
                std::min(kSdTransferSliceBytes, bytes_to_read - total_read);
            const int current =
                impl_->sdfat_file.read(buffer + total_read, slice);
            if (current <= 0)
            {
                sd_io_end("file_read_bytes",
                          impl_->path,
                          start_ms,
                          current == 0,
                          total_read,
                          current);
                return total_read;
            }
            total_read += static_cast<std::size_t>(current);
            if (static_cast<std::size_t>(current) < slice)
            {
                break;
            }
        }
        sd_io_end("file_read_bytes",
                  impl_->path,
                  start_ms,
                  true,
                  total_read,
                  static_cast<int32_t>(total_read));
        return total_read;
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
        SdRuntimeOperationGuard guard(
            "sd_file_write",
            sys::runtime::BusAccessPolicy::DurableCommit,
            kSdDurableLockWaitMs);
        if (!guard.locked() || !is_open())
        {
            sd_io_end("file_write", impl_->path, start_ms, false, bytes_to_write, -2);
            return 0;
        }
        std::size_t total_written = 0;
        const auto* input = static_cast<const uint8_t*>(buffer);
        while (total_written < bytes_to_write)
        {
            const std::size_t slice =
                std::min(kSdTransferSliceBytes,
                         bytes_to_write - total_written);
            const std::size_t current =
                impl_->sdfat_file.write(input + total_written, slice);
            total_written += current;
            if (current != slice)
            {
                break;
            }
        }
        sd_io_end("file_write",
                  impl_->path,
                  start_ms,
                  total_written == bytes_to_write,
                  bytes_to_write,
                  static_cast<int32_t>(total_written));
        return total_written;
    }
    return 0;
}

std::size_t SdRuntimeFile::write_byte(uint8_t value)
{
    if (!is_open())
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        if (s_external_block_owner_active)
        {
            return 0;
        }
        SdRuntimeOperationGuard guard(
            "sd_file_write_byte",
            sys::runtime::BusAccessPolicy::DurableCommit,
            kSdDurableLockWaitMs);
        if (!guard.locked() || !is_open())
        {
            return 0;
        }
        return impl_->sdfat_file.write(value);
    }
    return 0;
}

std::size_t SdRuntimeFile::print(const char* text)
{
    if (!is_open() || text == nullptr)
    {
        return 0;
    }
    return write(text, std::strlen(text));
}

std::size_t SdRuntimeFile::print(double value, int digits)
{
    if (!is_open())
    {
        return 0;
    }
    const uint8_t precision = digits < 0 ? 0 : static_cast<uint8_t>(digits);
    if (impl_->backend == SdCardBackend::SdFat)
    {
        if (s_external_block_owner_active)
        {
            return 0;
        }
        SdRuntimeOperationGuard guard(
            "sd_file_print",
            sys::runtime::BusAccessPolicy::DurableCommit,
            kSdDurableLockWaitMs);
        if (!guard.locked() || !is_open())
        {
            return 0;
        }
        return impl_->sdfat_file.print(value, precision);
    }
    return 0;
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

    const std::size_t buffer_size = static_cast<std::size_t>(len) + 1U;
    auto* buffer = static_cast<char*>(psram_preferred_bytes(buffer_size));
    if (buffer == nullptr)
    {
        va_end(args);
        return 0;
    }
    std::vsnprintf(buffer, buffer_size, format, args);
    va_end(args);
    const std::size_t written = write(buffer, static_cast<std::size_t>(len));
    heap_caps_free(buffer);
    return written;
}

bool SdRuntimeFile::seek(uint64_t offset)
{
    if (!is_open())
    {
        return false;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        SdRuntimeOperationGuard guard("sd_file_seek");
        if (!guard.locked() || !is_open())
        {
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
        SdRuntimeOperationGuard guard("sd_file_position");
        if (!guard.locked() || !is_open())
        {
            return 0;
        }
        return impl_->sdfat_file.curPosition();
    }
    return 0;
}

uint64_t SdRuntimeFile::size() const
{
    if (!is_open())
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        SdRuntimeOperationGuard guard("sd_file_size");
        if (!guard.locked() || !is_open())
        {
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
        SdRuntimeOperationGuard guard(
            "sd_file_flush",
            sys::runtime::BusAccessPolicy::DurableCommit,
            kSdDurableLockWaitMs);
        if (!guard.locked() || !is_open())
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
    FsFile entry_scratch;
    SdCardBackend backend = SdCardBackend::None;
    char path[128]{};
};

SdRuntimeDir::SdRuntimeDir()
    : impl_(psram_preferred_object<Impl>())
{
}

SdRuntimeDir::~SdRuntimeDir()
{
    close();
    destroy_psram_preferred_object(impl_);
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
    SdRuntimeOperationGuard guard("sd_dir_open");
    if (!guard.locked())
    {
        sd_io_end("dir_open", impl_->path, start_ms, false, 0, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
        impl_->sdfat_dir = s_sdfat.open(normalized, O_RDONLY);
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
        SdRuntimeOperationGuard guard("sd_dir_close");
        if (guard.locked())
        {
            if (impl_->entry_scratch)
            {
                (void)impl_->entry_scratch.close();
            }
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
    return read_next_status(name, name_size, is_dir) == SdDirReadStatus::Entry;
}

SdDirReadStatus SdRuntimeDir::read_next_status(char* name, std::size_t name_size, bool* is_dir)
{
    if (name == nullptr || name_size == 0)
    {
        return SdDirReadStatus::Invalid;
    }
    name[0] = '\0';
    if (is_dir != nullptr)
    {
        *is_dir = false;
    }
    if (!is_open()) return SdDirReadStatus::Unavailable;

    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("dir_read", impl_->path);
        SdRuntimeOperationGuard guard("sd_dir_read");
        if (!guard.locked())
        {
            sd_io_end("dir_read", impl_->path, start_ms, false, 0, -2);
            return SdDirReadStatus::Busy;
        }
        if (impl_->entry_scratch)
        {
            (void)impl_->entry_scratch.close();
        }
        impl_->entry_scratch = impl_->sdfat_dir.openNextFile(O_RDONLY);
        FsFile& entry = impl_->entry_scratch;
        if (!entry)
        {
            const bool failed = impl_->sdfat_dir.getError() != 0;
            sd_io_end("dir_read", impl_->path, start_ms, !failed, 0, failed ? -1 : 0);
            return failed ? SdDirReadStatus::IoError : SdDirReadStatus::End;
        }
        entry.getName(name, name_size);
        if (is_dir != nullptr)
        {
            *is_dir = entry.isDir();
        }
        entry.close();
        sd_io_end("dir_read", impl_->path, start_ms, true, 0, name[0] != '\0' ? 1 : 0);
        return name[0] != '\0' ? SdDirReadStatus::Entry : SdDirReadStatus::IoError;
    }

    return SdDirReadStatus::Unavailable;
}

bool sd_read_raw(uint32_t lba, uint8_t* buffer)
{
    char path[32];
    std::snprintf(path, sizeof(path), "raw:%lu", static_cast<unsigned long>(lba));
    const uint32_t start_ms = sd_io_begin("raw_read", path, kSdSectorSize);
    bool result = false;
    const bool external_transfer = s_external_block_owner_active;
    SdRuntimeOperationGuard guard(
        "sd_raw_read",
        external_transfer ? sys::runtime::BusAccessPolicy::DurableCommit
                          : sys::runtime::BusAccessPolicy::BackgroundWorkerBounded,
        external_transfer ? kSdExternalTransferLockWaitMs : kSdRuntimeLockWaitMs,
        SdAccessOwner::ExternalStorageRaw);
    if (!guard.locked())
    {
        sd_io_end("raw_read", path, start_ms, false, kSdSectorSize, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
#if defined(TRAIL_MATE_SDFAT_SDMMC)
        result = s_sdmmc.readSector(lba, buffer);
#else
        result = s_sdfat.card() != nullptr &&
                 s_sdfat.card()->readSector(lba, buffer);
#endif
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
    SdRuntimeOperationGuard guard(
        "sd_raw_write",
        sys::runtime::BusAccessPolicy::DurableCommit,
        s_external_block_owner_active ? kSdExternalTransferLockWaitMs
                                      : kSdDurableLockWaitMs,
        SdAccessOwner::ExternalStorageRaw);
    if (!guard.locked())
    {
        sd_io_end("raw_write", path, start_ms, false, kSdSectorSize, -2);
        return false;
    }
    if (s_info.backend == SdCardBackend::SdFat)
    {
#if defined(TRAIL_MATE_SDFAT_SDMMC)
        result = s_sdmmc.writeSector(lba, buffer);
#else
        result = s_sdfat.card() != nullptr &&
                 s_sdfat.card()->writeSector(lba, buffer);
#endif
        sd_io_end("raw_write", path, start_ms, result, kSdSectorSize);
        return result;
    }
    sd_io_end("raw_write", path, start_ms, false, kSdSectorSize, -1);
    return false;
}

} // namespace platform::esp::arduino_common::storage
