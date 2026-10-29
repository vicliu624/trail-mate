#pragma once
#include "platform/ui/settings_store.h"
#include <string>

struct sqlite3;
namespace platform::linux_runtime
{
struct SettingsNamespaceBatch;
// Native repository seam: callbacks read one stable SQLite snapshot. The view
// cannot escape or be copied, and callbacks must not call other store APIs.
class SettingsReadView
{
  public:
    SettingsReadView(const SettingsReadView&) = delete;
    SettingsReadView& operator=(const SettingsReadView&) = delete;
    ::platform::ui::settings_store::ScalarReadResult read_int_into(const char*, const char*, int32_t*) const;
    ::platform::ui::settings_store::ScalarReadResult read_uint_into(const char*, const char*, uint32_t*) const;
    ::platform::ui::settings_store::ScalarReadResult read_u8_into(const char*, const char*, uint8_t*) const;
    ::platform::ui::settings_store::ScalarReadResult read_bool_into(const char*, const char*, bool*) const;
    ::platform::ui::settings_store::StringReadResult read_string_into(const char*, const char*, char*, std::size_t, std::size_t*) const;
    ::platform::ui::settings_store::BlobReadResult read_blob_into(const char*, const char*, void*, std::size_t, std::size_t*) const;

  private:
    explicit SettingsReadView(sqlite3* database) : database_(database) {}
    sqlite3* database_;
    friend bool read_settings_snapshot(bool (*)(void*, const SettingsReadView&), void*);
    friend bool commit_settings_import_if(const SettingsNamespaceBatch*, std::size_t,
                                          bool (*)(void*, const SettingsReadView&), void*);
};
bool read_settings_snapshot(bool (*read)(void*, const SettingsReadView&), void* context);

struct SettingsMutation
{
    std::string key;
    char kind = 's'; // Existing native kinds: i/u/b/s/x. Payloads remain BLOBs.
    std::string payload;
    bool erase = false;
};
// Applies all mutations to one namespace atomically, under the existing store
// lock and reset fence. A failed mutation leaves the complete old set intact.
bool write_settings_batch(const char* ns, const SettingsMutation*, std::size_t count);

struct SettingsNamespaceBatch
{
    std::string name_space;
    const SettingsMutation* changes = nullptr;
    std::size_t count = 0;
};
// Borrowed arrays remain valid for this blocking call. All namespaces commit
// together or roll back together. This does not quiesce live application owners;
// complete configuration replacement must arrange that lifecycle separately.
bool write_settings_transaction(const SettingsNamespaceBatch*, std::size_t count);
// Complete configuration replacement only: the owner must exclude new frontend
// work/calls and arrange teardown and exec/restart after success. The store lock protects commit
// and admission fencing, so delayed old-process saves cannot overwrite it.
bool commit_settings_import(const SettingsNamespaceBatch*, std::size_t count);
// Predicate reads the same connection after BEGIN IMMEDIATE and before writes.
// It must use only its borrowed view, without calling public settings APIs.
bool commit_settings_import_if(const SettingsNamespaceBatch*, std::size_t count,
                               bool (*admit)(void*, const SettingsReadView&), void*);
bool settings_import_committed();
} // namespace platform::linux_runtime
