#include "platform/linux/route_image_cache.h"
#include "route_image_source_io.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <jpeglib.h>
#include <memory>
#include <png.h>
#include <vector>

namespace platform::linux_runtime
{
namespace
{
struct Pixels { unsigned width = 0, height = 0; uint8_t* data = nullptr; };
struct JpegError { jpeg_error_mgr base; jmp_buf jump; };
struct JpegWork { jpeg_decompress_struct decoder; JpegError error; uint8_t* pixels; };
void jpeg_failure(j_common_ptr decoder)
{
    longjmp(reinterpret_cast<JpegError*>(decoder->err)->jump, 1);
}
void free_jpeg(JpegWork* work)
{
    if (work->decoder.mem) jpeg_destroy_decompress(&work->decoder);
    std::free(work->pixels);
    std::free(work);
}
bool decode_jpeg(FILE* file, Pixels& output, const std::atomic_bool* cancelled)
{
    // libjpeg errors longjmp only through POD/C-owned state in this function.
    auto* work = static_cast<JpegWork*>(std::calloc(1, sizeof(JpegWork)));
    if (!work) return false;
    work->decoder.err = jpeg_std_error(&work->error.base);
    work->error.base.error_exit = jpeg_failure;
    if (setjmp(work->error.jump)) { free_jpeg(work); return false; }
    jpeg_create_decompress(&work->decoder);
    jpeg_stdio_src(&work->decoder, file);
    if (jpeg_read_header(&work->decoder, TRUE) != JPEG_HEADER_OK ||
        !work->decoder.image_width || !work->decoder.image_height ||
        uint64_t(work->decoder.image_width) * work->decoder.image_height > 64U * 1024U * 1024U)
    { free_jpeg(work); return false; }
    work->decoder.out_color_space = JCS_RGB;
    work->decoder.scale_num = 1;
    work->decoder.scale_denom = 1;
    while (work->decoder.scale_denom < 8 && work->decoder.image_width / (2 * work->decoder.scale_denom) >= 320 &&
           work->decoder.image_height / (2 * work->decoder.scale_denom) >= 180) work->decoder.scale_denom *= 2;
    jpeg_start_decompress(&work->decoder);
    const auto width = work->decoder.output_width, height = work->decoder.output_height;
    if (!width || !height || work->decoder.output_components != 3 || uint64_t(width) * height > 16U * 1024U * 1024U)
    { free_jpeg(work); return false; }
    work->pixels = static_cast<uint8_t*>(std::malloc(size_t(width) * height * 3));
    if (!work->pixels) { free_jpeg(work); return false; }
    while (work->decoder.output_scanline < height)
    {
        if (cancelled && cancelled->load()) { free_jpeg(work); return false; }
        JSAMPROW row = work->pixels + size_t(work->decoder.output_scanline) * width * 3;
        if (jpeg_read_scanlines(&work->decoder, &row, 1) != 1) { free_jpeg(work); return false; }
    }
    jpeg_finish_decompress(&work->decoder);
    if (work->error.base.num_warnings) { free_jpeg(work); return false; }
    output = {width, height, work->pixels};
    work->pixels = nullptr;
    free_jpeg(work);
    return true;
}
bool decode_png(FILE* file, Pixels& output)
{
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_stdio(&image, file)) { png_image_free(&image); return false; }
    if (!image.width || !image.height || uint64_t(image.width) * image.height > 16U * 1024U * 1024U)
    { png_image_free(&image); return false; }
    image.format = PNG_FORMAT_RGB;
    auto* pixels = static_cast<uint8_t*>(std::malloc(PNG_IMAGE_SIZE(image)));
    png_color background{};
    const bool ok = pixels && png_image_finish_read(&image, &background, pixels, 0, nullptr);
    if (ok) output = {image.width, image.height, pixels};
    else std::free(pixels);
    png_image_free(&image);
    return ok;
}
void put32(uint8_t* output, uint32_t value)
{
    for (unsigned index = 0; index < 4; ++index) output[index] = uint8_t(value >> (index * 8));
}
bool write_bitmap(const Pixels& pixels, unsigned width, unsigned height, const std::filesystem::path& path)
{
    const unsigned stride = (width * 3 + 3) & ~3U;
    std::vector<uint8_t> bitmap(54 + stride * height, 0);
    bitmap[0] = 'B'; bitmap[1] = 'M';
    put32(bitmap.data() + 2, static_cast<uint32_t>(bitmap.size()));
    put32(bitmap.data() + 10, 54); put32(bitmap.data() + 14, 40);
    put32(bitmap.data() + 18, width); put32(bitmap.data() + 22, height);
    bitmap[26] = 1; bitmap[28] = 24;
    put32(bitmap.data() + 34, stride * height);
    const double scale = std::min(double(width) / pixels.width, double(height) / pixels.height);
    const unsigned draw_width = std::max(1U, unsigned(pixels.width * scale));
    const unsigned draw_height = std::max(1U, unsigned(pixels.height * scale));
    const unsigned left = (width - draw_width) / 2, top = (height - draw_height) / 2;
    for (unsigned y = 0; y < draw_height; ++y)
        for (unsigned x = 0; x < draw_width; ++x)
        {
            const auto* source = pixels.data + (size_t(y) * pixels.height / draw_height * pixels.width + size_t(x) * pixels.width / draw_width) * 3;
            auto* target = bitmap.data() + 54 + (height - 1 - top - y) * stride + (left + x) * 3;
            target[0] = source[2]; target[1] = source[1]; target[2] = source[0];
        }
    static std::atomic_uint64_t sequence{0};
    const auto temporary = path.string() + ".cache-" + std::to_string(getpid()) + "-" + std::to_string(sequence++);
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    size_t written = 0;
    while (written < bitmap.size())
    {
        const auto count = ::write(fd, bitmap.data() + written, bitmap.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        written += static_cast<size_t>(count);
    }
    const bool synced = written == bitmap.size() && fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
    const bool ok = synced && closed && ::rename(temporary.c_str(), path.c_str()) == 0;
    if (!ok) ::unlink(temporary.c_str());
    return ok;
}
} // namespace
bool build_route_image_bitmaps(const std::filesystem::path& source, const std::filesystem::path& thumbnail,
                              const std::filesystem::path& view, const uint8_t expected_digest[32],
                              uint32_t expected_bytes, const std::atomic_bool* cancelled)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!expected_digest || !expected_bytes || expected_bytes > 8U * 1024U * 1024U || (cancelled && cancelled->load())) return false;
    const int fd = ::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return false;
    struct stat info{};
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size != expected_bytes)
    { ::close(fd); return false; }
    const auto close_file = [](FILE* value) { if (value) fclose(value); };
    std::unique_ptr<FILE, decltype(close_file)> file(fdopen(fd, "rb"), close_file);
    if (!file) { ::close(fd); return false; }
    RouteSourceHash hash;
    if (!hash.start()) return false;
    std::array<uint8_t, 4096> scratch{};
    uint64_t bytes = 0;
    for (;;)
    {
        const auto count = fread(scratch.data(), 1, scratch.size(), file.get());
        bytes += count;
        if (bytes > expected_bytes || (cancelled && cancelled->load()) || !hash.update(scratch.data(), count)) return false;
        if (!count) break;
    }
    uint8_t digest[32]{};
    if (ferror(file.get()) || bytes != expected_bytes || !hash.finish(digest) || std::memcmp(digest, expected_digest, 32) || fseek(file.get(), 0, SEEK_SET)) return false;
    uint8_t magic[8]{};
    if (fread(magic, 1, sizeof(magic), file.get()) != sizeof(magic) || fseek(file.get(), 0, SEEK_SET)) return false;
    Pixels pixels;
    const bool decoded = png_sig_cmp(magic, 0, sizeof(magic)) == 0 ? decode_png(file.get(), pixels) : decode_jpeg(file.get(), pixels, cancelled);
    std::unique_ptr<uint8_t, decltype(&std::free)> owned(pixels.data, std::free);
    if (!decoded || (cancelled && cancelled->load())) return false;
    return write_bitmap(pixels, 200, 120, thumbnail) && !(cancelled && cancelled->load()) && write_bitmap(pixels, 320, 180, view);
#else
    return false;
#endif
}
} // namespace platform::linux_runtime
