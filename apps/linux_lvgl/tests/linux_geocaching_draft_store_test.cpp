#include "platform/linux/geocaching_draft_store.h"
#include <cassert>
#include <filesystem>
#include <future>
#include <sqlite3.h>
#include <unistd.h>

using Store = platform::linux_runtime::LinuxGeocachingDraftStore;
using Result = Store::Result;
std::vector<uint8_t> encoded(const Store::Id& id, uint64_t generation, const char* name = "Cache")
{
    geocaching::storage::DraftView draft;
    draft.generation = generation;
    draft.name = name;
    draft.description = "Description\nwith UTF-8: \xe5\xb1\xb1";
    draft.hint = "Under the rock";
    draft.has_coordinates = true;
    draft.latitude_e7 = 310000000;
    draft.longitude_e7 = 1210000000;
    std::vector<uint8_t> bytes(32768);
    size_t size;
    assert(geocaching::storage::encodeDraft({id.data(), id.size()}, draft, bytes.data(), bytes.size(), size));
    bytes.resize(size);
    return bytes;
}
Result save(Store& store, Store::Id id, uint64_t expected, uint64_t next)
{
    const auto bytes = encoded(id, next);
    return store.save(id, expected, {bytes.data(), bytes.size()});
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-geocaching-draft-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    const auto path = root / "drafts.sqlite3";
    Store::Id id{};
    id[0] = 1;
    {
        Store store(path);
        assert(store.open() == Result::Ready);
        assert(save(store, id, 0, 1) == Result::Ready);
        assert(save(store, id, 0, 1) == Result::Conflict);
        assert(save(store, id, 1, 3) == Result::Invalid);
        assert(save(store, id, 1, 2) == Result::Ready);
        Store::Page page;
        assert(store.page(0, page) == Result::Ready && page.total == 1 && page.count == 1);
        assert(page.rows[0].generation == 2 && page.rows[0].latitude_e7 == 310000000);
        assert(save(store, {}, 0, 1) == Result::Invalid);
    }
    {
        Store store(path);
        assert(store.open() == Result::Ready);
        Store::Row row;
        assert(store.read(id, row) == Result::Ready);
        geocaching::storage::DraftView draft;
        assert(row.view(draft) && draft.generation == 2 && draft.hint == "Under the rock");
        // Independent connections serialize the generation check and write.
        const auto contender = [&]
        {
            Store connection(path);
            assert(connection.open() == Result::Ready);
            return save(connection, id, 2, 3);
        };
        auto first = std::async(std::launch::async, contender);
        auto second = std::async(std::launch::async, contender);
        const auto a = first.get(), b = second.get();
        assert((a == Result::Ready && b == Result::Conflict) || (b == Result::Ready && a == Result::Conflict));
        assert(store.erase(id, 2) == Result::Conflict);
        assert(store.erase(id, 3) == Result::Ready);
        assert(store.read(id, row) == Result::Missing && row.bytes.empty());
        assert(save(store, id, 0, 1) == Result::Conflict);
        assert(save(store, id, 3, 4) == Result::Conflict);
        for (uint8_t i = 2; i < 8; ++i)
        {
            id[0] = i;
            assert(save(store, id, 0, 1) == Result::Ready);
        }
        Store::Page page;
        assert(store.page(0, page) == Result::Ready && page.total == 6 && page.count == 4);
        assert(store.page(4, page) == Result::Ready && page.total == 6 && page.count == 2);
        assert(store.page(99, page) == Result::Ready && page.total == 6 && !page.count);
        sqlite3* injection = nullptr;
        assert(sqlite3_open(path.c_str(), &injection) == SQLITE_OK);
        assert(sqlite3_exec(injection, "CREATE TRIGGER fail_draft BEFORE UPDATE ON linux_geocaching_drafts BEGIN SELECT RAISE(ABORT,'injected write failure'); END", nullptr, nullptr, nullptr) == SQLITE_OK);
        assert(save(store, id, 1, 2) == Result::Error);
        assert(store.read(id, row) == Result::Ready && row.view(draft) && draft.generation == 1);
        assert(sqlite3_exec(injection, "DROP TRIGGER fail_draft", nullptr, nullptr, nullptr) == SQLITE_OK);
        assert(save(store, id, 1, 2) == Result::Ready); // Failed write rolled back its transaction.
        // A committed author/base belongs to publication state, never editor input.
        std::array<uint8_t, 64> author{};
        author[0] = 0x55;
        std::array<uint8_t, 32> base{};
        base[0] = 0x66;
        draft = {};
        draft.generation = 2;
        draft.name = "Published draft";
        draft.author = {author.data(), author.size()};
        draft.base_hash = {base.data(), base.size()};
        std::vector<uint8_t> published(32768);
        size_t published_size = 0;
        assert(geocaching::storage::encodeDraft({id.data(), id.size()}, draft, published.data(), published.size(), published_size));
        assert(store.save(id, 1, {published.data(), published_size}) == Result::Invalid);
        sqlite3_stmt* seed = nullptr;
        assert(sqlite3_prepare_v2(injection, "UPDATE linux_geocaching_drafts SET encoded=? WHERE id=?", -1, &seed, nullptr) == SQLITE_OK);
        sqlite3_bind_blob(seed, 1, published.data(), published_size, SQLITE_TRANSIENT);
        sqlite3_bind_blob(seed, 2, id.data(), id.size(), SQLITE_TRANSIENT);
        assert(sqlite3_step(seed) == SQLITE_DONE);
        sqlite3_finalize(seed);
        assert(save(store, id, 2, 3) == Result::PublicationRequired);
        assert(store.read(id, row) == Result::Ready && row.view(draft) && draft.generation == 2 && draft.base_hash.size == 32);
        sqlite3_close(injection);
        // Fail closed on corrupt committed state; do not overwrite or hide it.
        sqlite3* db = nullptr;
        assert(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
        assert(sqlite3_exec(db, "UPDATE linux_geocaching_drafts SET encoded=x'00' WHERE deleted=0", nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);
        assert(store.read(id, row) == Result::Corrupt);
        assert(save(store, id, 1, 2) == Result::Corrupt);
        assert(store.page(0, page) == Result::Corrupt && !page.count);
    }
    std::filesystem::remove_all(root);
}
