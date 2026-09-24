#include "ui_map_runtime/map_tiles/map_tile_completion_queue.h"
#include <cassert>
#include <memory>

using Queue = ui::map_tiles::MapTileCompletionQueue<std::unique_ptr<int>, 2>;

int main()
{
    Queue queue;
    auto first = queue.reserve(1);
    auto second = queue.reserve(1);
    assert(first && second && queue.occupied() == 2);
    assert(!queue.reserve(1));
    assert(queue.highWater() == 2 && queue.backpressureCount() == 1);
    // Reverse completion order must not expose an unfinished reservation.
    assert(queue.commit(second, std::make_unique<int>(2)));
    std::unique_ptr<int> out;
    assert(queue.pop(out) && *out == 2);
    assert(queue.commit(first, std::make_unique<int>(1)));
    assert(queue.pop(out) && *out == 1);
    assert(queue.occupied() == 0);

    auto reserved = queue.reserve(2);
    auto ready = queue.reserve(2);
    assert(queue.commit(ready, std::make_unique<int>(3)));
    assert(queue.cancelGeneration(2) == 2);
    assert(!queue.active(reserved));
    assert(!queue.pop(out));
    // Cancellation must not recycle memory still owned by the producer.
    assert(!queue.reserve(3));
    auto payload = std::make_unique<int>(4);
    assert(!queue.commit(reserved, std::move(payload)) && payload && *payload == 4);
    assert(queue.release(reserved));
    assert(queue.takeDiscarded(out) && *out == 3);
    assert(!queue.takeDiscarded(out));
    assert(queue.occupied() == 0);

    auto replacement = queue.reserve(3);
    assert(replacement);
    assert(!queue.release(reserved));
    assert(!queue.commit(reserved, std::move(payload)) && payload);
    assert(queue.active(replacement));
    assert(queue.release(replacement));
    assert(!queue.release(replacement));

    // Slow/paused consumers and fast producers stay bounded and lose nothing.
    int delivered = 0;
    for (int i = 0; i < 1000; ++i)
    {
        auto token = queue.reserve(7);
        if (!token)
        {
            assert(queue.pop(out) && *out == delivered++);
            token = queue.reserve(7);
        }
        assert(queue.commit(token, std::make_unique<int>(i)));
        assert(queue.occupied() <= 2);
    }
    while (queue.pop(out)) assert(*out == delivered++);
    assert(delivered == 1000 && queue.occupied() == 0);
}
