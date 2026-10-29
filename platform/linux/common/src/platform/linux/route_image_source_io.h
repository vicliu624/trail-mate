#pragma once
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>

namespace platform::linux_runtime
{
class RouteImageLease
{
  public:
    explicit RouteImageLease(const char* path)
    {
        fd_ = ::open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
        struct stat info{};
        if (fd_ >= 0 && (fstat(fd_, &info) != 0 || !S_ISREG(info.st_mode) || flock(fd_, LOCK_EX | LOCK_NB) != 0))
        {
            ::close(fd_);
            fd_ = -1;
        }
    }
    ~RouteImageLease() { if (fd_ >= 0) ::close(fd_); }
    explicit operator bool() const { return fd_ >= 0; }
    RouteImageLease(const RouteImageLease&) = delete;
    RouteImageLease& operator=(const RouteImageLease&) = delete;
  private:
    int fd_ = -1;
};
// Native filesystem handles for the shared integrity/binding contracts.
class RouteSourceIo
{
  public:
    enum class Mode { Read, CreateTemporary };
    enum class OpenStatus { Ready, Missing, Unavailable };
    struct OpenResult { void* handle; OpenStatus status; };
    ~RouteSourceIo() { if (fd_ >= 0) ::close(fd_); }
    OpenResult open(const char* path, Mode mode)
    {
        if (fd_ >= 0) return {nullptr, OpenStatus::Unavailable};
        fd_ = ::open(path, O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | (mode == Mode::Read ? O_RDONLY : O_WRONLY | O_CREAT | O_EXCL), 0600);
        if (fd_ < 0) return {nullptr, errno == ENOENT ? OpenStatus::Missing : OpenStatus::Unavailable};
        struct stat info{};
        if (fstat(fd_, &info) != 0 || !S_ISREG(info.st_mode)) { close(this); return {nullptr, OpenStatus::Unavailable}; }
        return {this, OpenStatus::Ready};
    }
    bool close(void*)
    {
        const int fd = fd_;
        fd_ = -1;
        return fd >= 0 && ::close(fd) == 0;
    }
    bool size(void*, uint64_t& bytes)
    {
        struct stat info{};
        if (fstat(fd_, &info) != 0 || info.st_size < 0) return false;
        bytes = static_cast<uint64_t>(info.st_size);
        return true;
    }
    bool seek(void*, uint64_t position) { return position <= INT64_MAX && lseek(fd_, static_cast<off_t>(position), SEEK_SET) >= 0; }
    size_t read(void*, uint8_t* output, size_t bytes)
    {
        ssize_t count;
        do { count = ::read(fd_, output, bytes); } while (count < 0 && errno == EINTR);
        return count < 0 ? 0 : static_cast<size_t>(count);
    }
    size_t write(void*, const uint8_t* input, size_t bytes)
    {
        size_t done = 0;
        while (done < bytes)
        {
            const auto count = ::write(fd_, input + done, bytes - done);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            done += static_cast<size_t>(count);
        }
        return done;
    }
    bool sync(void*) { return fsync(fd_) == 0; }
    bool remove(const char* path) { return ::unlink(path) == 0; }
    bool rename(const char* from, const char* to) { return ::rename(from, to) == 0; }
  private:
    int fd_ = -1;
};
class RouteSourceHash
{
  public:
    RouteSourceHash() : context_(EVP_MD_CTX_new()) {}
    ~RouteSourceHash() { EVP_MD_CTX_free(context_); }
    bool start() { return context_ && EVP_DigestInit_ex(context_, EVP_sha256(), nullptr) == 1; }
    bool update(const uint8_t* data, size_t bytes) { return context_ && EVP_DigestUpdate(context_, data, bytes) == 1; }
    bool finish(uint8_t* output)
    {
        unsigned size = 0;
        return context_ && EVP_DigestFinal_ex(context_, output, &size) == 1 && size == 32;
    }
    void reset() { if (context_) EVP_MD_CTX_reset(context_); }
  private:
    EVP_MD_CTX* context_;
};
} // namespace platform::linux_runtime
#endif
