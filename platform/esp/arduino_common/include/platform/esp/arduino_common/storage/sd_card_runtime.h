#pragma once

#include "platform/esp/arduino_common/storage/sd_transport_config.h"
#include <cstddef>
#include <cstdint>

class SPIClass;

namespace platform::esp::arduino_common::storage
{

enum class SdCardBackend : uint8_t
{
    None = 0,
    SdFat,
};

// Describes the exact physical SPI wiring used by one shared-SPI SD mount.
// This is intentionally only a reference plus pin numbers: it owns no driver,
// sector buffer, or transfer payload. Keeping it explicit lets SdFat restore
// the same controller mapping after begin()/end(), including boards whose
// pins are not the Arduino global SCK/MISO/MOSI defaults.
struct SdSpiBusConfig
{
    SPIClass& spi;
    int sck = -1;
    int miso = -1;
    int mosi = -1;
};

struct SdCardInfo
{
    SdCardBackend backend = SdCardBackend::None;
    uint8_t card_type = 0;
    uint8_t fat_type = 0;
    // Zero until SdFat has completed a successful initialization. This is the
    // negotiated mount clock, not a card speed-class or throughput estimate.
    uint32_t initialized_spi_hz = 0;
    // Board-requested initial candidate and the number of distinct candidates
    // attempted before this successful mount. They make fallback observable
    // without retaining a dynamically allocated attempt log.
    uint32_t configured_spi_hz = 0;
    uint8_t initialization_attempts = 0;
    uint32_t sector_size = 0;
    uint32_t sector_count = 0;
    uint64_t card_size_bytes = 0;
    uint64_t total_bytes = 0;
    uint64_t used_bytes = 0;
};

static_assert(sizeof(SdCardInfo) <= 64U,
              "SD runtime metadata must remain a bounded scalar snapshot");

bool mount_sd_card(int sd_cs,
                   SPIClass& spi,
                   uint32_t spi_hz,
                   const char* mount_point,
                   uint8_t max_files);
bool mount_sd_card(int sd_cs,
                   const SdSpiBusConfig& spi_bus,
                   uint32_t spi_hz,
                   const char* mount_point,
                   uint8_t max_files);
void unmount_sd_card();

#if defined(TRAIL_MATE_SDFAT_SDMMC)
bool mount_sd_card(const SdmmcSdConfig& config);
// Compatibility for existing one-bit callers; new boards use typed config.
bool mount_sdmmc_card(int clock, int command, int data0);
#endif

bool sd_card_ready();
// Changes on mount/reset and ownership transitions, even if both transitions
// occur between a consumer's polls. Equality is a session check, not a card ID.
uint32_t sd_media_session();
enum class SdMediaStatus : uint8_t
{
    Ready,
    Busy,
    Unavailable,
    IoError,
};
// Rate-limited physical read, not a filesystem-cache lookup. I/O failure is
// latched for this session; recovery requires a new validated mount/session.
SdMediaStatus sd_probe_media();
// Bounded-frequency recovery through the existing board mount policy. Never
// formats media or takes ownership from USB. False includes deferred/absent.
bool sd_recover_media();
bool sd_card_uses_sdfat();
bool sd_card_is_exfat();
SdCardBackend sd_card_backend();
SdCardInfo sd_card_info();
void record_sd_card_mount_success(uint32_t configured_spi_hz,
                                  uint8_t initialization_attempts);
const char* sd_card_backend_name();
const char* sd_card_filesystem_name();
bool sd_external_block_owner_active();
/**
 * @brief Atomically transfer the SD runtime's logical owner.
 *
 * The transition waits for any operation already holding the shared runtime
 * lock, then changes the owner while that lock is held. Callers must treat a
 * false result as a failed ownership handoff and leave the previous owner in
 * control.
 */
bool sd_set_external_block_owner_active(bool active);

bool sd_exists(const char* path);
bool sd_is_directory(const char* path);
bool sd_mkdir(const char* path);
bool sd_rmdir(const char* path);
bool sd_remove(const char* path);
bool sd_rename(const char* old_path, const char* new_path);
bool sd_rename(const char* old_path, const char* new_path, uint32_t expected_session);

enum class SdFileReadStatus : uint8_t
{
    Ready,
    Missing,
    Busy,
    Unavailable,
    IoError,
    Invalid,
};

struct SdFileReadResult
{
    SdFileReadStatus status = SdFileReadStatus::IoError;
    std::size_t bytes_read = 0;
    uint64_t file_size = 0;
    int32_t error = -1;
    uint32_t lock_wait_ms = 0;
    uint32_t open_ms = 0;
    uint32_t read_ms = 0;
    uint32_t block_calls = 0;
    uint32_t block_sectors = 0;
    uint32_t block_max_sectors = 0;
    uint32_t block_us = 0;
    bool block_timing_available = false;
};

// Reads a file through bounded device-owned transactions. Callers receive a
// semantic storage result and do not provide SPI policy or lock metadata.
SdFileReadResult sd_read_file(const char* path,
                              uint8_t* buffer,
                              std::size_t capacity);

class SdRuntimeFile
{
  public:
    SdRuntimeFile();
    ~SdRuntimeFile();

    SdRuntimeFile(const SdRuntimeFile&) = delete;
    SdRuntimeFile& operator=(const SdRuntimeFile&) = delete;

    bool open(const char* path, const char* mode);
    // Reject a replaced or faulted medium under the filesystem lock.
    bool open(const char* path, const char* mode, uint32_t expected_session);
    void close();
    bool is_open() const;
    int available() const;
    int read(void* buffer, std::size_t bytes_to_read);
    int read_byte();
    std::size_t read_bytes(char* buffer, std::size_t bytes_to_read);
    std::size_t write(const void* buffer, std::size_t bytes_to_write);
    std::size_t write_byte(uint8_t value);
    std::size_t print(const char* text);
    std::size_t print(double value, int digits = 2);
    std::size_t printf(const char* format, ...);
    bool seek(uint64_t offset);
    uint64_t position() const;
    uint64_t size() const;
    bool flush();

  private:
    class Impl;
    Impl* impl_;
};

enum class SdDirReadStatus : uint8_t { Entry, End, Busy, Unavailable, Invalid, IoError };

class SdRuntimeDir
{
  public:
    SdRuntimeDir();
    ~SdRuntimeDir();

    SdRuntimeDir(const SdRuntimeDir&) = delete;
    SdRuntimeDir& operator=(const SdRuntimeDir&) = delete;

    bool open(const char* path);
    void close();
    bool is_open() const;
    bool read_next(char* name, std::size_t name_size, bool* is_dir);
    SdDirReadStatus read_next_status(char* name, std::size_t name_size, bool* is_dir);

  private:
    class Impl;
    Impl* impl_;
};

bool sd_read_raw(uint32_t lba, uint8_t* buffer);
bool sd_write_raw(uint32_t lba, const uint8_t* buffer);

} // namespace platform::esp::arduino_common::storage
