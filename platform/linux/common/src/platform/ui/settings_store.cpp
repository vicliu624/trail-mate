#include "platform/ui/settings_store.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
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

bool upsert_value(const char* ns,
                  const char* key,
                  ValueKind kind,
                  const void* payload,
                  std::size_t payload_len)
{
    if (!ns || !key || key[0] == '\0' ||
        (payload_len > 0U && payload == nullptr))
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
        "INSERT INTO settings(namespace, key, kind, payload, updated_at) "
        "VALUES(?1, ?2, ?3, ?4, CAST(strftime('%s','now') AS INTEGER)) "
        "ON CONFLICT(namespace, key) DO UPDATE SET "
        "kind=excluded.kind, "
        "payload=excluded.payload, "
        "updated_at=excluded.updated_at;";
    if (sqlite3_prepare_v2(handle.db, kSql, -1, &stmt, nullptr) != SQLITE_OK)
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

} // namespace

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

bool get_blob(const char* ns, const char* key, std::vector<uint8_t>& out)
{
    std::lock_guard<std::mutex> lock(s_store_mutex);
    return load_value(ns, key, ValueKind::Blob, out);
}

void remove_keys(const char* ns, const char* const* keys, std::size_t key_count)
{
    if (!ns || !keys || key_count == 0U)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(s_store_mutex);
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
