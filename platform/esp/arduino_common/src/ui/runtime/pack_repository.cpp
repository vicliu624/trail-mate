#include "platform/ui/pack_repository_runtime.h"

// ESP-specific package repository backend kept out of ui_shared so the shared
// presentation layer no longer owns storage/network implementation details.

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <map>
#include <new>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "platform/ui/device_runtime.h"
#include "platform/ui/http_client_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "ui/localization.h"
#include "ui/runtime/memory_profile.h"
#include "ui/support/lvgl_fs_utils.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#ifdef INADDR_NONE
#undef INADDR_NONE
#endif
#if __has_include(<miniz.h>)
#include <miniz.h>
#elif __has_include(<rom/miniz.h>)
#include <rom/miniz.h>
#else
#error "A compatible miniz header is required for pack_repository.cpp"
#endif

#if defined(ARDUINO)
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include <Arduino.h>
#else
#include "platform/esp/idf_common/flash_storage_runtime.h"
#endif

namespace ui::runtime::packs
{

bool install_package_impl(const PackageRecord& package,
                          std::string& out_error,
                          bool reload_language_after_install);

namespace
{

constexpr const char* kCatalogUrl = "https://vicliu624.github.io/trail-mate/data/packs.json";
constexpr const char* kCatalogBaseUrl = "https://vicliu624.github.io/trail-mate";
#if defined(ARDUINO)
constexpr const char* kFlashPackRoot = "/fs/trailmate/packs";
constexpr const char* kFlashIndexDir = "/fs/trailmate/packs/index";
constexpr const char* kFlashTempDir = "/fs/trailmate/packs/index/tmp";
constexpr const char* kFlashInstalledIndexPath = "/fs/trailmate/packs/index/installed.json";
#endif
constexpr const char* kSdPackRoot = "/trailmate/packs";
constexpr const char* kSdInstalledIndexPath = "/trailmate/packs/index/installed.json";
constexpr const char* kLegacyInstalledIndexPath = "/trailmate/packs/.index/installed.json";
constexpr const char* kStorageSd = "sd";
constexpr const char* kStorageFlash = "flash";
#if defined(ARDUINO)
constexpr const char* kPackRoot = kSdPackRoot;
constexpr const char* kIndexDir = "/trailmate/packs/index";
constexpr const char* kTempDir = "/trailmate/packs/index/tmp";
constexpr const char* kInstalledIndexPath = kSdInstalledIndexPath;
constexpr const char* kInstallStorage = kStorageSd;
#else
constexpr const char* kPackRoot = "/fs/trailmate/packs";
constexpr const char* kIndexDir = "/fs/trailmate/packs/index";
constexpr const char* kTempDir = "/fs/trailmate/packs/index/tmp";
constexpr const char* kInstalledIndexPath = "/fs/trailmate/packs/index/installed.json";
constexpr const char* kInstallStorage = kStorageFlash;
#endif
constexpr int kHttpBufferSize = 1024;
constexpr int kHttpTxBufferSize = 512;
constexpr std::size_t kLargeScratchThresholdBytes = 4096;
constexpr std::size_t kFileWriteChunkBytes = 4096;
constexpr std::size_t kFileReadChunkBytes = 4096;
constexpr std::size_t kMaxInstalledIndexTokenBytes = 512;
constexpr std::size_t kMaxZipEntryNameBytes = 512;
constexpr std::size_t kZipEocdMinSize = 22;
constexpr std::size_t kZipTailSearchMax = 0x10000 + kZipEocdMinSize;
constexpr std::uint32_t kZipEocdSignature = 0x06054B50u;
constexpr std::uint32_t kZipCentralSignature = 0x02014B50u;
constexpr std::uint32_t kZipLocalSignature = 0x04034B50u;

struct InstalledIndex
{
    std::vector<InstalledPackageRecord> packages;
};

constexpr uint32_t kInstallWorkerStackBytes = 16 * 1024;
constexpr UBaseType_t kInstallWorkerPriority = 3;

struct InstallWorkerContext
{
    PackageRecord package{};
};

struct AsyncInstallState
{
    SemaphoreHandle_t mutex = nullptr;
    TaskHandle_t worker_task = nullptr;
    bool launch_pending = false;
    PackageInstallStatus status{};
};

AsyncInstallState s_install_state{};

bool ensure_install_mutex()
{
    if (s_install_state.mutex != nullptr)
    {
        return true;
    }

    s_install_state.mutex = xSemaphoreCreateMutex();
    return s_install_state.mutex != nullptr;
}

class InstallStateLock
{
  public:
    explicit InstallStateLock(SemaphoreHandle_t mutex) : mutex_(mutex)
    {
        if (mutex_ != nullptr)
        {
            xSemaphoreTake(mutex_, portMAX_DELAY);
        }
    }

    ~InstallStateLock()
    {
        if (mutex_ != nullptr)
        {
            xSemaphoreGive(mutex_);
        }
    }

    InstallStateLock(const InstallStateLock&) = delete;
    InstallStateLock& operator=(const InstallStateLock&) = delete;

