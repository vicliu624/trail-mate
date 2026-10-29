#include "platform/linux/geocaching_source.h"
#include "ui_presentation/geocaching/local_map_overlay.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
#include <unistd.h>
#include <vector>

using Source = platform::linux_runtime::LinuxGeocachingSource;
namespace gc = ui::geocaching;
using namespace std::chrono_literals;
template <class F>
void eventually(F predicate)
{
    const auto end = std::chrono::steady_clock::now() + 8s;
    while (!predicate())
    {
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(2ms);
    }
}
gc::Snapshot published(Source& source)
{
    gc::Snapshot out;
    source.snapshot(gc::Section::Published, out);
    return out;
}
void save(Source& source, gc::DraftInput& draft)
{
    eventually([&]
               { return published(source).ready && published(source).can_create; });
    const auto expected = draft.generation;
    assert(source.saveDraft(draft));
    eventually([&]
               { return source.draftSaveStatus(draft.id, expected) != gc::DraftSaveStatus::Pending; });
    assert(source.draftSaveStatus(draft.id, expected) == gc::DraftSaveStatus::Saved);
    ++draft.generation;
    eventually([&]
               { return published(source).ready; });
}
bool contains(const ui::map::MapOverlaySnapshot& view, const char* name)
{
    for (size_t index = 0; index < view.item_count; ++index)
        if (std::string_view(view.items[index].label.c_str()) == name) return true;
    return false;
}
int main()
{
    char temporary[] = "/tmp/trailmate-geocaching-map-XXXXXX";
    assert(mkdtemp(temporary));
    const auto root = std::filesystem::path(temporary);
    const auto database = root / "state.sqlite3";
    {
        Source source(database);
        auto overlay = std::make_unique<gc::LocalMapOverlay>();
        auto view = std::make_unique<ui::map::MapOverlaySnapshot>();
        // Local database drafts work without a configured removable volume.
        eventually([&]
                   { return published(source).ready; });
        gc::Snapshot browse, map;
        source.snapshot(gc::Section::Downloaded, browse);
        source.mapSnapshot(gc::Section::Downloaded, map);
        assert(!browse.ready && map.ready && map.count == 0);
        std::vector<gc::DraftInput> drafts;
        for (unsigned index = 0; index < 8; ++index)
        {
            gc::DraftInput draft;
            draft.name = index == 6 ? "Archived" : index == 7 ? "No location"
                                                              : "Local cache";
            draft.description = "Stored in the application database";
            draft.latitude_e7 = static_cast<int32_t>(index) * 1000000;
            draft.longitude_e7 = static_cast<int32_t>(index) * 1000000;
            draft.has_coordinates = index != 7;
            draft.state = index == 6 ? 2 : 0;
            save(source, draft);
            drafts.push_back(draft);
        }
        eventually([&]
                   { overlay->update(source, 0, 0, 10); return overlay->finished() && overlay->markerCount() == 6; });
        overlay->append(*view);
        assert(view->header.valid && view->item_count == 6 && !contains(*view, "Archived") && !contains(*view, "No location"));
        assert(view->items[0].point.valid && view->items[0].point.lat == 0 && view->items[0].point.lon == 0);
        // Paging through a catalog must not change its local data revision or
        // continually restart a stationary map scan.
        uint64_t before = 0, after = 0;
        assert(source.localMapRevision(before));
        for (unsigned index = 0; index < 8; ++index)
        {
            source.requestMapWindow(gc::Section::Published, index % 2 ? 0 : 4, 4);
            eventually([&]
                       { return published(source).ready; });
        }
        assert(source.localMapRevision(after) && after == before);
        for (unsigned index = 0; index < 20; ++index) overlay->update(source, 0, 0, 10);
        assert(source.localMapRevision(after) && after == before && overlay->finished());

        drafts[0].state = 2;
        save(source, drafts[0]);
        eventually([&]
                   { overlay->update(source, 0, 0, 10); return overlay->finished() && overlay->markerCount() == 5; });
        assert(source.localMapRevision(after) && after != before);
        drafts[0].state = 1; // Temporarily unavailable remains a local marker.
        drafts[0].name = "Moved cache";
        drafts[0].latitude_e7 = 100000000;
        save(source, drafts[0]);
        eventually([&]
                   {
            overlay->update(source, 0, 0, 10);
            view = std::make_unique<ui::map::MapOverlaySnapshot>();
            overlay->append(*view);
            return overlay->finished() && view->item_count == 6 && contains(*view, "Moved cache"); });
        bool moved = false;
        for (size_t index = 0; index < view->item_count; ++index)
            if (std::string_view(view->items[index].label.c_str()) == "Moved cache") moved = view->items[index].point.lat == 10;
        assert(moved);
        const auto removed = drafts[0];
        assert(source.deleteDraft(removed.id, removed.generation));
        eventually([&]
                   { return source.draftSaveStatus(removed.id, removed.generation) != gc::DraftSaveStatus::Pending; });
        assert(source.draftSaveStatus(removed.id, removed.generation) == gc::DraftSaveStatus::Saved);
        eventually([&]
                   { overlay->update(source, 0, 0, 10); return overlay->finished() && overlay->markerCount() == 5; });
        view = std::make_unique<ui::map::MapOverlaySnapshot>();
        overlay->append(*view);
        assert(!contains(*view, "Moved cache"));
        assert(source.localMapRevision(before));
        // A rejected stale edit is not a local map mutation.
        eventually([&]
                   { return published(source).ready && published(source).can_create; });
        auto stale = drafts[1];
        stale.generation = 0;
        assert(source.saveDraft(stale));
        eventually([&]
                   { return source.draftSaveStatus(stale.id, 0) != gc::DraftSaveStatus::Pending; });
        assert(source.draftSaveStatus(stale.id, 0) == gc::DraftSaveStatus::Failed);
        eventually([&]
                   { return published(source).ready; });
        assert(source.localMapRevision(after) && after == before);
    }
    {
        // A missing offline mount retains its browse error while local drafts
        // continue to populate Map, including after reopening the application.
        Source source(database, root / "missing-volume");
        auto overlay = std::make_unique<gc::LocalMapOverlay>();
        eventually([&]
                   { overlay->update(source, 0, 0, 10); return overlay->finished() && overlay->markerCount() == 5; });
        gc::Snapshot browse;
        source.snapshot(gc::Section::Downloaded, browse);
        assert(!browse.ready && !std::filesystem::exists(root / "missing-volume"));
    }
    std::filesystem::remove_all(root);
}
