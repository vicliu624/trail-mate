#include "ui/runtime/pack_repository.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "platform/ui/device_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "ui/localization.h"
#include "ui/runtime/memory_profile.h"

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
#include "rom/miniz.h"

#define UI_PACKS_HAVE_CRT_BUNDLE 1

extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

#if defined(ARDUINO)
#include <Arduino.h>
#include <SD.h>
#else
#include "platform/esp/idf_common/bsp_runtime.h"
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace ui::runtime::packs
{
namespace
{

constexpr const char* kCatalogUrl = "https://vicliu624.github.io/trail-mate/data/packs.json";
constexpr const char* kCatalogBaseUrl = "https://vicliu624.github.io/trail-mate";
constexpr const char* kPackRoot = "/trailmate/packs";
constexpr const char* kIndexDir = "/trailmate/packs/.index";
constexpr const char* kTempDir = "/trailmate/packs/.index/tmp";
constexpr const char* kInstalledIndexPath = "/trailmate/packs/.index/installed.json";
constexpr int kHttpBufferSize = 1024;
constexpr int kHttpTxBufferSize = 512;
constexpr std::size_t kTlsLargeAllocThresholdBytes = 4096;
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
};

ParsedVersion parse_version(const std::string& text)
{
    ParsedVersion version{};
    if (text.empty())
    {
        return version;
    }

    std::string numeric = text;
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
            parts[part_index] = std::atoi(token.c_str());
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

bool ensure_dir_recursive(const std::string& logical_dir)
{
    if (logical_dir.empty())
    {
        return false;
    }

    std::string current;
    for (char ch : logical_dir)
    {
        current.push_back(ch);
        if (ch != '/')
        {
            continue;
        }
        if (!current.empty() && !SD.exists(current.c_str()))
        {
            if (!SD.mkdir(current.c_str()))
            {
                return false;
            }
        }
    }

    if (!SD.exists(logical_dir.c_str()))
    {
        return SD.mkdir(logical_dir.c_str());
    }
    return true;
}

bool logical_file_exists(const std::string& logical_path)
{
    return SD.exists(logical_path.c_str());
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

    if (SD.exists(logical_path.c_str()))
    {
        (void)SD.remove(logical_path.c_str());
    }

    File file = SD.open(logical_path.c_str(), FILE_WRITE);
    if (!file)
    {
        return false;
    }

    const std::size_t written = file.write(static_cast<const std::uint8_t*>(data), len);
    file.close();
    return written == len;
}

bool write_text_file(const std::string& logical_path, const std::string& text)
{
    return write_binary_file(logical_path, text.data(), text.size());
}

bool read_binary_file(const std::string& logical_path, std::vector<std::uint8_t>& out)
{
    out.clear();
    File file = SD.open(logical_path.c_str(), FILE_READ);
    if (!file)
    {
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

bool remove_file_if_exists(const std::string& logical_path)
{
    if (!SD.exists(logical_path.c_str()))
    {
        return true;
    }
    return SD.remove(logical_path.c_str());
}

class RandomAccessFile
{
  public:
    bool open(const std::string& logical_path)
    {
        file_ = SD.open(logical_path.c_str(), FILE_READ);
        return static_cast<bool>(file_);
    }

    void close()
    {
        if (file_)
        {
            file_.close();
        }
    }

    std::size_t size() const
    {
        return file_ ? static_cast<std::size_t>(file_.size()) : 0;
    }

    bool read_at(std::size_t offset, void* out, std::size_t len)
    {
        if (!file_ || !file_.seek(offset))
        {
            return false;
        }
        return static_cast<std::size_t>(file_.read(static_cast<std::uint8_t*>(out), len)) == len;
    }

  private:
    File file_{};
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
        if (SD.exists(logical_path.c_str()))
        {
            (void)SD.remove(logical_path.c_str());
        }
        file_ = SD.open(logical_path.c_str(), FILE_WRITE);
        return static_cast<bool>(file_);
    }

    bool write(const void* data, std::size_t len)
    {
        return file_ && static_cast<std::size_t>(file_.write(static_cast<const std::uint8_t*>(data), len)) == len;
    }

    void close()
    {
        if (file_)
        {
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
    const std::size_t written = std::fwrite(data, 1, len, file);
    std::fclose(file);
    return written == len;
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

bool remove_file_if_exists(const std::string& logical_path)
{
    if (!logical_file_exists(logical_path))
    {
        return true;
    }
    return std::remove(host_path(logical_path).c_str()) == 0;
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
    mbedtls_sha256_starts_ret(&ctx, 0);
    mbedtls_sha256_update_ret(&ctx, data, len);
    mbedtls_sha256_finish_ret(&ctx, hash);
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
    return compare_versions(platform::ui::device::firmware_version(),
                            package.min_firmware_version) >= 0;
}

bool load_installed_index(InstalledIndex& out_index, std::string& out_error)
{
    out_index = InstalledIndex{};
    out_error.clear();

    if (!logical_file_exists(kInstalledIndexPath))
    {
        return true;
    }

    std::string text;
    if (!read_text_file(kInstalledIndexPath, text))
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
            record.installed_at_epoch = json_u64(item, "installed_at_epoch");
            if (!record.id.empty())
            {
                out_index.packages.push_back(std::move(record));
            }
        }
    }

    cJSON_Delete(root);
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
            out_error = "Write extracted payload failed";
            file.close();
            return false;
        }
    }

    file.close();
    return true;
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
    return platform::ui::device::card_ready();
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

    if (package.id.empty() || package.download_url.empty())
    {
        out_error = "Package metadata is incomplete";
        return false;
    }

    const platform::ui::wifi::Status wifi_status = platform::ui::wifi::status();
    if (!wifi_status.connected)
    {
        out_error = "Wi-Fi is not connected";
        return false;
    }
    if (!platform::ui::device::card_ready())
    {
        out_error = "SD card is not ready";
        return false;
    }

    if (!ensure_dir_recursive(kPackRoot) ||
        !ensure_dir_recursive(kIndexDir) ||
        !ensure_dir_recursive(kTempDir))
    {
        out_error = "Create pack directories failed";
        return false;
    }

    const std::string temp_zip_path = std::string(kTempDir) + "/" + package.id + "-" + package.version + ".zip";
    if (!http_download_file(package.download_url, temp_zip_path, out_error))
    {
        return false;
    }

    std::string archive_sha256;
    if (!sha256_file(temp_zip_path, archive_sha256))
    {
        (void)remove_file_if_exists(temp_zip_path);
        out_error = "Hash downloaded archive failed";
        return false;
    }
    if (!package.archive_sha256.empty() &&
        lowercase_ascii(archive_sha256) != lowercase_ascii(package.archive_sha256))
    {
        (void)remove_file_if_exists(temp_zip_path);
        out_error = "Archive SHA256 mismatch";
        return false;
    }

    if (!extract_zip_payload(temp_zip_path, out_error))
    {
        (void)remove_file_if_exists(temp_zip_path);
        return false;
    }

    InstalledIndex index;
    std::string index_error;
    if (!load_installed_index(index, index_error))
    {
        (void)remove_file_if_exists(temp_zip_path);
        out_error = index_error;
        return false;
    }

    bool updated = false;
    for (InstalledPackageRecord& record : index.packages)
    {
        if (record.id != package.id)
        {
            continue;
        }
        record.version = package.version;
        record.archive_sha256 = archive_sha256;
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
        record.installed_at_epoch = static_cast<std::uint64_t>(std::time(nullptr));
        index.packages.push_back(std::move(record));
    }

    if (!save_installed_index(index, out_error))
    {
        (void)remove_file_if_exists(temp_zip_path);
        return false;
    }

    (void)remove_file_if_exists(temp_zip_path);
    ::ui::i18n::reload_language();
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

} // namespace ui::runtime::packs

#endif
