#include "platform/ui/settings_store.h"
#include "platform/linux/settings_reset_storage.h"
#include "platform/linux/settings_transaction.h"
#include "platform/ui/settings_reset_policy.h"

#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "platform/linux/runtime_paths.h"

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

std::mutex s_store_mutex;
bool s_factory_reset_committed = false;
bool s_settings_import_committed = false;

bool reset_blocks(const char* ns)
{
    if ((!s_factory_reset_committed && !s_settings_import_committed) || !ns) return false;
    using namespace ::platform::linux_runtime::settings_storage;
    if (!std::strcmp(ns, kAppNamespace) || !std::strcmp(ns, kNetworkNamespace) || !std::strcmp(ns, kConfigTransferNamespace)) return true;
    for (const auto* name : ::platform::ui::settings_reset::kLegacyNamespaces)
        if (!std::strcmp(ns, name)) return true;
    return false;
}

bool ensure_parent_directory(const std::filesystem::path& file_path)
{
    return ::platform::linux_runtime::ensure_directory(file_path.parent_path());
}

sqlite3* open_database()
{
    const std::filesystem::path db_path =
        ::platform::linux_runtime::sqlite_database_path();
    if (!ensure_parent_directory(db_path))
    {
        return nullptr;
    }

    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(db_path.string().c_str(),
                                   &db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                       SQLITE_OPEN_FULLMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK)
    {
        if (db != nullptr)
        {
            sqlite3_close(db);
        }
        return nullptr;
    }

    sqlite3_busy_timeout(db, 5000);
    return db;
}

bool exec_sql(sqlite3* db, const char* sql)
{
    if (db == nullptr || sql == nullptr)
    {
        return false;
    }

    char* error = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (error != nullptr)
    {
        sqlite3_free(error);
    }
    return rc == SQLITE_OK;
}

bool ensure_schema(sqlite3* db)
{
    return exec_sql(db, "PRAGMA busy_timeout=5000;") &&
           exec_sql(db, "PRAGMA journal_mode=WAL;") &&
           exec_sql(db,
                    "CREATE TABLE IF NOT EXISTS settings ("
                    "namespace TEXT NOT NULL,"
                    "key TEXT NOT NULL,"
                    "kind TEXT NOT NULL,"
                    "payload BLOB NOT NULL,"
                    "updated_at INTEGER NOT NULL DEFAULT "
                    "(CAST(strftime('%s','now') AS INTEGER)),"
                    "PRIMARY KEY(namespace, key)"
                    ");");
}

struct DatabaseHandle
{
    sqlite3* db = nullptr;

    DatabaseHandle()
    {
        db = open_database();
        if (db != nullptr && !ensure_schema(db))
        {
            sqlite3_close(db);
            db = nullptr;
        }
    }

    ~DatabaseHandle()
    {
        if (db != nullptr)
        {
            sqlite3_close(db);
        }
    }

    DatabaseHandle(const DatabaseHandle&) = delete;
    DatabaseHandle& operator=(const DatabaseHandle&) = delete;

    explicit operator bool() const noexcept
    {
        return db != nullptr;
    }
};

bool bind_text(sqlite3_stmt* stmt, int index, const char* text)
{
    return sqlite3_bind_text(stmt, index, text ? text : "", -1,
                             SQLITE_TRANSIENT) == SQLITE_OK;
}

