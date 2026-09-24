#include "ui_map_runtime/map_tiles/map_tile_async_runtime.h"

namespace ui
{
namespace map_tiles
{
namespace
{

uint32_t mixTileDedupe(uint32_t hash, uint32_t value)
{
    hash ^= value;
    hash *= 16777619u;
    return hash;
}

uint32_t dedupeKeyForTile(const MapTileRef& ref)
{
    uint32_t hash = 2166136261u;
    hash = mixTileDedupe(hash, static_cast<uint32_t>(ref.layer));
    hash = mixTileDedupe(hash, static_cast<uint32_t>(ref.z));
    hash = mixTileDedupe(hash, ref.x);
    hash = mixTileDedupe(hash, ref.y);
    return hash == 0 ? 1u : hash;
}

} // namespace

MapTileAsyncRuntime::MapTileAsyncRuntime(IMapTileCommandSink& commands)
    : commands_(commands)
{
}

uint32_t MapTileAsyncRuntime::activeGeneration() const
{
    return active_generation_;
}

TileSubmitResult MapTileAsyncRuntime::requestTile(const MapTileRef& tile, uint32_t generation,
                                                  MapTileInteractionMode mode, uint32_t now_ms)
{
    if (generation == 0) return {};
    if (generation != active_generation_)
    {
        if (active_generation_ != 0) (void)commands_.cancelGeneration(active_generation_);
        active_generation_ = generation;
    }
    sys::runtime::RuntimeIntent intent{};
    intent.kind = sys::runtime::RuntimeCommandKind::MapTileLoad;
    intent.priority_hint = priorityFor(mode);
    intent.cancel_policy = sys::runtime::RuntimeCancelPolicy::CancelByGeneration;
    intent.submitted_at_ms = now_ms;
    intent.generation = generation;
    intent.origin = static_cast<uint32_t>(mode);
    intent.dedupe_key = dedupeKeyForTile(tile);
    LoadTileCommand command{};
    command.tile = tile;
    command.runtime = commandFromIntent(intent);
    const auto result = commands_.enqueue(command);
    if (result)
    {
        state_.status = sys::runtime::RuntimeStatus::Queued;
        state_.active_kind = command.runtime.kind;
        state_.active_command_id = result.handle.command_id;
    }
    return result;
}

std::size_t MapTileAsyncRuntime::requestVisibleTiles(const MapViewportPlan& plan, uint32_t now_ms)
{
    if (plan.generation != active_generation_)
    {
        if (active_generation_ != 0) (void)commands_.cancelGeneration(active_generation_);
        active_generation_ = plan.generation;
    }
    std::size_t queued = 0;
    const auto count = plan.tile_count < MapViewportPlan::kMaxTiles ? plan.tile_count : MapViewportPlan::kMaxTiles;
    for (std::size_t i = 0; i < count; ++i)
        if (requestTile(plan.tiles[i], plan.generation, plan.interaction_mode, now_ms)) ++queued;
    return queued;
}

std::size_t MapTileAsyncRuntime::cancelGeneration(uint32_t generation)
{
    if (active_generation_ == generation)
    {
        active_generation_ = 0;
    }
    return commands_.cancelGeneration(generation);
}

bool MapTileAsyncRuntime::handleEvent(const MapTileAsyncEvent& event, MapTileRenderQueue& render_queue)
{
    if (event.generation != active_generation_)
    {
        return false;
    }

    MapTileRenderRef ref{};
    ref.tile = event.tile;
    switch (event.kind)
    {
    case MapTileAsyncEventKind::Ready:
        ref.state = MapTileRenderState::Ready;
        break;
    case MapTileAsyncEventKind::Cancelled:
        ref.state = MapTileRenderState::Cancelled;
        break;
    case MapTileAsyncEventKind::RetryLater:
        ref.state = MapTileRenderState::Loading;
        break;
    case MapTileAsyncEventKind::Failed:
    default:
        ref.state = MapTileRenderState::Error;
        break;
    }
    return render_queue.push(ref);
}

sys::runtime::RuntimePriority MapTileAsyncRuntime::priorityFor(MapTileInteractionMode mode)
{
    return mode == MapTileInteractionMode::InteractiveDrag ? sys::runtime::RuntimePriority::Interactive
                                                           : sys::runtime::RuntimePriority::Normal;
}

sys::runtime::RuntimeCommand MapTileAsyncRuntime::commandFromIntent(
    const sys::runtime::RuntimeIntent& intent)
{
    sys::runtime::RuntimeCommand command{};
    command.command_id = next_command_id_++;
    if (command.command_id == 0) command.command_id = next_command_id_++;
    command.kind = intent.kind;
    command.priority = intent.priority_hint;
    command.cancel_policy = intent.cancel_policy;
    command.created_at_ms = intent.submitted_at_ms;
    command.deadline_ms = intent.deadline_ms;
    command.generation = intent.generation;
    command.dedupe_key = intent.dedupe_key;
    command.origin = intent.origin;
    return command;
}

MapTileWorker::MapTileWorker(IMapTileWorkerBackend& backend,
                             IMapTileEventSink& events,
                             uint8_t* scratch,
                             std::size_t scratch_size)
    : backend_(backend),
      events_(events),
      scratch_(scratch),
      scratch_size_(scratch_size)
{
}

MapTileExecutionStatus MapTileWorker::execute(const LoadTileCommand& command, uint32_t now_ms)
{
    const auto reservation = events_.reserve(command);
    if (reservation == MapTileReservationStatus::Backpressured) return MapTileExecutionStatus::Backpressured;
    if (reservation == MapTileReservationStatus::Cancelled) return MapTileExecutionStatus::Cancelled;
    struct ReleaseReservation
    {
        IMapTileEventSink& sink;
        ~ReleaseReservation() { sink.releaseReservation(); }
    } release{events_};

    MapTileAsyncEvent event{};
    event.command_id = command.runtime.command_id;
    event.generation = command.runtime.generation;
    event.tile = command.tile;
    event.command_wait_ms = now_ms - command.runtime.created_at_ms;
    event.worker_started_ms = now_ms;

    const MapTileReadResult read_result =
        backend_.read(command.tile, scratch_, scratch_size_);
    const bool ok = read_result.status == MapTileReadStatus::Ready;
    if (read_result.status == MapTileReadStatus::RetryLater)
    {
        event.kind = MapTileAsyncEventKind::RetryLater;
    }
    else
    {
        event.kind = ok ? MapTileAsyncEventKind::Ready : MapTileAsyncEventKind::Failed;
    }
    event.format = read_result.format;
    event.read_timing = read_result.timing;
    event.payload_size = read_result.size;
    if (ok)
    {
        event.payload.ref = command.tile;
        event.payload.format = read_result.format;
        event.payload.data = scratch_;
        event.payload.size = read_result.size;
    }
    event.error = ok ? 0 : read_result.error;
    const bool delivered = events_.publish(event);
    (void)now_ms;
    return delivered ? MapTileExecutionStatus::Completed : MapTileExecutionStatus::Cancelled;
}

} // namespace map_tiles
} // namespace ui
