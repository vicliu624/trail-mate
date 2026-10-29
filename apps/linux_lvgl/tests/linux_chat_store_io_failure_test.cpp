#include "chat/linux_sqlite_chat_store.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sqlite3.h>
#include <unordered_map>
#include <unistd.h>

namespace
{
struct Intercept
{
    const sqlite3_io_methods* original;
    sqlite3_io_methods methods;
    bool wal;
};
std::unordered_map<sqlite3_file*, std::unique_ptr<Intercept>> files;
sqlite3_vfs* original_vfs;
bool fail_sync = false;
unsigned injected = 0;
int syncFile(sqlite3_file* file, int flags)
{
    auto& entry = *files.at(file);
    if (entry.wal && fail_sync)
    {
        fail_sync = false;
        ++injected;
        return SQLITE_IOERR_FSYNC;
    }
    return entry.original->xSync(file, flags);
}
int closeFile(sqlite3_file* file)
{
    const auto* original = files.at(file)->original;
    const int result = original->xClose(file);
    files.erase(file);
    return result;
}
int openFile(sqlite3_vfs*, const char* name, sqlite3_file* file, int flags, int* out_flags)
{
    const int result = original_vfs->xOpen(original_vfs, name, file, flags, out_flags);
    if (result != SQLITE_OK) return result;
    auto entry = std::make_unique<Intercept>();
    entry->original = file->pMethods;
    entry->methods = *file->pMethods;
    entry->wal = (flags & SQLITE_OPEN_WAL) != 0;
    entry->methods.xSync = syncFile;
    entry->methods.xClose = closeFile;
    file->pMethods = &entry->methods;
    files.emplace(file, std::move(entry));
    return result;
}
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-chat-io-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    trailmate::linux_app::LinuxSqliteChatStore store;
    chat::IChatStore& port = store;
    chat::ChatMessage message;
    message.msg_id = 1;
    message.from = message.peer = 17;
    message.text = "before disk failure";
    assert(port.appendIncomingDurably(message));
    original_vfs = sqlite3_vfs_find(nullptr);
    assert(original_vfs);
    sqlite3_vfs intercept = *original_vfs;
    intercept.zName = "trailmate-test-failed-wal-sync";
    intercept.xOpen = openFile;
    assert(sqlite3_vfs_register(&intercept, 1) == SQLITE_OK);
    message.msg_id = 2;
    message.text = "write during disk failure";
    fail_sync = true;
    assert(!port.appendIncomingDurably(message));
    assert(injected == 1 && files.empty());
    assert(sqlite3_vfs_register(original_vfs, 1) == SQLITE_OK);
    assert(sqlite3_vfs_unregister(&intercept) == SQLITE_OK);
    // A failed sync can leave the commit outcome uncertain. A successful retry
    // must establish durability without a second row or unread increment.
    assert(port.appendIncomingDurably(message));
    assert(port.appendIncomingDurably(message));
    const auto conversation = chat::conversationIdForMessage(message);
    assert(port.loadRecent(conversation, 10).size() == 2);
    assert(port.getUnread(conversation) == 2);
    std::filesystem::remove_all(root);
}
