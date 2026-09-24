#include "platform/esp/arduino_common/map_tiles/map_tile_command_queue.h"
#include "platform/esp/arduino_common/map_tiles/map_tile_event_queue.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#ifndef _WIN32
#include <thread>
#endif

using namespace ui::map_tiles;
using Queue = platform::esp::arduino_common::map_tiles::MapTileEventQueue;
static std::atomic<int> live_payloads{0};
static bool fail_allocation = false;
static Queue* allocation_lock_probe = nullptr;

static MapTileAsyncEvent copy_event(const MapTileAsyncEvent& input)
{
    Queue::Statistics stats;
    assert(!allocation_lock_probe || allocation_lock_probe->statistics(stats));
    auto owned = input;
    if (input.payload.data)
    {
        if (fail_allocation)
        {
            owned.payload = {};
            owned.payload_size = 0;
            owned.kind = MapTileAsyncEventKind::Failed;
            owned.error = -12;
        }
        else
        {
            auto* bytes = new uint8_t[input.payload.size];
            std::memcpy(bytes, input.payload.data, input.payload.size);
            owned.payload.data = bytes;
            ++live_payloads;
        }
    }
    return owned;
}

static void release_event(MapTileAsyncEvent& event)
{
    Queue::Statistics stats;
    assert(!allocation_lock_probe || allocation_lock_probe->statistics(stats));
    if (event.payload.data)
    {
        delete[] event.payload.data;
        --live_payloads;
    }
    event = {};
}

class Backend : public IMapTileWorkerBackend
{
  public:
    Queue* cancel_during_read = nullptr;
    int reads = 0;
    MapTileLookupResult lookup(const MapTileRef&) override { return {}; }
    MapTileReadResult read(const MapTileRef&, uint8_t* data, std::size_t) override
    {
        ++reads;
        data[0] = 42;
        if (cancel_during_read) cancel_during_read->cancelGeneration(1);
        return {MapTileReadStatus::Ready, 1, 0, MapTileFormat::Png};
    }
};

struct CancelContext
{
    Queue* queue;
    uint32_t generation;
};
struct WaitContext
{
    Queue* queue;
    std::atomic<bool> waiting{false};
    uint32_t elapsed_ms = 0;
};
static void wait_capacity(WaitContext& context)
{
    const auto start = std::chrono::steady_clock::now();
    context.waiting = true;
    context.queue->waitForCapacity(pdMS_TO_TICKS(2000));
    context.elapsed_ms = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                   std::chrono::steady_clock::now() - start)
                                                   .count());
}
#ifdef _WIN32
static DWORD WINAPI capacity_wait_thread(void* data)
{
    wait_capacity(*static_cast<WaitContext*>(data));
    return 0;
}
static DWORD WINAPI cancel_thread(void* data)
{
    const auto& context = *static_cast<CancelContext*>(data);
    context.queue->cancelGeneration(context.generation);
    return 0;
}
#endif

