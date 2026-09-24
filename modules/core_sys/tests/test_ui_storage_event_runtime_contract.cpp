#include "gps/track_runtime.h"
#include "sys/feedback_runtime.h"
#include "sys/runtime_harness.h"
#include "ui_map_runtime/map_tiles/map_tile_async_runtime.h"

#include <cassert>
#include <cstddef>
#include <cstring>

namespace
{

class MapCommandSink final : public ui::map_tiles::IMapTileCommandSink
{
  public:
    ui::map_tiles::TileSubmitResult enqueue(const ui::map_tiles::LoadTileCommand& command) override
    {
        if (count >= 4)
        {
            return {ui::map_tiles::TileSubmitStatus::Backpressured, {}};
        }
        commands[count++] = command;
        return {ui::map_tiles::TileSubmitStatus::Accepted, {command.runtime.generation, command.runtime.command_id}};
    }

    std::size_t cancelGeneration(uint32_t generation) override
    {
        std::size_t removed = 0;
        ui::map_tiles::LoadTileCommand kept[4]{};
        std::size_t kept_count = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (commands[i].runtime.generation == generation)
            {
                ++removed;
            }
            else
            {
                kept[kept_count++] = commands[i];
            }
        }
        for (std::size_t i = 0; i < kept_count; ++i)
        {
            commands[i] = kept[i];
        }
        count = kept_count;
        return removed;
    }

    ui::map_tiles::LoadTileCommand commands[4]{};
    std::size_t count = 0;
};

class MapUiSink final : public ui::map_tiles::IMapTileUiSink
{
  public:
    bool applyTile(const ui::map_tiles::MapTileEvent& event) override
    {
        last = event;
        ++count;
        return true;
    }

    ui::map_tiles::MapTileEvent last{};
    std::size_t count = 0;
};

class TrackEvents final : public gps::runtime::ITrackEventSink
{
  public:
    bool publish(const gps::runtime::TrackEvent& event) override
    {
        if (count >= 64)
        {
            return false;
        }
        events[count++] = event;
        return true;
    }

    gps::runtime::TrackEvent events[64]{};
    std::size_t count = 0;
};

class TrackFiles final : public gps::runtime::ITrackFileAdapter
{
  public:
    bool open(const gps::runtime::TrackStorageDescriptor& storage) override
    {
        active_track = storage.track_id;
        last_storage = storage;
        ++open_count;
        return open_ok;
    }

    bool append(const gps::runtime::TrackStorageDescriptor& storage,
                const gps::runtime::TrackPoint* points,
                std::size_t count) override
    {
        assert(storage.track_id == active_track);
        assert(points != nullptr || count == 0);
        last_storage = storage;
        appended += count;
        return true;
    }

    bool flush(const gps::runtime::TrackStorageDescriptor& storage) override
    {
        assert(storage.track_id == active_track);
        last_storage = storage;
        ++flush_count;
        return true;
    }

    bool close(const gps::runtime::TrackStorageDescriptor& storage) override
    {
        assert(storage.track_id == active_track);
        last_storage = storage;
        ++close_count;
        return true;
    }

    std::size_t list(gps::runtime::TrackStorageDescriptor* tracks,
                     std::size_t capacity) override
    {
        if (tracks && capacity > 0)
        {
            tracks[0] = last_storage;
            return 1;
        }
        return 0;
    }

    uint32_t active_track = 0;
    gps::runtime::TrackStorageDescriptor last_storage{};
    std::size_t appended = 0;
    std::size_t open_count = 0;
    std::size_t flush_count = 0;
    std::size_t close_count = 0;
    bool open_ok = true;
};

gps::runtime::TrackStorageDescriptor trackDescriptor()
{
    gps::runtime::TrackStorageDescriptor storage{};
    storage.track_id = 123;
    storage.path = "/trackers/session.gpx";
    storage.format = gps::runtime::TrackStorageFormat::Gpx;
    storage.active_state_path = "/trackers/active.bin";
    storage.manual_recording = true;
    storage.auto_recording = false;
    storage.persist_active_state = true;
    storage.append_footer_on_close = true;
    return storage;
}

ui::map_tiles::MapTileRef tileRef()
{
    ui::map_tiles::MapTileRef ref{};
    ref.layer = ui::map_tiles::MapTileLayer::Osm;
    ref.z = 12;
    ref.x = 100;
    ref.y = 200;
    return ref;
}

