#include "app/linux_app_services.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/settings_reset_storage.h"
#include "platform/ui/settings_store.h"
#include "sys/event_bus.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sqlite3.h>
#include <unistd.h>

namespace store = platform::ui::settings_store;
using S = store::ScalarReadResult;
using T = store::StringReadResult;
using B = store::BlobReadResult;
struct Database
{
    sqlite3* db = nullptr;
    Database()
    {
        assert(sqlite3_open(platform::linux_runtime::sqlite_database_path().string().c_str(), &db) == SQLITE_OK);
        assert(sqlite3_busy_timeout(db, 5000) == SQLITE_OK);
    }
    ~Database() { sqlite3_close(db); }
    void sql(const char* text) { assert(sqlite3_exec(db, text, nullptr, nullptr, nullptr) == SQLITE_OK); }
    void row(const char* kind, size_t kind_size, const void* bytes, size_t size)
    {
        sqlite3_stmt* statement = nullptr;
        assert(sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO settings(namespace,key,kind,payload) VALUES('strict','value',?1,?2);", -1, &statement, nullptr) == SQLITE_OK);
        assert(sqlite3_bind_text(statement, 1, kind, int(kind_size), SQLITE_TRANSIENT) == SQLITE_OK);
        assert(sqlite3_bind_blob(statement, 2, bytes, int(size), SQLITE_TRANSIENT) == SQLITE_OK);
        assert(sqlite3_step(statement) == SQLITE_DONE);
        sqlite3_finalize(statement);
    }
};
int main()
{
    char pattern[] = "/tmp/trailmate-strict-settings-XXXXXX";
    const auto* temporary = mkdtemp(pattern);
    assert(temporary);
    const std::filesystem::path root(temporary);
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    int32_t signed_value = 123;
    uint32_t unsigned_value = 123;
    uint8_t byte = 123;
    bool boolean = true;
    char text[16];
    std::memset(text, 'x', sizeof(text));
    size_t length = 123;
    assert(store::read_int_into("strict", "missing", &signed_value) == S::Missing && signed_value == 0);
    assert(store::read_string_into("strict", "missing", text, sizeof(text), &length) == T::Missing && !text[0] && length == 0);
    {
        Database db;
        for (const auto* value : {"-2147483648", "2147483647", "0"})
        {
            db.row("i", 1, value, std::strlen(value));
            assert(store::read_int_into("strict", "value", &signed_value) == S::Ok);
            assert(signed_value == std::stoll(value));
        }
        for (const auto* value : {"2147483648", "-2147483649", "1junk", " 1", "+1", "", "1\n"})
        {
            db.row("i", 1, value, std::strlen(value));
            signed_value = 123;
            assert(store::read_int_into("strict", "value", &signed_value) == S::Invalid && signed_value == 0);
        }
        db.row("u", 1, "4294967295", 10);
        assert(store::read_uint_into("strict", "value", &unsigned_value) == S::Ok && unsigned_value == UINT32_MAX);
        for (const auto* value : {"4294967296", "-1", "2tail"})
        {
            db.row("u", 1, value, std::strlen(value));
            assert(store::read_uint_into("strict", "value", &unsigned_value) == S::Invalid && unsigned_value == 0);
        }
        store::put_uint("strict", "value", 255);
        assert(store::read_u8_into("strict", "value", &byte) == S::Ok && byte == 255);
        store::put_uint("strict", "value", 256);
        assert(store::read_u8_into("strict", "value", &byte) == S::Invalid && byte == 0);
        store::put_bool("strict", "value", true);
        assert(store::read_bool_into("strict", "value", &boolean) == S::Ok && boolean);
        assert(store::read_uint_into("strict", "value", &unsigned_value) == S::Invalid);
        for (const auto* value : {"2", "true", "01", ""})
        {
            db.row("b", 1, value, std::strlen(value));
            boolean = true;
            assert(store::read_bool_into("strict", "value", &boolean) == S::Invalid && !boolean);
        }
        assert(store::put_string("strict", "value", "Ab!9\xe4\xb8\xad"));
        assert(store::read_string_into("strict", "value", text, sizeof(text), &length) == T::Ok && length == 7);
        assert(!std::strcmp(text, "Ab!9\xe4\xb8\xad"));
        assert(store::read_string_into("strict", "value", text, 7, &length) == T::Invalid && !text[0] && length == 0);
        const char nul_text[]{'a', 0, 'b'};
        db.row("s", 1, nul_text, sizeof(nul_text));
        assert(store::read_string_into("strict", "value", text, sizeof(text), &length) == T::Invalid && !text[0]);
        const char bad_kind[]{'s', 0, 'x'};
        db.row(bad_kind, sizeof(bad_kind), "abc", 3);
        assert(store::read_string_into("strict", "value", text, sizeof(text), &length) == T::Invalid);
        db.sql("UPDATE settings SET kind=X'73',payload=X'6162' WHERE namespace='strict';");
        assert(store::read_string_into("strict", "value", text, sizeof(text), &length) == T::Invalid);
        db.sql("UPDATE settings SET kind='s',payload='text storage' WHERE namespace='strict';");
        assert(store::read_string_into("strict", "value", text, sizeof(text), &length) == T::Invalid);
        db.row("x", 1, "", 0);
        assert(store::read_blob_into("strict", "value", nullptr, 0, &length) == B::Ok && length == 0);
        db.sql("UPDATE settings SET kind='x',payload=zeroblob(8388608) WHERE namespace='strict';");
        int current_allocation = 0, largest_allocation = 0;
        assert(sqlite3_status(SQLITE_STATUS_MALLOC_SIZE, &current_allocation, &largest_allocation, 1) == SQLITE_OK);
        assert(store::read_blob_into("strict", "value", text, sizeof(text), &length) == B::Invalid && !text[0] && !length);
        assert(sqlite3_status(SQLITE_STATUS_MALLOC_SIZE, &current_allocation, &largest_allocation, 0) == SQLITE_OK);
        assert(largest_allocation < 8388608); // Do not materialize the rejected 8 MiB payload.
        db.sql("UPDATE settings SET payload=CAST(zeroblob(8388608) AS TEXT) WHERE namespace='strict';");
        assert(sqlite3_status(SQLITE_STATUS_MALLOC_SIZE, &current_allocation, &largest_allocation, 1) == SQLITE_OK);
        assert(store::read_blob_into("strict", "value", text, sizeof(text), &length) == B::Invalid && !text[0] && !length);
        assert(sqlite3_status(SQLITE_STATUS_MALLOC_SIZE, &current_allocation, &largest_allocation, 0) == SQLITE_OK);
        assert(largest_allocation < 8388608);
        assert(store::read_int_into("strict", "value", nullptr) == S::Invalid);
        assert(store::read_string_into("strict", "value", text, 0, &length) == T::Invalid && length == 0);

        // Strict startup must preserve a malformed working value instead of
        // importing the old migration source or writing defaults over it.
        trailmate::linux_app::LinuxAppServicesOptions options;
        options.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::SimulatorDemo;
        {
            trailmate::linux_app::LinuxAppServices services(options);
            assert(services.initialize());
            services.saveConfig();
            services.shutdown();
        }
        std::vector<uint8_t> valid;
        assert(store::get_blob("linux_app_facade", "app_config_v1", valid));
        std::filesystem::create_directories(root / "sd/trailmate/reticulum");
        std::ofstream(root / "sd/trailmate/reticulum/groups.tsv") << "version\t1\n1\tDo not resurrect\t0123456789abcdef0123456789abcdef\n";
        for (unsigned mutation = 0; mutation < 4; ++mutation)
        {
            auto broken = valid;
            if (mutation == 0) broken[0] ^= 1;
            if (mutation == 1)
            {
                const uint32_t unsupported_version = 99;
                std::memcpy(broken.data() + 4, &unsupported_version, sizeof(unsupported_version));
            }
            if (mutation == 2) broken.pop_back();
            if (mutation == 3) broken.push_back(0);
            assert(store::put_blob("linux_app_facade", "app_config_v1", broken.data(), broken.size()));
            {
                trailmate::linux_app::LinuxAppServices services(options);
                assert(!services.initialize());
            }
            std::vector<uint8_t> retained;
            assert(store::get_blob("linux_app_facade", "app_config_v1", retained) && retained == broken);
        }
        assert(store::put_string("linux_app_facade", "app_config_v1", "wrong-kind"));
        {
            trailmate::linux_app::LinuxAppServices services(options);
            assert(!services.initialize());
        }
        std::string retained_text;
        assert(store::get_string("linux_app_facade", "app_config_v1", retained_text) && retained_text == "wrong-kind");
        assert(store::put_blob("linux_app_facade", "app_config_v1", "broken", 6));
        {
            trailmate::linux_app::LinuxAppServices services(options);
            assert(!services.initialize());
        }
        std::vector<uint8_t> saved;
        assert(store::get_blob("linux_app_facade", "app_config_v1", saved) && std::string(saved.begin(), saved.end()) == "broken");
        db.sql("ALTER TABLE settings RENAME TO preserved_settings; CREATE TABLE settings(namespace TEXT,key TEXT,broken BLOB);");
        signed_value = 123;
        assert(store::read_int_into("strict", "value", &signed_value) == S::Unavailable && signed_value == 0);
        assert(store::read_string_into("strict", "value", text, sizeof(text), &length) == T::Unavailable && !text[0] && !length);
        assert(store::read_bool_into("strict", "value", &boolean) == S::Unavailable && !boolean);
        assert(store::read_uint_into("strict", "value", &unsigned_value) == S::Unavailable && !unsigned_value);
        assert(store::read_u8_into("strict", "value", &byte) == S::Unavailable && !byte);
        assert(store::read_blob_into("strict", "value", text, sizeof(text), &length) == B::Unavailable && !text[0] && !length);
        {
            trailmate::linux_app::LinuxAppServices services(options);
            assert(!services.initialize());
        }
        db.sql("DROP TABLE settings; ALTER TABLE preserved_settings RENAME TO settings;");
        assert(store::get_blob("linux_app_facade", "app_config_v1", saved) && std::string(saved.begin(), saved.end()) == "broken");
        assert(store::put_blob("linux_app_facade", "app_config_v1", nullptr, 0));
        store::put_uint("linux_app_facade", "factory_reset_completed", 1);
        {
            trailmate::linux_app::LinuxAppServices services(options);
            assert(services.initialize());
            for (const auto& group : services.getConfig().reticulumConfig().reticulum_groups)
                assert(!chat::hasReticulumDestinationIdentity(group.identity));
            services.shutdown();
        }
    }
    sys::Event* event = nullptr;
    while (sys::EventBus::subscribe(&event, 0)) delete event;
    std::filesystem::remove_all(root);
}
