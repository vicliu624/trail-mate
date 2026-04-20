#include "platform/nrf52/arduino_common/internal_fs_utils.h"
#include "platform/ui/settings_store.h"

#include <Arduino.h>
#include <InternalFileSystem.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{

using Adafruit_LittleFS_Namespace::FILE_O_READ;

constexpr const char* kSettingsPath = "/ui_settings.bin";
constexpr const char* kLogTag = "[nrf52][ui_settings]";
constexpr uint32_t kMagic = 0x55535447UL; // USTG
constexpr uint16_t kVersion = 1;

enum class ValueType : uint8_t
{
    Int = 1,
    Bool = 2,
    Uint = 3,
    Blob = 4,
    String = 5,
};

struct FileHeader
{
    uint32_t magic = kMagic;
    uint16_t version = kVersion;
    uint16_t reserved = 0;
    uint32_t record_count = 0;
} __attribute__((packed));

struct RecordHeader
{
    uint8_t type = 0;
    uint8_t reserved = 0;
    uint16_t key_len = 0;
    uint32_t value_len = 0;
} __attribute__((packed));

std::string makeScopedKey(const char* ns, const char* key)
{
    const char* scope = ns ? ns : "";
    const char* name = key ? key : "";
    return std::string(scope) + ":" + name;
}

std::map<std::string, int>& intStore()
{
    static std::map<std::string, int> store;
    return store;
}

std::map<std::string, bool>& boolStore()
{
    static std::map<std::string, bool> store;
    return store;
}

std::map<std::string, uint32_t>& uintStore()
{
    static std::map<std::string, uint32_t> store;
    return store;
}

std::map<std::string, std::vector<uint8_t>>& blobStore()
{
    static std::map<std::string, std::vector<uint8_t>> store;
    return store;
}

std::map<std::string, std::string>& stringStore()
{
    static std::map<std::string, std::string> store;
    return store;
}

bool& loadedFlag()
{
    static bool loaded = false;
    return loaded;
}

void clearAllStores()
{
    intStore().clear();
    boolStore().clear();
    uintStore().clear();
    blobStore().clear();
    stringStore().clear();
}