  private:
    SemaphoreHandle_t mutex_ = nullptr;
};

void set_install_status_locked(PackageInstallPhase phase,
                               bool busy,
                               const std::string& package_id,
                               const char* message,
                               const char* detail = nullptr,
                               int progress_percent = -1)
{
    s_install_state.status.phase = phase;
    s_install_state.status.busy = busy;
    s_install_state.status.progress_percent = progress_percent;
    s_install_state.status.package_id = package_id;
    s_install_state.status.message = message ? message : "";
    s_install_state.status.detail = detail ? detail : "";
}

void set_install_status(PackageInstallPhase phase,
                        bool busy,
                        const std::string& package_id,
                        const char* message,
                        const char* detail = nullptr,
                        int progress_percent = -1)
{
    if (!ensure_install_mutex())
    {
        return;
    }

    InstallStateLock lock(s_install_state.mutex);
    set_install_status_locked(phase,
                              busy,
                              package_id,
                              message,
                              detail,
                              progress_percent);
}

void* allocate_pack_scratch(std::size_t bytes)
{
    if (bytes == 0)
    {
        return nullptr;
    }

    const bool prefer_psram =
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0 &&
        bytes > kLargeScratchThresholdBytes;
    const uint32_t primary_caps = prefer_psram ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                                               : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t secondary_caps = prefer_psram ? (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                                                 : (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    void* ptr = heap_caps_malloc(bytes, primary_caps);
    if (ptr == nullptr)
    {
        ptr = heap_caps_malloc(bytes, secondary_caps);
    }
    return ptr;
}

class ScopedPackScratch
{
  public:
    explicit ScopedPackScratch(std::size_t bytes) : data_(allocate_pack_scratch(bytes))
    {
        size_ = data_ != nullptr ? bytes : 0;
    }

    ~ScopedPackScratch()
    {
        if (data_ != nullptr)
        {
            heap_caps_free(data_);
        }
    }

    ScopedPackScratch(const ScopedPackScratch&) = delete;
    ScopedPackScratch& operator=(const ScopedPackScratch&) = delete;

    explicit operator bool() const
    {
        return data_ != nullptr;
    }

    void* data()
    {
        return data_;
    }

    std::uint8_t* bytes()
    {
        return static_cast<std::uint8_t*>(data_);
    }

    std::size_t size() const
    {
        return data_ != nullptr ? size_ : 0;
    }

  private:
    void* data_ = nullptr;
    std::size_t size_ = 0;
};

void install_worker_task_entry(void* param)
{
    InstallWorkerContext* ctx = static_cast<InstallWorkerContext*>(param);
    PackageRecord package{};
    if (ctx != nullptr)
    {
        package = std::move(ctx->package);
        delete ctx;
    }

    std::printf("[Packs][InstallAsync] worker start id=%s stack=%lu priority=%u\n",
                package.id.c_str(),
                static_cast<unsigned long>(kInstallWorkerStackBytes),
                static_cast<unsigned>(kInstallWorkerPriority));

    std::string error;
    const bool ok = install_package_impl(package, error, false);

    if (ensure_install_mutex())
    {
        InstallStateLock lock(s_install_state.mutex);
        s_install_state.worker_task = nullptr;
        s_install_state.launch_pending = false;
        set_install_status_locked(ok ? PackageInstallPhase::Succeeded : PackageInstallPhase::Failed,
                                  false,
                                  package.id,
                                  ok ? "Package installed"
                                     : (error.empty() ? "Install package failed" : error.c_str()),
                                  package.id.c_str());
    }

    std::printf("[Packs][InstallAsync] worker finish id=%s ok=%d error=%s\n",
                package.id.c_str(),
                ok ? 1 : 0,
                error.empty() ? "<none>" : error.c_str());
    vTaskDelete(nullptr);
}

std::string lowercase_ascii(std::string value)
{
    for (char& ch : value)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool starts_with(const std::string& value, const char* prefix)
{
    if (!prefix)
    {
        return false;
    }
    const std::size_t prefix_len = std::strlen(prefix);
    return value.size() >= prefix_len && value.compare(0, prefix_len, prefix) == 0;
}

std::string trim_trailing_slash(std::string value)
{
    while (!value.empty() && value.back() == '/')
    {
        value.pop_back();
    }
    return value;
}

std::string join_url(const std::string& base, const std::string& path)
{
    if (path.empty())
    {
        return base;
    }
    if (starts_with(path, "http://") || starts_with(path, "https://"))
    {
        return path;
    }
    const std::string normalized_base = trim_trailing_slash(base);
    if (path.front() == '/')
    {
        return normalized_base + path;
    }
    return normalized_base + "/" + path;
}

std::string join_logical_path(const std::string& base, const std::string& leaf)
{
    if (base.empty())
    {
        return leaf;
    }
    if (leaf.empty())
    {
        return base;
    }
    if (leaf.front() == '/')
    {
        return leaf;
    }
    if (base.back() == '/')
    {
        return base + leaf;
    }
    return base + "/" + leaf;
}

std::string active_memory_profile_name()
{
    const ui::runtime::MemoryProfile& profile = ui::runtime::current_memory_profile();
    return profile.name ? profile.name : "";
}

struct ParsedVersion
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string prerelease;
    bool valid = false;
};

ParsedVersion parse_version(const std::string& text)
{
    ParsedVersion version{};
    if (text.empty())
    {
        return version;
    }

    std::string numeric = text;

    const std::size_t first_digit = numeric.find_first_of("0123456789");
    if (first_digit == std::string::npos)
    {
        return version;
    }
    if (first_digit > 0)
    {
        numeric = numeric.substr(first_digit);
    }

    const std::size_t plus = numeric.find('+');
    if (plus != std::string::npos)
    {
        numeric = numeric.substr(0, plus);
    }

    const std::size_t dash = numeric.find('-');
    if (dash != std::string::npos)
    {
        version.prerelease = numeric.substr(dash + 1);
        numeric = numeric.substr(0, dash);
    }

    int parts[3] = {0, 0, 0};
    std::size_t part_index = 0;
    std::size_t start = 0;
    while (start <= numeric.size() && part_index < 3)
    {
        const std::size_t end = numeric.find('.', start);
        const std::string token = numeric.substr(
            start,
            end == std::string::npos ? std::string::npos : (end - start));
        if (!token.empty())
        {
            char* parse_end = nullptr;
            const long parsed = std::strtol(token.c_str(), &parse_end, 10);
            if (parse_end != token.c_str())
            {
                parts[part_index] = static_cast<int>(parsed);
                version.valid = true;
            }
        }
        ++part_index;
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }

    version.major = parts[0];
    version.minor = parts[1];
    version.patch = parts[2];
    return version;
}

int compare_versions(const std::string& lhs, const std::string& rhs)
{
    const ParsedVersion left = parse_version(lhs);
    const ParsedVersion right = parse_version(rhs);

    if (left.major != right.major)
    {
        return left.major < right.major ? -1 : 1;
    }
    if (left.minor != right.minor)
    {
        return left.minor < right.minor ? -1 : 1;
    }
    if (left.patch != right.patch)
    {
        return left.patch < right.patch ? -1 : 1;
    }
    if (left.prerelease == right.prerelease)
    {
        return 0;
    }
    if (left.prerelease.empty())
    {
        return 1;
    }
    if (right.prerelease.empty())
    {
        return -1;
    }
    return left.prerelease < right.prerelease ? -1 : 1;
}

#if defined(ARDUINO)

enum class PackStorageBackend : std::uint8_t
{
    None = 0,
    SdRuntime,
    FlashPosix,
};

struct PackStorageBinding
{
    PackStorageBackend backend = PackStorageBackend::None;
    std::string volume_path;
    const char* storage = nullptr;
};

bool is_flash_logical_path(const std::string& logical_path)
{
    return logical_path == "/fs" || starts_with(logical_path, "/fs/");
}

bool is_explicit_sd_logical_path(const std::string& logical_path)
{
    return logical_path == "/sd" || starts_with(logical_path, "/sd/");
}

const char* storage_for_logical_path(const std::string& logical_path)
{
    return is_explicit_sd_logical_path(logical_path) ? kStorageSd
                                                     : (is_flash_logical_path(logical_path) ? kStorageFlash
                                                                                            : kStorageSd);
}

std::string strip_mount_prefix(const std::string& logical_path, const char* prefix)
{
    if (!prefix || !starts_with(logical_path, prefix))
    {
        return logical_path;
    }

    std::string stripped = logical_path.substr(std::strlen(prefix));
    if (stripped.empty())
    {
        stripped = "/";
    }
    return stripped;
}

std::string flash_storage_path_from_logical(const std::string& logical_path)
{
    std::string suffix = strip_mount_prefix(logical_path, "/fs");
    if (suffix.empty())
    {
        suffix = "/";
    }
    if (suffix.front() != '/')
    {
        suffix.insert(suffix.begin(), '/');
    }
    return std::string("/fs") + suffix;
}

std::string file_entry_name(const char* raw_name)
{
    if (raw_name == nullptr || raw_name[0] == '\0')
    {
        return {};
    }

    std::string name = raw_name;
    const std::size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
    {
        name = name.substr(slash + 1);
    }
    return name;
}

bool ensure_flash_pack_storage_ready(bool allow_format_on_fail)
{
#if defined(ARDUINO)
    return ::ui::fs::ensure_flash_storage_ready(allow_format_on_fail);
#else
    return platform::esp::idf_common::flash_storage_runtime::ensure_ready(
        allow_format_on_fail);
#endif
}

bool resolve_storage_binding(const std::string& logical_path,
                             bool allow_format_on_fail,
                             PackStorageBinding& out)
{
    out = PackStorageBinding{};
    if (logical_path.empty())
    {
        return false;
    }

    if (is_flash_logical_path(logical_path))
    {
        if (!ensure_flash_pack_storage_ready(allow_format_on_fail))
        {
            return false;
        }

        out.backend = PackStorageBackend::FlashPosix;
        out.storage = kStorageFlash;
        out.volume_path = flash_storage_path_from_logical(logical_path);
        return true;
    }

    if (!platform::ui::device::card_ready())
    {
        return false;
    }

    out.backend = PackStorageBackend::SdRuntime;
    out.storage = kStorageSd;
    out.volume_path = is_explicit_sd_logical_path(logical_path)
                          ? strip_mount_prefix(logical_path, "/sd")
                          : logical_path;
    if (out.volume_path.empty())
    {
        out.volume_path = "/";
    }
    return true;
}

std::string normalize_pack_path(const std::string& logical_path, bool allow_format_on_fail)
{
    if (logical_path.empty())
    {
        return {};
    }

    if (is_flash_logical_path(logical_path) &&
        !ensure_flash_pack_storage_ready(allow_format_on_fail))
    {
        return {};
    }

    if (is_flash_logical_path(logical_path))
    {
        std::string suffix = logical_path.substr(3);
        if (suffix.empty())
        {
            suffix = "/";
        }
        if (suffix.front() != '/')
        {
            suffix.insert(suffix.begin(), '/');
        }
        return std::string("F:") + suffix;
    }

    (void)allow_format_on_fail;
    return ::ui::fs::normalize_path(logical_path.c_str());
}

std::string lvgl_real_path_suffix(const std::string& normalized_path)
{
    if (normalized_path.size() < 2 || normalized_path[1] != ':')
    {
        return {};
    }

    std::string suffix = normalized_path.substr(2);
    if (suffix.empty())
    {
        suffix = "/";
    }
    if (suffix.front() != '/')
    {
        suffix.insert(suffix.begin(), '/');
    }
    return suffix;
}

std::string host_path_from_normalized_lvgl_path(const std::string& normalized_path)
{
    if (normalized_path.size() < 2 || normalized_path[1] != ':')
    {
        return {};
    }

    const std::string suffix = lvgl_real_path_suffix(normalized_path);
    if (suffix.empty())
    {
        return {};
    }

    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(normalized_path[0])));
    if (letter == 'F')
    {
        return std::string("/fs") + suffix;
    }

#if defined(TRAIL_MATE_LVGL_SD_FS_LETTER)
    if (letter == TRAIL_MATE_LVGL_SD_FS_LETTER)
    {
        std::string root = TRAIL_MATE_LVGL_SD_FS_PATH;
        if (root.empty() || root == "/")
        {
            return suffix;
        }
        while (root.size() > 1 && root.back() == '/')
        {
            root.pop_back();
        }
        return root + suffix;
    }
#endif

    return {};
}

const char* safe_cstr(const std::string& value)
{
    return value.empty() ? "<none>" : value.c_str();
}

bool binding_is_sd_runtime(const PackStorageBinding& binding)
{
    return binding.backend == PackStorageBackend::SdRuntime;
}

bool binding_is_flash_posix(const PackStorageBinding& binding)
{
    return binding.backend == PackStorageBackend::FlashPosix;
}

bool posix_stat_path(const std::string& path, struct stat& st)
{
    return !path.empty() && ::stat(path.c_str(), &st) == 0;
}

bool posix_file_exists(const std::string& path)
{
    struct stat st
    {
    };
    return posix_stat_path(path, st) && S_ISREG(st.st_mode);
}

bool posix_dir_exists(const std::string& path)
{
    struct stat st
    {
    };
    return posix_stat_path(path, st) && S_ISDIR(st.st_mode);
}

std::string host_path_for_logical_path(const std::string& logical_path, bool allow_format_on_fail)
{
    return host_path_from_normalized_lvgl_path(
        normalize_pack_path(logical_path, allow_format_on_fail));
}

bool remove_file_if_exists(const std::string& logical_path);
void log_lvgl_path_probe(const char* scope, const std::string& logical_path);

bool ensure_dir_recursive(const std::string& logical_dir)
{
    if (logical_dir.empty())
    {
        return false;
    }

    PackStorageBinding binding;
    if (!resolve_storage_binding(logical_dir, true, binding))
    {
        std::printf("[Packs][Storage] mkdir resolve failed logical=%s storage=%s\n",
                    logical_dir.c_str(),
                    storage_for_logical_path(logical_dir));
        return false;
    }

    std::string current;
    std::size_t start = 0;
    while (start < binding.volume_path.size())
    {
        const std::size_t slash = binding.volume_path.find('/', start);
        const std::size_t end = (slash == std::string::npos) ? binding.volume_path.size() : slash;
        const std::string segment = binding.volume_path.substr(start, end - start);
        if (!segment.empty())
        {
            current.push_back('/');
            current += segment;

            bool exists = false;
            bool is_dir = false;
            if (binding_is_sd_runtime(binding))
            {
                exists = ::platform::esp::arduino_common::storage::sd_exists(current.c_str());
                is_dir = exists && ::platform::esp::arduino_common::storage::sd_is_directory(current.c_str());
            }
            else if (binding_is_flash_posix(binding))
            {
                exists = posix_file_exists(current) || posix_dir_exists(current);
                is_dir = exists && posix_dir_exists(current);
            }

            if (!exists)
            {
                const bool mkdir_ok = binding_is_sd_runtime(binding)
                                          ? ::platform::esp::arduino_common::storage::sd_mkdir(current.c_str())
                                          : (::mkdir(current.c_str(), 0775) == 0 || errno == EEXIST);
                if (!mkdir_ok)
                {
                    std::printf("[Packs][Storage] mkdir failed logical=%s storage=%s path=%s\n",
                                logical_dir.c_str(),
                                binding.storage ? binding.storage : "<none>",
                                current.c_str());
                    return false;
                }
            }
            else if (!is_dir)
            {
                std::printf("[Packs][Storage] mkdir blocked by file logical=%s storage=%s path=%s\n",
                            logical_dir.c_str(),
                            binding.storage ? binding.storage : "<none>",
                            current.c_str());
                return false;
            }
        }

        if (slash == std::string::npos)
        {
            break;
        }
        start = slash + 1;
    }
    return true;
}

bool logical_file_exists(const std::string& logical_path)
{
    PackStorageBinding binding;
    if (!resolve_storage_binding(logical_path, false, binding))
    {
        return false;
    }

    if (binding_is_sd_runtime(binding))
    {
        return ::platform::esp::arduino_common::storage::sd_exists(binding.volume_path.c_str()) &&
               !::platform::esp::arduino_common::storage::sd_is_directory(binding.volume_path.c_str());
    }

    return binding_is_flash_posix(binding) && posix_file_exists(binding.volume_path);
}

bool logical_dir_exists(const std::string& logical_path)
{
    PackStorageBinding binding;
    if (!resolve_storage_binding(logical_path, false, binding))
    {
        return false;
    }

    if (binding_is_sd_runtime(binding))
    {
        return ::platform::esp::arduino_common::storage::sd_is_directory(binding.volume_path.c_str());
    }

    return binding_is_flash_posix(binding) && posix_dir_exists(binding.volume_path);
}

void log_lvgl_path_probe(const char* scope, const std::string& logical_path)
{
    const std::string normalized = normalize_pack_path(logical_path, false);
    if (normalized.empty())
    {
        std::printf("[Packs][Probe][LVGL] %s logical=%s normalized=<none> file=0 dir=0 size_ok=0 size=0\n",
                    scope ? scope : "path",
                    logical_path.c_str());
        return;
    }

    bool file_exists = false;
    std::size_t size = 0;
    lv_fs_file_t file;
    if (lv_fs_open(&file, normalized.c_str(), LV_FS_MODE_RD) == LV_FS_RES_OK)
    {
        file_exists = true;
        uint32_t lv_size = 0;
        if (lv_fs_get_size(&file, &lv_size) == LV_FS_RES_OK)
        {
            size = lv_size;
        }
        lv_fs_close(&file);
    }

    lv_fs_dir_t dir;
    const bool dir_exists = lv_fs_dir_open(&dir, normalized.c_str()) == LV_FS_RES_OK;
    if (dir_exists)
    {
        lv_fs_dir_close(&dir);
    }

    std::printf("[Packs][Probe][LVGL] %s logical=%s normalized=%s file=%d dir=%d size_ok=%d size=%lu\n",
                scope ? scope : "path",
                logical_path.c_str(),
                normalized.c_str(),
                file_exists ? 1 : 0,
                dir_exists ? 1 : 0,
                file_exists && size > 0 ? 1 : 0,
                static_cast<unsigned long>(size));
}

bool write_binary_file(const std::string& logical_path, const void* data, std::size_t len)
{
    const std::size_t slash = logical_path.find_last_of('/');
    if (slash != std::string::npos &&
        !ensure_dir_recursive(logical_path.substr(0, slash)))
    {
        return false;
    }

    (void)remove_file_if_exists(logical_path);

    PackStorageBinding binding;
    if (!resolve_storage_binding(logical_path, true, binding))
    {
        std::printf("[Packs][Storage] write resolve failed logical=%s len=%lu storage=%s\n",
                    logical_path.c_str(),
                    static_cast<unsigned long>(len),
                    storage_for_logical_path(logical_path));
        return false;
    }

    if (binding_is_sd_runtime(binding))
    {
        ::platform::esp::arduino_common::storage::SdRuntimeFile file;
        if (!file.open(binding.volume_path.c_str(), "w"))
        {
            std::printf("[Packs][Storage] open for write failed logical=%s storage=%s path=%s len=%lu\n",
                        logical_path.c_str(),
                        binding.storage ? binding.storage : "<none>",
                        binding.volume_path.c_str(),
                        static_cast<unsigned long>(len));
            return false;
        }

        const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
        std::size_t written = 0;
        while (written < len)
        {
            const std::size_t chunk = std::min(kFileWriteChunkBytes, len - written);
            const std::size_t chunk_written = file.write(bytes + written, chunk);
            if (chunk_written != chunk)
            {
                std::printf("[Packs][Storage] write failed logical=%s storage=%s path=%s offset=%lu chunk=%lu wrote=%lu total=%lu\n",
                            logical_path.c_str(),
                            binding.storage ? binding.storage : "<none>",
                            binding.volume_path.c_str(),
                            static_cast<unsigned long>(written),
                            static_cast<unsigned long>(chunk),
                            static_cast<unsigned long>(chunk_written),
                            static_cast<unsigned long>(len));
                file.close();
                return false;
            }
            written += chunk_written;
        }
        file.flush();
        file.close();
        return true;
    }

    FILE* file = binding_is_flash_posix(binding) ? std::fopen(binding.volume_path.c_str(), "wb") : nullptr;
    if (!file)
    {
        std::printf("[Packs][Storage] open for write failed logical=%s storage=%s path=%s len=%lu\n",
                    logical_path.c_str(),
                    binding.storage ? binding.storage : "<none>",
                    binding.volume_path.c_str(),
                    static_cast<unsigned long>(len));
        return false;
    }

    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t written = 0;
    while (written < len)
    {
        const std::size_t chunk = std::min(kFileWriteChunkBytes, len - written);
        const std::size_t chunk_written = std::fwrite(bytes + written, 1, chunk, file);
        if (chunk_written != chunk)
        {
            std::printf("[Packs][Storage] write failed logical=%s storage=%s path=%s offset=%lu chunk=%lu wrote=%lu total=%lu\n",
                        logical_path.c_str(),
                        binding.storage ? binding.storage : "<none>",
                        binding.volume_path.c_str(),
                        static_cast<unsigned long>(written),
                        static_cast<unsigned long>(chunk),
                        static_cast<unsigned long>(chunk_written),
                        static_cast<unsigned long>(len));
            std::fclose(file);
            return false;
        }
        written += chunk_written;
    }
    std::fflush(file);
    std::fclose(file);
    return true;
}

bool write_text_file(const std::string& logical_path, const std::string& text)
{
    return write_binary_file(logical_path, text.data(), text.size());
}

bool logical_file_size(const std::string& logical_path, std::size_t& out_size)
{
    out_size = 0;

    PackStorageBinding binding;
    if (!resolve_storage_binding(logical_path, false, binding))
    {
        return false;
    }

    if (binding_is_sd_runtime(binding))
    {
        if (::platform::esp::arduino_common::storage::sd_is_directory(binding.volume_path.c_str()))
        {
            return false;
        }

        ::platform::esp::arduino_common::storage::SdRuntimeFile file;
        if (!file.open(binding.volume_path.c_str(), "r"))
        {
            return false;
        }
        out_size = static_cast<std::size_t>(file.size());
        file.close();
        return true;
    }

    if (!binding_is_flash_posix(binding))
    {
        return false;
    }
    struct stat st
    {
    };
    if (!posix_stat_path(binding.volume_path, st) || !S_ISREG(st.st_mode))
    {
        return false;
    }
    out_size = static_cast<std::size_t>(st.st_size);
    return true;
}

void log_path_probe(const char* scope, const std::string& logical_path)
{
    const std::string normalized = normalize_pack_path(logical_path, false);
    const std::string host = host_path_from_normalized_lvgl_path(normalized);
    std::size_t size = 0;
    const bool file_exists = logical_file_exists(logical_path);
    const bool dir_exists = logical_dir_exists(logical_path);
    const bool size_ok = file_exists && logical_file_size(logical_path, size);
    std::printf("[Packs][Probe] %s logical=%s normalized=%s host=%s file=%d dir=%d size_ok=%d size=%lu storage=%s\n",
                scope ? scope : "path",
                logical_path.c_str(),
                safe_cstr(normalized),
                safe_cstr(host),
                file_exists ? 1 : 0,
                dir_exists ? 1 : 0,
                size_ok ? 1 : 0,
                static_cast<unsigned long>(size),
                storage_for_logical_path(logical_path));
}

bool remove_file_if_exists(const std::string& logical_path)
{
    PackStorageBinding binding;
    if (!resolve_storage_binding(logical_path, false, binding))
    {
        return false;
    }

    bool exists = false;
    bool is_dir = false;
    if (binding_is_sd_runtime(binding))
    {
        exists = ::platform::esp::arduino_common::storage::sd_exists(binding.volume_path.c_str());
        is_dir = exists && ::platform::esp::arduino_common::storage::sd_is_directory(binding.volume_path.c_str());
    }
    else if (binding_is_flash_posix(binding))
    {
        exists = posix_file_exists(binding.volume_path) || posix_dir_exists(binding.volume_path);
        is_dir = exists && posix_dir_exists(binding.volume_path);
    }
    if (!exists)
    {
        return true;
    }
    if (is_dir)
    {
        return false;
    }
    const bool remove_ok = binding_is_sd_runtime(binding)
                               ? ::platform::esp::arduino_common::storage::sd_remove(binding.volume_path.c_str())
                               : (std::remove(binding.volume_path.c_str()) == 0);
    if (!remove_ok)
    {
        std::printf("[Packs][Storage] remove file failed logical=%s storage=%s path=%s\n",
                    logical_path.c_str(),
                    binding.storage ? binding.storage : "<none>",
                    binding.volume_path.c_str());
        return false;
    }
    return true;
}

bool remove_dir_recursive_if_exists(const std::string& logical_path)
{
    if (logical_path.empty())
    {
        return true;
    }

    PackStorageBinding binding;
    if (!resolve_storage_binding(logical_path, false, binding))
    {
        return false;
    }

    if (binding_is_sd_runtime(binding))
    {
        if (!::platform::esp::arduino_common::storage::sd_exists(binding.volume_path.c_str()))
        {
            return true;
        }
        if (!::platform::esp::arduino_common::storage::sd_is_directory(binding.volume_path.c_str()))
        {
            return ::platform::esp::arduino_common::storage::sd_remove(binding.volume_path.c_str());
        }

        ::platform::esp::arduino_common::storage::SdRuntimeDir dir;
        if (!dir.open(binding.volume_path.c_str()))
        {
            return false;
        }

        char name_buf[128];
        bool child_is_dir = false;
        while (dir.read_next(name_buf, sizeof(name_buf), &child_is_dir))
        {
            const std::string name = file_entry_name(name_buf);
            if (!name.empty())
            {
                const std::string child_logical = join_logical_path(logical_path, name);
                const bool ok = child_is_dir ? remove_dir_recursive_if_exists(child_logical)
                                             : remove_file_if_exists(child_logical);
                if (!ok)
                {
                    dir.close();
                    return false;
                }
            }
        }
        dir.close();
        if (!::platform::esp::arduino_common::storage::sd_rmdir(binding.volume_path.c_str()))
        {
            std::printf("[Packs][Storage] rmdir failed logical=%s storage=%s path=%s\n",
                        logical_path.c_str(),
                        binding.storage ? binding.storage : "<none>",
                        binding.volume_path.c_str());
            return false;
        }
        return true;
    }

    if (!binding_is_flash_posix(binding))
    {
        return false;
    }

    if (!posix_file_exists(binding.volume_path) && !posix_dir_exists(binding.volume_path))
    {
        return true;
    }

    if (posix_file_exists(binding.volume_path))
    {
        return std::remove(binding.volume_path.c_str()) == 0;
    }

    DIR* dir = ::opendir(binding.volume_path.c_str());
    if (dir == nullptr)
    {
        return false;
    }

    bool ok = true;
    while (ok)
    {
        struct dirent* entry = ::readdir(dir);
        if (entry == nullptr)
        {
            break;
        }
        const std::string name = file_entry_name(entry->d_name);
        if (name == "." || name == "..")
        {
            continue;
        }
        if (!name.empty())
        {
            const std::string child_logical = join_logical_path(logical_path, name);
            ok = remove_dir_recursive_if_exists(child_logical);
        }
    }

    ::closedir(dir);
    if (!ok || ::rmdir(binding.volume_path.c_str()) != 0)
    {
        std::printf("[Packs][Storage] rmdir failed logical=%s storage=%s path=%s\n",
                    logical_path.c_str(),
                    binding.storage ? binding.storage : "<none>",
                    binding.volume_path.c_str());
        return false;
    }
    return true;
}

class RandomAccessFile
{
  public:
    bool open(const std::string& logical_path)
    {
        close();

        PackStorageBinding binding;
        if (!resolve_storage_binding(logical_path, false, binding))
        {
            return false;
        }

        backend_ = binding.backend;
        if (backend_ == PackStorageBackend::SdRuntime)
        {
            if (::platform::esp::arduino_common::storage::sd_is_directory(binding.volume_path.c_str()) ||
                !sd_file_.open(binding.volume_path.c_str(), "r"))
            {
                backend_ = PackStorageBackend::None;
                return false;
            }
            size_ = static_cast<std::size_t>(sd_file_.size());
            return true;
        }

        if (backend_ != PackStorageBackend::FlashPosix || !posix_file_exists(binding.volume_path))
        {
            backend_ = PackStorageBackend::None;
            return false;
        }
        file_ = std::fopen(binding.volume_path.c_str(), "rb");
        if (file_ == nullptr)
        {
            backend_ = PackStorageBackend::None;
            return false;
        }
        std::fseek(file_, 0, SEEK_END);
        const long file_size = std::ftell(file_);
        if (file_size < 0)
        {
            close();
            return false;
        }
        std::rewind(file_);
        size_ = static_cast<std::size_t>(file_size);
        return true;
    }

