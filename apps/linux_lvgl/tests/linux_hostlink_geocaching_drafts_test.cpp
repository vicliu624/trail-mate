#include "platform/linux/geocaching_draft_store.h"
#include "platform/linux/geocaching_source.h"
#include "platform/linux/hostlink_geocaching_drafts.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "signed_route_package_fixture.h"
#include <chrono>
#include <sqlite3.h>
#include <thread>
#include <unistd.h>

namespace native = platform::linux_runtime;
namespace mgmt = hostlink::management;
using Store = native::LinuxGeocachingDraftStore;
using package_fixture::Bytes;
using package_fixture::encode;
using package_fixture::Writer;
Bytes editor(const Store::Id& id, uint64_t generation, const char* name = "Web draft")
{
    geocaching::storage::DraftView row;
    const std::string description(900, 'x');
    row.generation = generation;
    row.name = name;
    row.description = description;
    row.hint = "Hint";
    row.has_coordinates = true;
    row.latitude_e7 = 10000000;
    row.longitude_e7 = 20000000;
    Bytes result(32768);
    size_t bytes = 0;
    assert(geocaching::storage::encodeDraft({id.data(), 16}, row, result.data(), result.size(), bytes));
    result.resize(bytes);
    return result;
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string scenario(argv[1]);
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-web-drafts-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    {
        const auto database = root / "state.sqlite";
        native::LinuxReticulumIdentity identity(database);
        assert(identity.begin());
        auto frontend = std::make_unique<native::HostlinkGeocachingDrafts>(database);
        Store store(database);
        assert(store.open() == Store::Result::Ready);
        Store::Id id{}, transfer{}, session{}, other{};
        id[0] = 1;
        transfer[0] = 2;
        session[0] = 3;
        other[0] = 4;
        auto raw = editor(id, 1);
        auto hash = package_fixture::hash(raw);
        uint64_t now = 1;
        const auto call = [&](mgmt::Operation operation, const Bytes& body, mgmt::Status expected = mgmt::Status::Ok, const uint8_t* owner = nullptr)
        {
            const mgmt::EnvelopeView request{owner ? owner : session.data(), nullptr, uint16_t(operation), body.data(), body.size()};
            std::array<uint8_t, 448> output{};
            size_t written = 0;
            assert(frontend->process(request, ++now, output.data(), output.size(), written) == expected);
            return Bytes(output.begin(), output.begin() + written);
        };
        std::unique_ptr<native::LinuxGeocachingSource> source;
        if (scenario == "ui")
        {
            source = std::make_unique<native::LinuxGeocachingSource>(database);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            for (;;)
            {
                ui::geocaching::Snapshot snapshot;
                source->snapshot(ui::geocaching::Section::Published, snapshot);
                if (snapshot.ready) break;
                assert(std::chrono::steady_clock::now() < deadline);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        const auto begin = encode([&](Writer& w)
                                  { w.array(4); w.binary({transfer.data(), 16}); w.unsignedInteger(2); w.unsignedInteger(raw.size()); w.binary({hash.data(), 32}); });
        const auto one = encode([&](Writer& w)
                                { w.array(1); w.binary({transfer.data(), 16}); });
        call(mgmt::Operation::TransferBegin, begin);
        assert(frontend->identityBusy());
        for (size_t offset = 0; offset < raw.size(); offset += 320)
        {
            call(mgmt::Operation::TransferChunk, encode([&](Writer& w)
                                                        { w.array(3); w.binary({transfer.data(), 16}); w.unsignedInteger(offset); w.binary({raw.data() + offset, std::min<size_t>(320, raw.size() - offset)}); }));
            if (offset == 0 && scenario == "resume")
            {
                frontend.reset();
                frontend = std::make_unique<native::HostlinkGeocachingDrafts>(database);
                const auto resumed = call(mgmt::Operation::TransferBegin, begin);
                geocaching::protocol::CmpReader reader({resumed.data(), resumed.size()});
                size_t fields = 0;
                uint64_t value = 0;
                geocaching::ByteView key;
                assert(reader.array(fields, 4) && reader.binary(key, 16) && reader.unsignedInteger(value) && value == 320 && reader.unsignedInteger(value) && value == 320);
            }
        }
        call(mgmt::Operation::TransferFinish, one);
        const auto put = [&](uint32_t generation = 1)
        { return encode([&](Writer& w)
                        { w.array(4); w.binary({transfer.data(), 16}); w.binary({id.data(), 16}); w.unsignedInteger(0); w.unsignedInteger(generation); }); };
        if (scenario == "session") call(mgmt::Operation::CacheDraftPut, put(), mgmt::Status::Unauthorized, other.data());
        if (scenario == "stale") call(mgmt::Operation::CacheDraftPut, put(2), mgmt::Status::StaleIdentity);
        sqlite3* injection = nullptr;
        assert(sqlite3_open(database.c_str(), &injection) == SQLITE_OK);
        if (scenario == "rollback")
        {
            assert(sqlite3_exec(injection, "CREATE TRIGGER reject_web_receipt BEFORE INSERT ON linux_geocaching_web_draft_receipts BEGIN SELECT RAISE(ABORT,'injected'); END", nullptr, nullptr, nullptr) == SQLITE_OK);
            call(mgmt::Operation::CacheDraftPut, put(), mgmt::Status::StorageUnavailable);
            Store::Row missing;
            assert(store.read(id, missing) == Store::Result::Missing);
            assert(sqlite3_exec(injection, "DROP TRIGGER reject_web_receipt", nullptr, nullptr, nullptr) == SQLITE_OK);
        }
        const auto saved = call(mgmt::Operation::CacheDraftPut, put());
        assert(!saved.empty() && !frontend->identityBusy());
        uint64_t stamp = 0;
        assert(store.catalogGeneration(stamp) == Store::Result::Ready);
        assert(call(mgmt::Operation::CacheDraftPut, put()) == saved);
        uint64_t repeated = 0;
        assert(store.catalogGeneration(repeated) == Store::Result::Ready && repeated == stamp);
        if (scenario == "restart" || scenario == "receipt")
        {
            if (scenario == "receipt") assert(sqlite3_exec(injection, "UPDATE linux_geocaching_draft_upload SET phase=1,draft=zeroblob(16),expected=zeroblob(8)", nullptr, nullptr, nullptr) == SQLITE_OK);
            frontend.reset();
            frontend = std::make_unique<native::HostlinkGeocachingDrafts>(database);
            call(mgmt::Operation::TransferBegin, begin);
            assert(call(mgmt::Operation::CacheDraftPut, put()) == saved);
            assert(store.catalogGeneration(repeated) == Store::Result::Ready && repeated == stamp);
        }
        Bytes copied;
        for (size_t offset = 0; offset < raw.size(); offset += 320)
        {
            const auto response = call(mgmt::Operation::CacheDraftGet, encode([&](Writer& w)
                                                                              { w.array(5); w.binary({id.data(), 16}); w.unsignedInteger(offset); w.unsignedInteger(320); w.unsignedInteger(offset ? 1 : 0); w.unsignedInteger(1); }));
            geocaching::protocol::CmpReader reader({response.data(), response.size()});
            size_t fields = 0;
            uint64_t value = 0;
            geocaching::ByteView key, bytes;
            assert(reader.array(fields, 5) && reader.binary(key, 16) && reader.unsignedInteger(value) && value == 1 && reader.unsignedInteger(value) && value == raw.size() && reader.unsignedInteger(value) && value == offset && reader.binary(bytes, 320) && reader.finished());
            copied.insert(copied.end(), bytes.data, bytes.data + bytes.size);
        }
        assert(copied == raw);
        if (scenario == "catalog" || scenario == "ui" || scenario == "changed")
        {
            const auto next = editor(id, 2, "External edit");
            if (source)
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                for (;;)
                {
                    ui::geocaching::Snapshot snapshot;
                    source->snapshot(ui::geocaching::Section::Published, snapshot);
                    if (snapshot.ready && snapshot.count == 1) break;
                    assert(std::chrono::steady_clock::now() < deadline);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            assert(store.save(id, 1, {next.data(), next.size()}) == Store::Result::Ready);
            call(mgmt::Operation::CacheListOwned, encode([&](Writer& w)
                                                         { w.array(3); w.unsignedInteger(0); w.unsignedInteger(stamp); w.unsignedInteger(1); }),
                 mgmt::Status::Conflict);
            call(mgmt::Operation::CacheDraftGet, encode([&](Writer& w)
                                                        { w.array(5); w.binary({id.data(), 16}); w.unsignedInteger(320); w.unsignedInteger(320); w.unsignedInteger(1); w.unsignedInteger(1); }),
                 mgmt::Status::Conflict);
            call(mgmt::Operation::CacheDraftPut, put(), mgmt::Status::Conflict);
            if (source)
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                for (;;)
                {
                    ui::geocaching::Snapshot snapshot;
                    source->snapshot(ui::geocaching::Section::Published, snapshot);
                    ui::geocaching::Item item;
                    if (snapshot.ready && source->item(ui::geocaching::Section::Published, 0, snapshot.generation, item) && item.edit_generation == 2 && std::string(item.name.data()) == "External edit") break;
                    assert(std::chrono::steady_clock::now() < deadline);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }
        sqlite3_close(injection);
    }
    std::filesystem::remove_all(root);
}
