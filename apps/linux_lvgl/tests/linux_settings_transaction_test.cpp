#include "platform/linux/runtime_paths.h"
#include "platform/linux/settings_reset_storage.h"
#include "platform/linux/settings_transaction.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sqlite3.h>
#include <sys/wait.h>
#include <unistd.h>

namespace native = platform::linux_runtime;
namespace store = platform::ui::settings_store;
namespace reset = native::settings_storage;
using S = store::ScalarReadResult;
using B = store::BlobReadResult;
using T = store::StringReadResult;
struct Database
{
    sqlite3* db = nullptr;
    Database()
    {
        assert(sqlite3_open(native::sqlite_database_path().string().c_str(), &db) == SQLITE_OK);
        assert(sqlite3_busy_timeout(db, 2000) == SQLITE_OK);
    }
    ~Database() { sqlite3_close(db); }
    void sql(const char* text)
    {
        const int result = sqlite3_exec(db, text, nullptr, nullptr, nullptr);
        if (result != SQLITE_OK) std::fprintf(stderr, "SQLite fixture failure: %s\n", sqlite3_errmsg(db));
        assert(result == SQLITE_OK);
    }
};
void verify(unsigned revision)
{
    assert(native::read_settings_snapshot([](void* context, const native::SettingsReadView& view)
                                          {
        const auto expected = *static_cast<unsigned*>(context);
        uint32_t value = 0;
        assert(view.read_uint_into("settings", "revision", &value) == S::Ok && value == expected);
        uint8_t blob[3]{};
        size_t length = 0;
        assert(view.read_blob_into(reset::kAppNamespace, reset::kAppConfigKey, blob, sizeof(blob), &length) == B::Ok && length == 3);
        assert(blob[0] == expected && blob[1] == 0 && blob[2] == 'A');
        char text[8];
        assert(view.read_string_into(reset::kNetworkNamespace, "network", text, sizeof(text), &length) == T::Ok);
        assert(!std::strcmp(text, expected == 1 ? "old" : "new"));
        return true; },
                                          &revision));
}
int main(int argc, char** argv)
{
    const bool fresh = argc == 2;
    char pattern[] = "/tmp/trailmate-settings-transaction-XXXXXX";
    const auto* directory = fresh ? argv[1] : mkdtemp(pattern);
    assert(directory);
    setenv("TRAIL_MATE_SETTINGS_ROOT", directory, 1);
    if (fresh)
    {
        verify(2);
        return 0;
    }
    const native::SettingsMutation app[] = {{reset::kAppConfigKey, 'x', std::string("\1\0A", 3)}};
    const native::SettingsMutation settings[] = {{"revision", 'u', "1"}, {"byte", 'u', "255"}, {"empty", 'x', ""}};
    const native::SettingsMutation network[] = {{"network", 's', "old"}};
    const native::SettingsNamespaceBatch initial[] = {{reset::kAppNamespace, app, 1}, {"settings", settings, 3}, {reset::kNetworkNamespace, network, 1}};
    assert(native::write_settings_transaction(initial, 3));
    verify(1);
    assert(native::read_settings_snapshot([](void*, const native::SettingsReadView& view)
                                          {
        uint32_t revision = 0;
        assert(view.read_uint_into("settings", "revision", &revision) == S::Ok && revision == 1);
        uint8_t byte = 0;
        assert(view.read_u8_into("settings", "byte", &byte) == S::Ok && byte == UINT8_MAX);
        Database writer;
        writer.sql("BEGIN IMMEDIATE; UPDATE settings SET payload=x'020041' WHERE namespace='linux_app_facade' AND key='app_config_v1';"
                   "UPDATE settings SET payload=CAST('2' AS BLOB) WHERE namespace='settings' AND key='revision';"
                   "UPDATE settings SET payload=CAST('new' AS BLOB) WHERE namespace='linux_reticulum' AND key='network'; COMMIT;");
        uint8_t blob[3]; size_t length = 0;
        assert(view.read_blob_into(reset::kAppNamespace, reset::kAppConfigKey, blob, sizeof(blob), &length) == B::Ok && blob[0] == 1);
        char text[8];
        assert(view.read_string_into(reset::kNetworkNamespace, "network", text, sizeof(text), &length) == T::Ok && !std::strcmp(text, "old"));
        return true; },
                                          nullptr));
    verify(2);
    const auto child = fork();
    assert(child >= 0);
    if (!child)
    {
        execl(argv[0], argv[0], directory, nullptr);
        _exit(127);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    {
        Database db;
        db.sql("CREATE TRIGGER abort_final_namespace BEFORE INSERT ON settings WHEN NEW.namespace='linux_reticulum' AND NEW.key='blocked' BEGIN SELECT RAISE(ABORT,'injected'); END;");
    }
    const native::SettingsMutation app_new[] = {{reset::kAppConfigKey, 'x', std::string("\3\0A", 3)}};
    const native::SettingsMutation ui_new[] = {{"revision", 'u', "3"}};
    const native::SettingsMutation denied[] = {{"blocked", 's', "reject"}};
    const native::SettingsNamespaceBatch failed[] = {{reset::kAppNamespace, app_new, 1}, {"settings", ui_new, 1}, {reset::kNetworkNamespace, denied, 1}};
    assert(!native::write_settings_transaction(failed, 3));
    verify(2);
    auto invalid = initial[1];
    invalid.name_space = std::string("settings\0extra", 14);
    assert(!native::write_settings_transaction(&invalid, 1));
    invalid = {"settings", nullptr, 1};
    assert(!native::write_settings_transaction(&invalid, 1));
    assert(!native::write_settings_transaction(nullptr, 1));
    const native::SettingsMutation remove[] = {{"byte", 'u', "", true}};
    assert(native::write_settings_batch("settings", remove, 1));
    {
        Database db;
        db.sql("INSERT INTO settings(namespace,key,kind,payload) VALUES('strict_tx','uint','u',CAST('4294967295' AS BLOB));"
               "INSERT INTO settings(namespace,key,kind,payload) VALUES('strict_tx','overflow','u',CAST('4294967296' AS BLOB));"
               "INSERT INTO settings(namespace,key,kind,payload) VALUES('strict_tx','byte','u',CAST('256' AS BLOB));"
               "INSERT INTO settings(namespace,key,kind,payload) VALUES('strict_tx','blob','x',zeroblob(32));"
               "INSERT INTO settings(namespace,key,kind,payload) VALUES('strict_tx','text','u','3');");
    }
    assert(native::read_settings_snapshot([](void*, const native::SettingsReadView& view)
                                          {
        uint32_t value = 0;
        assert(view.read_uint_into("strict_tx", "uint", &value) == S::Ok && value == UINT32_MAX);
        assert(view.read_uint_into("strict_tx", "overflow", &value) == S::Invalid && value == 0);
        assert(view.read_uint_into("strict_tx", "text", &value) == S::Invalid && value == 0);
        uint8_t byte = 123;
        assert(view.read_u8_into("strict_tx", "byte", &byte) == S::Invalid && byte == 0);
        assert(view.read_u8_into("settings", "byte", &byte) == S::Missing && byte == 0);
        size_t length = 99; std::array<uint8_t, 2> bytes{1, 2};
        assert(view.read_blob_into("strict_tx", "blob", bytes.data(), bytes.size(), &length) == B::Invalid && length == 0 && bytes[0] == 0 && bytes[1] == 0);
        assert(view.read_blob_into("settings", "empty", nullptr, 0, &length) == B::Ok && length == 0);
        return true; },
                                          nullptr));
    assert(store::put_string("unrelated", "guard", "old"));
    const uint8_t synthetic_defaults[] = {4, 0, 'A'};
    assert(reset::commit_factory_reset(synthetic_defaults, sizeof(synthetic_defaults)));
    const native::SettingsMutation unrelated[] = {{"guard", 's', "new"}};
    const native::SettingsNamespaceBatch fenced[] = {{"unrelated", unrelated, 1}, {"settings", ui_new, 1}};
    assert(!native::write_settings_transaction(fenced, 2));
    std::string guard;
    assert(store::get_string("unrelated", "guard", guard) && guard == "old");
    assert(native::write_settings_batch("unrelated", unrelated, 1));
    assert(store::get_string("unrelated", "guard", guard) && guard == "new");
    std::filesystem::remove_all(directory);
}
