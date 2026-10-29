#include "platform/linux/geocaching_source.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <future>
#include <sqlite3.h>
#include <thread>
#include <unistd.h>

namespace gc = ui::geocaching;
using Source = platform::linux_runtime::LinuxGeocachingSource;
using namespace std::chrono_literals;
template <class Predicate>
void eventually(Predicate predicate)
{
    const auto end = std::chrono::steady_clock::now() + 3s;
    while (!predicate() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
    assert(predicate());
}
gc::Snapshot snapshot(Source& source)
{
    gc::Snapshot result;
    source.snapshot(gc::Section::Published, result);
    return result;
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-geocaching-source-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    const auto path = root / "state.sqlite3";
    gc::DraftInput input;
    {
        Source source(path);
        eventually([&]
                   { return snapshot(source).can_create && snapshot(source).ready; });
        input.name = "Native cache";
        input.description = "Original description";
        input.hint = "Under a rock";
        input.latitude_e7 = 310000000;
        input.longitude_e7 = 1210000000;
        input.has_coordinates = true;
        sqlite3* blocker = nullptr;
        assert(sqlite3_open(path.c_str(), &blocker) == SQLITE_OK);
        assert(sqlite3_exec(blocker, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK);
        assert(source.saveDraft(input));
        std::this_thread::sleep_for(50ms);
        auto getter = std::async(std::launch::async, [&]
                                 { return snapshot(source); });
        const bool responsive = getter.wait_for(200ms) == std::future_status::ready;
        assert(sqlite3_exec(blocker, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(blocker);
        getter.get();
        assert(responsive); // SQLite lock waits must not hold the UI mailbox.
        const auto id = input.id;
        input.name = "Caller changed borrowed text";
        eventually([&]
                   { return source.draftSaveStatus(id, 0) != gc::DraftSaveStatus::Pending; });
        assert(source.draftSaveStatus(id, 0) == gc::DraftSaveStatus::Saved);
        eventually([&]
                   { return snapshot(source).ready && snapshot(source).count == 1; });
        const auto view = snapshot(source);
        gc::Item item;
        assert(source.item(gc::Section::Published, 0, view.generation, item));
        assert(std::string(item.name.data()) == "Native cache" && item.edit_generation == 1);
        assert(!source.item(gc::Section::Published, 0, view.generation - 1, item));
        bool called = false;
        const auto read = [&](const gc::DraftInput& draft)
        {
            assert(draft.name == "Native cache" && draft.description == "Original description" && draft.generation == 1);
            called = true;
        };
        const auto sink = [](const gc::DraftInput& draft, void* context)
        { (*static_cast<decltype(read)*>(context))(draft); };
        auto mutable_read = read;
        eventually([&]
                   { return source.readDraft(id, sink, &mutable_read) != gc::DraftReadStatus::Pending; });
        assert(called);
        source.cancelDraftRead(id);
        input.name = "Edited cache";
        input.generation = 1;
        assert(source.saveDraft(input));
        eventually([&]
                   { return source.draftSaveStatus(id, 1) != gc::DraftSaveStatus::Pending; });
        assert(source.draftSaveStatus(id, 1) == gc::DraftSaveStatus::Saved);
    }
    {
        Source source(path);
        eventually([&]
                   { return snapshot(source).ready && snapshot(source).count == 1; });
        input.generation = 1;
        assert(source.saveDraft(input));
        eventually([&]
                   { return source.draftSaveStatus(input.id, 1) != gc::DraftSaveStatus::Pending; });
        assert(source.draftSaveStatus(input.id, 1) == gc::DraftSaveStatus::Failed);
        assert(source.deleteDraft(input.id, 2));
        eventually([&]
                   { return source.draftSaveStatus(input.id, 2) != gc::DraftSaveStatus::Pending; });
        assert(source.draftSaveStatus(input.id, 2) == gc::DraftSaveStatus::Saved);
        eventually([&]
                   { return snapshot(source).ready && snapshot(source).count == 0; });
        gc::Snapshot unavailable;
        source.snapshot(gc::Section::Discover, unavailable);
        assert(!unavailable.can_refresh && !unavailable.can_create && !unavailable.count);
    }
    std::filesystem::remove_all(root);
}
