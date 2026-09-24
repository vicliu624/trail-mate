#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ui_map_runtime/map_tiles/map_tile_request_queue.h"

namespace platform::esp::arduino_common::map_tiles
{
class MapTileCommandQueue final : public ui::map_tiles::IMapTileCommandSink
{
  public:
    MapTileCommandQueue() : mutex_(xSemaphoreCreateMutex()) {}
    ~MapTileCommandQueue()
    {
        if (mutex_) vSemaphoreDelete(mutex_);
    }
    MapTileCommandQueue(const MapTileCommandQueue&) = delete;
    MapTileCommandQueue& operator=(const MapTileCommandQueue&) = delete;
    bool available() const { return mutex_ != nullptr; }

    bool statistics(std::size_t& queued, std::size_t& in_flight)
    {
        if (!mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE) return false;
        queue_.statistics(queued, in_flight);
        xSemaphoreGive(mutex_);
        return true;
    }

    ui::map_tiles::TileSubmitResult enqueue(const ui::map_tiles::LoadTileCommand& command) override
    {
        if (!mutex_) return {};
        if (xSemaphoreTake(mutex_, 0) != pdTRUE) return {ui::map_tiles::TileSubmitStatus::Backpressured, {}};
        const auto result = queue_.enqueue(command);
        xSemaphoreGive(mutex_);
        return result;
    }

    std::size_t cancelGeneration(uint32_t generation) override
    {
        if (!mutex_) return 0;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const auto removed = queue_.cancelGeneration(generation);
        xSemaphoreGive(mutex_);
        return removed;
    }

    bool pop(uint32_t now_ms, ui::map_tiles::LoadTileCommand& out)
    {
        if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return false;
        const bool found = queue_.pop(now_ms, out);
        xSemaphoreGive(mutex_);
        return found;
    }

    void complete(ui::map_tiles::TileRequestHandle handle)
    {
        if (!mutex_) return;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        queue_.complete(handle);
        xSemaphoreGive(mutex_);
    }

  private:
    SemaphoreHandle_t mutex_;
    ui::map_tiles::MapTileRequestQueue<16> queue_{};
};
} // namespace platform::esp::arduino_common::map_tiles
