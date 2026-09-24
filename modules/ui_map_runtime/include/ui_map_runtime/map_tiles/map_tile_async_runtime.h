#pragma once

#include "sys/runtime_async.h"
#include "ui_map_runtime/map_tiles/map_tile_render_queue.h"
#include "ui_map_runtime/map_tiles/map_tile_source.h"

#include <cstddef>
#include <cstdint>

namespace ui
{
namespace map_tiles
{

enum class MapTileInteractionMode : uint8_t
{
    Idle,
    InteractiveDrag,
};

enum class MapTileAsyncEventKind : uint8_t
{
    Ready,
    Failed,
    Cancelled,
    RetryLater,
};

struct MapViewportPlan
{
    static constexpr std::size_t kMaxTiles = MapTileRenderQueue::kMaxTiles;

    uint32_t generation = 0;
    MapTileInteractionMode interaction_mode = MapTileInteractionMode::Idle;
    MapTileRef tiles[kMaxTiles]{};
    std::size_t tile_count = 0;
};

struct LoadTileCommand
{
    sys::runtime::RuntimeCommand runtime{};
    MapTileRef tile{};
};

struct TileRequestHandle
{
    uint32_t generation = 0;
    uint32_t command_id = 0;
    explicit operator bool() const { return generation != 0 && command_id != 0; }
    bool matches(uint32_t event_generation, uint32_t event_id) const
    {
        return *this && generation == event_generation && command_id == event_id;
    }
};

enum class TileSubmitStatus : uint8_t
{
    Accepted,
    AlreadyPending,
    Backpressured,
    Unavailable
};

struct TileSubmitResult
{
    TileSubmitStatus status = TileSubmitStatus::Unavailable;
    TileRequestHandle handle{};
    explicit operator bool() const
    {
        return status == TileSubmitStatus::Accepted || status == TileSubmitStatus::AlreadyPending;
    }
};

using MapTileEventKind = MapTileAsyncEventKind;

class MapTileEvent
{
  public:
    MapTileAsyncEventKind kind = MapTileAsyncEventKind::Failed;
    uint32_t command_id = 0;
    uint32_t generation = 0;
    MapTileRef tile{};
    MapTileFormat format = MapTileFormat::Unknown;
    MapTilePayload payload{};
    std::size_t payload_size = 0;
    int32_t error = 0;
    MapTileReadTiming read_timing{};
    uint32_t command_wait_ms = 0;
    uint32_t worker_started_ms = 0;
    uint32_t published_ms = 0;
    bool timing_available = false;
};

using MapTileAsyncEvent = MapTileEvent;

struct MapTileDecodeInput
{
    const uint8_t* payload = nullptr;
    std::size_t payload_size = 0;
    MapTileFormat format = MapTileFormat::Unknown;
};

struct MapTileDecodeResult
{
    bool ok = false;
    int32_t error = 0;
};

class IMapTileDecoder
{
  public:
    virtual ~IMapTileDecoder() = default;

    virtual MapTileDecodeResult decode(const MapTileDecodeInput& input) = 0;
};

class IMapTileUiSink
{
  public:
    virtual ~IMapTileUiSink() = default;

    virtual bool applyTile(const MapTileEvent& event) = 0;
};

class IMapTileCommandSink
{
  public:
    virtual ~IMapTileCommandSink() = default;

    virtual TileSubmitResult enqueue(const LoadTileCommand& command) = 0;
    virtual std::size_t cancelGeneration(uint32_t generation) = 0;
};

enum class MapTileReservationStatus : uint8_t
{
    Reserved,
    Backpressured,
    Cancelled,
};

enum class MapTileExecutionStatus : uint8_t
{
    Completed,
    Backpressured,
    Cancelled,
};

class IMapTileEventSink
{
  public:
    virtual ~IMapTileEventSink() = default;

    // Single producer: reserve before reading. A successful reservation makes
    // publication capacity-safe. false from publish means cancellation only,
    // not queue-full or transient lock contention. Payload copying is owned
    // by the adapter; allocation failure must publish a Failed completion.
    virtual MapTileReservationStatus reserve(const LoadTileCommand& command) = 0;
    virtual bool publish(const MapTileAsyncEvent& event) = 0;
    virtual void releaseReservation() = 0;
};

class IMapTileWorkerBackend
{
  public:
    virtual ~IMapTileWorkerBackend() = default;

