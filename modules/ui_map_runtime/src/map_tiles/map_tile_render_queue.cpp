#include "ui_map_runtime/map_tiles/map_tile_render_queue.h"

namespace ui
{
namespace map_tiles
{

void MapTileRenderQueue::clear()
{
    size_ = 0;
}

bool MapTileRenderQueue::push(const MapTileRenderRef& item)
{
    if (size_ >= kMaxTiles)
    {
        return false;
    }

    items_[size_] = item;
    ++size_;
    return true;
}

std::size_t MapTileRenderQueue::size() const
{
    return size_;
}

const MapTileRenderRef* MapTileRenderQueue::items() const
{
    return items_;
}

} // namespace map_tiles
} // namespace ui