    void close()
    {
        if (backend_ == PackStorageBackend::SdRuntime)
        {
            sd_file_.close();
            backend_ = PackStorageBackend::None;
            size_ = 0;
            return;
        }
        if (file_ != nullptr)
        {
            std::fclose(file_);
            file_ = nullptr;
            size_ = 0;
        }
        backend_ = PackStorageBackend::None;
    }

    std::size_t size() const
    {
        return size_;
    }

    bool read_at(std::size_t offset, void* out, std::size_t len)
    {
        if (backend_ == PackStorageBackend::SdRuntime)
        {
            if (!sd_file_.seek(offset))
            {
                return false;
            }
            const int read = sd_file_.read(out, len);
            return read >= 0 && static_cast<std::size_t>(read) == len;
        }
        if (file_ == nullptr || std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0)
        {
            return false;
        }
        return std::fread(out, 1, len, file_) == len;
    }

  private:
    FILE* file_ = nullptr;
    ::platform::esp::arduino_common::storage::SdRuntimeFile sd_file_{};
    PackStorageBackend backend_ = PackStorageBackend::None;
    std::size_t size_ = 0;
};

class SequentialWriteFile
{
  public:
    bool open(const std::string& logical_path)
    {
        close();

        const std::size_t slash = logical_path.find_last_of('/');
        if (slash != std::string::npos &&
            !ensure_dir_recursive(logical_path.substr(0, slash)))
        {
            return false;
        }

        (void)remove_file_if_exists(logical_path);

        PackStorageBinding binding;
        if (!resolve_storage_binding(logical_path, true, binding))
        {
            return false;
        }

        backend_ = binding.backend;
        if (backend_ == PackStorageBackend::SdRuntime)
        {
            if (!sd_file_.open(binding.volume_path.c_str(), "w"))
            {
                backend_ = PackStorageBackend::None;
                return false;
            }
            return true;
        }

        if (backend_ != PackStorageBackend::FlashPosix)
        {
            backend_ = PackStorageBackend::None;
            return false;
        }
        file_ = std::fopen(binding.volume_path.c_str(), "wb");
        if (file_ == nullptr)
        {
            backend_ = PackStorageBackend::None;
            return false;
        }
        return true;
    }

    bool write(const void* data, std::size_t len)
    {
        if (backend_ == PackStorageBackend::SdRuntime)
        {
            return sd_file_.write(data, len) == len;
        }
        if (file_ == nullptr)
        {
            return false;
        }
        return std::fwrite(data, 1, len, file_) == len;
    }

    void close()
    {
        if (backend_ == PackStorageBackend::SdRuntime)
        {
            sd_file_.flush();
            sd_file_.close();
            backend_ = PackStorageBackend::None;
            return;
        }
        if (file_ != nullptr)
        {
            std::fflush(file_);
            std::fclose(file_);
            file_ = nullptr;
        }
        backend_ = PackStorageBackend::None;
    }

