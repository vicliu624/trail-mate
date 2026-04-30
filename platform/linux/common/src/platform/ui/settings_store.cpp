#include "platform/ui/settings_store.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace platform::ui::settings_store
{
namespace
{

enum class ValueKind : char
{
    Int = 'i',
    Bool = 'b',
    Uint = 'u',
    String = 's',
    Blob = 'x',
};

struct StoredValue
{
    ValueKind kind = ValueKind::Int;
    std::string payload{};
};

using NamespaceMap = std::unordered_map<std::string, StoredValue>;

std::mutex s_store_mutex;

std::string sanitize_component(const char* value)
{
    if (!value || value[0] == '\0')
    {
        return "default";
    }

    std::string out;
    out.reserve(std::strlen(value));
    for (const unsigned char ch : std::string(value))
    {
        if (std::isalnum(ch) || ch == '_' || ch == '-')
        {
            out.push_back(static_cast<char>(ch));
        }
        else
        {
            out.push_back('_');
        }
    }
    return out.empty() ? "default" : out;
}

std::filesystem::path storage_root()
{
    if (const char* configured = std::getenv("TRAIL_MATE_SETTINGS_ROOT"))
    {
        if (configured[0] != '\0')
        {
            return std::filesystem::path(configured);
        }
    }

#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"))
    {
        if (appdata[0] != '\0')
        {
            return std::filesystem::path(appdata) / "TrailMateCardputerZero";
        }
    }
#endif

    if (const char* home = std::getenv("HOME"))
    {
        if (home[0] != '\0')
        {
            return std::filesystem::path(home) / ".trailmate_cardputer_zero";
        }
    }

    return std::filesystem::current_path() / ".trailmate_cardputer_zero";
}

std::filesystem::path namespace_path(const char* ns)
{
    return storage_root() / "settings" / (sanitize_component(ns) + ".kv");
}

bool ensure_parent_directory(const std::filesystem::path& file_path)
{
    std::error_code ec;
    std::filesystem::create_directories(file_path.parent_path(), ec);
    return !ec;
}

std::string hex_encode(const uint8_t* data, std::size_t len)
{
    static constexpr char kHex[] = "0123456789ABCDEF";

    std::string out;
    out.reserve(len * 2U);
    for (std::size_t index = 0; index < len; ++index)
    {
        const uint8_t value = data[index];
        out.push_back(kHex[(value >> 4) & 0x0F]);
        out.push_back(kHex[value & 0x0F]);
    }
    return out;
}

bool decode_hex_nibble(char ch, uint8_t* out)
{
    if (!out)
    {
        return false;
    }
    if (ch >= '0' && ch <= '9')
    {
        *out = static_cast<uint8_t>(ch - '0');
        return true;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        *out = static_cast<uint8_t>(10 + (ch - 'A'));
        return true;
    }
    if (ch >= 'a' && ch <= 'f')
    {
        *out = static_cast<uint8_t>(10 + (ch - 'a'));
        return true;
    }
    return false;
}

bool hex_decode(const std::string& hex, std::vector<uint8_t>* out)
{
    if (!out || (hex.size() % 2U) != 0U)
    {
        return false;
    }

    out->clear();
    out->reserve(hex.size() / 2U);
    for (std::size_t index = 0; index < hex.size(); index += 2U)
    {
        uint8_t high = 0;
        uint8_t low = 0;
        if (!decode_hex_nibble(hex[index], &high) || !decode_hex_nibble(hex[index + 1U], &low))
        {
            out->clear();
            return false;
        }
        out->push_back(static_cast<uint8_t>((high << 4) | low));
    }
    return true;
}

NamespaceMap load_namespace_map(const char* ns)
{
    NamespaceMap map;

    const std::filesystem::path path = namespace_path(ns);
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        return map;
    }

    std::string line;
    while (std::getline(stream, line))
    {
        const std::size_t first = line.find('|');
        const std::size_t second = (first == std::string::npos) ? std::string::npos : line.find('|', first + 1U);
        if (first == std::string::npos || second == std::string::npos || first == 0U || second <= first + 1U)
        {
            continue;
        }

        StoredValue value{};
        value.kind = static_cast<ValueKind>(line[first + 1U]);
        value.payload = line.substr(second + 1U);
        map[line.substr(0, first)] = std::move(value);
    }

    return map;
}

bool save_namespace_map(const char* ns, const NamespaceMap& map)
{
    const std::filesystem::path path = namespace_path(ns);
    if (!ensure_parent_directory(path))
    {
        return false;
    }

    if (map.empty())
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return true;
    }

    const std::filesystem::path temp_path = path.string() + ".tmp";
    {
        std::ofstream stream(temp_path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            return false;
        }

        std::vector<std::string> keys;
        keys.reserve(map.size());
        for (const auto& entry : map)
        {
            keys.push_back(entry.first);
        }
        std::sort(keys.begin(), keys.end());

        for (const auto& key : keys)
        {
            const auto found = map.find(key);
            if (found == map.end())
            {
                continue;
            }
            stream << key << "|" << static_cast<char>(found->second.kind) << "|" << found->second.payload << "\n";
        }
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temp_path, path, ec);
    if (ec)
    {
        std::filesystem::remove(temp_path, ec);
        return false;
    }

    return true;
}

