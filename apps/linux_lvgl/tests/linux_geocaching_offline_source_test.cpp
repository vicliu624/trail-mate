#include "geocaching_signed_fixture.h"
#include "platform/linux/geocaching_gpx_file_port.h"
#include "platform/linux/geocaching_source.h"
#include "ui_presentation/geocaching/local_map_overlay.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

using Source = platform::linux_runtime::LinuxGeocachingSource;
using Journal = platform::linux_runtime::LinuxGeocachingInstallJournal;
using Records = platform::linux_runtime::LinuxGeocachingRecordStore;
using Lease = platform::linux_runtime::LinuxGeocachingVolumeLease;
using Port = platform::linux_runtime::LinuxGeocachingGpxFilePort;
namespace ui_gc = ui::geocaching;
namespace gc = geocaching;
using namespace std::chrono_literals;
template <class F>
void eventually(F predicate)
{
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!predicate() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(2ms);
    assert(predicate());
}
ui_gc::Snapshot snapshot(Source& source)
{
    ui_gc::Snapshot out;
    source.snapshot(ui_gc::Section::Downloaded, out);
    return out;
}
int main(int argc, char** argv)
{
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    (void)argc;
    (void)argv;
    return 77;
#else
    assert(argc == 2);
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-offline-source-" + std::to_string(getpid()));
    const auto storage = root / "volume";
    const auto database = root / "drafts.sqlite3";
    std::filesystem::create_directories(storage);
    {
        Source source(database, storage);
        eventually([&]
                   { return snapshot(source).ready; });
        assert(snapshot(source).count == 0);
    }
    const auto cache = storage / "trailmate/geocaching";
    const auto metadata = cache / ".state/native-cache.sqlite3";
    linux_geocaching_test::FixtureSigner fixture(argv[1]);
    const auto one = fixture.make(1), two = fixture.make(2, &one.hash);
    {
        Lease lease(cache);
        Journal::Volume volume{};
        assert(lease.acquire(volume) == Lease::Result::Ready);
        Records records(metadata, fixture.crypto);
        Journal journal(metadata, volume, fixture.crypto);
        assert(records.open() == Records::Result::Ready && journal.open() == Journal::Result::Ready);
        const auto reserve = [&](uint8_t value, const linux_geocaching_test::SignedVersion& record)
        {
            assert(records.accept(record.id, record.hash, record.view()) == Records::Result::Ready);
            Journal::TaskId task{};
            task[0] = value;
            uint64_t generation = 0;
            assert(journal.begin(task, record.id, record.hash, generation) == Journal::Result::Ready);
            return std::make_pair(task, gc::InstallIdentity{record.id, record.hash, generation});
        };
        const auto first = reserve(1, one);
        Port first_port(first.first, lease, journal, records, fixture.crypto);
        assert(first_port.stage(first.second) == gc::InstallEffectResult::Complete);
        gc::GpxInstall install(first.second);
        for (unsigned i = 0; i < 20 && install.step() < gc::InstallStep::Complete; ++i) install.advance(first_port);
        assert(install.installed());
        auto cleanup = gc::InstallEffectResult::Pending;
        for (unsigned i = 0; i < 10 && cleanup == gc::InstallEffectResult::Pending; ++i) cleanup = first_port.cleanup(first.second);
        assert(cleanup == gc::InstallEffectResult::Complete);
        const auto second = reserve(2, two);
        Port second_port(second.first, lease, journal, records, fixture.crypto);
        assert(second_port.stage(second.second) == gc::InstallEffectResult::Complete);
        gc::GpxInstall pending(second.second);
        for (unsigned i = 0; i < 10 && pending.step() < gc::InstallStep::ReplaceFile; ++i) pending.advance(second_port);
        assert(pending.step() == gc::InstallStep::ReplaceFile);
        // Leave a durable prepared install for the real Source startup worker.
        Journal::Entry entry;
        assert(journal.read(second.first, entry) == Journal::Result::Ready);
        assert(entry.has_record && entry.phase == gc::storage::InstallPhase::Prepared);
    }
    ui_gc::Item item;
    {
        Source source(database, storage);
        eventually([&]
                   { return snapshot(source).ready && snapshot(source).count == 1; });
        auto view = snapshot(source);
        assert(source.item(ui_gc::Section::Downloaded, 0, view.generation, item));
        assert(item.downloaded && item.revision_hash == two.hash.bytes && item.has_coordinates);
        auto map = std::make_unique<ui_gc::LocalMapOverlay>();
        eventually([&]
                   { map->update(source, 0, 0, 10); return map->finished() && map->markerCount() == 1; });
        uint64_t map_before = 0, map_after = 0;
        assert(source.localMapRevision(map_before));
        source.refresh(ui_gc::Section::Downloaded);
        eventually([&]
                   { return snapshot(source).ready; });
        assert(source.localMapRevision(map_after) && map_before == map_after);
        view = snapshot(source);
        source.open(item, view.generation);
        ui_gc::DetailStatus status = ui_gc::DetailStatus::Pending;
        const auto sink = [](const ui_gc::DetailView& detail, void* context)
        { *static_cast<ui_gc::DetailStatus*>(context) = detail.status; };
        eventually([&]
                   { return source.readDetail(item.id, item.revision_hash, sink, &status) && status == ui_gc::DetailStatus::Ready; });
        std::filesystem::path target;
        for (const auto& path : std::filesystem::directory_iterator(cache / "caches")) target = path.path();
        assert(!target.empty());
        std::ofstream(target, std::ios::trunc) << "corrupt offline copy";
        source.refresh(ui_gc::Section::Downloaded);
        eventually([&]
                   {
            view = snapshot(source);
            return view.ready && source.item(ui_gc::Section::Downloaded, 0, view.generation, item) && !item.downloaded; });
        assert(source.readDetail(item.id, item.revision_hash, sink, &status) && status == ui_gc::DetailStatus::Failed);
        assert(source.localMapRevision(map_after) && map_before != map_after);
        eventually([&]
                   { map->update(source, 0, 0, 10); return map->finished() && map->markerCount() == 0; });
        source.closeDetail();
        assert(!source.readDetail(item.id, item.revision_hash, sink, &status));
    }
    {
        // Missing backing mounts must never be silently created by recovery.
        Source source(database, root / "missing-volume");
        std::this_thread::sleep_for(100ms);
        assert(!snapshot(source).ready && !std::filesystem::exists(root / "missing-volume"));
    }
    {
        // Lost metadata on an existing volume is retained for review, not reset.
        std::filesystem::rename(metadata, metadata.string() + ".retained");
        Source source(database, storage);
        std::this_thread::sleep_for(100ms);
        assert(!snapshot(source).ready && !std::filesystem::exists(metadata));
    }
    std::filesystem::remove_all(root);
    return 0;
#endif
}
