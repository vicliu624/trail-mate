#include "platform/linux/remote_pack_repository.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <sqlite3.h>
#include <stdexcept>

namespace platform::linux_runtime
{
namespace
{
void require(bool ready, const char* message) { if (!ready) throw std::runtime_error(message); }
struct Database
{
    sqlite3* value = nullptr;
    Database() { require(sqlite3_open(":memory:", &value) == SQLITE_OK, "JSON parser unavailable"); sqlite3_limit(value, SQLITE_LIMIT_LENGTH, 2 * 1024 * 1024); }
    ~Database() { if (value) sqlite3_close(value); }
};
struct Query
{
    sqlite3_stmt* value = nullptr;
    Query(sqlite3* database, const char* sql, const std::string& json, const char* path = nullptr)
    {
        require(sqlite3_prepare_v2(database, sql, -1, &value, nullptr) == SQLITE_OK, "JSON parser unavailable");
        sqlite3_bind_text(value, 1, json.data(), static_cast<int>(json.size()), SQLITE_TRANSIENT);
        if (path) sqlite3_bind_text(value, 2, path, -1, SQLITE_TRANSIENT);
    }
    ~Query() { sqlite3_finalize(value); }
};
std::string text(sqlite3_stmt* query, int column, bool optional = false)
{
    if (optional && sqlite3_column_type(query, column) == SQLITE_NULL) return {};
    require(sqlite3_column_type(query, column) == SQLITE_TEXT, "Catalog string has the wrong type");
    const auto* data = reinterpret_cast<const char*>(sqlite3_column_text(query, column));
    std::string result(data, static_cast<size_t>(sqlite3_column_bytes(query, column)));
    require(result.find('\0') == result.npos, "Catalog string contains NUL");
    return result;
}
std::string field(sqlite3* database, const std::string& json, const char* path, bool optional = false)
{
    Query query(database, "SELECT json_extract(?1,?2)", json, path);
    require(sqlite3_step(query.value) == SQLITE_ROW, "Invalid catalog field");
    return text(query.value, 0, optional);
}
size_t number(sqlite3* database, const std::string& json, const char* path, bool optional = false)
{
    Query query(database, "SELECT json_extract(?1,?2)", json, path);
    require(sqlite3_step(query.value) == SQLITE_ROW, "Invalid catalog number");
    if (optional && sqlite3_column_type(query.value, 0) == SQLITE_NULL) return 0;
    const auto value = sqlite3_column_int64(query.value, 0);
    require(sqlite3_column_type(query.value, 0) == SQLITE_INTEGER && value >= 0 && uint64_t(value) <= std::numeric_limits<size_t>::max(), "Catalog number is out of range");
    return static_cast<size_t>(value);
}
std::vector<std::string> strings(sqlite3* database, const std::string& json, const char* path, bool objects = false)
{
    Query kind(database, "SELECT json_type(?1,?2)", json, path);
    require(sqlite3_step(kind.value) == SQLITE_ROW, "Invalid catalog array");
    if (sqlite3_column_type(kind.value, 0) == SQLITE_NULL) return {};
    require(text(kind.value, 0) == "array", "Catalog array has the wrong type");
    Query query(database, "SELECT value,type FROM json_each(?1,?2)", json, path);
    std::vector<std::string> output;
    int result;
    while ((result = sqlite3_step(query.value)) == SQLITE_ROW)
    {
        require(output.size() < 256, "Catalog array is too large");
        const auto value = text(query.value, 0);
        require(text(query.value, 1) == (objects ? "object" : "text"), "Invalid catalog array member");
        output.push_back(objects ? field(database, value, "$.id") : value);
        require(!output.back().empty() && output.back().size() <= 128, "Invalid catalog array identifier");
    }
    require(result == SQLITE_DONE, "Invalid catalog array data");
    return output;
}
bool identifier(const std::string& value)
{
    return !value.empty() && value.size() <= 128 && value != "." && value != ".." &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; });
}
bool web_url(const std::string& value)
{
    return value.size() <= 4096 && (value.starts_with("https://") || value.starts_with("http://")) &&
           value.find_first_of("\r\n\t") == value.npos;
}
} // namespace
bool parse_pack_catalog(const std::string& json, const std::string& base_url,
                        std::vector<::ui::runtime::packs::PackageRecord>& packages, std::string& error)
{
    packages.clear();
    error.clear();
    try
    {
        require(!json.empty() && json.size() < 2 * 1024 * 1024 && web_url(base_url), "Invalid catalog size or base URL");
        Database database;
        Query valid(database.value, "SELECT json_valid(?1),json_type(?1,'$.packages')", json);
        require(sqlite3_step(valid.value) == SQLITE_ROW && sqlite3_column_int(valid.value, 0) == 1 && text(valid.value, 1) == "array", "Invalid catalog JSON or packages array");
        require(number(database.value, json, "$.schema_version") == 1 && field(database.value, json, "$.archive_layout") == "trailmate-pack-v1", "Unsupported catalog schema/layout");
        Query entries(database.value, "SELECT value,type FROM json_each(?1,'$.packages')", json);
        std::set<std::string> ids;
        int result;
        while ((result = sqlite3_step(entries.value)) == SQLITE_ROW)
        {
            require(packages.size() < 2048 && text(entries.value, 1) == "object", "Invalid or oversized package list");
            const auto entry = text(entries.value, 0);
            ::ui::runtime::packs::PackageRecord package;
            package.id = field(database.value, entry, "$.id");
            package.version = field(database.value, entry, "$.version");
            require(identifier(package.id) && !package.version.empty() && package.version.size() <= 128 &&
                    std::all_of(package.version.begin(), package.version.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '+'; }) &&
                    ids.insert(package.id).second, "Invalid or duplicate package identity");
            for (auto [path, output] : std::initializer_list<std::pair<const char*, std::string*>>{
                     {"$.package_type", &package.package_type}, {"$.display_name", &package.display_name}, {"$.summary", &package.summary},
                     {"$.description", &package.description}, {"$.author", &package.author}, {"$.homepage", &package.homepage},
                     {"$.min_firmware_version", &package.min_firmware_version}})
                *output = field(database.value, entry, path, true);
            if (package.display_name.empty()) package.display_name = package.id;
            package.supported_memory_profiles = strings(database.value, entry, "$.supported_memory_profiles");
            package.tags = strings(database.value, entry, "$.tags");
            package.provided_locale_ids = strings(database.value, entry, "$.provides.locales", true);
            package.provided_font_ids = strings(database.value, entry, "$.provides.fonts", true);
            package.provided_ime_ids = strings(database.value, entry, "$.provides.ime", true);
            package.estimated_unique_font_ram_bytes = number(database.value, entry, "$.runtime.estimated_unique_font_ram_bytes", true);
            require(field(database.value, entry, "$.archive.layout") == "trailmate-pack-v1", "Unsupported archive layout");
            package.archive_path = field(database.value, entry, "$.archive.path");
            package.archive_sha256 = field(database.value, entry, "$.archive.sha256");
            package.archive_size_bytes = number(database.value, entry, "$.archive.size_bytes");
            require(package.archive_size_bytes && package.archive_size_bytes <= 128U * 1024U * 1024U && package.archive_sha256.size() == 64 &&
                    std::all_of(package.archive_sha256.begin(), package.archive_sha256.end(), [](unsigned char c) { return std::isxdigit(c); }), "Invalid archive size or digest");
            std::transform(package.archive_sha256.begin(), package.archive_sha256.end(), package.archive_sha256.begin(), [](unsigned char c) { return std::tolower(c); });
            require(!package.archive_path.empty() && package.archive_path.front() != '/' && package.archive_path.find_first_of("\\\r\n\t") == package.archive_path.npos, "Invalid archive location");
            require(web_url(package.archive_path) || package.archive_path.find(':') == package.archive_path.npos, "Unsupported archive URL scheme");
            package.download_url = web_url(package.archive_path) ? package.archive_path : base_url + (base_url.ends_with('/') ? "" : "/") + package.archive_path;
            require(web_url(package.download_url), "Unsupported archive URL");
            packages.push_back(std::move(package));
        }
        require(result == SQLITE_DONE, "Invalid catalog document");
        return true;
    }
    catch (const std::exception& exception) { packages.clear(); error = exception.what(); return false; }
}
} // namespace platform::linux_runtime
