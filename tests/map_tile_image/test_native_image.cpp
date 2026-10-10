#include "platform/esp/arduino_common/map_tiles/lvgl_tile_image.h"
#include "ui_map_runtime/map_tiles/native_pixel_buffer.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif
using platform::esp::map_tiles::LvglTileImage;
using ui::map_tiles::NativePixelBuffer;
static unsigned pixel_frees = 0;
static void* allocate(size_t alignment, size_t bytes)
{
#ifdef _WIN32
    return _aligned_malloc(bytes, alignment);
#else
    void* out = nullptr;
    return posix_memalign(&out, alignment, bytes) ? nullptr : out;
#endif
}
static void release(void* bytes)
{
    ++pixel_frees;
#ifdef _WIN32
    _aligned_free(bytes);
#else
    std::free(bytes);
#endif
}
static void exercise(bool rgba, std::vector<uint8_t>& framebuffer, uint8_t alpha = 255)
{
    std::atomic<size_t> used{0};
    const size_t bytes = rgba ? 262144 : 131072;
    auto* pixels = NativePixelBuffer::create(bytes, used, 300000, allocate, release);
    assert(pixels);
    if (rgba)
    {
        for (size_t at = 0; at < bytes; at += 4)
        {
            pixels[at] = 255;
            pixels[at + 1] = 0;
            pixels[at + 2] = 0;
            pixels[at + 3] = alpha;
        }
        NativePixelBuffer::convertRgbaToBgra(pixels, bytes);
    }
    else
        for (size_t at = 0; at < bytes; at += 2)
        {
            pixels[at] = 0;
            pixels[at + 1] = 0xf8;
        }
    const auto format = rgba ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_RGB565;
    assert(!LvglTileImage::captureNative(pixels, bytes, format, NativePixelBuffer::retain, NativePixelBuffer::release,
                                         [](size_t) -> void*
                                         { return nullptr; }));
    assert(used > 0);
    auto* image = LvglTileImage::captureNative(pixels, bytes, format, NativePixelBuffer::retain, NativePixelBuffer::release);
    assert(image && image->data == pixels && image->header.stride == (rgba ? 1024 : 512));
    // The LVGL binary decoder must also borrow the original pixels. Checking
    // only descriptor->data would miss a second whole-image allocation here.
    lv_image_decoder_dsc_t decoder{};
    lv_image_decoder_args_t args{};
    args.no_cache = true;
    args.stride_align = true;
    assert(lv_image_decoder_open(&decoder, image, &args) == LV_RESULT_OK);
    assert(decoder.decoded && decoder.decoded->data == pixels);
    lv_image_decoder_close(&decoder);
    const auto before = pixel_frees;
    NativePixelBuffer::release(pixels); // Queue event disappears before drawing.
    assert(used > 0 && pixel_frees == before);
    auto* object = lv_image_create(lv_screen_active());
    lv_obj_set_pos(object, 0, 0);
    lv_image_set_src(object, image);
    lv_refr_now(nullptr);
    const auto offset = (128 * 256 + 128) * 2;
    const auto rendered = static_cast<uint16_t>(framebuffer[offset] | (uint16_t(framebuffer[offset + 1]) << 8));
    if (!rgba || alpha == 255) assert(rendered == 0xf800);
    else if (alpha == 0) assert(rendered == 0xffff);
    else
    {
        // Straight-alpha red over an explicitly white background, allowing
        // one quantisation step in RGB565 for LVGL's integer blending.
        assert(std::abs(int(rendered >> 11) - 31) <= 1);
        assert(std::abs(int((rendered >> 5) & 63) - int(((255 - alpha) * 63 + 127) / 255)) <= 1);
        assert(std::abs(int(rendered & 31) - int(((255 - alpha) * 31 + 127) / 255)) <= 1);
    }
    lv_obj_delete(object);
    LvglTileImage::destroy(image);
    assert(used == 0 && pixel_frees == before + 1);
}
int main()
{
    lv_init();
    auto* display = lv_display_create(256, 256);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    std::vector<uint8_t> framebuffer(131072);
    lv_display_set_buffers(display, framebuffer.data(), nullptr, framebuffer.size(), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* output, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(output); });
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    for (unsigned i = 0; i < 8; ++i)
    {
        exercise(false, framebuffer);
        exercise(true, framebuffer);
        exercise(true, framebuffer, 0);
        exercise(true, framebuffer, 128);
    }
    lv_display_delete(display);
    lv_deinit();
    std::puts("native_image: RGB565/RGBA transparency, decoder borrows pixels, allocation failure and final release PASS");
}
