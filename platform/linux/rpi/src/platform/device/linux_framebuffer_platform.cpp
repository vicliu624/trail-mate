#include "platform/device/linux_framebuffer_platform.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace trailmate::cardputer_zero::platform::device
{
namespace
{

std::atomic_bool g_running{true};

void stopSignalHandler(int)
{
    g_running.store(false);
}

std::uint32_t scaleChannel(std::uint8_t value, const fb_bitfield& bitfield) noexcept
{
    if (bitfield.length == 0U)
    {
        return 0U;
    }

    const std::uint32_t max_value = (1U << bitfield.length) - 1U;
    const std::uint32_t scaled = (static_cast<std::uint32_t>(value) * max_value + 127U) / 255U;
    return scaled << bitfield.offset;
}

std::uint32_t packNativePixel(core::Color color, const fb_var_screeninfo& vinfo) noexcept
{
    return scaleChannel(color.r, vinfo.red) | scaleChannel(color.g, vinfo.green) | scaleChannel(color.b, vinfo.blue) | scaleChannel(color.a, vinfo.transp);
}

std::runtime_error makeSystemError(const std::string& step)
{
    return std::runtime_error(step + ": " + std::strerror(errno));
}

} // namespace

struct LinuxFramebufferPlatform::Impl
{
    explicit Impl(std::string framebuffer_path_in)
        : framebuffer_path(std::move(framebuffer_path_in))
    {
        g_running.store(true);
        std::signal(SIGINT, stopSignalHandler);
        std::signal(SIGTERM, stopSignalHandler);

        file_descriptor = ::open(framebuffer_path.c_str(), O_RDWR);
        if (file_descriptor < 0)
        {
            throw makeSystemError("open framebuffer");
        }

        if (::ioctl(file_descriptor, FBIOGET_FSCREENINFO, &finfo) == -1)
        {
            throw makeSystemError("FBIOGET_FSCREENINFO");
        }
        if (::ioctl(file_descriptor, FBIOGET_VSCREENINFO, &vinfo) == -1)
        {
            throw makeSystemError("FBIOGET_VSCREENINFO");
        }

        bytes_per_pixel = static_cast<int>(vinfo.bits_per_pixel / 8U);
        if (bytes_per_pixel < 2 || bytes_per_pixel > 4)
        {
            throw std::runtime_error("unsupported framebuffer pixel depth");
        }

        const auto mapped_size = static_cast<std::size_t>(finfo.line_length) * static_cast<std::size_t>(vinfo.yres_virtual);
        framebuffer_size = mapped_size;
        void* mapped = ::mmap(nullptr, framebuffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, file_descriptor, 0);
        if (mapped == MAP_FAILED)
        {
            framebuffer_memory = nullptr;
            throw makeSystemError("mmap framebuffer");
        }

        framebuffer_memory = static_cast<std::byte*>(mapped);
    }

    ~Impl()
    {
        if (framebuffer_memory)
        {
            ::munmap(framebuffer_memory, framebuffer_size);
        }
        if (file_descriptor >= 0)
        {
            ::close(file_descriptor);
        }
    }

    void writePackedPixel(int x, int y, std::uint32_t packed) const noexcept
    {
        const int pixel_x = x + static_cast<int>(vinfo.xoffset);
        const int pixel_y = y + static_cast<int>(vinfo.yoffset);
        const std::size_t offset = static_cast<std::size_t>(pixel_y) * static_cast<std::size_t>(finfo.line_length) + static_cast<std::size_t>(pixel_x) * static_cast<std::size_t>(bytes_per_pixel);

        for (int byte_index = 0; byte_index < bytes_per_pixel; ++byte_index)
        {
            framebuffer_memory[offset + static_cast<std::size_t>(byte_index)] =
                static_cast<std::byte>((packed >> (byte_index * 8)) & 0xFFU);
        }
    }

    std::string framebuffer_path{};
    int file_descriptor{-1};
    int bytes_per_pixel{};
    fb_fix_screeninfo finfo{};
    fb_var_screeninfo vinfo{};
    std::size_t framebuffer_size{};
    std::byte* framebuffer_memory{};
};

LinuxFramebufferPlatform::LinuxFramebufferPlatform(std::string framebuffer_path)
    : impl_(std::make_unique<Impl>(std::move(framebuffer_path)))
{
}

LinuxFramebufferPlatform::~LinuxFramebufferPlatform() = default;

bool LinuxFramebufferPlatform::pump()
{
    return g_running.load();
}

std::vector<app::InputEvent> LinuxFramebufferPlatform::drainInput()
{
    return {};
}

void LinuxFramebufferPlatform::present(const core::Canvas& canvas)
{
    const int physical_width = static_cast<int>(impl_->vinfo.xres);
    const int physical_height = static_cast<int>(impl_->vinfo.yres);
    const int scale_x = physical_width / canvas.width();
    const int scale_y = physical_height / canvas.height();
    const int scale = std::min(scale_x, scale_y);

    if (scale <= 0)
    {
        throw std::runtime_error("framebuffer is smaller than the logical display");
    }

    const int render_width = canvas.width() * scale;
    const int render_height = canvas.height() * scale;
    const int offset_x = (physical_width - render_width) / 2;
    const int offset_y = (physical_height - render_height) / 2;

    std::memset(impl_->framebuffer_memory, 0, impl_->framebuffer_size);

    for (int y = 0; y < canvas.height(); ++y)
    {
        for (int x = 0; x < canvas.width(); ++x)
        {
            const std::uint32_t packed = packNativePixel(canvas.pixel(x, y), impl_->vinfo);

            for (int dy = 0; dy < scale; ++dy)
            {
                for (int dx = 0; dx < scale; ++dx)
                {
                    impl_->writePackedPixel(offset_x + (x * scale) + dx, offset_y + (y * scale) + dy, packed);
                }
            }
        }
    }
}

} // namespace trailmate::cardputer_zero::platform::device
