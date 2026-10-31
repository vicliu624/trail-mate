#include "platform/linux/oneseg_pipe_sink.h"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

namespace platform::linux_runtime
{
namespace
{
// Suppress only this write thread's generated SIGPIPE, preserving the process's
// signal disposition and original thread mask. Never install SIG_IGN globally.
class PipeSignalScope
{
  public:
    PipeSignalScope()
    {
        sigemptyset(&set_);
        sigaddset(&set_, SIGPIPE);
        changed_ = pthread_sigmask(SIG_BLOCK, &set_, &saved_) == 0;
        if (!changed_) return;
        sigset_t pending;
        ready_ = sigpending(&pending) == 0;
        if (ready_) pending_before_ = sigismember(&pending, SIGPIPE) == 1;
    }
    ~PipeSignalScope()
    {
        if (!changed_) return;
        if (ready_ && !pending_before_)
        {
            const timespec immediate{0, 0};
            (void)sigtimedwait(&set_, nullptr, &immediate);
        }
        (void)pthread_sigmask(SIG_SETMASK, &saved_, nullptr);
    }
    bool ready() const { return ready_; }

  private:
    sigset_t set_{}, saved_{};
    bool changed_ = false, ready_ = false, pending_before_ = false;
};
} // namespace

OneSegPipeSink::~OneSegPipeSink() { (void)finish(); }
bool OneSegPipeSink::fail(const char* message)
{
    std::snprintf(error_, sizeof(error_), "%s", message);
    return false;
}
bool OneSegPipeSink::open(unsigned channel, uint64_t generation, const std::atomic_bool& cancel)
{
    if (fd_ >= 0) return fail("Decoder IQ pipe already open");
    error_[0] = 0;
    if (channel < 13 || channel > 62 || !generation || !factory_.open) return fail("Invalid decoder IQ pipe configuration");
    if (cancel.load()) return false;
    fd_ = factory_.open(factory_.context, channel, generation, cancel);
    if (fd_ < 0) return fail("Decoder did not provide an IQ pipe");
    struct stat info
    {
    };
    const int flags = fcntl(fd_, F_GETFL);
    const int fd_flags = fcntl(fd_, F_GETFD);
    if (fstat(fd_, &info) != 0 || !S_ISFIFO(info.st_mode) || flags < 0 || fd_flags < 0 ||
        (flags & O_ACCMODE) == O_RDONLY || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) != 0 ||
        fcntl(fd_, F_SETFD, fd_flags | FD_CLOEXEC) != 0)
    {
        (void)finish();
        return fail("Decoder IQ endpoint is not a writable pipe");
    }
    generation_ = generation;
    next_pair_ = 0;
    return true;
}
bool OneSegPipeSink::write(const uint8_t* data, size_t bytes, uint64_t first_pair,
                           uint64_t generation, const std::atomic_bool& cancel)
{
    if (fd_ < 0 || !data || !bytes || bytes > 1280 || bytes % 2 || generation != generation_ ||
        first_pair != next_pair_ || first_pair > std::numeric_limits<uint64_t>::max() - bytes / 2)
        return fail("Invalid or discontinuous decoder IQ data");
    PipeSignalScope signals;
    if (!signals.ready()) return fail("Cannot protect decoder pipe write");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    size_t offset = 0;
    while (offset < bytes)
    {
        if (cancel.load(std::memory_order_acquire)) return false;
        if (std::chrono::steady_clock::now() >= deadline) return fail("Decoder IQ pipe stalled");
        const ssize_t written = ::write(fd_, data + offset, bytes - offset);
        if (written > 0)
        {
            offset += static_cast<size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            pollfd descriptor{fd_, POLLOUT | POLLERR | POLLHUP, 0};
            const int result = poll(&descriptor, 1, 10);
            if (result < 0 && errno != EINTR) return fail("Decoder IQ pipe poll failed");
            if (result > 0 && (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)))
                return fail("Decoder IQ pipe closed");
            continue;
        }
        return fail("Decoder IQ pipe write failed");
    }
    next_pair_ += bytes / 2;
    return true;
}
bool OneSegPipeSink::finish()
{
    if (fd_ < 0) return true;
    const int fd = fd_;
    fd_ = -1;
    generation_ = next_pair_ = 0;
    // Linux closes the FD even on EINTR. Retrying risks closing a reused FD.
    if (::close(fd) != 0) return fail("Decoder IQ pipe close failed");
    return true;
}
} // namespace platform::linux_runtime