template <typename T>
bool writePod(Adafruit_LittleFS_Namespace::File& file, const T& value)
{
    return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

bool writeBytes(Adafruit_LittleFS_Namespace::File& file, const uint8_t* data, std::size_t len)
{
    if (!data && len != 0)
    {
        return false;
    }

    constexpr std::size_t kChunkSize = 128;
    std::size_t offset = 0;
    while (offset < len)
    {
        const std::size_t chunk = std::min(kChunkSize, len - offset);
        const std::size_t written = file.write(data + offset, chunk);
        if (written != chunk)
        {
            return false;
        }
        offset += written;
    }
    return true;
}

template <typename T>
bool readPod(Adafruit_LittleFS_Namespace::File& file, T* value)
{
    return value && file.read(value, sizeof(T)) == sizeof(T);
}

bool readBytes(Adafruit_LittleFS_Namespace::File& file, uint8_t* data, std::size_t len)
{
    if (!data && len != 0)
    {
        return false;
    }

    constexpr std::size_t kChunkSize = 128;
    std::size_t offset = 0;
    while (offset < len)
    {
        const std::size_t chunk = std::min(kChunkSize, len - offset);
        const int read = file.read(data + offset, static_cast<uint32_t>(chunk));
        if (read != static_cast<int>(chunk))
        {
            return false;
        }
        offset += static_cast<std::size_t>(read);
    }
    return true;
}

bool writeRecordHeader(Adafruit_LittleFS_Namespace::File& file,
                       ValueType type,
                       const std::string& key,
                       uint32_t value_len)
{
    RecordHeader header{};
    header.type = static_cast<uint8_t>(type);
    header.key_len = static_cast<uint16_t>(key.size());
    header.value_len = value_len;
    return writePod(file, header) &&
           (key.empty() || writeBytes(file, reinterpret_cast<const uint8_t*>(key.data()), key.size()));
}

bool saveToFs()
{
    if (!::platform::nrf52::arduino_common::internal_fs::ensureMounted(false, kLogTag))
    {
        return false;
    }

    Adafruit_LittleFS_Namespace::File file(InternalFS);
    if (!::platform::nrf52::arduino_common::internal_fs::openForOverwrite(kSettingsPath, &file, false, kLogTag))
    {
        Serial.printf("%s open failed path=%s\n", kLogTag, kSettingsPath);
        return false;
    }
    if (!::platform::nrf52::arduino_common::internal_fs::rewindForOverwrite(file))
    {
        file.close();
        Serial.printf("%s seek failed path=%s\n", kLogTag, kSettingsPath);
        return false;
    }

    const uint32_t record_count = static_cast<uint32_t>(
        intStore().size() + boolStore().size() + uintStore().size() + blobStore().size() + stringStore().size());
    uint32_t final_size = static_cast<uint32_t>(sizeof(FileHeader));
    FileHeader header{};
    header.record_count = record_count;
    if (!writePod(file, header))
    {
        file.close();
        Serial.printf("%s header write failed records=%lu\n",
                      kLogTag,
                      static_cast<unsigned long>(record_count));
        return false;
    }

    for (const auto& entry : intStore())
    {
        const int32_t value = entry.second;
        if (!writeRecordHeader(file, ValueType::Int, entry.first, sizeof(value)) ||
            !writeBytes(file, reinterpret_cast<const uint8_t*>(&value), sizeof(value)))
        {
            file.close();
            Serial.printf("%s int write failed key=%s\n", kLogTag, entry.first.c_str());
            return false;
        }
        final_size += static_cast<uint32_t>(sizeof(RecordHeader) + entry.first.size() + sizeof(value));
    }

    for (const auto& entry : boolStore())
    {
        const uint8_t value = entry.second ? 1U : 0U;
        if (!writeRecordHeader(file, ValueType::Bool, entry.first, sizeof(value)) ||
            !writeBytes(file, &value, sizeof(value)))
        {
            file.close();
            Serial.printf("%s bool write failed key=%s\n", kLogTag, entry.first.c_str());
            return false;
        }
        final_size += static_cast<uint32_t>(sizeof(RecordHeader) + entry.first.size() + sizeof(value));
    }

    for (const auto& entry : uintStore())
    {
        const uint32_t value = entry.second;
        if (!writeRecordHeader(file, ValueType::Uint, entry.first, sizeof(value)) ||
            !writeBytes(file, reinterpret_cast<const uint8_t*>(&value), sizeof(value)))
        {
            file.close();
            Serial.printf("%s uint write failed key=%s\n", kLogTag, entry.first.c_str());
            return false;
        }
        final_size += static_cast<uint32_t>(sizeof(RecordHeader) + entry.first.size() + sizeof(value));
    }

    for (const auto& entry : blobStore())
    {
        if (!writeRecordHeader(file, ValueType::Blob, entry.first, static_cast<uint32_t>(entry.second.size())) ||
            (!entry.second.empty() && !writeBytes(file, entry.second.data(), entry.second.size())))
        {
            file.close();
            Serial.printf("%s blob write failed key=%s len=%lu\n",
                          kLogTag,
                          entry.first.c_str(),
                          static_cast<unsigned long>(entry.second.size()));
            return false;
        }
        final_size += static_cast<uint32_t>(sizeof(RecordHeader) + entry.first.size() + entry.second.size());
    }

    for (const auto& entry : stringStore())
    {
        if (!writeRecordHeader(file, ValueType::String, entry.first, static_cast<uint32_t>(entry.second.size())) ||
            (!entry.second.empty() &&
             !writeBytes(file,
                         reinterpret_cast<const uint8_t*>(entry.second.data()),
                         entry.second.size())))
        {
            file.close();
            Serial.printf("%s string write failed key=%s len=%lu\n",
                          kLogTag,
                          entry.first.c_str(),
                          static_cast<unsigned long>(entry.second.size()));
            return false;
        }
        final_size += static_cast<uint32_t>(sizeof(RecordHeader) + entry.first.size() + entry.second.size());
    }

    const bool trunc_ok = ::platform::nrf52::arduino_common::internal_fs::truncateAfterWrite(file, final_size);
    file.flush();
    file.close();

    if (!trunc_ok)
    {
        Serial.printf("%s truncate failed path=%s size=%lu\n",
                      kLogTag,
                      kSettingsPath,
                      static_cast<unsigned long>(final_size));
    }
    return trunc_ok;
}

void ensureLoaded()
{
    if (loadedFlag())
    {
        return;
    }

    clearAllStores();
    if (!::platform::nrf52::arduino_common::internal_fs::ensureMounted(false, kLogTag))
    {
        return;
    }
    loadedFlag() = true;

    if (!InternalFS.exists(kSettingsPath))
    {
        return;
    }

    auto file = InternalFS.open(kSettingsPath, FILE_O_READ);
    if (!file)
    {
        return;
    }

    FileHeader header{};
    if (!readPod(file, &header) || header.magic != kMagic || header.version != kVersion)
    {
        file.close();
        return;
    }

    std::map<std::string, int> loaded_ints;
    std::map<std::string, bool> loaded_bools;
    std::map<std::string, uint32_t> loaded_uints;
    std::map<std::string, std::vector<uint8_t>> loaded_blobs;
    std::map<std::string, std::string> loaded_strings;

    for (uint32_t i = 0; i < header.record_count; ++i)
    {
        RecordHeader rec{};
        if (!readPod(file, &rec))
        {
            file.close();
            return;
        }

        std::string key(rec.key_len, '\0');
        if (rec.key_len > 0 &&
            !readBytes(file, reinterpret_cast<uint8_t*>(&key[0]), rec.key_len))
        {
            file.close();
            return;
        }

        switch (static_cast<ValueType>(rec.type))
        {
        case ValueType::Int:
        {
            int32_t value = 0;
            if (rec.value_len != sizeof(value) || !readPod(file, &value))
            {
                file.close();
                return;
            }
            loaded_ints[key] = value;
            break;
        }
        case ValueType::Bool:
        {
            uint8_t value = 0;
            if (rec.value_len != sizeof(value) || !readPod(file, &value))
            {
                file.close();
                return;
            }
            loaded_bools[key] = (value != 0);
            break;
        }
        case ValueType::Uint:
        {
            uint32_t value = 0;
            if (rec.value_len != sizeof(value) || !readPod(file, &value))
            {
                file.close();
                return;
            }
            loaded_uints[key] = value;
            break;
        }
        case ValueType::Blob:
        {
            std::vector<uint8_t> value(rec.value_len, 0);
            if (rec.value_len > 0 && !readBytes(file, value.data(), rec.value_len))
            {
                file.close();
                return;
            }
            loaded_blobs[key] = std::move(value);
            break;
        }
        case ValueType::String:
        {
            std::string value(rec.value_len, '\0');
            if (rec.value_len > 0 &&
                !readBytes(file, reinterpret_cast<uint8_t*>(&value[0]), rec.value_len))
            {
                file.close();
                return;
            }
            loaded_strings[key] = std::move(value);
            break;
        }
        default:
        {
            std::vector<uint8_t> skip(rec.value_len, 0);
            if (rec.value_len > 0 && !readBytes(file, skip.data(), rec.value_len))
            {
                file.close();
                return;
            }
            break;
        }
        }
    }

    file.close();
    intStore() = std::move(loaded_ints);
    boolStore() = std::move(loaded_bools);
    uintStore() = std::move(loaded_uints);
    blobStore() = std::move(loaded_blobs);
    stringStore() = std::move(loaded_strings);
}

} // namespace

