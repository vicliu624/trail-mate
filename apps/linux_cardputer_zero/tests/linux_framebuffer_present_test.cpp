#include "platform/device/evdev_input.h"
#include "platform/device/linux_framebuffer_platform.h"
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <linux/fb.h>
#include <poll.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace
{
constexpr std::size_t kBytes = 320 * 170 * 2;
enum class Failure
{
    None,
    FixedInfo,
    VariableInfo,
    PixelDepth,
    Mapping
};
Failure failure = Failure::None;

std::size_t descriptorCount()
{
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd"))
    {
        (void)entry;
        ++count;
    }
    return count;
}

void checkPrivateDescriptor(const char* path, int fixture)
{
    bool found = false;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd"))
    {
        const int descriptor = std::stoi(entry.path().filename().string());
        if (descriptor == fixture || std::filesystem::read_symlink(entry.path()) != path) continue;
        const auto flags = fcntl(descriptor, F_GETFD);
        assert(flags >= 0 && (flags & FD_CLOEXEC) != 0);
        found = true;
    }
    assert(found);
}
} // namespace

extern "C" int __wrap_ioctl(int, unsigned long request, ...)
{
    va_list arguments;
    va_start(arguments, request);
    void* output = va_arg(arguments, void*);
    va_end(arguments);
    if (request == FBIOGET_FSCREENINFO)
    {
        if (failure == Failure::FixedInfo)
        {
            errno = EIO;
            return -1;
        }
        auto& info = *static_cast<fb_fix_screeninfo*>(output);
        info = {};
        info.line_length = failure == Failure::Mapping ? 0 : 320 * 2;
        info.smem_len = kBytes;
        return 0;
    }
    if (request == FBIOGET_VSCREENINFO)
    {
        if (failure == Failure::VariableInfo)
        {
            errno = EIO;
            return -1;
        }
        auto& info = *static_cast<fb_var_screeninfo*>(output);
        info = {};
        info.xres = info.xres_virtual = 320;
        info.yres = info.yres_virtual = 170;
        info.bits_per_pixel = failure == Failure::PixelDepth ? 8 : 16;
        info.red = {11, 5, 0};
        info.green = {5, 6, 0};
        info.blue = {0, 5, 0};
        return 0;
    }
    return -1;
}

int main()
{
    using trailmate::cardputer_zero::core::Canvas;
    using trailmate::cardputer_zero::core::rgba;
    using trailmate::cardputer_zero::platform::device::LinuxFramebufferPlatform;
    char path[] = "/tmp/trailmate-framebuffer-XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0 && ftruncate(fd, kBytes) == 0);
    setenv("TRAIL_MATE_INPUT_DEVICE", "/dev/trailmate-test-missing-keyboard", 1);
    const auto baseline = descriptorCount();
    for (const auto mode : {Failure::FixedInfo, Failure::VariableInfo, Failure::PixelDepth, Failure::Mapping})
    {
        failure = mode;
        for (unsigned attempt = 0; attempt < 20; ++attempt)
        {
            bool failed = false;
            try
            {
                LinuxFramebufferPlatform presenter(path);
            }
            catch (const std::runtime_error&)
            {
                failed = true;
            }
            assert(failed);
            assert(descriptorCount() == baseline);
        }
    }
    failure = Failure::None;
    {
        LinuxFramebufferPlatform presenter(path);
        checkPrivateDescriptor(path, fd);
        Canvas canvas(320, 170);
        canvas.clear(rgba(255, 255, 255));
        presenter.present(canvas);
        for (int n = 0; n < 1000; ++n) presenter.present(canvas);
        assert(presenter.statistics().requests == 1001);
        assert(presenter.statistics().commits == 1);
        assert(presenter.statistics().bytes_written == kBytes);
        std::vector<unsigned char> frame(kBytes);
        assert(pread(fd, frame.data(), frame.size(), 0) == static_cast<ssize_t>(frame.size()));
        for (const auto byte : frame) assert(byte == 0xff);

        // A separate scanout process must never observe a black clearing phase.
        // Both alternating frames contain only nonzero RGB565 bytes. This fails
        // the former clear-live-mmap/slow-per-pixel publishing implementation.
        int ready[2], done[2];
        assert(pipe(ready) == 0 && pipe(done) == 0);
        const pid_t scanner = fork();
        assert(scanner >= 0);
        if (scanner == 0)
        {
            close(ready[0]);
            close(done[1]);
            const char started = 1;
            if (write(ready[1], &started, 1) != 1) _exit(2);
            pollfd finished{done[0], POLLIN, 0};
            for (;;)
            {
                if (pread(fd, frame.data(), frame.size(), 0) != static_cast<ssize_t>(frame.size())) _exit(3);
                for (const auto byte : frame)
                    if (byte == 0) _exit(4);
                if (poll(&finished, 1, 0) > 0) _exit(0);
            }
        }
        close(ready[1]);
        close(done[0]);
        char started = 0;
        assert(read(ready[0], &started, 1) == 1);
        for (int n = 0; n < 100; ++n)
        {
            canvas.clear(n % 2 ? rgba(255, 255, 255) : rgba(255, 255, 0));
            presenter.present(canvas);
        }
        const char finished = 1;
        assert(write(done[1], &finished, 1) == 1);
        int status = 0;
        assert(waitpid(scanner, &status, 0) == scanner);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        close(ready[0]);
        close(done[1]);
        assert(presenter.statistics().commits == 101);
        assert(presenter.statistics().bytes_written == kBytes * 101);
    }
    assert(descriptorCount() == baseline);
    {
        trailmate::cardputer_zero::platform::device::EvdevInput input(path);
        assert(input.isOpen());
        for (unsigned attempt = 0; attempt < 100; ++attempt)
        {
            checkPrivateDescriptor(path, fd);
            assert(descriptorCount() == baseline + 1);
            assert(input.reopen(path));
        }
    }
    assert(descriptorCount() == baseline);
    close(fd);
    std::filesystem::remove(path);
}