bool parse_int_value(const std::string& payload, int* out)
{
    if (!out)
    {
        return false;
    }
    try
    {
        *out = std::stoi(payload);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool parse_uint_value(const std::string& payload, uint32_t* out)
{
    if (!out)
    {
        return false;
    }
    try
    {
        *out = static_cast<uint32_t>(std::stoul(payload));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void upsert_numeric_value(const char* ns, const char* key, ValueKind kind, const std::string& payload)
{
    if (!ns || !key || key[0] == '\0')
    {
        return;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    NamespaceMap map = load_namespace_map(ns);
    map[std::string(key)] = StoredValue{kind, payload};
    (void)save_namespace_map(ns, map);
}

} // namespace

void put_int(const char* ns, const char* key, int value)
{
    upsert_numeric_value(ns, key, ValueKind::Int, std::to_string(value));
}

void put_bool(const char* ns, const char* key, bool value)
{
    upsert_numeric_value(ns, key, ValueKind::Bool, value ? "1" : "0");
}

void put_uint(const char* ns, const char* key, uint32_t value)
{
    upsert_numeric_value(ns, key, ValueKind::Uint, std::to_string(value));
}

bool put_string(const char* ns, const char* key, const char* value)
{
    if (!ns || !key || !value)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    NamespaceMap map = load_namespace_map(ns);
    if (value[0] == '\0')
    {
        map.erase(std::string(key));
    }
    else
    {
        const auto encoded = hex_encode(reinterpret_cast<const uint8_t*>(value), std::strlen(value));
        map[std::string(key)] = StoredValue{ValueKind::String, encoded};
    }
    return save_namespace_map(ns, map);
}

bool put_blob(const char* ns, const char* key, const void* data, std::size_t len)
{
    if (!ns || !key)
    {
        return false;
    }
    if (len > 0U && data == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    NamespaceMap map = load_namespace_map(ns);
    if (len == 0U)
    {
        map.erase(std::string(key));
    }
    else
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        map[std::string(key)] = StoredValue{ValueKind::Blob, hex_encode(bytes, len)};
    }
    return save_namespace_map(ns, map);
}

int get_int(const char* ns, const char* key, int default_value)
{
    if (!ns || !key)
    {
        return default_value;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    const NamespaceMap map = load_namespace_map(ns);
    const auto found = map.find(key);
    if (found == map.end() || found->second.kind != ValueKind::Int)
    {
        return default_value;
    }

    int parsed = default_value;
    return parse_int_value(found->second.payload, &parsed) ? parsed : default_value;
}

bool get_bool(const char* ns, const char* key, bool default_value)
{
    if (!ns || !key)
    {
        return default_value;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    const NamespaceMap map = load_namespace_map(ns);
    const auto found = map.find(key);
    if (found == map.end() || found->second.kind != ValueKind::Bool)
    {
        return default_value;
    }
    return found->second.payload == "1";
}

uint32_t get_uint(const char* ns, const char* key, uint32_t default_value)
{
    if (!ns || !key)
    {
        return default_value;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    const NamespaceMap map = load_namespace_map(ns);
    const auto found = map.find(key);
    if (found == map.end() || found->second.kind != ValueKind::Uint)
    {
        return default_value;
    }

    uint32_t parsed = default_value;
    return parse_uint_value(found->second.payload, &parsed) ? parsed : default_value;
}

bool get_string(const char* ns, const char* key, std::string& out)
{
    if (!ns || !key)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    const NamespaceMap map = load_namespace_map(ns);
    const auto found = map.find(key);
    if (found == map.end() || found->second.kind != ValueKind::String)
    {
        return false;
    }

    std::vector<uint8_t> decoded;
    if (!hex_decode(found->second.payload, &decoded))
    {
        return false;
    }

    out.assign(decoded.begin(), decoded.end());
    return true;
}

bool get_blob(const char* ns, const char* key, std::vector<uint8_t>& out)
{
    if (!ns || !key)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    const NamespaceMap map = load_namespace_map(ns);
    const auto found = map.find(key);
    if (found == map.end() || found->second.kind != ValueKind::Blob)
    {
        return false;
    }

    return hex_decode(found->second.payload, &out);
}

void remove_keys(const char* ns, const char* const* keys, std::size_t key_count)
{
    if (!ns || !keys || key_count == 0U)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    NamespaceMap map = load_namespace_map(ns);
    for (std::size_t index = 0; index < key_count; ++index)
    {
        if (keys[index] && keys[index][0] != '\0')
        {
            map.erase(std::string(keys[index]));
        }
    }
    (void)save_namespace_map(ns, map);
}

void clear_namespace(const char* ns)
{
    if (!ns)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    std::error_code ec;
    std::filesystem::remove(namespace_path(ns), ec);
}

} // namespace platform::ui::settings_store
