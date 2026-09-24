#pragma once

#include "ui_map_runtime/map_tiles/map_tile_async_runtime.h"
#include <array>
#include <cstdint>

namespace ui::map_tiles
{
// UI-thread-owned, bounded summaries. Worker evidence travels with completion
// ownership; no shared mutable worker/UI counters and no per-sector logging.
class MapTilePipelineMetrics
{
  public:
    enum Stage : unsigned
    {
        CommandWait,
        Worker,
        FileLock,
        FileOpen,
        FileRead,
        ResultWait,
        Decode,
        Apply,
        StageCount
    };
    struct Sample
    {
        uint64_t total_ms = 0;
        uint32_t count = 0;
        uint32_t max_ms = 0;
        void add(uint32_t ms)
        {
            total_ms += ms;
            ++count;
            if (ms > max_ms) max_ms = ms;
        }
        uint32_t average() const { return count ? static_cast<uint32_t>(total_ms / count) : 0; }
    };

    void consumed(const MapTileAsyncEvent& event, uint32_t now_ms)
    {
        ++consumed_count;
        if (event.kind == MapTileAsyncEventKind::Ready) ++ready_count;
        else if (event.kind == MapTileAsyncEventKind::RetryLater) ++retry_count;
        else ++failed_count;
        if (event.timing_available)
        {
            stages[CommandWait].add(event.command_wait_ms);
            stages[Worker].add(event.published_ms - event.worker_started_ms);
            stages[ResultWait].add(now_ms - event.published_ms);
        }
        if (event.read_timing.available)
        {
            stages[FileLock].add(event.read_timing.lock_wait_ms);
            stages[FileOpen].add(event.read_timing.open_ms);
            stages[FileRead].add(event.read_timing.read_ms);
        }
        if (event.read_timing.block_available)
        {
            ++block_samples;
            block_calls += event.read_timing.block_calls;
            block_sectors += event.read_timing.block_sectors;
            block_us += event.read_timing.block_us;
            if (event.read_timing.block_max_sectors > block_max_sectors)
                block_max_sectors = event.read_timing.block_max_sectors;
        }
    }

    std::array<Sample, StageCount> stages{};
    uint32_t consumed_count = 0;
    uint32_t ready_count = 0;
    uint32_t retry_count = 0;
    uint32_t failed_count = 0;
    uint32_t rendered_count = 0;
    uint32_t submission_backpressure = 0;
    uint32_t block_samples = 0;
    uint64_t block_calls = 0;
    uint64_t block_sectors = 0;
    uint64_t block_us = 0;
    uint32_t block_max_sectors = 0;
};
} // namespace ui::map_tiles