namespace platform::ui::settings_store
{

void put_int(const char* ns, const char* key, int value)
{
    if (!key)
    {
        return;
    }
    ensureLoaded();
    intStore()[makeScopedKey(ns, key)] = value;
    (void)saveToFs();
}

void put_bool(const char* ns, const char* key, bool value)
{
    if (!key)
    {
        return;
    }
    ensureLoaded();
    boolStore()[makeScopedKey(ns, key)] = value;
    (void)saveToFs();
}

void put_uint(const char* ns, const char* key, uint32_t value)
{
    if (!key)
    {
        return;
    }
    ensureLoaded();
    uintStore()[makeScopedKey(ns, key)] = value;
    (void)saveToFs();
}

bool put_string(const char* ns, const char* key, const char* value)
{
    if (!key || !value)
    {
        return false;
    }
    ensureLoaded();
    const std::string scoped = makeScopedKey(ns, key);
    if (value[0] == '\0')
    {
        stringStore().erase(scoped);
    }
    else
    {
        stringStore()[scoped] = value;
    }
    return saveToFs();
}

bool put_blob(const char* ns, const char* key, const void* data, std::size_t len)
{
    if (!key || (!data && len != 0))
    {
        return false;
    }
    ensureLoaded();
    auto& blob = blobStore()[makeScopedKey(ns, key)];
    if (len == 0)
    {
        blob.clear();
    }
    else
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        blob.assign(bytes, bytes + len);
    }
    return saveToFs();
}

