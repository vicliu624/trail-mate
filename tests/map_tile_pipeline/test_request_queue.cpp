#include "ui_map_runtime/map_tiles/map_tile_request_queue.h"
#include <cassert>

using namespace ui::map_tiles;

int main()
{
    MapTileRequestQueue<2> queue;
    std::size_t queued = 99, in_flight = 99;
    queue.statistics(queued, in_flight);
    assert(queued == 0 && in_flight == 0);
    LoadTileCommand first;
    first.runtime.generation = 1;
    first.runtime.command_id = 1;
    first.runtime.created_at_ms = 10;
    first.tile.x = 10;
    const auto accepted = queue.enqueue(first);
    assert(accepted.status == TileSubmitStatus::Accepted);
    queue.statistics(queued, in_flight);
    assert(queued == 1 && in_flight == 0);
    auto duplicate = first;
    duplicate.runtime.command_id = 2;
    assert(queue.enqueue(duplicate).handle.command_id == 1);
    LoadTileCommand running;
    assert(queue.pop(11, running) && running.runtime.command_id == 1);
    queue.statistics(queued, in_flight);
    assert(queued == 0 && in_flight == 1);
    assert(!queue.pop(11, running));
    assert(queue.enqueue(duplicate).status == TileSubmitStatus::AlreadyPending);

    auto second = first;
    second.tile.layer = MapTileLayer::Poi;
    second.runtime.command_id = 3;
    assert(queue.enqueue(second).status == TileSubmitStatus::Accepted);
    queue.statistics(queued, in_flight);
    assert(queued == 1 && in_flight == 1);
    auto third = first;
    third.tile.x = 11;
    third.runtime.command_id = 4;
    assert(queue.enqueue(third).status == TileSubmitStatus::Backpressured);
    assert(!queue.complete({1, 2})); // Duplicate submission did not replace identity.
    assert(queue.complete(accepted.handle));
    assert(queue.enqueue(third).status == TileSubmitStatus::Accepted);
    assert(queue.cancelGeneration(1) == 2);
    queue.statistics(queued, in_flight);
    assert(queued == 0 && in_flight == 0);
    assert(!queue.pop(20, running));

    first.runtime.generation = 2;
    assert(queue.enqueue(first));
    assert(queue.pop(21, running));
    assert(!queue.complete({1, 1})); // A late old-generation result cannot retire new work.
    assert(queue.complete({2, 1}));
    assert(!queue.complete({2, 1}));
}