    virtual MapTileLookupResult lookup(const MapTileRef& ref) = 0;
    virtual MapTileReadResult read(const MapTileRef& ref,
                                   uint8_t* buffer,
                                   std::size_t capacity) = 0;
};

struct MapTileStateSnapshot
{
    uint32_t active_generation = 0;
    std::size_t ready_count = 0;
    std::size_t failed_count = 0;
    int32_t last_error = 0;
};

class MapTileStateMachine
{
  public:
    void transition(const MapTileEvent& event)
    {
        active_generation_ = event.generation;
        last_error_ = event.error;
        if (event.kind == MapTileAsyncEventKind::Ready)
        {
            ++ready_count_;
        }
        else if (event.kind == MapTileAsyncEventKind::Failed ||
                 event.kind == MapTileAsyncEventKind::RetryLater)
        {
            ++failed_count_;
        }
    }

    void cancelGeneration(uint32_t generation)
    {
        if (active_generation_ == generation)
        {
            active_generation_ = 0;
        }
    }

    MapTileStateSnapshot snapshot() const
    {
        MapTileStateSnapshot snapshot{};
        snapshot.active_generation = active_generation_;
        snapshot.ready_count = ready_count_;
        snapshot.failed_count = failed_count_;
        snapshot.last_error = last_error_;
        return snapshot;
    }

  private:
    uint32_t active_generation_ = 0;
    std::size_t ready_count_ = 0;
    std::size_t failed_count_ = 0;
    int32_t last_error_ = 0;
};

class MapTileAsyncRuntime
{
  public:
    explicit MapTileAsyncRuntime(IMapTileCommandSink& commands);

    uint32_t activeGeneration() const;
    TileSubmitResult requestTile(const MapTileRef& tile, uint32_t generation,
                                 MapTileInteractionMode mode, uint32_t now_ms);
    std::size_t requestVisibleTiles(const MapViewportPlan& plan, uint32_t now_ms);
    std::size_t cancelGeneration(uint32_t generation);
    bool handleEvent(const MapTileAsyncEvent& event, MapTileRenderQueue& render_queue);

  private:
    static sys::runtime::RuntimePriority priorityFor(MapTileInteractionMode mode);
    sys::runtime::RuntimeCommand commandFromIntent(const sys::runtime::RuntimeIntent& intent);

    IMapTileCommandSink& commands_;
    sys::runtime::RuntimeState state_{};
    uint32_t active_generation_ = 0;
    uint32_t next_command_id_ = 1;
};

class MapTileRuntime
{
  public:
    MapTileRuntime(MapTileAsyncRuntime& runtime,
                   MapTileStateMachine& state_machine,
                   IMapTileUiSink* ui_sink = nullptr)
        : runtime_(runtime), state_machine_(state_machine), ui_sink_(ui_sink)
    {
    }

    std::size_t requestVisibleTiles(const MapViewportPlan& plan, uint32_t now_ms)
    {
        return runtime_.requestVisibleTiles(plan, now_ms);
    }

    TileSubmitResult requestTile(const MapTileRef& tile, uint32_t generation,
                                 MapTileInteractionMode mode, uint32_t now_ms)
    {
        return runtime_.requestTile(tile, generation, mode, now_ms);
    }

    std::size_t cancelGeneration(uint32_t generation)
    {
        state_machine_.cancelGeneration(generation);
        return runtime_.cancelGeneration(generation);
    }

    bool handle(const MapTileEvent& event, MapTileRenderQueue& render_queue)
    {
        const bool accepted = runtime_.handleEvent(event, render_queue);
        if (!accepted)
        {
            return false;
        }

        state_machine_.transition(event);
        if (ui_sink_)
        {
            (void)ui_sink_->applyTile(event);
        }
        return true;
    }

    MapTileStateSnapshot snapshot() const
    {
        return state_machine_.snapshot();
    }

  private:
    MapTileAsyncRuntime& runtime_;
    MapTileStateMachine& state_machine_;
    IMapTileUiSink* ui_sink_ = nullptr;
};

class MapTileWorker
{
  public:
    MapTileWorker(IMapTileWorkerBackend& backend,
                  IMapTileEventSink& events,
                  uint8_t* scratch,
                  std::size_t scratch_size);

    MapTileExecutionStatus execute(const LoadTileCommand& command, uint32_t now_ms);

  private:
    IMapTileWorkerBackend& backend_;
    IMapTileEventSink& events_;
    uint8_t* scratch_ = nullptr;
    std::size_t scratch_size_ = 0;
};

} // namespace map_tiles
} // namespace ui