void test_map_tile_runtime_contract()
{
    MapCommandSink commands;
    ui::map_tiles::MapTileAsyncRuntime async(commands);
    ui::map_tiles::MapTileStateMachine states;
    MapUiSink ui;
    ui::map_tiles::MapTileRuntime runtime(async, states, &ui);

    ui::map_tiles::MapViewportPlan plan{};
    plan.generation = 7;
    plan.tile_count = 1;
    plan.tiles[0] = tileRef();
    assert(runtime.requestVisibleTiles(plan, 10) == 1);
    assert(commands.count == 1);

    ui::map_tiles::MapTileEvent ready{};
    ready.kind = ui::map_tiles::MapTileEventKind::Ready;
    ready.generation = 7;
    ready.tile = tileRef();
    ui::map_tiles::MapTileRenderQueue render_queue;
    assert(runtime.handle(ready, render_queue));
    assert(ui.count == 1);
    assert(runtime.snapshot().ready_count == 1);

    for (std::size_t i = 1; i < 4; ++i)
    {
        auto tile = tileRef();
        tile.x += static_cast<uint32_t>(i);
        const auto result = async.requestTile(tile, 7, ui::map_tiles::MapTileInteractionMode::Idle, 20);
        assert(result.status == ui::map_tiles::TileSubmitStatus::Accepted);
        assert(result.handle.matches(commands.commands[i].runtime.generation,
                                     commands.commands[i].runtime.command_id));
    }
    const auto full = async.requestTile(tileRef(), 7, ui::map_tiles::MapTileInteractionMode::Idle, 30);
    assert(full.status == ui::map_tiles::TileSubmitStatus::Backpressured);
    assert(!full.handle);
    assert(commands.count == 4);
}

void test_feedback_runtime_contract()
{
    sys::runtime::RuntimeHarness harness;
    sys::runtime::FeedbackQueue<4> queue;
    sys::runtime::DefaultFeedbackPolicy policy;
    sys::runtime::FeedbackRuntime<4> runtime(queue,
                                             policy,
                                             harness.feedbackPresenter(),
                                             harness.events());

    sys::runtime::NoticeIntent sent{};
    sent.category = sys::runtime::NoticeCategory::ChatDelivery;
    sent.severity = sys::runtime::NoticeSeverity::Success;
    sys::runtime::setNoticeMessage(sent, "Sent");
    sent.dedupe_key = 42;
    sent.created_at_ms = 10;
    assert(runtime.post(sent));
    assert(runtime.post(sent));
    assert(runtime.drainToUi(20) == 1);
    assert(harness.feedbackPresenter().captured() == 1);
    assert(harness.feedbackPresenter().capturedNotice(0).duration_ms == 1400);
}

void test_track_runtime_contract()
{
    sys::runtime::RuntimeHarness harness;
    TrackFiles files;
    TrackEvents events;
    gps::runtime::DefaultTrackFlushPolicy policy;
    gps::runtime::TrackStorageWorker worker(files, events, policy);
    gps::runtime::TrackPointBuffer<8> points;
    gps::runtime::TrackStateMachine states;
    gps::runtime::TrackRuntime<8> runtime(points, states, policy, worker, events);

    const gps::runtime::TrackStorageDescriptor storage = trackDescriptor();
    assert(runtime.startNewTrack(storage, 0));
    runtime.tick(1);
    assert(events.count == 1);
    runtime.handle(events.events[0]);
    assert(states.state() == gps::runtime::TrackRecorderStatus::Recording);
    assert(events.events[0].storage.track_id == storage.track_id);
    assert(events.events[0].storage.format == storage.format);
    assert(std::strcmp(events.events[0].storage.path, storage.path) == 0);

    gps::runtime::TrackPoint point{};
    point.latitude = 1.0;
    point.longitude = 2.0;
    for (int i = 0; i < 8; ++i)
    {
        point.timestamp_ms = static_cast<uint32_t>(i);
        assert(runtime.appendPoint(point, 10 + static_cast<uint32_t>(i)));
    }
    runtime.tick(30);
    assert(files.appended == 8);
    assert(files.flush_count == 1);
}

