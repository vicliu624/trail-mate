#include "ui_map_runtime/map_tiles/map_tile_render_queue.h"

#include <cassert>

namespace
{

ui::map_tiles::MapTileRenderRef make_item(uint32_t x)
{
    ui::map_tiles::MapTileRenderRef item{};
    item.tile.layer = ui::map_tiles::MapTileLayer::Osm;
    item.tile.z = 12;
    item.tile.x = x;
    item.tile.y = x + 1;
    item.rect.x = static_cast<int16_t>(x);
    item.rect.y = static_cast<int16_t>(x + 2);
    item.rect.width = 256;
    item.rect.height = 256;
    item.state = ui::map_tiles::MapTileRenderState::Loading;
    return item;
}

void test_push_and_read()
{
    ui::map_tiles::MapTileRenderQueue queue;
    assert(queue.size() == 0);
    assert(queue.push(make_item(4)));
    assert(queue.size() == 1);
    assert(queue.items()[0].tile.x == 4);
    assert(queue.items()[0].rect.width == 256);
    assert(queue.items()[0].state == ui::map_tiles::MapTileRenderState::Loading);
}

void test_clear()
{
    ui::map_tiles::MapTileRenderQueue queue;
    assert(queue.push(make_item(1)));
    queue.clear();
    assert(queue.size() == 0);
    assert(queue.push(make_item(2)));
    assert(queue.size() == 1);
    assert(queue.items()[0].tile.x == 2);
}

void test_capacity()
{
    ui::map_tiles::MapTileRenderQueue queue;
    for (std::size_t i = 0; i < ui::map_tiles::MapTileRenderQueue::kMaxTiles; ++i)
    {
        assert(queue.push(make_item(static_cast<uint32_t>(i))));
    }
    assert(queue.size() == ui::map_tiles::MapTileRenderQueue::kMaxTiles);
    assert(!queue.push(make_item(99)));
}

} // namespace

int main()
{
    test_push_and_read();
    test_clear();
    test_capacity();
    return 0;
}
