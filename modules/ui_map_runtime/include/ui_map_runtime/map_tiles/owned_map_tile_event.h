#pragma once

#include "ui_map_runtime/map_tiles/map_tile_async_runtime.h"
#include <utility>

namespace ui::map_tiles
{
// Platform-provided deleter keeps heap/PSRAM policy outside the map domain.
class OwnedMapTileEvent
{
  public:
    using Deleter = void (*)(MapTileAsyncEvent&);
    OwnedMapTileEvent() = default;
    OwnedMapTileEvent(MapTileAsyncEvent event, Deleter deleter)
        : event_(event), deleter_(deleter) {}
    ~OwnedMapTileEvent() { reset(); }
    OwnedMapTileEvent(const OwnedMapTileEvent&) = delete;
    OwnedMapTileEvent& operator=(const OwnedMapTileEvent&) = delete;
    OwnedMapTileEvent(OwnedMapTileEvent&& other) noexcept
        : event_(other.release()), deleter_(other.deleter_) {}
    OwnedMapTileEvent& operator=(OwnedMapTileEvent&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            event_ = other.release();
            deleter_ = other.deleter_;
        }
        return *this;
    }
    const MapTileAsyncEvent& get() const { return event_; }
    MapTileAsyncEvent release()
    {
        auto result = event_;
        event_ = {};
        return result;
    }

  private:
    void reset()
    {
        if (deleter_ && event_.payload.data) deleter_(event_);
        event_ = {};
    }
    MapTileAsyncEvent event_{};
    Deleter deleter_ = nullptr;
};
} // namespace ui::map_tiles
