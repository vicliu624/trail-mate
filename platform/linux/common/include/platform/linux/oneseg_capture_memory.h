#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace platform::linux_runtime
{
// The DMA transfer can run without its caller, but the next transaction still
// has a bounded FIFO deadline. Admit the capture code/runtime and hot stack
// before RX, including heap-backed GPIO map nodes accessed by every BUSY poll.
// A locked transport object does not lock its separately allocated containers.
class OneSegCaptureMemory
{
  public:
    OneSegCaptureMemory() = default;
    OneSegCaptureMemory(const OneSegCaptureMemory&) = delete;
    OneSegCaptureMemory& operator=(const OneSegCaptureMemory&) = delete;
    ~OneSegCaptureMemory() { release(); }
    bool lock()
    {
        if (count_)
        {
            error_ = EALREADY;
            return false;
        }
        error_ = 0;
        char executable[4096];
        const ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        if (length <= 0)
        {
            error_ = errno;
            return false;
        }
        executable[length] = 0;
        FILE* maps = std::fopen("/proc/self/maps", "r");
        if (!maps)
        {
            error_ = errno;
            return false;
        }
        const uintptr_t marker = reinterpret_cast<uintptr_t>(&maps);
        const long page = sysconf(_SC_PAGESIZE);
        bool executable_found = false, stack_found = false;
        char line[8192], permissions[5];
        unsigned long begin, end;
        while (!error_ && std::fgets(line, sizeof(line), maps))
        {
            if (std::sscanf(line, "%lx-%lx %4s", &begin, &end, permissions) != 3 || permissions[0] != 'r') continue;
            const bool image = std::strstr(line, executable);
            const bool runtime = std::strstr(line, "/libc.so") || std::strstr(line, "/libc-") ||
                                 std::strstr(line, "/libstdc++") || std::strstr(line, "/libgcc_s") ||
                                 std::strstr(line, "/libarmmem-") || std::strstr(line, "/ld-linux-") ||
                                 std::strstr(line, "/libatomic") || std::strstr(line, "/libpthread") ||
                                 std::strstr(line, "/libm.so");
            const bool stack = marker >= begin && marker < end;
            const bool heap = std::strstr(line, "[heap]");
            if (!image && !runtime && !stack && !heap) continue;
            uintptr_t start = begin, finish = end;
            if (stack)
            {
                if (page <= 0)
                {
                    error_ = EINVAL;
                    break;
                }
                // Retain current frames, room for deeper calls, and the thread
                // descriptor/TLS at the upper end of this stack mapping.
                const uintptr_t lower = marker > 256 * 1024 ? marker - 256 * 1024 : 0;
                start = std::max(start, lower / static_cast<uintptr_t>(page) * static_cast<uintptr_t>(page));
            }
            const size_t bytes = finish - start;
            if (count_ == ranges_.size() || bytes > 48 * 1024 * 1024 - bytes_)
            {
                error_ = ENOMEM;
                break;
            }
            if (mlock(reinterpret_cast<void*>(start), bytes))
            {
                error_ = errno;
                break;
            }
            ranges_[count_++] = {reinterpret_cast<void*>(start), bytes};
            bytes_ += bytes;
            executable_found = executable_found || image;
            stack_found = stack_found || stack;
        }
        std::fclose(maps);
        if (!error_ && (!executable_found || !stack_found)) error_ = EINVAL;
        if (error_)
        {
            release();
            return false;
        }
        return true;
    }
    int error() const { return error_; }
    size_t bytes() const { return bytes_; }

  private:
    struct Range
    {
        void* start;
        size_t bytes;
    };
    void release()
    {
        while (count_)
        {
            const auto& range = ranges_[--count_];
            (void)munlock(range.start, range.bytes);
        }
        bytes_ = 0;
    }
    std::array<Range, 64> ranges_{};
    size_t count_ = 0, bytes_ = 0;
    int error_ = 0;
};
} // namespace platform::linux_runtime
