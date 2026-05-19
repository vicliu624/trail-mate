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
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "platform/ui/device_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "ui/localization.h"
#include "ui/runtime/memory_profile.h"
#include "ui/support/lvgl_fs_utils.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)

#include "cJSON.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "mbedtls/platform.h"
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

#define UI_PACKS_HAVE_CRT_BUNDLE 1

extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

#if defined(ARDUINO)
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#else
#include "platform/esp/idf_common/bsp_runtime.h"
#endif

namespace ui::runtime::packs
{
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
constexpr const char* kPackRoot = "/trailmate/packs";
constexpr const char* kIndexDir = "/trailmate/packs/index";
constexpr const char* kTempDir = "/trailmate/packs/index/tmp";
constexpr const char* kInstalledIndexPath = "/trailmate/packs/index/installed.json";
constexpr const char* kInstallStorage = kStorageSd;
#endif
constexpr int kHttpBufferSize = 1024;
constexpr int kHttpTxBufferSize = 512;
constexpr std::size_t kTlsLargeAllocThresholdBytes = 4096;
constexpr std::size_t kFileWriteChunkBytes = 4096;
constexpr std::size_t kZipEocdMinSize = 22;
constexpr std::size_t kZipTailSearchMax = 0x10000 + kZipEocdMinSize;
constexpr std::uint32_t kZipEocdSignature = 0x06054B50u;
constexpr std::uint32_t kZipCentralSignature = 0x02014B50u;
constexpr std::uint32_t kZipLocalSignature = 0x04034B50u;

struct InstalledIndex
{
    std::vector<InstalledPackageRecord> packages;
};

void log_pack_memory_snapshot(const char* stage)
{
    std::printf("[Packs][TLS] %s ram_free=%u ram_largest=%u psram_free=%u psram_largest=%u\n",
                stage ? stage : "state",
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}

// Arduino's prebuilt mbedTLS keeps 16 KB TLS record buffers, which can exhaust
// internal RAM on ESP32 UI builds. Route large allocations to PSRAM first while
// leaving small control allocations in internal RAM for lower latency.
void* mbedtls_calloc_prefer_psram(std::size_t count, std::size_t size)
{
    if (count == 0 || size == 0)
    {
        return nullptr;
    }
    if (count > (static_cast<std::size_t>(-1) / size))
    {
        return nullptr;
    }

    const std::size_t bytes = count * size;
    const bool prefer_psram =
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0 &&
        bytes >= kTlsLargeAllocThresholdBytes;

    const uint32_t primary_caps = prefer_psram ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                                               : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t secondary_caps = prefer_psram ? (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                                                 : (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    return heap_caps_calloc_prefer(count, size, 2, primary_caps, secondary_caps);
}

void mbedtls_free_prefer_psram(void* ptr)
{
    heap_caps_free(ptr);
}

bool ensure_tls_allocator_configured()
{
    static bool attempted = false;
    static bool configured = false;

    if (attempted)
    {
        return configured;
    }

    attempted = true;
    log_pack_memory_snapshot("before allocator config");
    configured = mbedtls_platform_set_calloc_free(&mbedtls_calloc_prefer_psram,
                                                  &mbedtls_free_prefer_psram) == 0;
    std::printf("[Packs][TLS] allocator configured=%d threshold=%lu\n",
                configured ? 1 : 0,
                static_cast<unsigned long>(kTlsLargeAllocThresholdBytes));
    log_pack_memory_snapshot("after allocator config");
    return configured;
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

struct ArduinoStorageBinding
{
    ::fs::FS* volume = nullptr;
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

bool resolve_storage_binding(const std::string& logical_path,
                             bool allow_format_on_fail,
                             ArduinoStorageBinding& out)
{
    out = ArduinoStorageBinding{};
    if (logical_path.empty())
    {
        return false;
    }

#if UI_FS_HAS_FLASH_PACK_STORAGE
    if (is_flash_logical_path(logical_path))
    {
        if (!::ui::fs::ensure_flash_storage_ready(allow_format_on_fail))
        {
            return false;
        }

        out.volume = &FFat;
        out.storage = kStorageFlash;
        out.volume_path = strip_mount_prefix(logical_path, "/fs");
        return true;
    }
#else
    if (is_flash_logical_path(logical_path))
    {
        return false;
    }
#endif

    if (!platform::ui::device::card_ready())
    {
        return false;
    }

    out.volume = &SD;
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

#if UI_FS_HAS_FLASH_PACK_STORAGE
    if (is_flash_logical_path(logical_path) &&
        !::ui::fs::ensure_flash_storage_ready(allow_format_on_fail))
    {
        return {};
    }
#endif

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

#if LV_USE_FS_POSIX
    if (letter == LV_FS_POSIX_LETTER)
    {
        std::string root = LV_FS_POSIX_PATH;
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

    ArduinoStorageBinding binding;
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

            File existing = binding.volume->open(current.c_str(), FILE_READ);
            const bool exists = static_cast<bool>(existing);
            const bool is_dir = exists && existing.isDirectory();
            if (existing)
            {
                existing.close();
            }

            if (!exists)
            {
                if (!binding.volume->mkdir(current.c_str()))
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
    ArduinoStorageBinding binding;
    if (!resolve_storage_binding(logical_path, false, binding))
    {
        return false;
    }

    File file = binding.volume->open(binding.volume_path.c_str(), FILE_READ);
    const bool exists = static_cast<bool>(file) && !file.isDirectory();
    if (file)
    {
        file.close();
    }
    return exists;
}

bool logical_dir_exists(const std::string& logical_path)
{
    ArduinoStorageBinding binding;
    if (!resolve_storage_binding(logical_path, false, binding))
    {
        return false;
    }

    File dir = binding.volume->open(binding.volume_path.c_str(), FILE_READ);
    const bool exists = static_cast<bool>(dir) && dir.isDirectory();
    if (dir)
    {
        dir.close();
    }
    return exists;
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

    ArduinoStorageBinding binding;
    if (!resolve_storage_binding(logical_path, true, binding))
    {
        std::printf("[Packs][Storage] write resolve failed logical=%s len=%lu storage=%s\n",
                    logical_path.c_str(),
                    static_cast<unsigned long>(len),
                    storage_for_logical_path(logical_path));
        return false;
    }

    File file = binding.volume->open(binding.volume_path.c_str(), FILE_WRITE);
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

bool write_text_file(const std::string& logical_path, const std::string& text)
{
    return write_binary_file(logical_path, text.data(), text.size());
}

bool read_binary_file(const std::string& logical_path, std::vector<std::uint8_t>& out)
{
    out.clear();

    ArduinoStorageBinding binding;
    if (!resolve_storage_binding(logical_path, false, binding))
    {
        return false;
    }

    File file = binding.volume->open(binding.volume_path.c_str(), FILE_READ);
    if (!file || file.isDirectory())
    {
        if (file)
        {
            file.close();
        }
        return false;
    }

    const std::size_t size = static_cast<std::size_t>(file.size());
    out.resize(size);
    const std::size_t read = file.read(out.data(), size);
    file.close();
    if (read != size)
    {
        out.clear();
        return false;
    }
    return true;
}

bool logical_file_size(const std::string& logical_path, std::size_t& out_size)
{
    out_size = 0;

    ArduinoStorageBinding binding;
    if (!resolve_storage_binding(logical_path, false, binding))
    {
        return false;
    }

    File file = binding.volume->open(binding.volume_path.c_str(), FILE_READ);
    if (!file || file.isDirectory())
    {
        if (file)
        {
            file.close();
        }
        return false;
    }
    out_size = static_cast<std::size_t>(file.size());
    file.close();
    return true;
}

bool read_text_file(const std::string& logical_path, std::string& out)
{
    std::vector<std::uint8_t> bytes;
    if (!read_binary_file(logical_path, bytes))
    {
        out.clear();
        return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
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
    ArduinoStorageBinding binding;
    if (!resolve_storage_binding(logical_path, false, binding))
    {
        return false;
    }

    File file = binding.volume->open(binding.volume_path.c_str(), FILE_READ);
    const bool exists = static_cast<bool>(file);
    const bool is_dir = exists && file.isDirectory();
    if (file)
    {
        file.close();
    }
    if (!exists)
    {
        return true;
    }
    if (is_dir)
    {
        return false;
    }
    if (!binding.volume->remove(binding.volume_path.c_str()))
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

    ArduinoStorageBinding binding;
    if (!resolve_storage_binding(logical_path, false, binding))
    {
        return false;
    }

    File node = binding.volume->open(binding.volume_path.c_str(), FILE_READ);
    if (!node)
    {
        return true;
    }

    if (!node.isDirectory())
    {
        node.close();
        return binding.volume->remove(binding.volume_path.c_str());
    }

    File child = node.openNextFile();
    while (child)
    {
        const std::string name = file_entry_name(child.name());
        const bool is_dir = child.isDirectory();
        child.close();

        if (!name.empty())
        {
            const std::string child_logical = join_logical_path(logical_path, name);
            const bool ok = is_dir ? remove_dir_recursive_if_exists(child_logical)
                                   : remove_file_if_exists(child_logical);
            if (!ok)
            {
                node.close();
                return false;
            }
        }

        child = node.openNextFile();
    }

    node.close();
    if (!binding.volume->rmdir(binding.volume_path.c_str()))
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
        ArduinoStorageBinding binding;
        if (!resolve_storage_binding(logical_path, false, binding))
        {
            return false;
        }

        file_ = binding.volume->open(binding.volume_path.c_str(), FILE_READ);
        if (!file_ || file_.isDirectory())
        {
            if (file_)
            {
                file_.close();
            }
            return false;
        }
        size_ = static_cast<std::size_t>(file_.size());
        return true;
    }

    void close()
    {
        if (file_)
        {
            file_.close();
            size_ = 0;
        }
    }

    std::size_t size() const
    {
        return size_;
    }

    bool read_at(std::size_t offset, void* out, std::size_t len)
    {
        if (!file_ || !file_.seek(static_cast<uint32_t>(offset)))
        {
            return false;
        }
        return file_.read(static_cast<std::uint8_t*>(out), len) == len;
    }

  private:
    File file_{};
    std::size_t size_ = 0;
};

class SequentialWriteFile
{
  public:
    bool open(const std::string& logical_path)
    {
        const std::size_t slash = logical_path.find_last_of('/');
        if (slash != std::string::npos &&
            !ensure_dir_recursive(logical_path.substr(0, slash)))
        {
            return false;
        }

        (void)remove_file_if_exists(logical_path);

        ArduinoStorageBinding binding;
        if (!resolve_storage_binding(logical_path, true, binding))
        {
            return false;
        }

        file_ = binding.volume->open(binding.volume_path.c_str(), FILE_WRITE);
        if (!file_)
        {
            return false;
        }
        return true;
    }

    bool write(const void* data, std::size_t len)
    {
        if (!file_)
        {
            return false;
        }
        return file_.write(static_cast<const std::uint8_t*>(data), len) == len;
    }

    void close()
    {
        if (file_)
        {
            file_.flush();
            file_.close();
        }
    }

  private:
    File file_{};
};

#else

std::string host_path(const std::string& logical_path)
{
    return std::string(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()) + logical_path;
}

bool ensure_dir_recursive(const std::string& logical_dir)
{
    if (logical_dir.empty())
    {
        return false;
    }

    std::string host_dir = host_path(logical_dir);
    std::string current;
    for (char ch : host_dir)
    {
        current.push_back(ch);
        if (ch != '/')
        {
            continue;
        }
        if (current.size() <= 1)
        {
            continue;
        }
        if (::mkdir(current.c_str(), 0775) != 0 && errno != EEXIST)
        {
            return false;
        }
    }

    return ::mkdir(host_dir.c_str(), 0775) == 0 || errno == EEXIST;
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

bool read_binary_file(const std::string& logical_path, std::vector<std::uint8_t>& out)
{
    out.clear();
    FILE* file = std::fopen(host_path(logical_path).c_str(), "rb");
    if (!file)
    {
        return false;
    }

    if (std::fseek(file, 0, SEEK_END) != 0)
    {
        std::fclose(file);
        return false;
    }
    const long file_size = std::ftell(file);
    if (file_size < 0)
    {
        std::fclose(file);
        return false;
    }
    if (std::fseek(file, 0, SEEK_SET) != 0)
    {
        std::fclose(file);
        return false;
    }

    out.resize(static_cast<std::size_t>(file_size));
    const std::size_t read = std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);
    if (read != out.size())
    {
        out.clear();
        return false;
    }
    return true;
}

bool read_text_file(const std::string& logical_path, std::string& out)
{
    std::vector<std::uint8_t> bytes;
    if (!read_binary_file(logical_path, bytes))
    {
        out.clear();
        return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
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
                kStorageSd);
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

void configure_http_client(esp_http_client_config_t& config, const std::string& url)
{
    (void)ensure_tls_allocator_configured();
    config = esp_http_client_config_t{};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 30000;
    config.disable_auto_redirect = false;
    config.buffer_size = kHttpBufferSize;
    config.buffer_size_tx = kHttpTxBufferSize;
#if UI_PACKS_HAVE_CRT_BUNDLE
    config.crt_bundle_attach = esp_crt_bundle_attach;
#endif
}

bool http_get_text(const std::string& url, std::string& out, std::string& out_error)
{
    out.clear();
    out_error.clear();

    esp_http_client_config_t config{};
    configure_http_client(config, url);

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr)
    {
        out_error = "Create HTTP client failed";
        return false;
    }

    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK)
    {
        if (esp_http_client_fetch_headers(client) >= 0)
        {
            const int status_code = esp_http_client_get_status_code(client);
            if (status_code < 200 || status_code >= 300)
            {
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "Catalog HTTP %d", status_code);
                out_error = buffer;
            }
            else
            {
                char buffer[kHttpBufferSize];
                while (true)
                {
                    const int read = esp_http_client_read(client, buffer, sizeof(buffer));
                    if (read < 0)
                    {
                        out_error = "Read catalog failed";
                        break;
                    }
                    if (read == 0)
                    {
                        ok = true;
                        break;
                    }
                    out.append(buffer, static_cast<std::size_t>(read));
                }
            }
        }
        else
        {
            out_error = "Fetch catalog headers failed";
        }
    }
    else
    {
        log_pack_memory_snapshot("catalog open failed");
        out_error = "Open catalog request failed";
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

bool http_download_file(const std::string& url,
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

    esp_http_client_config_t config{};
    configure_http_client(config, url);

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr)
    {
        file.close();
        out_error = "Create download client failed";
        return false;
    }

    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK)
    {
        if (esp_http_client_fetch_headers(client) >= 0)
        {
            const int status_code = esp_http_client_get_status_code(client);
            if (status_code < 200 || status_code >= 300)
            {
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "Download HTTP %d", status_code);
                out_error = buffer;
            }
            else
            {
                std::uint8_t buffer[kHttpBufferSize];
                while (true)
                {
                    const int read = esp_http_client_read(client,
                                                          reinterpret_cast<char*>(buffer),
                                                          sizeof(buffer));
                    if (read < 0)
                    {
                        out_error = "Download read failed";
                        break;
                    }
                    if (read == 0)
                    {
                        ok = true;
                        break;
                    }
                    if (!file.write(buffer, static_cast<std::size_t>(read)))
                    {
                        out_error = "Write download file failed";
                        break;
                    }
                }
            }
        }
        else
        {
            out_error = "Fetch download headers failed";
        }
    }
    else
    {
        log_pack_memory_snapshot("download open failed");
        out_error = "Open download request failed";
    }

    file.close();
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!ok)
    {
        (void)remove_file_if_exists(logical_path);
    }
    return ok;
}

std::string sha256_hex_of_bytes(const std::uint8_t* data, std::size_t len)
{
    unsigned char hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, data, len);
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

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
    std::vector<std::uint8_t> data;
    if (!read_binary_file(logical_path, data))
    {
        out_hex.clear();
        return false;
    }
    out_hex = sha256_hex_of_bytes(data.data(), data.size());
    return true;
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

    std::string text;
    if (!read_text_file(index_path, text))
    {
        out_error = "Read installed index failed";
        return false;
    }

    cJSON* root = parse_json_document(text);
    if (!root)
    {
        out_error = "Parse installed index failed";
        return false;
    }

    cJSON* packages = cJSON_GetObjectItemCaseSensitive(root, "packages");
    if (cJSON_IsArray(packages))
    {
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, packages)
        {
            InstalledPackageRecord record{};
            record.id = json_string(item, "id");
            record.version = json_string(item, "version");
            record.archive_sha256 = json_string(item, "archive_sha256");
            record.storage = json_string(item, "storage");
            if (record.storage.empty())
            {
                record.storage = default_storage ? default_storage : default_storage_for_index_path(index_path);
            }
            record.installed_at_epoch = json_u64(item, "installed_at_epoch");
            if (!record.id.empty())
            {
                std::printf("[Packs][Index] record path=%s id=%s version=%s storage=%s sha=%s\n",
                            index_path,
                            record.id.c_str(),
                            record.version.c_str(),
                            record.storage.c_str(),
                            record.archive_sha256.empty() ? "<none>" : record.archive_sha256.c_str());
                out_index.packages.push_back(std::move(record));
            }
        }
    }

    cJSON_Delete(root);
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

    std::vector<std::uint8_t> central(central_size);
    if (!file.read_at(central_offset, central.data(), central.size()))
    {
        out_error = "Read zip central directory failed";
        return false;
    }

    std::size_t offset = 0;
    for (std::uint16_t entry_index = 0; entry_index < entry_count; ++entry_index)
    {
        if (offset + 46 > central.size() || read_le32(&central[offset]) != kZipCentralSignature)
        {
            out_error = "Zip central entry invalid";
            return false;
        }

        const std::uint16_t method = read_le16(&central[offset + 10]);
        const std::uint32_t compressed_size = read_le32(&central[offset + 20]);
        const std::uint32_t uncompressed_size = read_le32(&central[offset + 24]);
        const std::uint16_t name_len = read_le16(&central[offset + 28]);
        const std::uint16_t extra_len = read_le16(&central[offset + 30]);
        const std::uint16_t comment_len = read_le16(&central[offset + 32]);
        const std::uint32_t local_offset = read_le32(&central[offset + 42]);
        if (offset + 46 + name_len + extra_len + comment_len > central.size())
        {
            out_error = "Zip central entry truncated";
            return false;
        }

        ZipEntry entry{};
        entry.name.assign(reinterpret_cast<const char*>(&central[offset + 46]), name_len);
        entry.method = method;
        entry.compressed_size = compressed_size;
        entry.uncompressed_size = uncompressed_size;
        entry.local_header_offset = local_offset;
        out_entries.push_back(std::move(entry));

        offset += 46 + name_len + extra_len + comment_len;
    }

    return true;
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

        std::vector<std::uint8_t> compressed(entry.compressed_size);
        if (!compressed.empty() &&
            !file.read_at(data_offset, compressed.data(), compressed.size()))
        {
            out_error = "Read zip entry data failed";
            file.close();
            return false;
        }

        std::vector<std::uint8_t> output;
        if (entry.method == 0)
        {
            output = std::move(compressed);
        }
        else if (entry.method == 8)
        {
            output.resize(entry.uncompressed_size);
            const std::size_t actual =
                tinfl_decompress_mem_to_mem(output.data(),
                                            output.size(),
                                            compressed.data(),
                                            compressed.size(),
                                            0);
            if (actual == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || actual != output.size())
            {
                out_error = "Inflate zip entry failed";
                file.close();
                return false;
            }
        }
        else
        {
            out_error = "Unsupported zip compression";
            file.close();
            return false;
        }

        if (!write_binary_file(logical_target, output.data(), output.size()))
        {
            out_error = std::string("Write extracted payload failed: ") + logical_target;
            file.close();
            return false;
        }

        ++payload_files;
        payload_bytes += output.size();
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
    return platform::ui::device::card_ready();
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
    if (!http_get_text(kCatalogUrl, text, out_error))
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

bool install_package(const PackageRecord& package, std::string& out_error)
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
    if (!http_download_file(package.download_url, temp_zip_path, out_error))
    {
        std::printf("[Packs][Install] download failed error=%s\n", out_error.c_str());
        return false;
    }
    log_path_probe("temp_zip.after_download", temp_zip_path);

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
    std::printf("[Packs][Install] reload_language id=%s\n", package.id.c_str());
    ::ui::i18n::reload_language();
    std::printf("[Packs][Install] complete id=%s\n", package.id.c_str());
    return true;
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

bool uninstall_package(const PackageRecord& package, std::string& out_error)
{
    (void)package;
    out_error = "Pack installation is unsupported on this platform";
    return false;
}

} // namespace ui::runtime::packs

#endif