  private:
    FILE* file_ = nullptr;
    ::platform::esp::arduino_common::storage::SdRuntimeFile sd_file_{};
    PackStorageBackend backend_ = PackStorageBackend::None;
};

#else

std::string host_path(const std::string& logical_path)
{
    if (!platform::esp::idf_common::flash_storage_runtime::ensure_ready(true))
    {
        return {};
    }
    if (logical_path == "/fs" || starts_with(logical_path, "/fs/"))
    {
        return logical_path;
    }
    return std::string(platform::esp::idf_common::flash_storage_runtime::mount_point()) +
           logical_path;
}

bool ensure_dir_recursive(const std::string& logical_dir)
{
    if (logical_dir.empty())
    {
        return false;
    }

    std::string host_dir = host_path(logical_dir);
    const std::string mount_point = platform::esp::idf_common::flash_storage_runtime::mount_point();
    if (host_dir.empty() || mount_point.empty() || !starts_with(host_dir, mount_point.c_str()) ||
        (host_dir.size() > mount_point.size() && host_dir[mount_point.size()] != '/'))
    {
        std::printf("[Packs][Storage] mkdir invalid flash path logical=%s host=%s mount=%s\n",
                    logical_dir.c_str(),
                    host_dir.c_str(),
                    mount_point.c_str());
        return false;
    }

    std::string current = mount_point;
    std::size_t segment_start = mount_point.size();
    while (segment_start < host_dir.size())
    {
        while (segment_start < host_dir.size() && host_dir[segment_start] == '/')
        {
            ++segment_start;
        }
        if (segment_start == host_dir.size())
        {
            break;
        }

        const std::size_t segment_end = host_dir.find('/', segment_start);
        current.push_back('/');
        current.append(host_dir,
                       segment_start,
                       segment_end == std::string::npos ? std::string::npos
                                                        : segment_end - segment_start);

        errno = 0;
        if (::mkdir(current.c_str(), 0775) != 0 && errno != EEXIST)
        {
            const int error = errno;
            std::printf("[Packs][Storage] mkdir failed logical=%s host=%s path=%s errno=%d (%s)\n",
                        logical_dir.c_str(),
                        host_dir.c_str(),
                        current.c_str(),
                        error,
                        std::strerror(error));
            return false;
        }

        struct stat st
        {
        };
        if (::stat(current.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        {
            const int error = errno;
            std::printf("[Packs][Storage] mkdir path is not a directory logical=%s host=%s path=%s errno=%d (%s)\n",
                        logical_dir.c_str(),
                        host_dir.c_str(),
                        current.c_str(),
                        error,
                        std::strerror(error));
            return false;
        }

        if (segment_end == std::string::npos)
        {
            break;
        }
        segment_start = segment_end + 1;
    }
    return true;
}

bool logical_file_exists(const std::string& logical_path)
{
    struct stat st
    {
    };
    return ::stat(host_path(logical_path).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool write_binary_file(const std::string& logical_path, const void* data, std::size_t len)
{
    const std::size_t slash = logical_path.find_last_of('/');
    if (slash != std::string::npos)
    {
        if (!ensure_dir_recursive(logical_path.substr(0, slash)))
        {
            return false;
        }
    }

    FILE* file = std::fopen(host_path(logical_path).c_str(), "wb");
    if (!file)
    {
        return false;
    }

    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t written = 0;
    while (written < len)
    {
        const std::size_t chunk = std::min(kFileWriteChunkBytes, len - written);
        const std::size_t chunk_written = std::fwrite(bytes + written, 1, chunk, file);
        if (chunk_written != chunk)
        {
            std::fclose(file);
            return false;
        }
        written += chunk_written;
    }

    std::fclose(file);
    return true;
}

bool write_text_file(const std::string& logical_path, const std::string& text)
{
    return write_binary_file(logical_path, text.data(), text.size());
}

bool logical_dir_exists(const std::string& logical_path)
{
    const std::string dir = host_path(logical_path);
    struct stat st
    {
    };
    return ::stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool logical_file_size(const std::string& logical_path, std::size_t& out_size)
{
    out_size = 0;
    const std::string path = host_path(logical_path);
    struct stat st
    {
    };
    if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    {
        return false;
    }
    out_size = static_cast<std::size_t>(st.st_size);
    return true;
}

void log_path_probe(const char* scope, const std::string& logical_path)
{
    std::size_t size = 0;
    const bool file_exists = logical_file_exists(logical_path);
    const bool dir_exists = logical_dir_exists(logical_path);
    const bool size_ok = file_exists && logical_file_size(logical_path, size);
    std::printf("[Packs][Probe] %s logical=%s host=%s file=%d dir=%d size_ok=%d size=%lu storage=%s\n",
                scope ? scope : "path",
                logical_path.c_str(),
                host_path(logical_path).c_str(),
                file_exists ? 1 : 0,
                dir_exists ? 1 : 0,
                size_ok ? 1 : 0,
                static_cast<unsigned long>(size),
                kStorageFlash);
}

void log_lvgl_path_probe(const char* scope, const std::string& logical_path)
{
    std::printf("[Packs][Probe][LVGL] %s logical=%s mapped=0 reason=idf_posix_mount\n",
                scope ? scope : "path",
                logical_path.c_str());
}

bool remove_file_if_exists(const std::string& logical_path)
{
    if (!logical_file_exists(logical_path))
    {
        return true;
    }
    return std::remove(host_path(logical_path).c_str()) == 0;
}

bool remove_dir_recursive_if_exists(const std::string& logical_path)
{
    if (logical_path.empty())
    {
        return true;
    }

    const std::string host_dir = host_path(logical_path);
    struct stat st
    {
    };
    if (::stat(host_dir.c_str(), &st) != 0)
    {
        return errno == ENOENT;
    }

    if (!S_ISDIR(st.st_mode))
    {
        return std::remove(host_dir.c_str()) == 0;
    }

    DIR* dir = ::opendir(host_dir.c_str());
    if (!dir)
    {
        return false;
    }

    bool ok = true;
    while (ok)
    {
        struct dirent* entry = ::readdir(dir);
        if (!entry)
        {
            break;
        }

        const char* name = entry->d_name;
        if (!name || std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0)
        {
            continue;
        }

        const std::string child_logical = join_logical_path(logical_path, name);
        const std::string child_host = host_path(child_logical);

        struct stat child_st
        {
        };
        if (::stat(child_host.c_str(), &child_st) != 0)
        {
            ok = false;
            break;
        }

        if (S_ISDIR(child_st.st_mode))
        {
            ok = remove_dir_recursive_if_exists(child_logical);
        }
        else
        {
            ok = std::remove(child_host.c_str()) == 0;
        }
    }

    ::closedir(dir);
    return ok && ::rmdir(host_dir.c_str()) == 0;
}

class RandomAccessFile
{
  public:
    bool open(const std::string& logical_path)
    {
        file_ = std::fopen(host_path(logical_path).c_str(), "rb");
        if (!file_)
        {
            return false;
        }
        if (std::fseek(file_, 0, SEEK_END) != 0)
        {
            std::fclose(file_);
            file_ = nullptr;
            return false;
        }
        const long len = std::ftell(file_);
        if (len < 0)
        {
            std::fclose(file_);
            file_ = nullptr;
            return false;
        }
        size_ = static_cast<std::size_t>(len);
        return std::fseek(file_, 0, SEEK_SET) == 0;
    }

    void close()
    {
        if (file_)
        {
            std::fclose(file_);
            file_ = nullptr;
            size_ = 0;
        }
    }

    std::size_t size() const
    {
        return size_;
    }

    bool read_at(std::size_t offset, void* out, std::size_t len)
    {
        if (!file_ || std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0)
        {
            return false;
        }
        return std::fread(out, 1, len, file_) == len;
    }

  private:
    FILE* file_ = nullptr;
    std::size_t size_ = 0;
};

class SequentialWriteFile
{
  public:
    bool open(const std::string& logical_path)
    {
        const std::size_t slash = logical_path.find_last_of('/');
        if (slash != std::string::npos)
        {
            if (!ensure_dir_recursive(logical_path.substr(0, slash)))
            {
                return false;
            }
        }
        file_ = std::fopen(host_path(logical_path).c_str(), "wb");
        return file_ != nullptr;
    }

    bool write(const void* data, std::size_t len)
    {
        return file_ && std::fwrite(data, 1, len, file_) == len;
    }

    void close()
    {
        if (file_)
        {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

  private:
    FILE* file_ = nullptr;
};

#endif

template <typename ChunkHandler>
bool read_file_chunks(const std::string& logical_path, ChunkHandler on_chunk)
{
    RandomAccessFile file;
    if (!file.open(logical_path))
    {
        return false;
    }

    ScopedPackScratch buffer(kFileReadChunkBytes);
    if (!buffer)
    {
        file.close();
        return false;
    }

    std::size_t offset = 0;
    const std::size_t size = file.size();
    bool ok = true;
    while (offset < size)
    {
        const std::size_t chunk = std::min<std::size_t>(buffer.size(), size - offset);
        if (!file.read_at(offset, buffer.bytes(), chunk) || !on_chunk(buffer.bytes(), chunk))
        {
            ok = false;
            break;
        }
        offset += chunk;
    }
    file.close();
    return ok;
}

std::uint16_t read_le16(const std::uint8_t* ptr)
{
    return static_cast<std::uint16_t>(ptr[0]) |
           (static_cast<std::uint16_t>(ptr[1]) << 8);
}

std::uint32_t read_le32(const std::uint8_t* ptr)
{
    return static_cast<std::uint32_t>(ptr[0]) |
           (static_cast<std::uint32_t>(ptr[1]) << 8) |
           (static_cast<std::uint32_t>(ptr[2]) << 16) |
           (static_cast<std::uint32_t>(ptr[3]) << 24);
}

bool fetch_catalog_text(const std::string& url, std::string& out, std::string& out_error)
{
    out.clear();
    out_error.clear();

    platform::ui::http_client::Request request{};
    request.url = url.c_str();
    request.client = platform::ui::wifi_access::Client::PackRepository;
    request.access_kind = platform::ui::wifi_access::AccessKind::HttpMetadata;
    request.priority = platform::ui::wifi_access::Priority::UserForeground;
    request.reason = "pack_catalog";
    request.buffer_size = kHttpBufferSize;
    request.tx_buffer_size = kHttpTxBufferSize;
    return platform::ui::http_client::get_text(request, out, out_error);
}

struct PackageDownloadContext
{
    SequentialWriteFile* file = nullptr;
    const PackageRecord* package = nullptr;
    std::size_t bytes_written = 0;
    int last_progress = -1;
};

bool download_package_archive(const PackageRecord& package,
                              const std::string& logical_path,
                              std::string& out_error)
{
    out_error.clear();

    const std::size_t slash = logical_path.find_last_of('/');
    if (slash != std::string::npos)
    {
        if (!ensure_dir_recursive(logical_path.substr(0, slash)))
        {
            out_error = "Create temp directory failed";
            return false;
        }
    }

    SequentialWriteFile file;
    if (!file.open(logical_path))
    {
        out_error = "Open destination file failed";
        return false;
    }

    const bool has_total = package.archive_size_bytes > 0;
    set_install_status(PackageInstallPhase::Installing,
                       true,
                       package.id,
                       "Downloading package...",
                       has_total ? "0%" : package.id.c_str(),
                       has_total ? 0 : -1);

    platform::ui::http_client::Request request{};
    request.url = package.download_url.c_str();
    request.client = platform::ui::wifi_access::Client::PackRepository;
    request.access_kind = platform::ui::wifi_access::AccessKind::HttpDownload;
    request.priority = platform::ui::wifi_access::Priority::UserForeground;
    request.reason = "pack_install";
    request.buffer_size = kHttpBufferSize;
    request.tx_buffer_size = kHttpTxBufferSize;

    PackageDownloadContext context{};
    context.file = &file;
    context.package = &package;

    bool ok = platform::ui::http_client::download(
        request,
        [](const std::uint8_t* data, std::size_t len, void* context)
        {
            auto* state = static_cast<PackageDownloadContext*>(context);
            if (!state || !state->file || !state->file->write(data, len))
            {
                return false;
            }

            state->bytes_written += len;
            const std::size_t total = state->package ? state->package->archive_size_bytes : 0;
            if (state->package && total > 0)
            {
                int progress = static_cast<int>(
                    (static_cast<std::uint64_t>(state->bytes_written) * 100U) /
                    total);
                if (progress > 100)
                {
                    progress = 100;
                }
                if (progress != state->last_progress)
                {
                    state->last_progress = progress;
                    char detail[32];
                    std::snprintf(detail, sizeof(detail), "%d%%", progress);
                    set_install_status(PackageInstallPhase::Installing,
                                       true,
                                       state->package->id,
                                       "Downloading package...",
                                       detail,
                                       progress);
                }
            }
            return true;
        },
        &context,
        out_error);
    file.close();

    if (!ok)
    {
        (void)remove_file_if_exists(logical_path);
    }
    return ok;
}

std::string sha256_hex_from_digest(const unsigned char hash[32])
{
    char hex[65];
    for (int i = 0; i < 32; ++i)
    {
        std::snprintf(hex + (i * 2), 3, "%02x", hash[i]);
    }
    hex[64] = '\0';
    return hex;
}

bool sha256_file(const std::string& logical_path, std::string& out_hex)
{
    out_hex.clear();

    unsigned char hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    const bool ok = read_file_chunks(logical_path, [&ctx](const std::uint8_t* data, std::size_t len)
                                     {
        mbedtls_sha256_update(&ctx, data, len);
        return true; });
    if (!ok)
    {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    out_hex = sha256_hex_from_digest(hash);
    return true;
}

enum class JsonTokenKind : std::uint8_t
{
    End,
    ObjectStart,
    ObjectEnd,
    ArrayStart,
    ArrayEnd,
    Colon,
    Comma,
    String,
    Number,
    Literal,
};

struct JsonToken
{
    JsonTokenKind kind = JsonTokenKind::End;
    std::string text;
};

class JsonTokenStream
{
  public:
    bool open(const std::string& logical_path)
    {
        close();
        return file_.open(logical_path);
    }

    void close()
    {
        file_.close();
        offset_ = 0;
        buffered_ = 0;
        buffer_pos_ = 0;
        has_pushback_ = false;
    }

    bool next(JsonToken& out, std::string& out_error)
    {
        out = JsonToken{};

        char ch = '\0';
        do
        {
            if (!read_char(ch))
            {
                out.kind = JsonTokenKind::End;
                return true;
            }
        } while (std::isspace(static_cast<unsigned char>(ch)) != 0);

        switch (ch)
        {
        case '{':
            out.kind = JsonTokenKind::ObjectStart;
            return true;
        case '}':
            out.kind = JsonTokenKind::ObjectEnd;
            return true;
        case '[':
            out.kind = JsonTokenKind::ArrayStart;
            return true;
        case ']':
            out.kind = JsonTokenKind::ArrayEnd;
            return true;
        case ':':
            out.kind = JsonTokenKind::Colon;
            return true;
        case ',':
            out.kind = JsonTokenKind::Comma;
            return true;
        case '"':
            out.kind = JsonTokenKind::String;
            return read_string(out.text, out_error);
        default:
            break;
        }

        if ((ch >= '0' && ch <= '9') || ch == '-')
        {
            out.kind = JsonTokenKind::Number;
            out.text.push_back(ch);
            while (read_char(ch))
            {
                if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' ||
                    ch == '.' || ch == 'e' || ch == 'E')
                {
                    if (!append_token_char(out.text, ch, out_error))
                    {
                        return false;
                    }
                    continue;
                }
                push_back(ch);
                break;
            }
            return true;
        }

        if (std::isalpha(static_cast<unsigned char>(ch)) != 0)
        {
            out.kind = JsonTokenKind::Literal;
            out.text.push_back(ch);
            while (read_char(ch))
            {
                if (std::isalpha(static_cast<unsigned char>(ch)) != 0)
                {
                    if (!append_token_char(out.text, ch, out_error))
                    {
                        return false;
                    }
                    continue;
                }
                push_back(ch);
                break;
            }
            return true;
        }

        out_error = "Installed index JSON token invalid";
        return false;
    }

  private:
    bool append_token_char(std::string& text, char ch, std::string& out_error)
    {
        if (text.size() >= kMaxInstalledIndexTokenBytes)
        {
            out_error = "Installed index JSON token too long";
            return false;
        }
        text.push_back(ch);
        return true;
    }

    bool read_string(std::string& out, std::string& out_error)
    {
        out.clear();
        char ch = '\0';
        while (read_char(ch))
        {
            if (ch == '"')
            {
                return true;
            }
            if (ch == '\\')
            {
                if (!read_char(ch))
                {
                    out_error = "Installed index JSON escape truncated";
                    return false;
                }
                switch (ch)
                {
                case 'n':
                    ch = '\n';
                    break;
                case 'r':
                    ch = '\r';
                    break;
                case 't':
                    ch = '\t';
                    break;
                case 'b':
                    ch = '\b';
                    break;
                case 'f':
                    ch = '\f';
                    break;
                case 'u':
                    for (int index = 0; index < 4; ++index)
                    {
                        if (!read_char(ch) ||
                            std::isxdigit(static_cast<unsigned char>(ch)) == 0)
                        {
                            out_error = "Installed index JSON unicode escape invalid";
                            return false;
                        }
                    }
                    ch = '?';
                    break;
                default:
                    break;
                }
            }
            if (!append_token_char(out, ch, out_error))
            {
                return false;
            }
        }
        out_error = "Installed index JSON string truncated";
        return false;
    }

    bool read_char(char& out)
    {
        if (has_pushback_)
        {
            out = pushback_;
            has_pushback_ = false;
            return true;
        }
        if (buffer_pos_ >= buffered_)
        {
            if (offset_ >= file_.size())
            {
                return false;
            }
            buffered_ = std::min<std::size_t>(sizeof(buffer_), file_.size() - offset_);
            if (!file_.read_at(offset_, buffer_, buffered_))
            {
                buffered_ = 0;
                return false;
            }
            offset_ += buffered_;
            buffer_pos_ = 0;
        }
        out = static_cast<char>(buffer_[buffer_pos_++]);
        return true;
    }

    void push_back(char ch)
    {
        pushback_ = ch;
        has_pushback_ = true;
    }

    RandomAccessFile file_;
    std::uint8_t buffer_[256]{};
    std::size_t offset_ = 0;
    std::size_t buffered_ = 0;
    std::size_t buffer_pos_ = 0;
    bool has_pushback_ = false;
    char pushback_ = '\0';
};

bool skip_json_value(JsonTokenStream& stream,
                     const JsonToken& first,
                     std::string& out_error)
{
    if (first.kind != JsonTokenKind::ObjectStart && first.kind != JsonTokenKind::ArrayStart)
    {
        return true;
    }

    int depth = 1;
    JsonToken token;
    while (depth > 0)
    {
        if (!stream.next(token, out_error))
        {
            return false;
        }
        if (token.kind == JsonTokenKind::End)
        {
            out_error = "Installed index JSON value truncated";
            return false;
        }
        if (token.kind == JsonTokenKind::ObjectStart || token.kind == JsonTokenKind::ArrayStart)
        {
            ++depth;
        }
        else if (token.kind == JsonTokenKind::ObjectEnd ||
                 token.kind == JsonTokenKind::ArrayEnd)
        {
            --depth;
        }
    }
    return true;
}

bool expect_json_token(JsonTokenStream& stream,
                       JsonTokenKind kind,
                       std::string& out_error)
{
    JsonToken token;
    if (!stream.next(token, out_error))
    {
        return false;
    }
    if (token.kind != kind)
    {
        out_error = "Installed index JSON shape invalid";
        return false;
    }
    return true;
}

bool parse_installed_package_object(JsonTokenStream& stream,
                                    InstalledPackageRecord& record,
                                    std::string& out_error)
{
    JsonToken token;
    while (true)
    {
        if (!stream.next(token, out_error))
        {
            return false;
        }
        if (token.kind == JsonTokenKind::ObjectEnd)
        {
            return true;
        }
        if (token.kind == JsonTokenKind::Comma)
        {
            continue;
        }
        if (token.kind != JsonTokenKind::String)
        {
            out_error = "Installed index package key invalid";
            return false;
        }

        const std::string key = token.text;
        if (!expect_json_token(stream, JsonTokenKind::Colon, out_error) ||
            !stream.next(token, out_error))
        {
            return false;
        }

        if (key == "id" && token.kind == JsonTokenKind::String)
        {
            record.id = token.text;
        }
        else if (key == "version" && token.kind == JsonTokenKind::String)
        {
            record.version = token.text;
        }
        else if (key == "archive_sha256" && token.kind == JsonTokenKind::String)
        {
            record.archive_sha256 = token.text;
        }
        else if (key == "storage" && token.kind == JsonTokenKind::String)
        {
            record.storage = token.text;
        }
        else if (key == "installed_at_epoch" && token.kind == JsonTokenKind::Number)
        {
            record.installed_at_epoch = std::strtoull(token.text.c_str(), nullptr, 10);
        }
        else if (!skip_json_value(stream, token, out_error))
        {
            return false;
        }
    }
}

bool parse_installed_packages_array(JsonTokenStream& stream,
                                    const char* default_storage,
                                    InstalledIndex& out_index,
                                    std::string& out_error)
{
    JsonToken token;
    while (true)
    {
        if (!stream.next(token, out_error))
        {
            return false;
        }
        if (token.kind == JsonTokenKind::ArrayEnd)
        {
            return true;
        }
        if (token.kind == JsonTokenKind::Comma)
        {
            continue;
        }
        if (token.kind != JsonTokenKind::ObjectStart)
        {
            if (!skip_json_value(stream, token, out_error))
            {
                return false;
            }
            continue;
        }

        InstalledPackageRecord record{};
        if (!parse_installed_package_object(stream, record, out_error))
        {
            return false;
        }
        if (!record.id.empty())
        {
            if (record.storage.empty())
            {
                record.storage = default_storage ? default_storage : kStorageSd;
            }
            out_index.packages.push_back(std::move(record));
        }
    }
}

cJSON* parse_json_document(const std::string& text)
{
    return cJSON_ParseWithLength(text.c_str(), text.size());
}

std::string json_string(cJSON* object, const char* key)
{
    if (!object || !key)
    {
        return {};
    }
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

std::size_t json_size_t(cJSON* object, const char* key)
{
    if (!object || !key)
    {
        return 0;
    }
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item))
    {
        return 0;
    }
    return static_cast<std::size_t>(item->valuedouble);
}

std::uint64_t json_u64(cJSON* object, const char* key)
{
    if (!object || !key)
    {
        return 0;
    }
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item))
    {
        return 0;
    }
    return static_cast<std::uint64_t>(item->valuedouble);
}

std::vector<std::string> json_string_array(cJSON* object, const char* key)
{
    std::vector<std::string> out;
    if (!object || !key)
    {
        return out;
    }

    cJSON* array = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsArray(array))
    {
        return out;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, array)
    {
        if (cJSON_IsString(item) && item->valuestring)
        {
            out.emplace_back(item->valuestring);
        }
    }
    return out;
}

void collect_provided_ids(cJSON* provides,
                          const char* group_key,
                          const char* id_key,
                          std::vector<std::string>& out)
{
    out.clear();
    if (!provides || !group_key || !id_key)
    {
        return;
    }

    cJSON* array = cJSON_GetObjectItemCaseSensitive(provides, group_key);
    if (!cJSON_IsArray(array))
    {
        return;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, array)
    {
        const std::string id = json_string(item, id_key);
        if (!id.empty())
        {
            out.push_back(id);
        }
    }
}

bool compatible_with_profile(const PackageRecord& package)
{
    if (package.supported_memory_profiles.empty())
    {
        return true;
    }
    const std::string current = lowercase_ascii(active_memory_profile_name());
    for (const std::string& profile : package.supported_memory_profiles)
    {
        if (lowercase_ascii(profile) == current)
        {
            return true;
        }
    }
    return false;
}

bool compatible_with_firmware(const PackageRecord& package)
{
    if (package.min_firmware_version.empty())
    {
        return true;
    }

    const char* current_version = platform::ui::device::firmware_version();
    const ParsedVersion current = parse_version(current_version ? current_version : "");
    const ParsedVersion required = parse_version(package.min_firmware_version);

    if (!required.valid)
    {
        return true;
    }
    if (!current.valid)
    {
        std::printf("[Packs] firmware gate skipped current=%s required=%s\n",
                    current_version ? current_version : "<null>",
                    package.min_firmware_version.c_str());
        return true;
    }

    return compare_versions(current_version ? current_version : "",
                            package.min_firmware_version) >= 0;
}

const char* default_storage_for_index_path(const char* index_path)
{
    if (!index_path)
    {
        return kStorageFlash;
    }

#if defined(ARDUINO)
    return starts_with(index_path, "/fs/") ? kStorageFlash : kStorageSd;
#else
    return kStorageSd;
#endif
}

bool load_installed_index_file(const char* index_path,
                               const char* default_storage,
                               InstalledIndex& out_index,
                               std::string& out_error)
{
    out_error.clear();
    if (!index_path || !logical_file_exists(index_path))
    {
        if (index_path)
        {
            std::printf("[Packs][Index] not found path=%s default_storage=%s\n",
                        index_path,
                        default_storage ? default_storage : "<none>");
        }
        return true;
    }

    const char* effective_default_storage =
        default_storage ? default_storage : default_storage_for_index_path(index_path);

    JsonTokenStream stream;
    if (!stream.open(index_path))
    {
        out_error = "Read installed index failed";
        return false;
    }

    JsonToken token;
    if (!stream.next(token, out_error) || token.kind != JsonTokenKind::ObjectStart)
    {
        stream.close();
        out_error = "Parse installed index failed";
        return false;
    }

    bool ok = true;
    while (ok)
    {
        if (!stream.next(token, out_error))
        {
            ok = false;
            break;
        }
        if (token.kind == JsonTokenKind::ObjectEnd)
        {
            break;
        }
        if (token.kind == JsonTokenKind::Comma)
        {
            continue;
        }
        if (token.kind != JsonTokenKind::String)
        {
            out_error = "Parse installed index failed";
            ok = false;
            break;
        }

        const std::string key = token.text;
        if (!expect_json_token(stream, JsonTokenKind::Colon, out_error) ||
            !stream.next(token, out_error))
        {
            ok = false;
            break;
        }

        if (key == "packages")
        {
            if (token.kind != JsonTokenKind::ArrayStart)
            {
                out_error = "Parse installed index failed";
                ok = false;
                break;
            }
            ok = parse_installed_packages_array(stream,
                                                effective_default_storage,
                                                out_index,
                                                out_error);
        }
        else
        {
            ok = skip_json_value(stream, token, out_error);
        }
    }

    stream.close();
    if (!ok)
    {
        if (out_error.empty())
        {
            out_error = "Parse installed index failed";
        }
        return false;
    }

    for (const InstalledPackageRecord& record : out_index.packages)
    {
        std::printf("[Packs][Index] record path=%s id=%s version=%s storage=%s sha=%s\n",
                    index_path,
                    record.id.c_str(),
                    record.version.c_str(),
                    record.storage.c_str(),
                    record.archive_sha256.empty() ? "<none>" : record.archive_sha256.c_str());
    }
    std::printf("[Packs][Index] loaded path=%s records=%lu\n",
                index_path,
                static_cast<unsigned long>(out_index.packages.size()));
    return true;
}

bool load_installed_index(InstalledIndex& out_index, std::string& out_error)
{
    out_index = InstalledIndex{};
    out_error.clear();

    log_path_probe("index.flash", kInstalledIndexPath);

    if (logical_file_exists(kInstalledIndexPath))
    {
        return load_installed_index_file(
            kInstalledIndexPath, default_storage_for_index_path(kInstalledIndexPath), out_index, out_error);
    }

#if defined(ARDUINO)
    log_path_probe("index.sd", kSdInstalledIndexPath);
    if (logical_file_exists(kSdInstalledIndexPath))
    {
        return load_installed_index_file(kSdInstalledIndexPath, kStorageSd, out_index, out_error);
    }
    log_path_probe("index.sd_legacy", kLegacyInstalledIndexPath);
    if (logical_file_exists(kLegacyInstalledIndexPath))
    {
        return load_installed_index_file(kLegacyInstalledIndexPath, kStorageSd, out_index, out_error);
    }
#elif defined(ESP_PLATFORM)
    if (logical_file_exists(kLegacyInstalledIndexPath))
    {
        return load_installed_index_file(
            kLegacyInstalledIndexPath, default_storage_for_index_path(kLegacyInstalledIndexPath), out_index, out_error);
    }
#endif

    return true;
}

bool save_installed_index(const InstalledIndex& index, std::string& out_error)
{
    out_error.clear();
    if (!ensure_dir_recursive(kIndexDir))
    {
        out_error = "Create index directory failed";
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema_version", 1);
    cJSON* packages = cJSON_AddArrayToObject(root, "packages");
    for (const InstalledPackageRecord& record : index.packages)
    {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", record.id.c_str());
        cJSON_AddStringToObject(item, "version", record.version.c_str());
        cJSON_AddStringToObject(item, "archive_sha256", record.archive_sha256.c_str());
        cJSON_AddStringToObject(item,
                                "storage",
                                record.storage.empty() ? default_storage_for_index_path(kInstalledIndexPath)
                                                       : record.storage.c_str());
        cJSON_AddNumberToObject(item,
                                "installed_at_epoch",
                                static_cast<double>(record.installed_at_epoch));
        cJSON_AddItemToArray(packages, item);
    }

    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text)
    {
        out_error = "Serialize installed index failed";
        return false;
    }

    std::string output(text);
    cJSON_free(text);
    if (!write_text_file(kInstalledIndexPath, output))
    {
        out_error = "Write installed index failed";
        return false;
    }
    std::printf("[Packs][Index] saved path=%s records=%lu bytes=%lu\n",
                kInstalledIndexPath,
                static_cast<unsigned long>(index.packages.size()),
                static_cast<unsigned long>(output.size()));
    log_path_probe("index.saved", kInstalledIndexPath);
    return true;
}

bool read_zip_tail(RandomAccessFile& file,
                   std::vector<std::uint8_t>& out_tail)
{
    const std::size_t size = file.size();
    if (size < kZipEocdMinSize)
    {
        return false;
    }

    const std::size_t tail_size = std::min(size, kZipTailSearchMax);
    out_tail.resize(tail_size);
    return file.read_at(size - tail_size, out_tail.data(), tail_size);
}

struct ZipEntry
{
    std::string name;
    std::uint16_t method = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint32_t local_header_offset = 0;
};

bool enumerate_zip_entries(RandomAccessFile& file,
                           std::vector<ZipEntry>& out_entries,
                           std::string& out_error)
{
    out_entries.clear();
    out_error.clear();

    std::vector<std::uint8_t> tail;
    if (!read_zip_tail(file, tail))
    {
        out_error = "Read zip tail failed";
        return false;
    }

    std::size_t eocd_offset = std::string::npos;
    for (std::size_t i = tail.size() - kZipEocdMinSize + 1; i > 0; --i)
    {
        const std::size_t pos = i - 1;
        if (read_le32(&tail[pos]) == kZipEocdSignature)
        {
            eocd_offset = pos;
            break;
        }
    }
    if (eocd_offset == std::string::npos)
    {
        out_error = "Zip EOCD not found";
        return false;
    }

    const std::uint16_t entry_count = read_le16(&tail[eocd_offset + 10]);
    const std::uint32_t central_size = read_le32(&tail[eocd_offset + 12]);
    const std::uint32_t central_offset = read_le32(&tail[eocd_offset + 16]);

    if (central_offset > file.size() ||
        central_size > file.size() - static_cast<std::size_t>(central_offset))
    {
        out_error = "Zip central directory truncated";
        return false;
    }

    std::size_t offset = central_offset;
    const std::size_t central_end = offset + central_size;
    for (std::uint16_t entry_index = 0; entry_index < entry_count; ++entry_index)
    {
        std::uint8_t header[46];
        if (offset + sizeof(header) > central_end ||
            !file.read_at(offset, header, sizeof(header)) ||
            read_le32(header) != kZipCentralSignature)
        {
            out_error = "Zip central entry invalid";
            return false;
        }

        const std::uint16_t method = read_le16(&header[10]);
        const std::uint32_t compressed_size = read_le32(&header[20]);
        const std::uint32_t uncompressed_size = read_le32(&header[24]);
        const std::uint16_t name_len = read_le16(&header[28]);
        const std::uint16_t extra_len = read_le16(&header[30]);
        const std::uint16_t comment_len = read_le16(&header[32]);
        const std::uint32_t local_offset = read_le32(&header[42]);
        if (name_len > kMaxZipEntryNameBytes ||
            offset + sizeof(header) + name_len + extra_len + comment_len > central_end)
        {
            out_error = "Zip central entry truncated";
            return false;
        }

        ZipEntry entry{};
        entry.name.resize(name_len);
        if (name_len > 0 &&
            !file.read_at(offset + sizeof(header), &entry.name[0], name_len))
        {
            out_error = "Read zip central entry name failed";
            return false;
        }
        entry.method = method;
        entry.compressed_size = compressed_size;
        entry.uncompressed_size = uncompressed_size;
        entry.local_header_offset = local_offset;
        out_entries.push_back(std::move(entry));

        offset += sizeof(header) + name_len + extra_len + comment_len;
    }

    return true;
}

bool copy_file_range(RandomAccessFile& source,
                     std::size_t source_offset,
                     std::size_t len,
                     SequentialWriteFile& target,
                     std::string& out_error)
{
    if (source_offset > source.size() || len > source.size() - source_offset)
    {
        out_error = "Zip entry data truncated";
        return false;
    }

    ScopedPackScratch buffer(kFileReadChunkBytes);
    if (!buffer)
    {
        out_error = "Allocate zip copy buffer failed";
        return false;
    }

    std::size_t copied = 0;
    while (copied < len)
    {
        const std::size_t chunk = std::min<std::size_t>(buffer.size(), len - copied);
        if (!source.read_at(source_offset + copied, buffer.bytes(), chunk) ||
            !target.write(buffer.bytes(), chunk))
        {
            out_error = "Copy zip entry data failed";
            return false;
        }
        copied += chunk;
    }
    return true;
}

bool inflate_zip_entry_to_file(RandomAccessFile& source,
                               std::size_t data_offset,
                               std::size_t compressed_size,
                               std::size_t uncompressed_size,
                               SequentialWriteFile& target,
                               std::string& out_error)
{
    if (data_offset > source.size() || compressed_size > source.size() - data_offset)
    {
        out_error = "Zip entry data truncated";
        return false;
    }
    if (compressed_size == 0)
    {
        if (uncompressed_size == 0)
        {
            return true;
        }
        out_error = "Zip entry data truncated";
        return false;
    }

    ScopedPackScratch inflator_storage(sizeof(tinfl_decompressor));
    ScopedPackScratch input_storage(kFileReadChunkBytes);
    ScopedPackScratch output_storage(TINFL_LZ_DICT_SIZE);
    if (!inflator_storage || !input_storage || !output_storage)
    {
        out_error = "Allocate zip inflate buffers failed";
        return false;
    }

    auto* inflator = static_cast<tinfl_decompressor*>(inflator_storage.data());
    tinfl_init(inflator);

    std::uint8_t* input = input_storage.bytes();
    std::uint8_t* output = output_storage.bytes();
    std::size_t loaded = 0;
    std::size_t input_pos = 0;
    std::size_t input_size = 0;
    std::size_t output_pos = 0;
    std::size_t total_written = 0;

    while (true)
    {
        if (input_pos == input_size && loaded < compressed_size)
        {
            input_size = std::min<std::size_t>(input_storage.size(), compressed_size - loaded);
            if (!source.read_at(data_offset + loaded, input, input_size))
            {
                out_error = "Read zip entry data failed";
                return false;
            }
            loaded += input_size;
            input_pos = 0;
        }

        std::size_t in_avail = input_size - input_pos;
        const std::size_t dict_ofs = output_pos & (TINFL_LZ_DICT_SIZE - 1U);
        std::size_t out_avail = output_storage.size() - dict_ofs;
        const mz_uint32 flags = loaded < compressed_size ? TINFL_FLAG_HAS_MORE_INPUT : 0;
        const tinfl_status status =
            tinfl_decompress(inflator,
                             reinterpret_cast<const mz_uint8*>(input + input_pos),
                             &in_avail,
                             reinterpret_cast<mz_uint8*>(output),
                             reinterpret_cast<mz_uint8*>(output + dict_ofs),
                             &out_avail,
                             flags);
        input_pos += in_avail;

        if (out_avail > 0)
        {
            if (!target.write(output + dict_ofs, out_avail))
            {
                out_error = "Write inflated payload failed";
                return false;
            }
            output_pos += out_avail;
            total_written += out_avail;
        }

        if (status == TINFL_STATUS_DONE)
        {
            if (total_written != uncompressed_size)
            {
                out_error = "Inflated zip entry size mismatch";
                return false;
            }
            return true;
        }
        if (status < 0)
        {
            out_error = "Inflate zip entry failed";
            return false;
        }
        if (in_avail == 0 && out_avail == 0)
        {
            out_error = "Inflate zip entry stalled";
            return false;
        }
        if (status == TINFL_STATUS_NEEDS_MORE_INPUT &&
            input_pos == input_size &&
            loaded >= compressed_size)
        {
            out_error = "Inflate zip entry truncated";
            return false;
        }
    }
}

bool extract_zip_payload(const std::string& logical_zip_path, std::string& out_error)
{
    out_error.clear();

    RandomAccessFile file;
    if (!file.open(logical_zip_path))
    {
        out_error = "Open zip file failed";
        return false;
    }

    std::vector<ZipEntry> entries;
    if (!enumerate_zip_entries(file, entries, out_error))
    {
        file.close();
        return false;
    }

    std::printf("[Packs][Install] zip entries=%lu path=%s\n",
                static_cast<unsigned long>(entries.size()),
                logical_zip_path.c_str());

    std::size_t payload_files = 0;
    std::size_t payload_dirs = 0;
    std::size_t payload_bytes = 0;
    std::size_t payload_logged = 0;

    for (const ZipEntry& entry : entries)
    {
        if (!starts_with(entry.name, "payload/"))
        {
            continue;
        }

        const std::string logical_target = std::string(kPackRoot) + "/" + entry.name.substr(8);
        if (!entry.name.empty() && entry.name.back() == '/')
        {
            if (!ensure_dir_recursive(logical_target))
            {
                out_error = "Create payload directory failed";
                file.close();
                return false;
            }
            ++payload_dirs;
            if (payload_logged < 24)
            {
                std::printf("[Packs][Install] payload dir entry=%s target=%s\n",
                            entry.name.c_str(),
                            logical_target.c_str());
                ++payload_logged;
            }
            continue;
        }

        std::uint8_t local_header[30];
        if (!file.read_at(entry.local_header_offset, local_header, sizeof(local_header)) ||
            read_le32(local_header) != kZipLocalSignature)
        {
            out_error = "Read zip local header failed";
            file.close();
            return false;
        }

        const std::uint16_t name_len = read_le16(&local_header[26]);
        const std::uint16_t extra_len = read_le16(&local_header[28]);
        const std::size_t data_offset =
            static_cast<std::size_t>(entry.local_header_offset) + 30U + name_len + extra_len;

        SequentialWriteFile output_file;
        if (!output_file.open(logical_target))
        {
            out_error = std::string("Open extracted payload failed: ") + logical_target;
            file.close();
            return false;
        }

        bool wrote_payload = false;
        if (entry.method == 0)
        {
            if (entry.compressed_size != entry.uncompressed_size)
            {
                out_error = "Stored zip entry size mismatch";
            }
            else
            {
                wrote_payload = copy_file_range(file,
                                                data_offset,
                                                entry.compressed_size,
                                                output_file,
                                                out_error);
            }
        }
        else if (entry.method == 8)
        {
            wrote_payload = inflate_zip_entry_to_file(file,
                                                      data_offset,
                                                      entry.compressed_size,
                                                      entry.uncompressed_size,
                                                      output_file,
                                                      out_error);
        }
        else
        {
            out_error = "Unsupported zip compression";
        }

        output_file.close();
        if (!wrote_payload)
        {
            (void)remove_file_if_exists(logical_target);
            file.close();
            return false;
        }

        ++payload_files;
        payload_bytes += entry.uncompressed_size;
        if (payload_logged < 24 ||
            entry.name.find("/manifest.ini") != std::string::npos ||
            entry.name.find("/font.bin") != std::string::npos)
        {
            std::printf("[Packs][Install] payload file entry=%s target=%s method=%u usize=%lu csize=%lu\n",
                        entry.name.c_str(),
                        logical_target.c_str(),
                        static_cast<unsigned>(entry.method),
                        static_cast<unsigned long>(entry.uncompressed_size),
                        static_cast<unsigned long>(entry.compressed_size));
            ++payload_logged;
        }
    }

    file.close();
    std::printf("[Packs][Install] payload summary files=%lu dirs=%lu bytes=%lu\n",
                static_cast<unsigned long>(payload_files),
                static_cast<unsigned long>(payload_dirs),
                static_cast<unsigned long>(payload_bytes));
    return true;
}

const char* storage_pack_root(const std::string& storage)
{
#if defined(ARDUINO)
    return lowercase_ascii(storage) == kStorageSd ? kSdPackRoot : kFlashPackRoot;
#else
    (void)storage;
    return kPackRoot;
#endif
}

const InstalledPackageRecord* find_installed_record(const InstalledIndex& index, const std::string& package_id)
{
    for (const InstalledPackageRecord& record : index.packages)
    {
        if (record.id == package_id)
        {
            return &record;
        }
    }
    return nullptr;
}

bool remove_package_payload(const PackageRecord& package,
                            const std::string& storage,
                            std::string& out_error)
{
    const struct PayloadGroup
    {
        const char* dir_name;
        const std::vector<std::string>* ids;
        const char* label;
    } groups[] = {
        {"locales", &package.provided_locale_ids, "locale"},
        {"fonts", &package.provided_font_ids, "font"},
        {"ime", &package.provided_ime_ids, "IME"},
    };
    const char* pack_root = storage_pack_root(storage);

    for (const PayloadGroup& group : groups)
    {
        for (const std::string& id : *group.ids)
        {
            if (id.empty())
            {
                continue;
            }

            const std::string logical_path =
                join_logical_path(join_logical_path(pack_root, group.dir_name), id);
            if (!remove_dir_recursive_if_exists(logical_path))
            {
                out_error = std::string("Remove ") + group.label + " pack failed";
                return false;
            }
        }
    }

    return true;
}

bool package_payload_visible(const PackageRecord& package, const std::string& storage)
{
    const char* pack_root = storage_pack_root(storage);
    bool has_declared_payload = false;
    bool all_declared_payload_visible = true;

    for (const std::string& id : package.provided_locale_ids)
    {
        if (id.empty())
        {
            continue;
        }
        has_declared_payload = true;
        const std::string manifest =
            join_logical_path(join_logical_path(join_logical_path(pack_root, "locales"), id),
                              "manifest.ini");
        all_declared_payload_visible = all_declared_payload_visible && logical_file_exists(manifest);
    }
    for (const std::string& id : package.provided_font_ids)
    {
        if (id.empty())
        {
            continue;
        }
        has_declared_payload = true;
        const std::string font_root = join_logical_path(join_logical_path(pack_root, "fonts"), id);
        all_declared_payload_visible =
            all_declared_payload_visible &&
            logical_file_exists(join_logical_path(font_root, "manifest.ini")) &&
            logical_file_exists(join_logical_path(font_root, "font.bin"));
    }
    for (const std::string& id : package.provided_ime_ids)
    {
        if (id.empty())
        {
            continue;
        }
        has_declared_payload = true;
        const std::string manifest =
            join_logical_path(join_logical_path(join_logical_path(pack_root, "ime"), id),
                              "manifest.ini");
        all_declared_payload_visible = all_declared_payload_visible && logical_file_exists(manifest);
    }

    return has_declared_payload && all_declared_payload_visible;
}

void log_package_payload_probe(const char* scope,
                               const PackageRecord& package,
                               const std::string& storage)
{
    const char* pack_root = storage_pack_root(storage);
    std::printf("[Packs][Verify] %s id=%s storage=%s root=%s locales=%lu fonts=%lu ime=%lu\n",
                scope ? scope : "payload",
                package.id.c_str(),
                storage.empty() ? "<none>" : storage.c_str(),
                pack_root,
                static_cast<unsigned long>(package.provided_locale_ids.size()),
                static_cast<unsigned long>(package.provided_font_ids.size()),
                static_cast<unsigned long>(package.provided_ime_ids.size()));

    for (const std::string& id : package.provided_locale_ids)
    {
        const std::string manifest =
            join_logical_path(join_logical_path(join_logical_path(pack_root, "locales"), id),
                              "manifest.ini");
        log_path_probe("locale.manifest", manifest);
        log_lvgl_path_probe("locale.manifest", manifest);
    }
    for (const std::string& id : package.provided_font_ids)
    {
        const std::string font_root = join_logical_path(join_logical_path(pack_root, "fonts"), id);
        const std::string manifest = join_logical_path(font_root, "manifest.ini");
        const std::string font_bin = join_logical_path(font_root, "font.bin");
        log_path_probe("font.manifest", manifest);
        log_lvgl_path_probe("font.manifest", manifest);
        log_path_probe("font.bin", font_bin);
        log_lvgl_path_probe("font.bin", font_bin);
    }
    for (const std::string& id : package.provided_ime_ids)
    {
        const std::string manifest =
            join_logical_path(join_logical_path(join_logical_path(pack_root, "ime"), id),
                              "manifest.ini");
        log_path_probe("ime.manifest", manifest);
        log_lvgl_path_probe("ime.manifest", manifest);
    }
}

void merge_installed_state(const std::vector<InstalledPackageRecord>& installed,
                           std::vector<PackageRecord>& packages)
{
    for (PackageRecord& package : packages)
    {
        for (const InstalledPackageRecord& record : installed)
        {
            if (record.id != package.id)
            {
                continue;
            }
            if (!package_payload_visible(package, record.storage))
            {
                std::printf("[Packs][Index] stale installed record id=%s version=%s storage=%s reason=payload_not_visible\n",
                            record.id.c_str(),
                            record.version.c_str(),
                            record.storage.c_str());
                log_package_payload_probe("stale_index", package, record.storage);
                break;
            }
            package.installed = true;
            package.installed_record = record;
            package.update_available =
                record.version != package.version ||
                lowercase_ascii(record.archive_sha256) != lowercase_ascii(package.archive_sha256);
            break;
        }
    }
}

} // namespace

bool is_supported()
{
#if defined(ARDUINO)
    return true;
#else
    return platform::esp::idf_common::flash_storage_runtime::ensure_ready(true);
#endif
}

bool load_installed_packages(std::vector<InstalledPackageRecord>& out_installed, std::string& out_error)
{
    InstalledIndex index;
    if (!load_installed_index(index, out_error))
    {
        out_installed.clear();
        return false;
    }
    out_installed = index.packages;
    std::printf("[Packs][Index] load_installed_packages count=%lu\n",
                static_cast<unsigned long>(out_installed.size()));
    return true;
}

CatalogStatus catalog_status() { return {}; }
void request_catalog_refresh() {}

bool fetch_catalog(std::vector<PackageRecord>& out_packages, std::string& out_error)
{
    out_packages.clear();
    out_error.clear();

    const platform::ui::wifi::Status wifi_status = platform::ui::wifi::status();
    if (!wifi_status.supported)
    {
        out_error = "Wi-Fi unsupported";
        return false;
    }
    if (!wifi_status.connected)
    {
        out_error = "Connect Wi-Fi in Settings first";
        return false;
    }

    std::string text;
    if (!fetch_catalog_text(kCatalogUrl, text, out_error))
    {
        return false;
    }

    cJSON* root = parse_json_document(text);
    if (!root)
    {
        out_error = "Parse remote catalog failed";
        return false;
    }

    cJSON* packages = cJSON_GetObjectItemCaseSensitive(root, "packages");
    if (!cJSON_IsArray(packages))
    {
        cJSON_Delete(root);
        out_error = "Remote catalog packages missing";
        return false;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, packages)
    {
        PackageRecord package{};
        package.id = json_string(item, "id");
        package.package_type = json_string(item, "package_type");
        package.version = json_string(item, "version");
        package.display_name = json_string(item, "display_name");
        package.summary = json_string(item, "summary");
        package.description = json_string(item, "description");
        package.author = json_string(item, "author");
        package.homepage = json_string(item, "homepage");
        package.min_firmware_version = json_string(item, "min_firmware_version");
        package.supported_memory_profiles = json_string_array(item, "supported_memory_profiles");
        package.tags = json_string_array(item, "tags");

        cJSON* runtime = cJSON_GetObjectItemCaseSensitive(item, "runtime");
        package.estimated_unique_font_ram_bytes =
            json_size_t(runtime, "estimated_unique_font_ram_bytes");

        cJSON* archive = cJSON_GetObjectItemCaseSensitive(item, "archive");
        package.archive_path = json_string(archive, "path");
        package.archive_size_bytes = json_size_t(archive, "size_bytes");
        package.archive_sha256 = json_string(archive, "sha256");
        package.download_url = join_url(kCatalogBaseUrl, package.archive_path);

        cJSON* provides = cJSON_GetObjectItemCaseSensitive(item, "provides");
        collect_provided_ids(provides, "locales", "id", package.provided_locale_ids);
        collect_provided_ids(provides, "fonts", "id", package.provided_font_ids);
        collect_provided_ids(provides, "ime", "id", package.provided_ime_ids);

        package.compatible_memory_profile = compatible_with_profile(package);
        package.compatible_firmware = compatible_with_firmware(package);

        if (!package.id.empty())
        {
            out_packages.push_back(std::move(package));
        }
    }

    cJSON_Delete(root);

    std::vector<InstalledPackageRecord> installed;
    std::string installed_error;
    if (load_installed_packages(installed, installed_error))
    {
        merge_installed_state(installed, out_packages);
    }
    else
    {
        std::printf("[Packs][Index] load installed during catalog failed error=%s\n",
                    installed_error.c_str());
    }

    std::sort(out_packages.begin(),
              out_packages.end(),
              [](const PackageRecord& lhs, const PackageRecord& rhs)
              {
                  return lowercase_ascii(lhs.display_name) < lowercase_ascii(rhs.display_name);
              });
    return true;
}

bool start_install_package(const PackageRecord& package, std::string& out_error)
{
    out_error.clear();

    if (!ensure_install_mutex())
    {
        out_error = "Create package install mutex failed";
        std::printf("[Packs][InstallAsync] start rejected id=%s reason=%s\n",
                    package.id.c_str(),
                    out_error.c_str());
        return false;
    }

    InstallWorkerContext* ctx = new (std::nothrow) InstallWorkerContext{};
    if (ctx == nullptr)
    {
        out_error = "Allocate package install task failed";
        std::printf("[Packs][InstallAsync] start rejected id=%s reason=%s\n",
                    package.id.c_str(),
                    out_error.c_str());
        return false;
    }
    ctx->package = package;

    {
        InstallStateLock lock(s_install_state.mutex);
        if (s_install_state.status.busy ||
            s_install_state.worker_task != nullptr ||
            s_install_state.launch_pending)
        {
            out_error = "Package install already in progress";
            std::printf("[Packs][InstallAsync] start rejected id=%s reason=%s current_id=%s\n",
                        package.id.c_str(),
                        out_error.c_str(),
                        s_install_state.status.package_id.c_str());
            delete ctx;
            return false;
        }

        s_install_state.launch_pending = true;
        set_install_status_locked(PackageInstallPhase::Installing,
                                  true,
                                  package.id,
                                  "Installing package...",
                                  package.id.c_str());
    }

    TaskHandle_t task_handle = nullptr;
    const BaseType_t task_ok = xTaskCreate(install_worker_task_entry,
                                           "pack_install",
                                           kInstallWorkerStackBytes,
                                           ctx,
                                           kInstallWorkerPriority,
                                           &task_handle);

    {
        InstallStateLock lock(s_install_state.mutex);
        if (task_ok != pdPASS || task_handle == nullptr)
        {
            s_install_state.worker_task = nullptr;
            s_install_state.launch_pending = false;
            set_install_status_locked(PackageInstallPhase::Failed,
                                      false,
                                      package.id,
                                      "Create package install task failed",
                                      package.id.c_str());
            out_error = s_install_state.status.message;
            delete ctx;
            std::printf("[Packs][InstallAsync] start failed id=%s result=%d handle=%p\n",
                        package.id.c_str(),
                        static_cast<int>(task_ok),
                        static_cast<void*>(task_handle));
            return false;
        }

        if (s_install_state.launch_pending)
        {
            s_install_state.worker_task = task_handle;
            s_install_state.launch_pending = false;
        }
    }

    std::printf("[Packs][InstallAsync] start accepted id=%s task=%p\n",
                package.id.c_str(),
                static_cast<void*>(task_handle));
    return true;
}

PackageInstallStatus install_status()
{
    PackageInstallStatus status{};
    if (!ensure_install_mutex())
    {
        status.phase = PackageInstallPhase::Failed;
        status.message = "Create package install mutex failed";
        return status;
    }

    InstallStateLock lock(s_install_state.mutex);
    status = s_install_state.status;
    return status;
}

bool install_package_impl(const PackageRecord& package,
                          std::string& out_error,
                          bool reload_language_after_install)
{
    out_error.clear();

    std::printf("[Packs][Install] begin id=%s version=%s url=%s root=%s\n",
                package.id.c_str(),
                package.version.c_str(),
                package.download_url.c_str(),
                kPackRoot);
    std::printf("[Packs][Install] provides locales=%lu fonts=%lu ime=%lu expected_sha=%s\n",
                static_cast<unsigned long>(package.provided_locale_ids.size()),
                static_cast<unsigned long>(package.provided_font_ids.size()),
                static_cast<unsigned long>(package.provided_ime_ids.size()),
                package.archive_sha256.empty() ? "<none>" : package.archive_sha256.c_str());

    if (package.id.empty() || package.download_url.empty())
    {
        out_error = "Package metadata is incomplete";
        std::printf("[Packs][Install] failed reason=%s\n", out_error.c_str());
        return false;
    }

    const platform::ui::wifi::Status wifi_status = platform::ui::wifi::status();
    if (!wifi_status.connected)
    {
        out_error = "Wi-Fi is not connected";
        std::printf("[Packs][Install] failed reason=%s\n", out_error.c_str());
        return false;
    }

    const bool root_ok = ensure_dir_recursive(kPackRoot);
    const bool index_ok = ensure_dir_recursive(kIndexDir);
    const bool temp_ok = ensure_dir_recursive(kTempDir);
    std::printf("[Packs][Install] dirs root=%d index=%d temp=%d\n",
                root_ok ? 1 : 0,
                index_ok ? 1 : 0,
                temp_ok ? 1 : 0);
    log_path_probe("root.after_ensure", kPackRoot);
    log_path_probe("index_dir.after_ensure", kIndexDir);
    log_path_probe("temp_dir.after_ensure", kTempDir);

    if (!root_ok || !index_ok || !temp_ok)
    {
        out_error = "Create pack directories failed";
        std::printf("[Packs][Install] failed reason=%s\n", out_error.c_str());
        return false;
    }

    const std::string temp_zip_path = std::string(kTempDir) + "/" + package.id + "-" + package.version + ".zip";
    std::printf("[Packs][Install] download temp=%s\n", temp_zip_path.c_str());
    if (!download_package_archive(package, temp_zip_path, out_error))
    {
        std::printf("[Packs][Install] download failed error=%s\n", out_error.c_str());
        return false;
    }
    log_path_probe("temp_zip.after_download", temp_zip_path);

    set_install_status(PackageInstallPhase::Installing,
                       true,
                       package.id,
                       "Verifying package...",
                       package.id.c_str());

    std::string archive_sha256;
    if (!sha256_file(temp_zip_path, archive_sha256))
    {
        (void)remove_file_if_exists(temp_zip_path);
        out_error = "Hash downloaded archive failed";
        std::printf("[Packs][Install] failed reason=%s\n", out_error.c_str());
        return false;
    }
    std::printf("[Packs][Install] downloaded sha=%s\n", archive_sha256.c_str());
    if (!package.archive_sha256.empty() &&
        lowercase_ascii(archive_sha256) != lowercase_ascii(package.archive_sha256))
    {
        (void)remove_file_if_exists(temp_zip_path);
        out_error = "Archive SHA256 mismatch";
        std::printf("[Packs][Install] failed reason=%s actual=%s expected=%s\n",
                    out_error.c_str(),
                    archive_sha256.c_str(),
                    package.archive_sha256.c_str());
        return false;
    }

    set_install_status(PackageInstallPhase::Installing,
                       true,
                       package.id,
                       "Extracting package...",
                       package.id.c_str());

    if (!extract_zip_payload(temp_zip_path, out_error))
    {
        (void)remove_file_if_exists(temp_zip_path);
        std::printf("[Packs][Install] extract failed error=%s\n", out_error.c_str());
        return false;
    }
    log_package_payload_probe("after_extract", package, kInstallStorage);
    if (!package_payload_visible(package, kInstallStorage))
    {
        (void)remove_file_if_exists(temp_zip_path);
        out_error = "Installed payload is missing after extract";
        std::printf("[Packs][Install] failed reason=%s\n", out_error.c_str());
        return false;
    }

    InstalledIndex index;
    std::string index_error;
    if (!load_installed_index(index, index_error))
    {
        (void)remove_file_if_exists(temp_zip_path);
        out_error = index_error;
        std::printf("[Packs][Install] load index failed error=%s\n", out_error.c_str());
        return false;
    }

    const InstalledPackageRecord* previous_record = find_installed_record(index, package.id);
    const std::string previous_storage = previous_record ? previous_record->storage : "";

    set_install_status(PackageInstallPhase::Installing,
                       true,
                       package.id,
                       "Writing package index...",
                       package.id.c_str());

    bool updated = false;
    for (InstalledPackageRecord& record : index.packages)
    {
        if (record.id != package.id)
        {
            continue;
        }
        record.version = package.version;
        record.archive_sha256 = archive_sha256;
        record.storage = kInstallStorage;
        record.installed_at_epoch = static_cast<std::uint64_t>(std::time(nullptr));
        updated = true;
        break;
    }

    if (!updated)
    {
        InstalledPackageRecord record{};
        record.id = package.id;
        record.version = package.version;
        record.archive_sha256 = archive_sha256;
        record.storage = kInstallStorage;
        record.installed_at_epoch = static_cast<std::uint64_t>(std::time(nullptr));
        index.packages.push_back(std::move(record));
    }

    if (!save_installed_index(index, out_error))
    {
        (void)remove_file_if_exists(temp_zip_path);
        std::printf("[Packs][Install] save index failed error=%s\n", out_error.c_str());
        return false;
    }

    (void)remove_file_if_exists(temp_zip_path);
    log_path_probe("temp_zip.after_cleanup", temp_zip_path);

#if defined(ARDUINO)
    if (!previous_storage.empty() &&
        lowercase_ascii(previous_storage) == kStorageSd &&
        platform::ui::device::card_ready())
    {
        std::string cleanup_error;
        if (!remove_package_payload(package, previous_storage, cleanup_error))
        {
            std::printf("[Packs] legacy sd cleanup skipped id=%s reason=%s\n",
                        package.id.c_str(),
                        cleanup_error.c_str());
        }
    }
#endif

    log_package_payload_probe("before_reload", package, kInstallStorage);
    if (reload_language_after_install)
    {
        std::printf("[Packs][Install] reload_language id=%s\n", package.id.c_str());
        ::ui::i18n::reload_language();
    }
    else
    {
        std::printf("[Packs][Install] reload_language deferred id=%s\n",
                    package.id.c_str());
    }
    std::printf("[Packs][Install] complete id=%s\n", package.id.c_str());
    return true;
}

bool install_package(const PackageRecord& package, std::string& out_error)
{
    return install_package_impl(package, out_error, true);
}

bool uninstall_package(const PackageRecord& package, std::string& out_error)
{
    out_error.clear();

    std::printf("[Packs][Uninstall] begin id=%s version=%s\n",
                package.id.c_str(),
                package.version.c_str());

    if (package.id.empty())
    {
        out_error = "Package metadata is incomplete";
        std::printf("[Packs][Uninstall] failed reason=%s\n", out_error.c_str());
        return false;
    }

    InstalledIndex index;
    if (!load_installed_index(index, out_error))
    {
        std::printf("[Packs][Uninstall] load index failed error=%s\n", out_error.c_str());
        return false;
    }

    const InstalledPackageRecord* installed = find_installed_record(index, package.id);
    const std::string storage = installed ? installed->storage : std::string(kInstallStorage);
    std::printf("[Packs][Uninstall] resolved storage=%s installed_record=%d records=%lu\n",
                storage.c_str(),
                installed ? 1 : 0,
                static_cast<unsigned long>(index.packages.size()));
    log_package_payload_probe("before_uninstall", package, storage);

#if defined(ARDUINO)
    if (lowercase_ascii(storage) == kStorageSd && !platform::ui::device::card_ready())
    {
        out_error = "SD card is not ready";
        std::printf("[Packs][Uninstall] failed reason=%s\n", out_error.c_str());
        return false;
    }
#endif

    if (!remove_package_payload(package, storage, out_error))
    {
        std::printf("[Packs][Uninstall] remove payload failed error=%s\n", out_error.c_str());
        return false;
    }
    log_package_payload_probe("after_remove_payload", package, storage);

    index.packages.erase(
        std::remove_if(index.packages.begin(),
                       index.packages.end(),
                       [&](const InstalledPackageRecord& record)
                       {
                           return record.id == package.id;
                       }),
        index.packages.end());

    if (!save_installed_index(index, out_error))
    {
        std::printf("[Packs][Uninstall] save index failed error=%s\n", out_error.c_str());
        return false;
    }

    std::printf("[Packs][Uninstall] reload_language id=%s\n", package.id.c_str());
    ::ui::i18n::reload_language();
    std::printf("[Packs][Uninstall] complete id=%s\n", package.id.c_str());
    return true;
}

} // namespace ui::runtime::packs

#else

namespace ui::runtime::packs
{

bool is_supported()
{
    return false;
}

bool load_installed_packages(std::vector<InstalledPackageRecord>& out_installed, std::string& out_error)
{
    out_installed.clear();
    out_error = "Pack installation is unsupported on this platform";
    return false;
}

bool fetch_catalog(std::vector<PackageRecord>& out_packages, std::string& out_error)
{
    out_packages.clear();
    out_error = "Pack installation is unsupported on this platform";
    return false;
}

bool install_package(const PackageRecord& package, std::string& out_error)
{
    (void)package;
    out_error = "Pack installation is unsupported on this platform";
    return false;
}

bool start_install_package(const PackageRecord& package, std::string& out_error)
{
    return install_package(package, out_error);
}

PackageInstallStatus install_status()
{
    PackageInstallStatus status{};
    status.phase = PackageInstallPhase::Idle;
    return status;
}

bool uninstall_package(const PackageRecord& package, std::string& out_error)
{
    (void)package;
    out_error = "Pack installation is unsupported on this platform";
    return false;
}

} // namespace ui::runtime::packs

#endif
