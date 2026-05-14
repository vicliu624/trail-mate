#include "ui_map_runtime/map_tiles/map_tile_resolver.h"

#include <cassert>
#include <cstring>

namespace
{

void test_base_paths()
{
    ui::map_tiles::MapTileResolver resolver("/sd");
    char path[160]{};

    ui::map_tiles::MapTileRef ref{};
    ref.layer = ui::map_tiles::MapTileLayer::Osm;
    ref.z = 12;
    ref.x = 656;
    ref.y = 1582;
    assert(resolver.resolvePath(ref, path, sizeof(path)));
    assert(std::strcmp(path, "/sd/maps/base/osm/12/656/1582.png") == 0);

    ref.layer = ui::map_tiles::MapTileLayer::Satellite;
    assert(resolver.resolvePath(ref, path, sizeof(path)));
    assert(std::strcmp(path, "/sd/maps/base/satellite/12/656/1582.jpg") == 0);
}

void test_contour_paths()
{
    ui::map_tiles::MapTileResolver resolver("A:");
    char path[160]{};
    char dir[160]{};

    ui::map_tiles::MapTileRef ref{};
    ref.layer = ui::map_tiles::MapTileLayer::ContourMajor50;
    ref.z = 16;
    ref.x = 10495;
    ref.y = 25312;
    assert(resolver.resolvePath(ref, path, sizeof(path)));
    assert(std::strcmp(path, "A:/maps/contour/major-50/16/10495/25312.png") == 0);

    assert(resolver.resolveDirectory(ref.layer, dir, sizeof(dir)));
    assert(std::strcmp(dir, "A:/maps/contour/major-50") == 0);
}

void test_layer_helpers()
{
    assert(ui::map_tiles::mapTileLayerFromBaseSource(0) ==
           ui::map_tiles::MapTileLayer::Osm);
    assert(ui::map_tiles::mapTileLayerFromBaseSource(1) ==
           ui::map_tiles::MapTileLayer::Terrain);
    assert(ui::map_tiles::mapTileLayerFromBaseSource(2) ==
           ui::map_tiles::MapTileLayer::Satellite);

    bool supported = false;
    assert(ui::map_tiles::mapTileContourLayerForZoom(8, &supported) ==
           ui::map_tiles::MapTileLayer::ContourMajor500);
    assert(supported);
    assert(ui::map_tiles::mapTileContourLayerForZoom(16, &supported) ==
           ui::map_tiles::MapTileLayer::ContourMajor50);
    assert(supported);
    (void)ui::map_tiles::mapTileContourLayerForZoom(7, &supported);
    assert(!supported);
}

} // namespace

int main()
{
    test_base_paths();
    test_contour_paths();
    test_layer_helpers();
    return 0;
}
