#include "lvgl.h"
#include "platform/desktop/dirty_regions.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include <SDL.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <vector>

namespace
{
std::vector<uint16_t> captured;
uint64_t uploaded = 0;
int render_width = 0, render_height = 0;
constexpr int width = 320, height = 240;

void checkRegions()
{
    platform::desktop::DirtyRegions dirty;
    assert(dirty.pending() && dirty.full());
    dirty.clear();
    dirty.add({-10, -20, 9, 7}, width, height);
    assert(dirty.size() == 1 && dirty[0].x1 == 0 && dirty[0].y1 == 0);
    dirty.add({10, 0, 20, 7}, width, height);
    assert(dirty.size() == 1 && dirty[0].x2 == 20);
    dirty.add({200, 200, 210, 210}, width, height);
    assert(dirty.size() == 2);
    dirty.add({400, 500, 600, 700}, width, height);
    assert(dirty.size() == 2);
    dirty.invalidate();
    assert(dirty.full());
    dirty.clear();
    for (int i = 0; i < 33; ++i) dirty.add({i * 3, 0, i * 3, 0}, width, height);
    assert(dirty.full());
}
lv_obj_t* rectangle(int x, int y, int w, int h, uint32_t color)
{
    auto* object = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, w, h);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    return object;
}
void verify(unsigned rotation, int ax, int ay, uint16_t ac, uint16_t bc)
{
    assert(captured.size() == std::size_t(width * height));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const uint16_t expected = x >= ax && x < ax + 23 && y >= ay && y < ay + 17 ? ac
                                      : x >= 280 && x < 305 && y >= 212 && y < 232     ? bc
                                                                                       : 0xffff;
            int px = x, py = y;
            if (rotation == 90)
            {
                px = height - 1 - y;
                py = x;
            }
            if (rotation == 180)
            {
                px = width - 1 - x;
                py = height - 1 - y;
            }
            if (rotation == 270)
            {
                px = y;
                py = width - 1 - x;
            }
            if (captured[std::size_t(py) * render_width + px] != expected)
            {
                std::fprintf(stderr, "rotation=%u logical=%d,%d output=%d,%d actual=%04x expected=%04x uploads=%llu\n",
                             rotation, x, y, px, py, captured[std::size_t(py) * render_width + px], expected,
                             (unsigned long long)uploaded);
                assert(false);
            }
        }
}
} // namespace

extern "C" int SDL_UpdateTexture(SDL_Texture* texture, const SDL_Rect* area, const void* pixels, int pitch)
{
    using Update = int (*)(SDL_Texture*, const SDL_Rect*, const void*, int);
    static const auto original = reinterpret_cast<Update>(dlsym(RTLD_NEXT, "SDL_UpdateTexture"));
    assert(original);
    int w, h;
    assert(SDL_QueryTexture(texture, nullptr, nullptr, &w, &h) == 0);
    uploaded += uint64_t(area ? area->w * area->h : w * h) * 2;
    return original(texture, area, pixels, pitch);
}
extern "C" void SDL_RenderPresent(SDL_Renderer* renderer)
{
    // Synthetic in-memory readback before present, never a device screenshot.
    assert(SDL_GetRendererOutputSize(renderer, &render_width, &render_height) == 0);
    captured.resize(std::size_t(render_width) * render_height);
    assert(SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB565, captured.data(), render_width * 2) == 0);
}
int main(int argc, char** argv)
{
    assert(argc == 1 || (argc == 2 && !std::strcmp(argv[1], "--native")));
    checkRegions();
    if (argc == 1)
    {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv("SDL_RENDER_DRIVER", "software", 1);
    }
    lv_init();
    for (unsigned rotation : {0u, 90u, 180u, 270u})
    {
        platform::desktop::DeviceOptions options;
        options.ux_pack = "t_display_p4_touch";
        options.width = width;
        options.height = height;
        options.rotation = rotation;
        options.fullscreen = false;
        platform::desktop::SdlLvglHost host(options);
        assert(host.pump());
        auto* screen = lv_screen_active();
        lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(screen, 0, 0);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        auto* a = rectangle(7, 9, 23, 17, 0xff0000);
        auto* b = rectangle(280, 212, 25, 20, 0x00ff00);
        lv_refr_now(lv_display_get_default());
        uploaded = 0;
        host.present();
        assert(uploaded == width * height * 2);
        verify(rotation, 7, 9, 0xf800, 0x07e0);
        lv_obj_set_pos(a, 50, 60);
        lv_obj_set_style_bg_color(a, lv_color_hex(0x0000ff), 0);
        lv_refr_now(lv_display_get_default());
        lv_obj_set_style_bg_color(b, lv_color_hex(0xff0000), 0);
        lv_refr_now(lv_display_get_default());
        uploaded = 0;
        host.present();
        assert(uploaded > 0 && uploaded < width * height * 2 / 4);
        verify(rotation, 50, 60, 0x001f, 0xf800);
        uploaded = 0;
        host.present();
        assert(uploaded == 0);
        lv_obj_clean(screen);
    }
    lv_deinit();
    std::puts("Dirty-region clipping, merging, fallback and exact SDL pixels at all four rotations passed");
}