bool upsert_value_in(sqlite3* database, const char* ns,
                     const char* key,
                     ValueKind kind,
                     const void* payload,
                     std::size_t payload_len)
{
    if (!ns || !key || key[0] == '\0' ||
        (payload_len > 0U && payload == nullptr) || payload_len > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }
    if (reset_blocks(ns)) return false;

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "INSERT INTO settings(namespace, key, kind, payload, updated_at) "
        "VALUES(?1, ?2, ?3, ?4, CAST(strftime('%s','now') AS INTEGER)) "
        "ON CONFLICT(namespace, key) DO UPDATE SET "
        "kind=excluded.kind, "
        "payload=excluded.payload, "
        "updated_at=excluded.updated_at;";
    if (sqlite3_prepare_v2(database, kSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    const char kind_text[2] = {static_cast<char>(kind), '\0'};
    bool ok = bind_text(stmt, 1, ns) && bind_text(stmt, 2, key) &&
              bind_text(stmt, 3, kind_text) &&
              sqlite3_bind_blob(stmt,
                                4,
                                payload,
                                static_cast<int>(payload_len),
                                SQLITE_TRANSIENT) == SQLITE_OK;
    ok = ok && sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool upsert_value(const char* ns, const char* key, ValueKind kind,
                  const void* payload, std::size_t payload_len)
{
    if (!ns || !key || !*key || reset_blocks(ns) || (payload_len && !payload) ||
        payload_len > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
    DatabaseHandle handle;
    return handle && upsert_value_in(handle.db, ns, key, kind, payload, payload_len);
}

bool remove_key(sqlite3* db, const char* ns, const char* key)
{
    if (db == nullptr || !ns || !key || key[0] == '\0')
    {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "DELETE FROM settings WHERE namespace=?1 AND key=?2;";
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    const bool ok = bind_text(stmt, 1, ns) && bind_text(stmt, 2, key) &&
                    sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool load_value(const char* ns,
                const char* key,
                ValueKind expected_kind,
                std::vector<uint8_t>& out)
{
    out.clear();
    if (!ns || !key)
    {
        return false;
    }

    DatabaseHandle handle;
    if (!handle)
    {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "SELECT kind, payload FROM settings WHERE namespace=?1 AND key=?2;";
    if (sqlite3_prepare_v2(handle.db, kSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    bool ok = bind_text(stmt, 1, ns) && bind_text(stmt, 2, key);
    if (ok && sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char* kind_text = sqlite3_column_text(stmt, 0);
        if (kind_text != nullptr &&
            kind_text[0] == static_cast<unsigned char>(expected_kind))
        {
            const void* blob = sqlite3_column_blob(stmt, 1);
            const int len = sqlite3_column_bytes(stmt, 1);
            if (len > 0 && blob != nullptr)
            {
                const auto* bytes = static_cast<const uint8_t*>(blob);
                out.assign(bytes, bytes + len);
            }
            else
            {
                out.clear();
            }
            ok = true;
        }
        else
        {
            ok = false;
        }
    }
    else
    {
        ok = false;
    }

    sqlite3_finalize(stmt);
    return ok;
}

std::string bytes_to_string(const std::vector<uint8_t>& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

bool parse_int_value(const std::vector<uint8_t>& payload, int* out)
{
    if (!out)
    {
        return false;
    }
    try
    {
        *out = std::stoi(bytes_to_string(payload));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool parse_uint_value(const std::vector<uint8_t>& payload, uint32_t* out)
{
    if (!out)
    {
        return false;
    }
    try
    {
        *out = static_cast<uint32_t>(std::stoul(bytes_to_string(payload)));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void put_numeric_value(const char* ns,
                       const char* key,
                       ValueKind kind,
                       const std::string& payload)
{
    std::lock_guard<std::mutex> lock(s_store_mutex);
    (void)upsert_value(ns, key, kind, payload.data(), payload.size());
}

template <class Result>
Result read_typed_value_into(const char* ns, const char* key, ValueKind expected,
                             void* out, std::size_t capacity, std::size_t* out_len,
                             sqlite3* database = nullptr)
{
    if (out_len) *out_len = 0;
    if (capacity > static_cast<std::size_t>(std::numeric_limits<sqlite3_int64>::max())) return Result::Invalid;
    if (out && capacity) std::memset(out, 0, capacity);
    if (!ns || !key || !*key || (!out && capacity)) return Result::Invalid;
    std::unique_lock<std::mutex> lock(s_store_mutex, std::defer_lock);
    std::optional<DatabaseHandle> handle;
    if (!database)
    {
        lock.lock();
        handle.emplace();
        if (!*handle) return Result::Unavailable;
        database = handle->db;
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database,
                           "SELECT kind,CASE WHEN typeof(payload)='blob' THEN length(payload) ELSE -1 END,"
                           "CASE WHEN typeof(payload)='blob' THEN CASE WHEN length(payload)<=?3 THEN payload END END "
                           "FROM settings WHERE namespace=?1 AND key=?2;",
                           -1, &statement, nullptr) != SQLITE_OK)
        return Result::Unavailable;
    Result result = Result::Unavailable;
    if (bind_text(statement, 1, ns) && bind_text(statement, 2, key) &&
        sqlite3_bind_int64(statement, 3, static_cast<sqlite3_int64>(capacity)) == SQLITE_OK)
    {
        const int row = sqlite3_step(statement);
        if (row == SQLITE_DONE) result = Result::Missing;
        else if (row == SQLITE_ROW)
        {
            result = Result::Invalid;
            const int kind_type = sqlite3_column_type(statement, 0);
            const auto* kind = sqlite3_column_text(statement, 0);
            const auto length = sqlite3_column_int64(statement, 1);
            if (kind_type == SQLITE_TEXT && sqlite3_column_bytes(statement, 0) == 1 &&
                kind && kind[0] == static_cast<unsigned char>(expected) && length >= 0 &&
                static_cast<uint64_t>(length) <= capacity && sqlite3_column_type(statement, 2) == SQLITE_BLOB)
            {
                const auto* bytes = sqlite3_column_blob(statement, 2);
                if (length == 0 || bytes)
                {
                    if (length) std::memcpy(out, bytes, static_cast<size_t>(length));
                    if (out_len) *out_len = static_cast<size_t>(length);
                    result = Result::Ok;
                }
                else result = Result::Unavailable;
            }
        }
    }
    sqlite3_finalize(statement);
    return result;
}

template <class Value>
ScalarReadResult read_number_into(const char* ns, const char* key, ValueKind kind, Value* out, sqlite3* database = nullptr)
{
    if (!out) return ScalarReadResult::Invalid;
    *out = 0;
    char bytes[32]{};
    std::size_t length = 0;
    const auto result = read_typed_value_into<ScalarReadResult>(ns, key, kind, bytes, sizeof(bytes), &length, database);
    if (result != ScalarReadResult::Ok) return result;
    Value parsed = 0;
    const auto converted = std::from_chars(bytes, bytes + length, parsed, 10);
    if (converted.ec != std::errc{} || converted.ptr != bytes + length) return ScalarReadResult::Invalid;
    *out = parsed;
    return ScalarReadResult::Ok;
}
ScalarReadResult read_boolean_value(const char* ns, const char* key, bool* out, sqlite3* database = nullptr)
{
    if (!out) return ScalarReadResult::Invalid;
    *out = false;
    char bytes[2]{};
    size_t length = 0;
    const auto result = read_typed_value_into<ScalarReadResult>(ns, key, ValueKind::Bool, bytes, sizeof(bytes), &length, database);
    if (result != ScalarReadResult::Ok) return result;
    if (length != 1 || (bytes[0] != '0' && bytes[0] != '1')) return ScalarReadResult::Invalid;
    *out = bytes[0] == '1';
    return ScalarReadResult::Ok;
}
StringReadResult read_text_value(const char* ns, const char* key, char* out,
                                 std::size_t capacity, std::size_t* out_len, sqlite3* database = nullptr)
{
    if (out_len) *out_len = 0;
    if (!out || !capacity || capacity > static_cast<std::size_t>(std::numeric_limits<sqlite3_int64>::max())) return StringReadResult::Invalid;
    std::memset(out, 0, capacity);
    size_t length = 0;
    const auto result = read_typed_value_into<StringReadResult>(ns, key, ValueKind::String, out, capacity - 1, &length, database);
    if (result != StringReadResult::Ok) return result;
    if (std::memchr(out, 0, length))
    {
        std::memset(out, 0, capacity);
        return StringReadResult::Invalid;
    }
    out[length] = 0;
    if (out_len) *out_len = length;
    return StringReadResult::Ok;
}
} // namespace

ScalarReadResult read_int_into(const char* ns, const char* key, int32_t* out)
{
    return read_number_into(ns, key, ValueKind::Int, out);
}
ScalarReadResult read_uint_into(const char* ns, const char* key, uint32_t* out)
{
    return read_number_into(ns, key, ValueKind::Uint, out);
}
ScalarReadResult read_u8_into(const char* ns, const char* key, uint8_t* out)
{
    if (!out) return ScalarReadResult::Invalid;
    *out = 0;
    uint32_t value = 0;
    const auto result = read_uint_into(ns, key, &value);
    if (result != ScalarReadResult::Ok) return result;
    if (value > UINT8_MAX) return ScalarReadResult::Invalid;
    *out = static_cast<uint8_t>(value);
    return ScalarReadResult::Ok;
}
ScalarReadResult read_bool_into(const char* ns, const char* key, bool* out)
{
    return read_boolean_value(ns, key, out);
}
StringReadResult read_string_into(const char* ns, const char* key, char* out,
                                  std::size_t capacity, std::size_t* out_len)
{
    return read_text_value(ns, key, out, capacity, out_len);
}

void put_int(const char* ns, const char* key, int value)
{
    put_numeric_value(ns, key, ValueKind::Int, std::to_string(value));
}

void put_bool(const char* ns, const char* key, bool value)
{
    put_numeric_value(ns, key, ValueKind::Bool, value ? "1" : "0");
}

void put_uint(const char* ns, const char* key, uint32_t value)
{
    put_numeric_value(ns, key, ValueKind::Uint, std::to_string(value));
}

bool put_string(const char* ns, const char* key, const char* value)
{
    if (!ns || !key || !value)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    if (reset_blocks(ns)) return false;
    if (value[0] == '\0')
    {
        DatabaseHandle handle;
        return handle ? remove_key(handle.db, ns, key) : false;
    }

    return upsert_value(ns,
                        key,
                        ValueKind::String,
                        value,
                        std::strlen(value));
}

bool put_blob(const char* ns, const char* key, const void* data, std::size_t len)
{
    if (!ns || !key || (len > 0U && data == nullptr))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    if (reset_blocks(ns)) return false;
    if (len == 0U)
    {
        DatabaseHandle handle;
        return handle ? remove_key(handle.db, ns, key) : false;
    }

    return upsert_value(ns, key, ValueKind::Blob, data, len);
}

int get_int(const char* ns, const char* key, int default_value)
{
    std::lock_guard<std::mutex> lock(s_store_mutex);
    std::vector<uint8_t> payload;
    if (!load_value(ns, key, ValueKind::Int, payload))
    {
        return default_value;
    }

    int parsed = default_value;
    return parse_int_value(payload, &parsed) ? parsed : default_value;
}

bool get_bool(const char* ns, const char* key, bool default_value)
{
    std::lock_guard<std::mutex> lock(s_store_mutex);
    std::vector<uint8_t> payload;
    if (!load_value(ns, key, ValueKind::Bool, payload))
    {
        return default_value;
    }

    return bytes_to_string(payload) == "1";
}

uint32_t get_uint(const char* ns, const char* key, uint32_t default_value)
{
    std::lock_guard<std::mutex> lock(s_store_mutex);
    std::vector<uint8_t> payload;
    if (!load_value(ns, key, ValueKind::Uint, payload))
    {
        return default_value;
    }

    uint32_t parsed = default_value;
    return parse_uint_value(payload, &parsed) ? parsed : default_value;
}

bool get_string(const char* ns, const char* key, std::string& out)
{
    std::lock_guard<std::mutex> lock(s_store_mutex);
    std::vector<uint8_t> payload;
    if (!load_value(ns, key, ValueKind::String, payload))
    {
        return false;
    }

    out.assign(payload.begin(), payload.end());
    return true;
}

bool get_string_into(const char* ns,
                     const char* key,
                     char* out,
                     std::size_t capacity,
                     std::size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0U;
    }
    if (!out || capacity == 0U)
    {
        return false;
    }
    out[0] = '\0';
    std::lock_guard<std::mutex> lock(s_store_mutex);
    std::vector<uint8_t> payload;
    if (!load_value(ns, key, ValueKind::String, payload) || payload.size() >= capacity)
    {
        return false;
    }
    if (!payload.empty())
    {
        std::memcpy(out, payload.data(), payload.size());
    }
    out[payload.size()] = '\0';
    if (out_len)
    {
        *out_len = payload.size();
    }
    return true;
}

bool get_blob(const char* ns, const char* key, std::vector<uint8_t>& out)
{
    std::lock_guard<std::mutex> lock(s_store_mutex);
    return load_value(ns, key, ValueKind::Blob, out);
}

BlobReadResult read_blob_into(const char* ns, const char* key, void* out,
                              std::size_t capacity, std::size_t* out_len)
{
    return read_typed_value_into<BlobReadResult>(ns, key, ValueKind::Blob, out, capacity, out_len);
}

bool get_blob_into(const char* ns,
                   const char* key,
                   void* out,
                   std::size_t capacity,
                   std::size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0;
    }
    if (!out && capacity != 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    std::vector<uint8_t> payload;
    if (!load_value(ns, key, ValueKind::Blob, payload))
    {
        return false;
    }
    if (out_len)
    {
        *out_len = payload.size();
    }
    if (payload.size() > capacity)
    {
        return false;
    }
    if (!payload.empty())
    {
        std::memcpy(out, payload.data(), payload.size());
    }
    return true;
}

void remove_keys(const char* ns, const char* const* keys, std::size_t key_count)
{
    if (!ns || !keys || key_count == 0U)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    if (reset_blocks(ns)) return;
    DatabaseHandle handle;
    if (!handle)
    {
        return;
    }

    (void)exec_sql(handle.db, "BEGIN IMMEDIATE;");
    for (std::size_t index = 0; index < key_count; ++index)
    {
        if (keys[index] && keys[index][0] != '\0')
        {
            (void)remove_key(handle.db, ns, keys[index]);
        }
    }
    (void)exec_sql(handle.db, "COMMIT;");
}

void clear_namespace(const char* ns)
{
    if (!ns)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
    if (reset_blocks(ns)) return;
    DatabaseHandle handle;
    if (handle)
    {
        sqlite3_stmt* stmt = nullptr;
        constexpr const char* kSql =
            "DELETE FROM settings WHERE namespace=?1;";
        if (sqlite3_prepare_v2(handle.db, kSql, -1, &stmt, nullptr) ==
            SQLITE_OK)
        {
            (void)(bind_text(stmt, 1, ns) &&
                   sqlite3_step(stmt) == SQLITE_DONE);
        }
        sqlite3_finalize(stmt);
    }
}

} // namespace platform::ui::settings_store

namespace platform::linux_runtime::settings_storage
{
bool commit_factory_reset(const void* defaults, std::size_t length)
{
    using namespace ::platform::ui::settings_store;
    if (!defaults || !length || length > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
    std::lock_guard<std::mutex> lock(s_store_mutex);
    if (s_factory_reset_committed) return true;
    if (s_settings_import_committed) return false;
    DatabaseHandle handle;
    if (!handle || !exec_sql(handle.db, "BEGIN IMMEDIATE;")) return false;
    sqlite3_stmt* statement = nullptr;
    bool ok = sqlite3_prepare_v2(handle.db, "DELETE FROM settings WHERE namespace=?1;", -1, &statement, nullptr) == SQLITE_OK;
    const auto erase = [&](const char* ns)
    {
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
        return bind_text(statement, 1, ns) && sqlite3_step(statement) == SQLITE_DONE;
    };
    for (const auto* ns : ::platform::ui::settings_reset::kLegacyNamespaces)
        if (ok) ok = erase(ns);
    if (ok) ok = erase(kAppNamespace);
    if (ok) ok = erase(kNetworkNamespace);
    if (ok) ok = erase(kConfigTransferNamespace);
    sqlite3_finalize(statement);
    statement = nullptr;
    if (ok) ok = sqlite3_prepare_v2(handle.db, "INSERT INTO settings(namespace,key,kind,payload) VALUES(?1,?2,?3,?4);", -1, &statement, nullptr) == SQLITE_OK;
    const auto insert = [&](const char* key, const char* kind, const void* data, int bytes)
    {
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
        return bind_text(statement, 1, kAppNamespace) && bind_text(statement, 2, key) && bind_text(statement, 3, kind) &&
               sqlite3_bind_blob(statement, 4, data, bytes, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_step(statement) == SQLITE_DONE;
    };
    if (ok) ok = insert(kAppConfigKey, "x", defaults, static_cast<int>(length));
    if (ok) ok = insert(kResetMarker, "b", "1", 1);
    sqlite3_finalize(statement);
    if (ok && exec_sql(handle.db, "COMMIT;"))
    {
        s_factory_reset_committed = true;
        return true;
    }
    (void)exec_sql(handle.db, "ROLLBACK;");
    return false;
}
bool factory_reset_committed()
{
    using namespace ::platform::ui::settings_store;
    std::lock_guard<std::mutex> lock(s_store_mutex);
    return s_factory_reset_committed;
}
} // namespace platform::linux_runtime::settings_storage

namespace platform::linux_runtime
{
::platform::ui::settings_store::ScalarReadResult SettingsReadView::read_int_into(const char* ns, const char* key, int32_t* out) const
{
    using namespace ::platform::ui::settings_store;
    return read_number_into(ns, key, ValueKind::Int, out, database_);
}
::platform::ui::settings_store::ScalarReadResult SettingsReadView::read_bool_into(const char* ns, const char* key, bool* out) const
{
    return ::platform::ui::settings_store::read_boolean_value(ns, key, out, database_);
}
::platform::ui::settings_store::ScalarReadResult SettingsReadView::read_uint_into(const char* ns, const char* key, uint32_t* out) const
{
    using namespace ::platform::ui::settings_store;
    return read_number_into(ns, key, ValueKind::Uint, out, database_);
}
::platform::ui::settings_store::ScalarReadResult SettingsReadView::read_u8_into(const char* ns, const char* key, uint8_t* out) const
{
    using Result = ::platform::ui::settings_store::ScalarReadResult;
    if (!out) return Result::Invalid;
    *out = 0;
    uint32_t value = 0;
    const auto result = read_uint_into(ns, key, &value);
    if (result != Result::Ok) return result;
    if (value > UINT8_MAX) return Result::Invalid;
    *out = static_cast<uint8_t>(value);
    return Result::Ok;
}
::platform::ui::settings_store::BlobReadResult SettingsReadView::read_blob_into(const char* ns, const char* key, void* out,
                                                                                std::size_t capacity, std::size_t* length) const
{
    using namespace ::platform::ui::settings_store;
    return read_typed_value_into<BlobReadResult>(ns, key, ValueKind::Blob, out, capacity, length, database_);
}
::platform::ui::settings_store::StringReadResult SettingsReadView::read_string_into(const char* ns, const char* key, char* out,
                                                                                    std::size_t capacity, std::size_t* length) const
{
    return ::platform::ui::settings_store::read_text_value(ns, key, out, capacity, length, database_);
}
bool read_settings_snapshot(bool (*read)(void*, const SettingsReadView&), void* context)
{
    using namespace ::platform::ui::settings_store;
    if (!read) return false;
    std::lock_guard<std::mutex> lock(s_store_mutex);
    DatabaseHandle handle;
    if (!handle || !exec_sql(handle.db, "BEGIN;")) return false;
    const SettingsReadView view(handle.db);
    const bool ok = read(context, view);
    if (ok && exec_sql(handle.db, "COMMIT;")) return true;
    (void)exec_sql(handle.db, "ROLLBACK;");
    return false;
}
bool write_settings_batch(const char* ns, const SettingsMutation* changes, std::size_t count)
{
    if (!ns) return false;
    const SettingsNamespaceBatch batch{ns, changes, count};
    return write_settings_transaction(&batch, 1);
}
static bool write_transaction(const SettingsNamespaceBatch* batches, std::size_t count, bool importing,
                              bool (*admit)(void*, sqlite3*) = nullptr, void* context = nullptr)
{
    using namespace ::platform::ui::settings_store;
    if ((count && !batches) || (importing && !count)) return false;
    for (size_t i = 0; i < count; ++i)
    {
        const auto& batch = batches[i];
        if (batch.name_space.empty() || batch.name_space.find('\0') != std::string::npos || (batch.count && !batch.changes)) return false;
        for (size_t j = 0; j < batch.count; ++j)
        {
            const auto& change = batch.changes[j];
            if (change.key.empty() || change.key.find('\0') != std::string::npos ||
                change.payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
            if (!change.erase && change.kind != 'i' && change.kind != 'u' && change.kind != 'b' && change.kind != 's' && change.kind != 'x') return false;
        }
    }
    std::lock_guard<std::mutex> lock(s_store_mutex);
    if (importing && (s_factory_reset_committed || s_settings_import_committed)) return false;
    for (size_t i = 0; i < count; ++i)
        if (reset_blocks(batches[i].name_space.c_str())) return false;
    DatabaseHandle handle;
    if (!handle || !exec_sql(handle.db, "BEGIN IMMEDIATE;")) return false;
    bool ok = !admit || admit(context, handle.db);
    for (size_t i = 0; ok && i < count; ++i)
    {
        const auto& batch = batches[i];
        for (size_t j = 0; ok && j < batch.count; ++j)
        {
            const auto& change = batch.changes[j];
            ok = change.erase ? remove_key(handle.db, batch.name_space.c_str(), change.key.c_str())
                              : upsert_value_in(handle.db, batch.name_space.c_str(), change.key.c_str(), static_cast<ValueKind>(change.kind), change.payload.data(), change.payload.size());
        }
    }
    if (ok && exec_sql(handle.db, "COMMIT;"))
    {
        if (importing) s_settings_import_committed = true;
        return true;
    }
    (void)exec_sql(handle.db, "ROLLBACK;");
    return false;
}
bool write_settings_transaction(const SettingsNamespaceBatch* batches, std::size_t count)
{
    return write_transaction(batches, count, false);
}
bool commit_settings_import(const SettingsNamespaceBatch* batches, std::size_t count)
{
    return write_transaction(batches, count, true);
}
bool commit_settings_import_if(const SettingsNamespaceBatch* batches, std::size_t count,
                               bool (*admit)(void*, const SettingsReadView&), void* context)
{
    if (!admit) return false;
    struct Check
    {
        bool (*admit)(void*, const SettingsReadView&);
        void* context;
    } check{admit, context};
    return write_transaction(
        batches, count, true, [](void* opaque, sqlite3* database)
        {
            auto& check = *static_cast<Check*>(opaque);
            const SettingsReadView view(database);
            return check.admit(check.context, view); },
        &check);
}
bool settings_import_committed()
{
    using namespace ::platform::ui::settings_store;
    std::lock_guard<std::mutex> lock(s_store_mutex);
    return s_settings_import_committed;
}
} // namespace platform::linux_runtime
