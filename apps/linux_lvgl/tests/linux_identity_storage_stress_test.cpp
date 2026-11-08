#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "platform/ui/settings_store.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <sqlite3.h>
#include <thread>
#include <unistd.h>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-identity-storage-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    const auto path = root / "trailmate.sqlite3";
    platform::linux_runtime::LinuxReticulumIdentity original(path);
    assert(original.begin());
    const auto public_key = original.publicKey();
    sqlite3* database = nullptr;
    assert(sqlite3_open(path.c_str(), &database) == SQLITE_OK);
    assert(sqlite3_exec(database, "PRAGMA journal_mode=WAL; CREATE TABLE stress_messages(id INTEGER PRIMARY KEY,text TEXT NOT NULL);", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(database);
    std::atomic_bool start{false};
    std::thread config([&]
                       {
        while(!start.load()) std::this_thread::yield();
        std::array<uint8_t,4096> snapshot{};
        for(unsigned index=0;index<128;++index)
        {
            snapshot.fill(uint8_t(index));
            assert(platform::ui::settings_store::put_blob("stress","snapshot",snapshot.data(),snapshot.size()));
        } });
    std::thread messages([&]
                         {
        sqlite3* connection=nullptr;
        assert(sqlite3_open(path.c_str(),&connection)==SQLITE_OK);
        sqlite3_busy_timeout(connection,5000);
        while(!start.load()) std::this_thread::yield();
        for(unsigned index=0;index<128;++index)
        {
            const std::string sql="INSERT INTO stress_messages VALUES("+std::to_string(index)+",'message-"+std::to_string(index)+"')";
            assert(sqlite3_exec(connection,sql.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK);
        }
        sqlite3_close(connection); });
    start = true;
    for (unsigned index = 0; index < 128; ++index)
    {
        platform::linux_runtime::LinuxReticulumIdentity identity(path);
        // Contention may report busy; a successful load must preserve the key.
        if (identity.begin()) assert(identity.publicKey() == public_key);
    }
    config.join();
    messages.join();
    assert(sqlite3_open(path.c_str(), &database) == SQLITE_OK);
    sqlite3_stmt* query = nullptr;
    assert(sqlite3_prepare_v2(database, "PRAGMA integrity_check", -1, &query, nullptr) == SQLITE_OK);
    assert(sqlite3_step(query) == SQLITE_ROW && std::string(reinterpret_cast<const char*>(sqlite3_column_text(query, 0))) == "ok");
    sqlite3_finalize(query);
    assert(sqlite3_prepare_v2(database, "SELECT count(*) FROM stress_messages WHERE text='message-' || id", -1, &query, nullptr) == SQLITE_OK);
    assert(sqlite3_step(query) == SQLITE_ROW && sqlite3_column_int(query, 0) == 128);
    sqlite3_finalize(query);
    sqlite3_close(database);
    std::array<uint8_t, 4096> snapshot{};
    size_t length = 0;
    assert(platform::ui::settings_store::get_blob_into("stress", "snapshot", snapshot.data(), snapshot.size(), &length));
    assert(length == snapshot.size() && std::all_of(snapshot.begin(), snapshot.end(), [](uint8_t byte)
                                                    { return byte == 127; }));
    platform::linux_runtime::LinuxReticulumIdentity reopened(path);
    assert(reopened.begin() && reopened.publicKey() == public_key);
    std::filesystem::remove_all(root);
}
