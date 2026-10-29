#include "app/linux_phone_bluetooth_config.h"
#include "platform/linux/runtime_paths.h"
#include "platform/ui/settings_store.h"
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <sqlite3.h>
#include <stdexcept>
#include <unistd.h>

using trailmate::linux_app::LinuxPhoneBluetoothConfig;
namespace store = platform::ui::settings_store;
namespace
{
void sql(const char* text)
{
    sqlite3* database = nullptr;
    assert(sqlite3_open(platform::linux_runtime::sqlite_database_path().c_str(), &database) == SQLITE_OK);
    assert(sqlite3_exec(database, text, nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(sqlite3_close(database) == SQLITE_OK);
}
} // namespace
int main()
{
    char temporary[] = "/tmp/trailmate-phone-bt-XXXXXX";
    assert(mkdtemp(temporary));
    setenv("TRAIL_MATE_SETTINGS_ROOT", temporary, 1);
    bool enabled = false;
    LinuxPhoneBluetoothConfig config([&]
                                     { return enabled; });
    assert(config.ready() && config.value().mode == meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN);
    auto next = config.value();
    next.enabled = true;
    next.mode = meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN;
    next.fixed_pin = 123456;
    config.saveBluetoothConfig(next);
    assert(config.revision() == 1);
    LinuxPhoneBluetoothConfig reopened([&]
                                       { return enabled; });
    meshtastic_Config_BluetoothConfig view{};
    assert(reopened.ready() && reopened.value().fixed_pin == 123456);
    assert(reopened.loadBluetoothConfig(&view) && !view.enabled);
    enabled = true;
    assert(reopened.loadBluetoothConfig(&view) && view.enabled);
    sql("CREATE TRIGGER reject_phone_settings BEFORE UPDATE ON settings WHEN NEW.namespace='phone' BEGIN SELECT RAISE(ABORT,'injected failure'); END;");
    next.fixed_pin = 234567;
    bool failed = false;
    try
    {
        config.saveBluetoothConfig(next);
    }
    catch (const std::runtime_error&)
    {
        failed = true;
    }
    assert(failed && config.value().fixed_pin == 123456 && config.revision() == 1);
    LinuxPhoneBluetoothConfig after_failure([]
                                            { return true; });
    assert(after_failure.ready() && after_failure.value().fixed_pin == 123456);
    sql("DROP TRIGGER reject_phone_settings;");
    next.fixed_pin = 12;
    failed = false;
    try
    {
        config.saveBluetoothConfig(next);
    }
    catch (const std::invalid_argument&)
    {
        failed = true;
    }
    assert(failed && config.value().fixed_pin == 123456);
    next.mode = meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
    next.fixed_pin = 123456;
    config.saveBluetoothConfig(next);
    assert(config.value().fixed_pin == 0);
    assert(store::put_string("phone", "bluetooth_v1", "wrong kind"));
    LinuxPhoneBluetoothConfig wrong_kind([]
                                         { return true; });
    assert(!wrong_kind.ready() && !wrong_kind.loadBluetoothConfig(&view));
    sql("UPDATE settings SET kind='x',payload=zeroblob(1000000) WHERE namespace='phone';");
    std::array<uint8_t, 32> output;
    output.fill(255);
    size_t length = 99;
    assert(store::read_blob_into("phone", "bluetooth_v1", output.data(), output.size(), &length) == store::BlobReadResult::Invalid);
    assert(length == 0 && output[0] == 0);
    LinuxPhoneBluetoothConfig oversized([]
                                        { return false; });
    assert(!oversized.ready());
    const std::array<uint8_t, 5> corrupt{'B', 'L', 'C', 1, 255};
    assert(store::put_blob("phone", "bluetooth_v1", corrupt.data(), corrupt.size()));
    LinuxPhoneBluetoothConfig invalid_wire([]
                                           { return true; });
    assert(!invalid_wire.ready());
    sql("UPDATE settings SET payload=zeroblob(0) WHERE namespace='phone';");
    assert(store::read_blob_into("phone", "bluetooth_v1", nullptr, 0, &length) == store::BlobReadResult::Ok && length == 0);
    const auto unavailable = std::filesystem::path(temporary) / "not-a-directory";
    assert(std::filesystem::copy_file(platform::linux_runtime::sqlite_database_path(), unavailable));
    setenv("TRAIL_MATE_SETTINGS_ROOT", unavailable.c_str(), 1);
    LinuxPhoneBluetoothConfig io_failure([]
                                         { return true; });
    assert(!io_failure.ready() && !io_failure.error().empty());
    std::filesystem::remove_all(temporary);
}