int get_int(const char* ns, const char* key, int default_value)
{
    if (!key)
    {
        return default_value;
    }
    ensureLoaded();
    const auto it = intStore().find(makeScopedKey(ns, key));
    return it == intStore().end() ? default_value : it->second;
}

bool get_bool(const char* ns, const char* key, bool default_value)
{
    if (!key)
    {
        return default_value;
    }
    ensureLoaded();
    const auto it = boolStore().find(makeScopedKey(ns, key));
    return it == boolStore().end() ? default_value : it->second;
}

uint32_t get_uint(const char* ns, const char* key, uint32_t default_value)
{
    if (!key)
    {
        return default_value;
    }
    ensureLoaded();
    const auto it = uintStore().find(makeScopedKey(ns, key));
    return it == uintStore().end() ? default_value : it->second;
}

bool get_string(const char* ns, const char* key, std::string& out)
{
    out.clear();
    if (!key)
    {
        return false;
    }
    ensureLoaded();
    const auto it = stringStore().find(makeScopedKey(ns, key));
    if (it == stringStore().end())
    {
        return false;
    }
    out = it->second;
    return true;
}

bool get_blob(const char* ns, const char* key, std::vector<uint8_t>& out)
{
    out.clear();
    if (!key)
    {
        return false;
    }
    ensureLoaded();
    const auto it = blobStore().find(makeScopedKey(ns, key));
    if (it == blobStore().end())
    {
        return false;
    }
    out = it->second;
    return true;
}

void remove_keys(const char* ns, const char* const* keys, std::size_t key_count)
{
    if (!keys)
    {
        return;
    }
    ensureLoaded();
    for (std::size_t index = 0; index < key_count; ++index)
    {
        if (!keys[index])
        {
            continue;
        }
        const std::string scoped = makeScopedKey(ns, keys[index]);
        intStore().erase(scoped);
        boolStore().erase(scoped);
        uintStore().erase(scoped);
        blobStore().erase(scoped);
        stringStore().erase(scoped);
    }
    (void)saveToFs();
}

void clear_namespace(const char* ns)
{
    ensureLoaded();
    const std::string prefix = std::string(ns ? ns : "") + ":";

    for (auto it = intStore().begin(); it != intStore().end();)
    {
        it = (it->first.rfind(prefix, 0) == 0) ? intStore().erase(it) : std::next(it);
    }
    for (auto it = boolStore().begin(); it != boolStore().end();)
    {
        it = (it->first.rfind(prefix, 0) == 0) ? boolStore().erase(it) : std::next(it);
    }
    for (auto it = uintStore().begin(); it != uintStore().end();)
    {
        it = (it->first.rfind(prefix, 0) == 0) ? uintStore().erase(it) : std::next(it);
    }
    for (auto it = blobStore().begin(); it != blobStore().end();)
    {
        it = (it->first.rfind(prefix, 0) == 0) ? blobStore().erase(it) : std::next(it);
    }
    for (auto it = stringStore().begin(); it != stringStore().end();)
    {
        it = (it->first.rfind(prefix, 0) == 0) ? stringStore().erase(it) : std::next(it);
    }
    (void)saveToFs();
}

} // namespace platform::ui::settings_store
