#include "platform/linux/mesh/linux_sqlite_mesh_identity_store.h"
#include "platform/linux/runtime_paths.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <sqlite3.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

struct sqlite3_api_routines;
namespace native = platform::linux_runtime;
using Store = native::mesh::LinuxSqliteLocalIdentityStore;
using Failure = mesh::StoreFailure;
mesh::LocalIdentity identity(uint8_t revision)
{
    mesh::LocalIdentity value;
    value.valid = true;
    std::fill_n(value.public_key, 32, revision);
    std::fill_n(value.private_key, 32, uint8_t(revision + 0x80));
    return value;
}
bool equal(const mesh::LocalIdentity& a, const mesh::LocalIdentity& b)
{
    return a.valid == b.valid && !std::memcmp(a.public_key, b.public_key, 32) && !std::memcmp(a.private_key, b.private_key, 32);
}
void sql(const char* text)
{
    sqlite3* db = nullptr;
    assert(sqlite3_open(native::sqlite_database_path().c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, text, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}
struct Interleave
{
    int go = -1, done = -1;
    bool fired = false;
};
Interleave* interleave = nullptr;
int trace(unsigned, void* context, void* statement, void*)
{
    auto& state = *static_cast<Interleave*>(context);
    auto* query = static_cast<sqlite3_stmt*>(statement);
    if (state.fired || !sqlite3_stmt_readonly(query)) return 0;
    char* text = sqlite3_expanded_sql(query);
    const bool public_read = text && std::strstr(text, "'atomic_identity'") && std::strstr(text, "'pub'");
    sqlite3_free(text);
    if (!public_read) return 0;
    state.fired = true;
    assert(write(state.go, "1", 1) == 1);
    char byte;
    assert(read(state.done, &byte, 1) == 1);
    return 0;
}
extern "C" int attach_trace(sqlite3* db, char**, const sqlite3_api_routines*)
{
    return sqlite3_trace_v2(db, SQLITE_TRACE_PROFILE, trace, interleave);
}
void snapshot(Store& store, const mesh::LocalIdentity& original, const mesh::LocalIdentity& next)
{
    int go[2], done[2];
    assert(pipe(go) == 0 && pipe(done) == 0);
    const auto child = fork();
    assert(child >= 0);
    if (!child)
    {
        close(go[1]);
        close(done[0]);
        char byte;
        if (read(go[0], &byte, 1) != 1 || !store.save(next).ok || write(done[1], "1", 1) != 1) _exit(2);
        _exit(0);
    }
    close(go[0]);
    close(done[1]);
    Interleave state{go[1], done[0]};
    interleave = &state;
    assert(sqlite3_auto_extension(reinterpret_cast<void (*)()>(attach_trace)) == SQLITE_OK);
    mesh::LocalIdentity loaded;
    assert(store.load(loaded).ok);
    sqlite3_reset_auto_extension();
    interleave = nullptr;
    close(go[1]);
    close(done[0]);
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(state.fired && equal(loaded, original));
    assert(store.load(loaded).ok && equal(loaded, next));
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    char directory[] = "/tmp/trailmate-local-identity-XXXXXX";
    assert(mkdtemp(directory));
    setenv("TRAIL_MATE_SETTINGS_ROOT", directory, 1);
    Store store({"atomic_identity", "pub", "priv"});
    const auto original = identity(0x11), next = identity(0x22);
    mesh::LocalIdentity loaded;
    assert(store.load(loaded).failure == Failure::NotFound && !loaded.valid);
    assert(store.save(original).ok);
    const std::string mode(argv[1]);
    if (mode == "save")
    {
        sql("CREATE TRIGGER fail_private BEFORE INSERT ON settings WHEN NEW.namespace='atomic_identity' AND NEW.key='priv' BEGIN SELECT RAISE(ABORT,'injected'); END;");
        assert(store.save(next).failure == Failure::IoError);
        assert(store.load(loaded).ok && equal(loaded, original));
        sql("DROP TRIGGER fail_private;");
        assert(store.save(next).ok && store.load(loaded).ok && equal(loaded, next));
    }
    else if (mode == "clear")
    {
        sql("CREATE TRIGGER fail_private BEFORE DELETE ON settings WHEN OLD.namespace='atomic_identity' AND OLD.key='priv' BEGIN SELECT RAISE(ABORT,'injected'); END;");
        assert(store.clear().failure == Failure::IoError);
        assert(store.load(loaded).ok && equal(loaded, original));
        sql("DROP TRIGGER fail_private;");
        assert(store.clear().ok && store.load(loaded).failure == Failure::NotFound);
    }
    else if (mode == "read")
    {
        sql("ALTER TABLE settings RENAME TO saved_settings; CREATE VIEW settings AS SELECT namespace,key,kind,unavailable_function(payload) AS payload,updated_at FROM saved_settings;");
        assert(store.load(loaded).failure == Failure::IoError && !loaded.valid);
        assert(std::all_of(std::begin(loaded.private_key), std::end(loaded.private_key), [](uint8_t byte)
                           { return byte == 0; }));
    }
    else if (mode == "snapshot") snapshot(store, original, next);
    else std::abort();
    std::filesystem::remove_all(directory);
}