void test_track_worker_failure_completes_semantically()
{
    sys::runtime::RuntimeHarness harness;
    TrackFiles files;
    files.open_ok = false;
    TrackEvents events;
    gps::runtime::DefaultTrackFlushPolicy policy;
    gps::runtime::TrackStorageWorker worker(files, events, policy);

    gps::runtime::TrackCommand command{};
    command.command_id = 44;
    command.kind = gps::runtime::TrackCommandKind::StartNewTrack;
    command.storage = trackDescriptor();

    assert(worker.submit(command));
    worker.tick(10);
    assert(!worker.busy());
    assert(files.open_count == 1);
    assert(events.count == 1);
    assert(events.events[0].kind == gps::runtime::TrackEventKind::Failed);
}

void test_track_runtime_keeps_buffered_points_while_worker_busy()
{
    sys::runtime::RuntimeHarness harness;
    TrackFiles files;
    TrackEvents events;
    gps::runtime::DefaultTrackFlushPolicy policy;
    gps::runtime::TrackStorageWorker worker(files, events, policy);
    gps::runtime::TrackPointBuffer<8> points;
    gps::runtime::TrackStateMachine states;
    gps::runtime::TrackRuntime<8> runtime(points, states, policy, worker, events);

    const gps::runtime::TrackStorageDescriptor storage = trackDescriptor();
    assert(runtime.startNewTrack(storage, 0));
    runtime.tick(1);
    runtime.handle(events.events[0]);
    assert(states.state() == gps::runtime::TrackRecorderStatus::Recording);

    gps::runtime::TrackPoint point{};
    point.latitude = 1.0;
    point.longitude = 2.0;

    for (int i = 0; i < 8; ++i)
    {
        point.timestamp_ms = static_cast<uint32_t>(i);
        assert(runtime.appendPoint(point, 10 + static_cast<uint32_t>(i)));
    }
    runtime.tick(30);
    assert(!worker.busy());
    assert(files.appended == 8);

    for (int i = 0; i < 8; ++i)
    {
        point.timestamp_ms = static_cast<uint32_t>(100 + i);
        assert(runtime.appendPoint(point, 40 + static_cast<uint32_t>(i)));
    }

    runtime.tick(60);
    assert(files.appended == 16);
    assert(!worker.busy());
}

void test_track_stop_appends_pending_points_before_close()
{
    sys::runtime::RuntimeHarness harness;
    TrackFiles files;
    TrackEvents events;
    gps::runtime::DefaultTrackFlushPolicy policy;
    gps::runtime::TrackStorageWorker worker(files, events, policy);
    gps::runtime::TrackPointBuffer<8> points;
    gps::runtime::TrackStateMachine states;
    gps::runtime::TrackRuntime<8> runtime(points, states, policy, worker, events);

    const gps::runtime::TrackStorageDescriptor storage = trackDescriptor();
    assert(runtime.startNewTrack(storage, 0));
    runtime.tick(1);
    runtime.handle(events.events[0]);

    gps::runtime::TrackPoint point{};
    point.latitude = 1.0;
    point.longitude = 2.0;
    point.timestamp_ms = 10;
    assert(runtime.appendPoint(point, 10));
    point.timestamp_ms = 11;
    assert(runtime.appendPoint(point, 11));

    assert(runtime.stopTrack(20));
    runtime.tick(21);
    assert(files.appended == 2);
    assert(files.flush_count == 1);
    assert(files.close_count == 1);
    assert(files.last_storage.track_id == storage.track_id);
    assert(files.last_storage.append_footer_on_close);
    assert(std::strcmp(files.last_storage.active_state_path, storage.active_state_path) == 0);
}

void test_runtime_harness_keeps_ui_drain_separate()
{
    sys::runtime::RuntimeHarness harness;
    harness.advance(5);
    harness.ui().assertNoBlockingCalls();
    assert(harness.drainUi() == 0);
}

} // namespace

int main()
{
    test_map_tile_runtime_contract();
    test_feedback_runtime_contract();
    test_track_runtime_contract();
    test_track_worker_failure_completes_semantically();
    test_track_runtime_keeps_buffered_points_while_worker_busy();
    test_track_stop_appends_pending_points_before_close();
    test_runtime_harness_keeps_ui_drain_separate();
    return 0;
}
