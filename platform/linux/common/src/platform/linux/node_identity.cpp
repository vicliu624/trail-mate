#include "platform/linux/node_identity.h"
#include "platform/linux/runtime_paths.h"
#include <charconv>
#include <cstdlib>
#include <limits>
#include <sqlite3.h>
#include <string_view>

namespace platform::linux_runtime
{
namespace
{
bool valid(sqlite3_int64 number) { return number > 0 && number < 0xFFFFFFFFLL; }
struct Database
{
    sqlite3* value = nullptr;
    ~Database()
    {
        if (value) sqlite3_close(value);
    }
};
struct Statement
{
    sqlite3_stmt* value = nullptr;
    ~Statement()
    {
        if (value) sqlite3_finalize(value);
    }
};
} // namespace
uint32_t load_or_create_node_id(std::string& error)
{
    error.clear();
    if (const char* text = std::getenv("TRAIL_MATE_NODE_ID"))
    {
        std::string_view input(text);
        unsigned base = 10;
        if (input.substr(0, 2) == "0x" || input.substr(0, 2) == "0X")
        {
            input.remove_prefix(2);
            base = 16;
        }
        uint32_t result = 0;
        const auto parsed = std::from_chars(input.data(), input.data() + input.size(), result, base);
        if (input.empty() || parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() || !valid(result))
        {
            error = "Invalid TRAIL_MATE_NODE_ID";
            return 0;
        }
        return result; // Explicit process override does not rotate stored identity.
    }
    const auto path = sqlite_database_path();
    if (!ensure_directory(path.parent_path()))
    {
        error = "Cannot create node identity directory";
        return 0;
    }
    Database database;
    if (sqlite3_open_v2(path.string().c_str(), &database.value, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        error = "Cannot open node identity database";
        return 0;
    }
    sqlite3_busy_timeout(database.value, 5000);
    if (sqlite3_exec(database.value, "BEGIN IMMEDIATE; CREATE TABLE IF NOT EXISTS linux_device_identity (singleton INTEGER PRIMARY KEY CHECK(singleton=1), node_id INTEGER NOT NULL);", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error = "Cannot begin node identity transaction";
        return 0;
    }
    uint32_t result = 0;
    {
        Statement query;
        if (sqlite3_prepare_v2(database.value, "SELECT node_id FROM linux_device_identity WHERE singleton=1", -1, &query.value, nullptr) == SQLITE_OK)
        {
            const auto status = sqlite3_step(query.value);
            if (status == SQLITE_ROW)
            {
                const auto number = sqlite3_column_int64(query.value, 0);
                if (valid(number)) result = static_cast<uint32_t>(number);
                else error = "Stored node identity is invalid";
            }
            else if (status != SQLITE_DONE) error = "Cannot read node identity";
        }
        else error = "Cannot query node identity";
    }
    if (!result && error.empty())
    {
        Statement random;
        if (sqlite3_prepare_v2(database.value, "SELECT random() & 4294967295", -1, &random.value, nullptr) == SQLITE_OK)
            for (unsigned attempt = 0; attempt < 16 && !result; ++attempt)
            {
                if (sqlite3_step(random.value) != SQLITE_ROW) break;
                const auto number = sqlite3_column_int64(random.value, 0);
                if (valid(number)) result = static_cast<uint32_t>(number);
                sqlite3_reset(random.value);
            }
        Statement insert;
        if (!result || sqlite3_prepare_v2(database.value, "INSERT INTO linux_device_identity VALUES(1, ?)", -1, &insert.value, nullptr) != SQLITE_OK)
            error = "Cannot allocate node identity";
        else
        {
            sqlite3_bind_int64(insert.value, 1, result);
            if (sqlite3_step(insert.value) != SQLITE_DONE) error = "Cannot persist node identity";
        }
    }
    if (error.empty() && sqlite3_exec(database.value, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK) return result;
    sqlite3_exec(database.value, "ROLLBACK", nullptr, nullptr, nullptr);
    if (error.empty()) error = "Cannot commit node identity";
    return 0;
}
} // namespace platform::linux_runtime
