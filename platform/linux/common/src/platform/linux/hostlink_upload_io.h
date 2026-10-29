#pragma once
#include "track_sharing/upload_space_budget.h"
#include <array>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <linux/fs.h>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace platform::linux_runtime
{
// Descriptor-relative paths reject every symlink below the configured root.
// CreateTemporary rewrites alternating journal slots; candidates are separately
// created with Exclusive. All open handles are scoped to one shared-core call.
class UploadIo
{
  public:
    enum class Mode
    {
        Read,
        Update,
        CreateTemporary,
        Exclusive
    };
    enum class OpenStatus
    {
        Ready,
        Missing,
        Unavailable
    };
    struct OpenResult
    {
        void* handle;
        OpenStatus status;
    };
    explicit UploadIo(std::filesystem::path root) : path_(std::move(root))
    {
        root_ = ::open(path_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (root_ >= 0 && fstat(root_, &identity_) != 0)
        {
            ::close(root_);
            root_ = -1;
        }
    }
    ~UploadIo()
    {
        close(nullptr);
        if (lease_ >= 0) ::close(lease_);
        if (root_ >= 0) ::close(root_);
    }
    bool current() const
    {
        struct stat info
        {
        };
        return root_ >= 0 && ::stat(path_.c_str(), &info) == 0 && info.st_dev == identity_.st_dev && info.st_ino == identity_.st_ino;
    }
    bool mkdirs(const char* path)
    {
        if (!current() || !path || path[0] != '/') return false;
        int directory = fcntl(root_, F_DUPFD_CLOEXEC, 0);
        for (const auto& part : std::filesystem::path(path + 1))
        {
            const auto name = part.string();
            if (name.empty() || name == "." || name == "..")
            {
                ::close(directory);
                return false;
            }
            if (mkdirat(directory, name.c_str(), 0700) != 0 && errno != EEXIST)
            {
                ::close(directory);
                return false;
            }
            const int next = openat(directory, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            const bool synced = fsync(directory) == 0;
            ::close(directory);
            if (next < 0 || !synced)
            {
                if (next >= 0) ::close(next);
                return false;
            }
            directory = next;
        }
        ::close(directory);
        return current();
    }
    bool lock()
    {
        if (lease_ >= 0) return current();
        if (!mkdirs("/trailmate")) return false;
        std::string name;
        const int directory = parent("/trailmate/.hostlink-upload.lock", name);
        if (directory < 0) return false;
        lease_ = openat(directory, name.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
        ::close(directory);
        struct stat info
        {
        };
        if (lease_ < 0 || fstat(lease_, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
            flock(lease_, LOCK_EX | LOCK_NB) != 0)
        {
            if (lease_ >= 0) ::close(lease_);
            lease_ = -1;
            return false;
        }
        return true;
    }
    struct ScanEntry
    {
        std::string name;
        bool regular = false;
    };
    // Worker-owned descriptor cursor support. Parent traversal retains the
    // same captured-root and no-follow policy as ordinary file operations.
    int directory(const char* path) const
    {
        if(!path || !*path) {errno=EINVAL;return -1;}
        std::string ignored;
        const auto probe=std::string(path)+"/placeholder";
        return parent(probe.c_str(),ignored);
    }
    enum class ScanStatus
    {
        Ready,
        Missing,
        Invalid,
        Unavailable
    };
    ScanStatus scan(const char* path, uint32_t start, std::array<ScanEntry, 4>& entries, size_t& count, bool& ended, uint32_t limit = 192)
    {
        count = 0;
        ended = false;
        if (!limit || limit > 128U * 1024U * 1024U / 32 || start > limit) return ScanStatus::Invalid;
        std::string ignored;
        const auto probe = std::string(path) + "/placeholder";
        const int fd = parent(probe.c_str(), ignored);
        if (fd < 0) return errno == ENOENT ? ScanStatus::Missing : ScanStatus::Unavailable;
        DIR* directory = fdopendir(fd);
        if (!directory)
        {
            ::close(fd);
            return ScanStatus::Unavailable;
        }
        struct Release
        {
            DIR* directory;
            ~Release() { closedir(directory); }
        } release{directory};
        uint32_t seen = 0;
        while (count < entries.size())
        {
            errno = 0;
            const auto* entry = readdir(directory);
            if (!entry)
            {
                if (errno) return ScanStatus::Unavailable;
                if (seen < start) return ScanStatus::Invalid;
                ended = true;
                return ScanStatus::Ready;
            }
            if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
            if (seen++ < start) continue;
            if (seen > limit) return ScanStatus::Invalid;
            struct stat info
            {
            };
            if (fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) return ScanStatus::Unavailable;
            entries[count++] = {entry->d_name, S_ISREG(info.st_mode) && info.st_nlink == 1};
        }
        return ScanStatus::Ready;
    }
    uint32_t media_token(const uint8_t* session, uint32_t generation, const uint8_t* author, const char* directory) const
    {
        // Opaque pagination binding, never an authorization credential. Real
        // descriptor/inode checks still guard every read and delete operation.
        uint32_t hash = 2166136261U;
        const auto add = [&](uint64_t value)
        { for (unsigned i = 0; i < 8; ++i) { hash = (hash ^ uint8_t(value)) * 16777619U; value >>= 8; } };
        add(uint64_t(identity_.st_dev));
        add(uint64_t(identity_.st_ino));
        add(generation);
        for (unsigned i = 0; i < 16; ++i) add(session[i]);
        for (unsigned i = 0; i < 32; ++i) add(author[i]);
        std::string ignored;
        const auto probe = std::string(directory) + "/placeholder";
        const int fd = parent(probe.c_str(), ignored);
        if (fd < 0)
        {
            if (errno != ENOENT) return 0;
            add(0);
        }
        else
        {
            struct stat info
            {
            };
            const bool valid = fstat(fd, &info) == 0;
            ::close(fd);
            if (!valid) return 0;
            add(uint64_t(info.st_dev));
            add(uint64_t(info.st_ino));
            add(uint64_t(info.st_mtim.tv_sec));
            add(uint64_t(info.st_mtim.tv_nsec));
            add(uint64_t(info.st_ctim.tv_sec));
            add(uint64_t(info.st_ctim.tv_nsec));
        }
        return hash ? hash : 1;
    }
    OpenResult open(const char* path, Mode mode)
    {
        if (fd_ >= 0 || !current()) return {nullptr, OpenStatus::Unavailable};
        std::string name;
        parent_ = parent(path, name);
        if (parent_ < 0) return {nullptr, errno == ENOENT ? OpenStatus::Missing : OpenStatus::Unavailable};
        int flags = O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | (mode == Mode::Read ? O_RDONLY : O_RDWR);
        if (mode == Mode::CreateTemporary || mode == Mode::Exclusive) flags |= O_CREAT;
        if (mode == Mode::Exclusive) flags |= O_EXCL;
        fd_ = openat(parent_, name.c_str(), flags, 0600);
        const int error = errno;
        const bool missing = fd_ < 0 && error == ENOENT;
        struct stat info
        {
        };
        if (fd_ < 0 || fstat(fd_, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
            (mode == Mode::CreateTemporary && ftruncate(fd_, 0) != 0))
        {
            close(nullptr);
            return {nullptr, missing ? OpenStatus::Missing : OpenStatus::Unavailable};
        }
        return {this, OpenStatus::Ready};
    }
    bool close(void*)
    {
        const int fd = fd_, directory = parent_;
        fd_ = parent_ = -1;
        const bool closed = fd >= 0 && ::close(fd) == 0;
        if (directory >= 0) ::close(directory);
        return closed;
    }
    bool size(void*, uint64_t& bytes)
    {
        struct stat info
        {
        };
        if (!current() || fstat(fd_, &info) != 0 || info.st_size < 0) return false;
        bytes = static_cast<uint64_t>(info.st_size);
        return true;
    }
    struct FileStamp
    {
        dev_t device{};
        ino_t inode{};
        off_t bytes{};
        timespec modified{}, changed{};
        bool operator==(const FileStamp& other) const
        {
            return device == other.device && inode == other.inode && bytes == other.bytes &&
                   modified.tv_sec == other.modified.tv_sec && modified.tv_nsec == other.modified.tv_nsec &&
                   changed.tv_sec == other.changed.tv_sec && changed.tv_nsec == other.changed.tv_nsec;
        }
    };
    bool stamp(FileStamp& output) const
    {
        struct stat info{};
        if (!current() || fstat(fd_, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 0) return false;
        output = {info.st_dev, info.st_ino, info.st_size, info.st_mtim, info.st_ctim};
        return true;
    }
    bool seek(void*, uint64_t offset) { return current() && offset <= INT64_MAX && lseek(fd_, offset, SEEK_SET) >= 0; }
    size_t read(void*, uint8_t* bytes, size_t count)
    {
        if (!current()) return 0;
        ssize_t result;
        do
        {
            result = ::read(fd_, bytes, count);
        } while (result < 0 && errno == EINTR);
        return result < 0 ? 0 : size_t(result);
    }
    size_t write(void*, const uint8_t* bytes, size_t count)
    {
        size_t done = 0;
        while (current() && done < count)
        {
            const auto result = ::write(fd_, bytes + done, count - done);
            if (result < 0 && errno == EINTR) continue;
            if (result <= 0) break;
            done += size_t(result);
        }
        return done;
    }
    bool sync(void*) { return current() && fsync(fd_) == 0 && fsync(parent_) == 0; }
    bool lock_open(void*) { return current() && fd_ >= 0 && flock(fd_, LOCK_EX | LOCK_NB) == 0; }
    bool truncate(void*, uint64_t bytes) { return current() && bytes <= INT64_MAX && ftruncate(fd_, off_t(bytes)) == 0; }
    bool still_named(const char* path)
    {
        std::string name;
        const int directory = parent(path, name);
        if (directory < 0) return false;
        struct stat opened
        {
        }, named{};
        const bool same = fstat(fd_, &opened) == 0 && fstatat(directory, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
                          S_ISREG(named.st_mode) && named.st_nlink == 1 && opened.st_dev == named.st_dev && opened.st_ino == named.st_ino;
        ::close(directory);
        return same;
    }
    bool remove(const char* path)
    {
        std::string name;
        const int directory = parent(path, name);
        if (directory < 0) return false;
        const bool removed = unlinkat(directory, name.c_str(), 0) == 0 && fsync(directory) == 0;
        ::close(directory);
        return removed;
    }
    void unlock()
    {
        if (lease_ >= 0) ::close(lease_);
        lease_ = -1;
    }
    bool publish(const char* from, const char* to)
    {
        std::string source, destination;
        const int source_directory = parent(from, source);
        if (source_directory < 0) return false;
        const int destination_directory = parent(to, destination);
        if (destination_directory < 0)
        {
            ::close(source_directory);
            return false;
        }
        bool published = false;
#if defined(SYS_renameat2)
        if (::syscall(SYS_renameat2, source_directory, source.c_str(), destination_directory, destination.c_str(), RENAME_NOREPLACE) == 0) published = true;
        else if (errno == ENOSYS)
#endif
        {
            published = ::linkat(source_directory, source.c_str(), destination_directory, destination.c_str(), 0) == 0 &&
                        ::unlinkat(source_directory, source.c_str(), 0) == 0;
        }
        const bool synced = published && fsync(source_directory) == 0 && fsync(destination_directory) == 0;
        ::close(source_directory);
        ::close(destination_directory);
        return synced && current();
    }
    bool admit_directory(const char* path, const char* wanted, unsigned maximum)
    {
        std::string ignored;
        const auto probe = std::string(path) + "/placeholder";
        const int fd = parent(probe.c_str(), ignored);
        if (fd < 0) return false;
        DIR* directory = fdopendir(fd);
        if (!directory)
        {
            ::close(fd);
            return false;
        }
        unsigned count = 0;
        bool matched = false, valid = true;
        errno = 0;
        while (const auto* entry = readdir(directory))
        {
            if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
            if (++count > maximum)
            {
                valid = false;
                break;
            }
            if (!std::strcmp(entry->d_name, wanted))
            {
                struct stat info
                {
                };
                if (fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(info.st_mode))
                {
                    valid = false;
                    break;
                }
                matched = true;
            }
            errno = 0;
        }
        valid = valid && errno == 0 && (matched || count < maximum);
        closedir(directory);
        return valid && current();
    }
    bool admit_copy(const char* temporary, const char* final, uint32_t bytes, uint64_t limit, unsigned file_limit, bool route_directory = false)
    {
        const std::filesystem::path temporary_path(temporary), final_path(final);
        if (temporary_path.parent_path() != final_path.parent_path() || !bytes) return false;
        std::string final_name;
        const int fd = parent(final, final_name);
        if (fd < 0) return false;
        DIR* directory = fdopendir(fd);
        if (!directory)
        {
            ::close(fd);
            return false;
        }
        const auto temporary_name = temporary_path.filename().string();
        uint64_t total = 0, partial = 0;
        unsigned files = 0;
        bool has_final = false, has_temporary = false, valid = true;
        errno = 0;
        while (const auto* entry = readdir(directory))
        {
            if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
            struct stat info
            {
            };
            if (fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0)
            {
                valid = false;
                break;
            }
            if (route_directory && S_ISDIR(info.st_mode) && (!std::strcmp(entry->d_name, ".trailmate") || !std::strcmp(entry->d_name, "navigation"))) continue;
            if (!S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 0 || ++files > file_limit || uint64_t(info.st_size) > limit - total)
            {
                valid = false;
                break;
            }
            total += uint64_t(info.st_size);
            if (final_name == entry->d_name) has_final = true;
            if (temporary_name == entry->d_name)
            {
                has_temporary = true;
                partial = uint64_t(info.st_size);
                if (partial > bytes)
                {
                    valid = false;
                    break;
                }
            }
            errno = 0;
        }
        valid = valid && errno == 0 && !(!has_final && !has_temporary && files == file_limit) &&
                (has_final ? 0 : bytes - partial) <= limit - total;
        closedir(directory);
        return valid && current();
    }
    bool admit(track_sharing::UploadKind kind, uint32_t bytes)
    {
        std::string ignored;
        const auto path = std::string(track_sharing::uploadDirectory(kind)) + "/placeholder";
        const int fd = parent(path.c_str(), ignored);
        if (fd < 0) return false;
        DIR* directory = fdopendir(fd);
        if (!directory)
        {
            ::close(fd);
            return false;
        }
        track_sharing::UploadSpaceBudget budget(bytes, kind);
        bool valid = true;
        errno = 0;
        while (const auto* entry = readdir(directory))
        {
            if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
            struct stat info
            {
            };
            if (fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
                info.st_size < 0 || !budget.add(info.st_size, false))
            {
                valid = false;
                break;
            }
            errno = 0;
        }
        valid = valid && errno == 0 && budget.complete();
        closedir(directory);
        return valid;
    }

  private:
    int parent(const char* path, std::string& name) const
    {
        if (!current() || !path || path[0] != '/')
        {
            errno = EINVAL;
            return -1;
        }
        const std::filesystem::path relative(path + 1);
        for (const auto& part : relative)
            if (part.empty() || part == "." || part == "..")
            {
                errno = EINVAL;
                return -1;
            }
        name = relative.filename().string();
        if (name.empty())
        {
            errno = EINVAL;
            return -1;
        }
        int directory = fcntl(root_, F_DUPFD_CLOEXEC, 0);
        for (const auto& part : relative.parent_path())
        {
            const int next = openat(directory, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            const int error = errno;
            ::close(directory);
            if (next < 0)
            {
                errno = error;
                return -1;
            }
            directory = next;
        }
        return directory;
    }
    std::filesystem::path path_;
    struct stat identity_
    {
    };
    int root_ = -1, fd_ = -1, parent_ = -1, lease_ = -1;
};
} // namespace platform::linux_runtime
