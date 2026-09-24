#include "ui_map_runtime/map_tiles/map_tile_pipeline_metrics.h"
#include <cassert>
#include <limits>

using namespace ui::map_tiles;
int main()
{
    MapTilePipelineMetrics metrics;
    MapTileAsyncEvent event;
    event.kind = MapTileAsyncEventKind::Ready;
    event.timing_available = true;
    event.command_wait_ms = 17;
    event.worker_started_ms = UINT32_MAX - 4;
    event.published_ms = 3; // wraparound is a normal monotonic-clock event
    event.read_timing = {2, 3, 4, true};
    event.read_timing.block_available = true;
    event.read_timing.block_calls = 3;
    event.read_timing.block_sectors = 19;
    event.read_timing.block_max_sectors = 8;
    event.read_timing.block_us = 1234;
    metrics.consumed(event, 13);
    assert(metrics.block_samples == 1 && metrics.block_calls == 3 && metrics.block_sectors == 19);
    assert(metrics.block_max_sectors == 8 && metrics.block_us == 1234);
    assert(metrics.stages[MapTilePipelineMetrics::Worker].average() == 8);
    assert(metrics.stages[MapTilePipelineMetrics::ResultWait].average() == 10);
    assert(metrics.stages[MapTilePipelineMetrics::CommandWait].average() == 17);
    assert(metrics.stages[MapTilePipelineMetrics::FileOpen].average() == 3);
    event.read_timing = {}; // cache-only result must not dilute measured file times
    event.timing_available = false;
    event.kind = MapTileAsyncEventKind::RetryLater;
    metrics.consumed(event, 20);
    assert(metrics.consumed_count == 2 && metrics.ready_count == 1 && metrics.retry_count == 1);
    assert(metrics.stages[MapTilePipelineMetrics::FileOpen].count == 1);
    assert(metrics.stages[MapTilePipelineMetrics::Worker].count == 1);
    assert(metrics.block_samples == 1);
    auto& decode = metrics.stages[MapTilePipelineMetrics::Decode];
    decode.add(UINT32_MAX);
    decode.add(UINT32_MAX);
    assert(decode.average() == UINT32_MAX && decode.max_ms == UINT32_MAX);
    metrics = {};
    assert(metrics.consumed_count == 0 && metrics.stages[MapTilePipelineMetrics::Decode].count == 0);
}
