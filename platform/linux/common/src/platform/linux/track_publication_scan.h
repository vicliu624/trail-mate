#pragma once
#include "platform/linux/hostlink_upload_io.h"
#include "track_sharing/publication_slot_path.h"

namespace platform::linux_runtime
{
// Native descriptor cursors avoid restarting an O(n) directory traversal for
// every history slice. Names remain untrusted candidates until authentication.
class TrackPublicationScan
{
  public:
    enum class Result
    {
        Pending,
        Candidate,
        End,
        Unavailable
    };
    explicit TrackPublicationScan(UploadIo& io) : io_(io) {}
    ~TrackPublicationScan()
    {
        if (files_) closedir(files_);
        if (objects_) closedir(objects_);
    }
    Result step(const uint8_t operation[16], track_sharing::PublicationSlotKey& output)
    {
        if (!io_.current()) return Result::Unavailable;
        if (ended_) return Result::End;
        if (!objects_)
        {
            const int directory = io_.directory(track_sharing::kObjectDirectory);
            if (directory < 0)
            {
                if (errno == ENOENT)
                {
                    ended_ = true;
                    return Result::End;
                }
                return Result::Unavailable;
            }
            objects_ = fdopendir(directory);
            if (!objects_)
            {
                ::close(directory);
                return Result::Unavailable;
            }
        }
        if (files_)
        {
            errno = 0;
            const auto* entry = readdir(files_);
            if (!entry)
            {
                const bool good = errno == 0;
                closedir(files_);
                files_ = nullptr;
                return good ? Result::Pending : Result::Unavailable;
            }
            struct stat info
            {
            };
            if (fstatat(dirfd(files_), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) return Result::Unavailable;
            if (!S_ISREG(info.st_mode) || info.st_nlink != 1) return Result::Pending;
            const auto path = object_ + '/' + entry->d_name;
            if (!track_sharing::parsePublicationSlotPath(path.c_str(), path.size(), output) || std::memcmp(output.operation, operation, 16)) return Result::Pending;
            // Both journal slots select the same logical candidate. The
            // candidate reader validates both; do not emit it twice.
            closedir(files_);
            files_ = nullptr;
            return Result::Candidate;
        }
        errno = 0;
        const auto* entry = readdir(objects_);
        if (!entry)
        {
            if (errno) return Result::Unavailable;
            ended_ = true;
            return Result::End;
        }
        const std::string name(entry->d_name);
        if (name.size() != 64) return Result::Pending;
        for (const char byte : name)
            if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) return Result::Pending;
        struct stat info
        {
        };
        if (fstatat(dirfd(objects_), name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) return Result::Unavailable;
        if (!S_ISDIR(info.st_mode)) return Result::Pending;
        object_ = std::string(track_sharing::kObjectDirectory) + '/' + name;
        const int directory = io_.directory(object_.c_str());
        if (directory < 0) return Result::Unavailable;
        files_ = fdopendir(directory);
        if (!files_)
        {
            ::close(directory);
            return Result::Unavailable;
        }
        return Result::Pending;
    }

  private:
    UploadIo& io_;
    DIR* objects_ = nullptr;
    DIR* files_ = nullptr;
    std::string object_;
    bool ended_ = false;
};
} // namespace platform::linux_runtime