int main()
{
    Queue queue(copy_event, release_event);
    allocation_lock_probe = &queue;
    assert(queue.available());
    queue.activateGeneration(1);
    Backend backend;
    uint8_t scratch[8]{};
    MapTileWorker worker(backend, queue, scratch, sizeof(scratch));
    LoadTileCommand command;
    command.runtime.generation = 1;
    for (uint32_t id = 1; id <= 16; ++id)
    {
        command.runtime.command_id = id;
        assert(worker.execute(command, 0) == MapTileExecutionStatus::Completed);
    }
    assert(live_payloads == 16 && backend.reads == 16);
    assert(worker.execute(command, 0) == MapTileExecutionStatus::Backpressured);
    assert(backend.reads == 16); // No read or payload allocation under pressure.
    MapTileAsyncEvent event;
    for (uint32_t id = 1; id <= 16; ++id)
    {
        assert(queue.pop(event) && event.command_id == id && event.payload.data[0] == 42);
        release_event(event);
    }
    assert(live_payloads == 0);

    fail_allocation = true;
    assert(worker.execute(command, 0) == MapTileExecutionStatus::Completed);
    assert(queue.pop(event) && event.kind == MapTileAsyncEventKind::Failed && event.error == -12);
    release_event(event);
    fail_allocation = false;

    // Gesture maintenance must be able to remove an obsolete completion
    // behind a valid ready head, without decoding or dropping the valid head.
    command.runtime.command_id = 41;
    assert(worker.execute(command, 10) == MapTileExecutionStatus::Completed);
    command.runtime.command_id = 42;
    assert(worker.execute(command, 10) == MapTileExecutionStatus::Completed);
    assert(queue.popIf(event, [](const auto& value)
                       { return value.command_id == 42; }));
    assert(event.command_id == 42);
    release_event(event);
    assert(queue.pop(event) && event.command_id == 41);
    release_event(event);
    assert(live_payloads == 0);
    Queue::Statistics stats;
    assert(queue.statistics(stats) && stats.occupied == 0 && stats.high_water == 16 && stats.backpressure == 1);

    backend.cancel_during_read = &queue;
    assert(worker.execute(command, 0) == MapTileExecutionStatus::Cancelled);
    assert(live_payloads == 0 && !queue.pop(event));
    assert(worker.execute(command, 0) == MapTileExecutionStatus::Cancelled);
    backend.cancel_during_read = nullptr;

    // Exercise real producer/canceller interleavings, not only a fake sink.
    // A try-lock cannot distinguish another thread's legitimate ownership.
    allocation_lock_probe = nullptr;
    for (uint32_t generation = 2; generation < 102; ++generation)
    {
        command.runtime.generation = generation;
        queue.activateGeneration(generation);
        assert(queue.reserve(command) == MapTileReservationStatus::Reserved);
        MapTileAsyncEvent ready;
        ready.generation = generation;
        ready.kind = MapTileAsyncEventKind::Ready;
        ready.payload = {command.tile, MapTileFormat::Png, scratch, 1};
        CancelContext context{&queue, generation};
#ifdef _WIN32
        HANDLE cancel = CreateThread(nullptr, 0, cancel_thread, &context, 0, nullptr);
        assert(cancel);
#else
        std::thread cancel([&]
                           { context.queue->cancelGeneration(context.generation); });
#endif
        (void)queue.publish(ready);
        queue.releaseReservation();
#ifdef _WIN32
        assert(WaitForSingleObject(cancel, INFINITE) == WAIT_OBJECT_0);
        CloseHandle(cancel);
#else
        cancel.join();
#endif
        assert(!queue.pop(event) && live_payloads == 0);
    }
    allocation_lock_probe = &queue;
    queue.activateGeneration(103);
    command.runtime.generation = 103;
    assert(worker.execute(command, 0) == MapTileExecutionStatus::Completed);
    queue.clear();
    assert(live_payloads == 0);

    // Drive the production command adapter, runtime, worker and completion
    // adapter together. Dedupe covers queued, running and completed-but-not-
    // consumed work, and old completions cannot retire replacement requests.
    platform::esp::arduino_common::map_tiles::MapTileCommandQueue commands;
    MapTileAsyncRuntime runtime(commands);
    queue.activateGeneration(200);
    MapTileRef tile{MapTileLayer::Osm, 12, 100, 200};
    const auto first = runtime.requestTile(tile, 200, MapTileInteractionMode::Idle, 10);
    assert(first.status == TileSubmitStatus::Accepted);
    auto duplicate = runtime.requestTile(tile, 200, MapTileInteractionMode::Idle, 11);
    assert(duplicate.status == TileSubmitStatus::AlreadyPending);
    assert(duplicate.handle.matches(first.handle.generation, first.handle.command_id));
    assert(commands.pop(12, command));
    assert(worker.execute(command, 12) == MapTileExecutionStatus::Completed);
    duplicate = runtime.requestTile(tile, 200, MapTileInteractionMode::Idle, 13);
    assert(duplicate.status == TileSubmitStatus::AlreadyPending);
    assert(!commands.pop(13, command));
    assert(queue.pop(event));
    assert(event.command_wait_ms == 2 && event.worker_started_ms == 12);
    commands.complete({event.generation, event.command_id});
    release_event(event);
    const auto replacement = runtime.requestTile(tile, 200, MapTileInteractionMode::Idle, 14);
    assert(replacement.status == TileSubmitStatus::Accepted);
    assert(replacement.handle.command_id != first.handle.command_id);
    assert(commands.pop(15, command));
    commands.complete(first.handle);
    duplicate = runtime.requestTile(tile, 200, MapTileInteractionMode::Idle, 16);
    assert(duplicate.status == TileSubmitStatus::AlreadyPending);
    assert(duplicate.handle.command_id == replacement.handle.command_id);
    assert(queue.reserve(command) == MapTileReservationStatus::Reserved);
    queue.cancelGeneration(200);
    assert(runtime.cancelGeneration(200) == 1);
    queue.activateGeneration(201);
    const auto next = runtime.requestTile(tile, 201, MapTileInteractionMode::Idle, 17);
    assert(next.status == TileSubmitStatus::Accepted);
    MapTileAsyncEvent late;
    late.generation = 200;
    late.command_id = replacement.handle.command_id;
    late.kind = MapTileAsyncEventKind::Ready;
    late.payload = {tile, MapTileFormat::Png, scratch, 1};
    assert(!queue.publish(late));
    queue.releaseReservation();
    commands.complete(replacement.handle);
    assert(commands.pop(18, command) && command.runtime.generation == 201);
    assert(worker.execute(command, 18) == MapTileExecutionStatus::Completed);
    queue.cancelGeneration(201); // close viewport with a ready result
    assert(runtime.cancelGeneration(201) == 1);
    assert(!commands.pop(19, command) && !queue.pop(event) && live_payloads == 0);
    allocation_lock_probe = nullptr;
    // Closing the last viewport must wake a worker blocked on capacity, not
    // leave it waiting until a UI consumer (which no longer exists) pops data.
    queue.activateGeneration(300);
    queue.waitForCapacity(0); // consume the activation notification
    WaitContext waiting{&queue};
#ifdef _WIN32
    HANDLE waiter = CreateThread(nullptr, 0, capacity_wait_thread, &waiting, 0, nullptr);
    assert(waiter);
    while (!waiting.waiting) SwitchToThread();
#else
    std::thread waiter([&]
                       { wait_capacity(waiting); });
    while (!waiting.waiting) std::this_thread::yield();
#endif
    queue.cancelGeneration(300);
#ifdef _WIN32
    assert(WaitForSingleObject(waiter, 3000) == WAIT_OBJECT_0);
    CloseHandle(waiter);
#else
    waiter.join();
#endif
    assert(waiting.elapsed_ms < 1000);
}
