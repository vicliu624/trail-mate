#include "lvgl.h"
#include "src/draw/lv_draw_buf_private.h"
#include "src/draw/lv_image_decoder_private.h"
#define LODEPNG_NO_COMPILE_CPP
#include "src/libs/lodepng/lodepng.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

// Inject failures into the production ownership adapter, not into the decoder.
static int allocation_calls;
static int fail_allocation;
static size_t largest_allocation;
static void* capture_malloc(size_t bytes)
{
    ++allocation_calls;
    largest_allocation = std::max(largest_allocation, bytes);
    if (allocation_calls == fail_allocation) return nullptr;
    return lv_malloc(bytes);
}
#define lv_malloc capture_malloc
#include "platform/esp/arduino_common/map_tiles/lvgl_tile_image.h"
#undef lv_malloc
using platform::esp::map_tiles::LvglTileImage;

static lv_draw_buf_free_cb_t original_free;
static const void* watched_buffer;
static int watched_frees;
static void tracked_free(void* buffer)
{
    if (buffer == watched_buffer) ++watched_frees;
    original_free(buffer);
}

static void exercise(unsigned width, unsigned height, bool alpha, bool fallback, int fail)
{
    std::vector<uint8_t> rgba(width * height * 4);
    for (size_t i = 0; i < rgba.size(); i += 4)
    {
        rgba[i] = static_cast<uint8_t>(i / 4);
        rgba[i + 1] = 93;
        rgba[i + 2] = 211;
        rgba[i + 3] = alpha ? static_cast<uint8_t>(i / 16) : 255;
    }
    uint8_t* png = nullptr;
    size_t png_size = 0;
    assert(lodepng_encode32(&png, &png_size, rgba.data(), width, height) == 0);
    lv_image_dsc_t source{};
    source.header.magic = LV_IMAGE_HEADER_MAGIC;
    source.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    source.data = png;
    source.data_size = static_cast<uint32_t>(png_size);
    lv_image_decoder_dsc_t decoder{};
    lv_image_decoder_args_t args{};
    args.no_cache = true;
    args.stride_align = true;
    assert(lv_image_decoder_open(&decoder, &source, &args) == LV_RESULT_OK);
    assert(decoder.decoded && decoder.decoded->header.cf == LV_COLOR_FORMAT_ARGB8888);
    const auto* pixels = decoder.decoded->data;
    const uint32_t bytes = decoder.decoded->data_size;
    const uint32_t stride = decoder.decoded->header.stride;
    std::vector<uint8_t> expected(pixels, pixels + bytes);
    watched_buffer = decoder.decoded->unaligned_data;
    watched_frees = 0;

    // Unknown decoder names must take the compatibility path, even with the
    // same pixel format. Restore the real decoder before its original close.
    auto* original_decoder = decoder.decoder;
    lv_image_decoder_t unknown = *original_decoder;
    unknown.name = "UNKNOWN";
    if (fallback) decoder.decoder = &unknown;
    allocation_calls = 0;
    fail_allocation = fail;
    largest_allocation = 0;
    lv_image_dsc_t* image = LvglTileImage::capture(decoder);
    fail_allocation = 0;
    if (fail)
    {
        assert(!image);
        assert(decoder.decoded && decoder.decoded->data == pixels);
        assert(watched_frees == 0);
        decoder.decoder = original_decoder;
        lv_image_decoder_close(&decoder);
        assert(watched_frees == 1);
    }
    else
    {
        assert(image && image->data_size == bytes && image->header.stride == stride);
        assert(image->header.cf == LV_COLOR_FORMAT_ARGB8888);
        if (fallback)
        {
            assert(image->data != pixels && allocation_calls == 2);
            decoder.decoder = original_decoder;
        }
        else
        {
            assert(image->data == pixels); // Proves no full-image copy.
            assert(!decoder.decoder && !decoder.decoded);
            assert(allocation_calls == 1 && largest_allocation < 1024);
        }
        lv_image_decoder_close(&decoder);
        assert(watched_frees == (fallback ? 1 : 0));
        // The input payload and its descriptor may disappear before rendering.
        lv_free(png);
        png = nullptr;
        source = {};
        assert(std::memcmp(image->data, expected.data(), bytes) == 0);
        lv_obj_t* obj = lv_image_create(lv_screen_active());
        lv_image_set_src(obj, image);
        lv_refr_now(nullptr);
        assert(std::memcmp(image->data, expected.data(), bytes) == 0);
        lv_obj_delete(obj);
        LvglTileImage::destroy(image);
        assert(watched_frees == 1); // Eviction destroys the original exactly once.
    }
    lv_free(png);
    watched_buffer = nullptr;
}

int main()
{
    lv_init();
    auto* display = lv_display_create(256, 256);
    std::vector<uint8_t> framebuffer(256 * 256 * 2);
    lv_display_set_buffers(display, framebuffer.data(), nullptr, framebuffer.size(), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* disp, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(disp); });
    auto* handlers = lv_draw_buf_get_image_handlers();
    original_free = handlers->buf_free_cb;
    handlers->buf_free_cb = tracked_free;

    lv_image_decoder_dsc_t empty{};
    assert(!LvglTileImage::capture(empty));
    LvglTileImage::destroy(nullptr);
    for (int cycle = 0; cycle < 24; ++cycle)
    {
        exercise(256, 256, cycle % 2 != 0, false, 0);
        exercise(17, 13, true, false, 0); // Non-tile dimensions / stride.
    }
    exercise(256, 256, false, true, 0);
    exercise(256, 256, true, false, 1); // Owner allocation fails.
    exercise(256, 256, true, true, 2);  // Fallback pixel allocation fails.
    handlers->buf_free_cb = original_free;
    lv_display_delete(display);
    lv_deinit();
    std::puts("PNG zero-copy ownership, rendering, eviction, fallback and allocation failure passed");
}
