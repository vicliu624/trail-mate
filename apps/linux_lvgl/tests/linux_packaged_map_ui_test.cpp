#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/linux/packaged_map.h"
#include "ui/widgets/map/map_tiles.h"
#include "ui_map_runtime/map_poi/annotation_frame.h"
#include <SDL.h>
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
uint32_t ticks()
{
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct Color
{
    uint8_t r, g, b, a;
};
Color pixel(const fs::path& path, int x, int y)
{
    auto* loaded = SDL_LoadBMP(path.c_str());
    assert(loaded);
    auto* converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(loaded);
    assert(converted);
    Color color;
    const auto value = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(converted->pixels) + y * converted->pitch)[x];
    SDL_GetRGBA(value, converted->format, &color.r, &color.g, &color.b, &color.a);
    SDL_FreeSurface(converted);
    return color;
}
int main(int argc, char** argv)
{
    assert(argc >= 2);
    char pattern[] = "/tmp/trailmate-packaged-map-ui-XXXXXX";
    const char* temporary = mkdtemp(pattern);
    assert(temporary);
    const fs::path root(temporary);
    fs::create_directories(root / "maps/tmap/osm");
    fs::copy_file(argv[1], root / "maps/tmap/osm/map.tmap");
    assert(setenv("TRAIL_MATE_SD_ROOT", temporary, 1) == 0);
    assert(setenv("TRAIL_MATE_SETTINGS_ROOT", temporary, 1) == 0);
    lv_init();
    lv_tick_set_cb(ticks);
    {
        platform::desktop::DeviceOptions options;
        options.title = "Packaged map pixel regression";
        options.ux_pack = "t_display_p4_touch";
        options.width = options.height = 256;
        options.fullscreen = false;
        platform::desktop::SdlLvglHost host(options);
        auto* container = lv_obj_create(lv_screen_active());
        lv_obj_set_size(container, 256, 256);
        lv_obj_set_pos(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_update_layout(container);
        MapAnchor anchor;
        std::vector<MapTile> tiles;
        ui::map_tiles::MapTileRenderQueue queue;
        bool has_data = false, visible_data = false;
        TileContext context;
        init_tile_context(context, container, &anchor, &tiles, &queue, &has_data, &visible_data);
        const auto main_tile = [&]() -> MapTile*
        {
            for (auto& tile : tiles)
                if (tile.x == 131072 && tile.y == 131072 && tile.map_source == 0) return &tile;
            return nullptr;
        };
        const auto step = [&]
        {
            assert(host.pump());
            tile_loader_step(context);
            lv_timer_handler();
            host.present();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        };
        const auto until = [&](auto ready)
        {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            do
            {
                step();
                assert(std::chrono::steady_clock::now() < end);
            } while (!ready());
        };
        set_map_render_options(0, true);
        calculate_required_tiles(context, -.0006866, .0006866, 18, 0, 0, true);
        until([&]
              { auto* tile = main_tile(); return tile && tile->has_png_file && !tile->packaged_pending; });
        assert(has_data && visible_data);
        lv_refr_now(nullptr);
        host.present();
        host.saveScreenshot((root / "osm.bmp").string());
        const auto red = pixel(root / "osm.bmp", 20, 20);
        const auto green = pixel(root / "osm.bmp", 64, 128);
        const auto blue = pixel(root / "osm.bmp", 128, 64);
        const auto crossing = pixel(root / "osm.bmp", 64, 64);
        std::cerr << "pixels red=" << unsigned(red.r) << ',' << unsigned(red.g) << ',' << unsigned(red.b)
                  << " green=" << unsigned(green.r) << ',' << unsigned(green.g) << ',' << unsigned(green.b)
                  << " blue=" << unsigned(blue.r) << ',' << unsigned(blue.g) << ',' << unsigned(blue.b) << " root=" << root << '\n';
        assert(red.r > 250 && red.g < 4 && red.b < 4);
        // The actual host framebuffer is RGB565: allow one 5-bit component
        // step when checking a half-alpha composition, not an RGB888 oracle.
        assert(std::abs(int(green.r) - 127) <= 8 && std::abs(int(green.g) - 128) <= 4 && green.b < 4);
        assert(std::abs(int(blue.r) - 127) <= 8 && blue.g < 4 && std::abs(int(blue.b) - 128) <= 8);
        assert(std::abs(int(crossing.r) - 63) <= 8 && std::abs(int(crossing.g) - 64) <= 4 && std::abs(int(crossing.b) - 128) <= 8);
        ui::map::MapPoiSnapshot metadata;
        map_poi_snapshot(context, metadata);
        assert(metadata.enabled && metadata.candidate_count == 2);
        ui::map_poi::AnnotationFrame frame([](size_t bytes, void*)
                                           { return std::malloc(bytes); },
                                           [](void* data, void*)
                                           { std::free(data); });
        assert(frame.begin(metadata.candidate_count, metadata));
        visit_map_annotations(
            context, [](void* data, const ui::map_poi::AnnotationCandidate& candidate)
            { static_cast<ui::map_poi::AnnotationFrame*>(data)->add(candidate); },
            &frame);
        ui::map_poi::AnnotationLayoutOptions layout;
        layout.width = layout.height = 256;
        assert(frame.finish(
            layout, [](void*, const char*, size_t bytes, bool, int16_t& width, int16_t& height)
            { width = int16_t(bytes * 6); height = 14; return true; },
            nullptr));
        assert(frame.snapshot().item_count == 2);
        bool road = false, poi = false;
        for (size_t index = 0; index < frame.snapshot().item_count; ++index)
        {
            road = road || frame.snapshot().items[index].kind == ui::map::AnnotationKind::Road;
            poi = poi || frame.snapshot().items[index].kind == ui::map::AnnotationKind::Poi;
        }
        assert(road && poi);
        if (argc > 2)
        {
            fs::create_directories(argv[2]);
            fs::copy_file(root / "osm.bmp", fs::path(argv[2]) / "osm.bmp", fs::copy_options::overwrite_existing);
        }

        set_map_render_options(2, false);
        calculate_required_tiles(context, -.0006866, .0006866, 18, 0, 0, true);
        until([&]
              { for (const auto& tile : tiles) if (tile.x == 131072 && tile.y == 131072 && tile.map_source == 2 && tile.has_png_file && !tile.packaged_pending) return true; return false; });
        lv_refr_now(nullptr);
        host.present();
        host.saveScreenshot((root / "satellite.bmp").string());
        const auto satellite = pixel(root / "satellite.bmp", 128, 128);
        assert(std::abs(int(satellite.r) - 11) <= 8 && std::abs(int(satellite.g) - 22) <= 4 && std::abs(int(satellite.b) - 33) <= 8);
        set_map_render_options(0, true);
        calculate_required_tiles(context, -.0006866, .0006866, 18, 0, 0, true);
        until([&]
              { auto* tile = main_tile(); return tile && tile->has_png_file && !tile->packaged_pending; });
        lv_refr_now(nullptr);
        host.present();
        host.saveScreenshot((root / "repeat.bmp").string());
        const auto repeated = pixel(root / "repeat.bmp", 128, 64);
        assert(std::abs(int(repeated.b) - 128) <= 8 && repeated.g < 4); // Cached RGBA converts exactly once.
        frame.clear();
        cleanup_tiles(context);
        release_tile_context(context);
        assert(platform::linux_runtime::PackagedMapSession::pixelBytes() == 0);
        lv_obj_del(container);
    }
    fs::remove_all(root);
    std::cout << "Native LVGL TMAP RGB565, RGBA alpha, annotation projection, source switching and descriptor release passed\n";
}
