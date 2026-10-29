#pragma once
#include "platform/linux/hostlink_upload_io.h"
#include <stdexcept>
#include <vector>

namespace platform::linux_runtime
{
// This marked namespace contains ONLY producer workspaces, never input PBFs
// or published maps. The lease is inherited by the producer so parent death
// cannot let recovery delete an orphan producer's live files.
class TmapJobWorkspace
{
  public:
    explicit TmapJobWorkspace(const std::filesystem::path& storage) : storage_(storage), io_(storage)
    {
        if (!io_.mkdirs(root())) throw std::runtime_error("Cannot prepare TMAP workspace storage.");
        directory_.value = io_.directory(root());
        struct stat info
        {
        };
        if (directory_.value < 0 || fstat(directory_.value, &info) || info.st_uid != geteuid() || (info.st_mode & 077))
            throw std::runtime_error("TMAP workspace directory is not private.");
        device_ = info.st_dev;
        lease_.value = openat(directory_.value, ".owner", O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
        if (lease_.value < 0 || fstat(lease_.value, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_uid != geteuid())
            throw std::runtime_error("Cannot open TMAP workspace ownership record.");
        if (flock(lease_.value, LOCK_EX | LOCK_NB))
        {
            if (errno != EWOULDBLOCK && errno != EAGAIN) throw std::runtime_error("Cannot lock TMAP workspace.");
            return;
        }
        constexpr char magic[] = "trailmate-tmap-jobs-v1\n";
        if (info.st_size == 0)
        {
            // An interrupted first initialization can be retried only while
            // the namespace is empty; never adopt an unknown nonempty tree.
            for (const auto& name : names(directory_.value))
                if (name != ".owner") throw std::runtime_error("Unmarked TMAP workspace contains unknown files.");
            if (pwrite(lease_.value, magic, sizeof(magic) - 1, 0) != sizeof(magic) - 1 || fsync(lease_.value) || fsync(directory_.value))
                throw std::runtime_error("Cannot persist TMAP workspace ownership.");
        }
        else
        {
            char value[sizeof(magic)]{};
            if (info.st_size != sizeof(magic) - 1 || pread(lease_.value, value, sizeof(value), 0) != sizeof(magic) - 1 || std::strcmp(value, magic))
                throw std::runtime_error("TMAP workspace ownership does not match.");
        }
        ready_ = true;
    }
    ~TmapJobWorkspace()
    {
        if (!job_.empty())
        {
            try
            {
                remove_tree(directory_.value, job_, device_);
            }
            catch (...)
            {
            }
        }
    }
    TmapJobWorkspace(const TmapJobWorkspace&) = delete;
    TmapJobWorkspace& operator=(const TmapJobWorkspace&) = delete;
    bool ready() const { return ready_; }
    int lease() const { return lease_.value; }
    static const char* root() { return "/trailmate/.tmap-jobs"; }
    void recover()
    {
        if (!ready_) throw std::runtime_error("Another TMAP producer still owns its workspace.");
        for (const auto& name : names(directory_.value))
            if (job_name(name)) remove_tree(directory_.value, name, device_);
        if (fsync(directory_.value)) throw std::runtime_error("Cannot sync TMAP workspace recovery.");
    }
    void create()
    {
        recover();
        auto pattern = (storage_ / "trailmate/.tmap-jobs/job-XXXXXX").string();
        if (!mkdtemp(pattern.data())) throw std::runtime_error("Cannot create private TMAP job.");
        job_ = std::filesystem::path(pattern).filename().string();
        if (fsync(directory_.value)) throw std::runtime_error("Cannot persist TMAP job directory.");
    }
    std::filesystem::path path() const { return storage_ / "trailmate/.tmap-jobs" / job_; }
    std::string relative() const { return std::string(root()) + '/' + job_; }

  private:
    struct Descriptor
    {
        int value = -1;
        ~Descriptor()
        {
            if (value >= 0) ::close(value);
        }
    };
    static bool job_name(const std::string& name)
    {
        return name.size() == 10 && name.rfind("job-", 0) == 0 &&
               name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 4) == std::string::npos;
    }
    static std::vector<std::string> names(int fd)
    {
        // openat(".") gives an independent directory cursor, unlike dup().
        const int copy = openat(fd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        DIR* directory = copy < 0 ? nullptr : fdopendir(copy);
        if (!directory)
        {
            if (copy >= 0) ::close(copy);
            throw std::runtime_error("Cannot scan TMAP workspace.");
        }
        std::vector<std::string> result;
        errno = 0;
        while (const auto* entry = readdir(directory))
            if (std::strcmp(entry->d_name, ".") && std::strcmp(entry->d_name, "..")) result.emplace_back(entry->d_name);
        const int error = errno;
        closedir(directory);
        if (error) throw std::runtime_error("Cannot finish TMAP workspace scan.");
        return result;
    }
    static void remove_tree(int parent, const std::string& name, dev_t device)
    {
        struct stat info
        {
        };
        if (fstatat(parent, name.c_str(), &info, AT_SYMLINK_NOFOLLOW))
        {
            if (errno == ENOENT) return;
            throw std::runtime_error("Cannot inspect interrupted TMAP job.");
        }
        if (S_ISDIR(info.st_mode))
        {
            Descriptor directory;
            directory.value = openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            struct stat opened
            {
            };
            if (directory.value < 0 || fstat(directory.value, &opened) || opened.st_dev != device ||
                opened.st_dev != info.st_dev || opened.st_ino != info.st_ino || opened.st_uid != geteuid())
                throw std::runtime_error("Interrupted TMAP job directory changed.");
            for (const auto& child : names(directory.value)) remove_tree(directory.value, child, device);
            if (unlinkat(parent, name.c_str(), AT_REMOVEDIR)) throw std::runtime_error("Cannot remove interrupted TMAP job directory.");
        }
        else if (unlinkat(parent, name.c_str(), 0)) throw std::runtime_error("Cannot remove interrupted TMAP job file.");
        if (fsync(parent)) throw std::runtime_error("Cannot sync interrupted TMAP job cleanup.");
    }
    std::filesystem::path storage_;
    UploadIo io_;
    Descriptor directory_, lease_;
    dev_t device_{};
    bool ready_ = false;
    std::string job_;
};
} // namespace platform::linux_runtime
