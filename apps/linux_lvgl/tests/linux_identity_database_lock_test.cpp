#include "platform/linux/mesh/linux_reticulum_identity.h"
#include <cassert>
#include <fcntl.h>
#include <filesystem>
#include <sqlite3.h>
#include <sys/wait.h>
#include <unistd.h>

bool reservedLockHeld(const std::filesystem::path& path)
{
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        const int file = open(path.c_str(), O_RDWR);
        struct flock lock
        {
        };
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0x40000001;
        lock.l_len = 1;
        const bool held = file >= 0 && fcntl(file, F_GETLK, &lock) == 0 && lock.l_type != F_UNLCK;
        _exit(held ? 0 : 1);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-identity-lock-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    const auto path = root / "identity.sqlite3";
    sqlite3* database = nullptr;
    assert(sqlite3_open(path.c_str(), &database) == SQLITE_OK);
    assert(sqlite3_exec(database, "PRAGMA journal_mode=DELETE; CREATE TABLE fixture(x); BEGIN IMMEDIATE; INSERT INTO fixture VALUES(1);", nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(reservedLockHeld(path));
    platform::linux_runtime::LinuxReticulumIdentity identity(path);
    assert(!identity.begin());      // Another connection owns the pending writer.
    assert(reservedLockHeld(path)); // Identity validation must not unlock it.
    assert(sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(database);
    std::filesystem::remove_all(root);
}
