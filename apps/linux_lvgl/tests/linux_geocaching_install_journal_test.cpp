#include "geocaching_signed_fixture.h"
#include "platform/linux/geocaching_install_journal.h"
#include "platform/linux/geocaching_record_store.h"
#include <cassert>
#include <filesystem>
#include <sqlite3.h>
#include <thread>
#include <unistd.h>

using Journal = platform::linux_runtime::LinuxGeocachingInstallJournal;
using Records = platform::linux_runtime::LinuxGeocachingRecordStore;
using Result = Journal::Result;
using Phase = geocaching::storage::InstallPhase;
void sql(const std::filesystem::path& path, const char* command)
{
    sqlite3* db = nullptr;
    assert(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, command, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}
Journal::TaskId task(uint8_t value)
{
    Journal::TaskId out{};
    out[0] = value;
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
    linux_geocaching_test::FixtureSigner fixture(argv[1]);
    const auto one = fixture.make(1), two = fixture.make(2, &one.hash), three = fixture.make(3, &two.hash);
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-install-journal-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    const auto path = root / "journal.sqlite3";
    Journal::Volume volume{};
    volume[0] = 0x11;
    Journal::Hash file_one{}, file_two{}, file_three{}, wrong{};
    file_one.fill(0x41);
    file_two.fill(0x42);
    file_three.fill(0x43);
    wrong.fill(0x99); // Metadata fixtures; not filesystem installation proofs.
    Journal::Entry entry;
    uint64_t generation = 0;
    Records records(path, fixture.crypto);
    assert(records.open() == Records::Result::Ready);
    {
        Journal journal(path, volume, fixture.crypto);
        assert(journal.open() == Result::Ready);
        assert(journal.begin({}, one.id, one.hash, generation) == Result::Invalid);
        // Reserve/cancel must work before a Get response has arrived.
        assert(journal.begin(task(1), one.id, one.hash, generation) == Result::Ready && generation == 1);
        assert(journal.read(task(1), entry) == Result::Ready && entry.active && entry.continue_intent && !entry.has_record && !entry.has_current);
        assert(journal.prepare(task(1), 1, file_one, {}) == Result::Missing);
        assert(records.accept(one.id, one.hash, one.view()) == Records::Result::Ready);
        assert(journal.prepare(task(1), 1, file_one, {}) == Result::Ready);
        assert(journal.prepare(task(1), 1, file_one, {}) == Result::Ready);
        assert(journal.prepare(task(1), 1, wrong, {}) == Result::Conflict);
        assert(journal.nextRecovery(nullptr, entry) == Result::Ready && entry.task == task(1) && entry.phase == Phase::Prepared);
        assert(journal.cancel(task(1), 1) == Result::Ready);
        assert(journal.commit(task(1), 1, file_one) == Result::Stale);
        assert(journal.begin(task(2), one.id, one.hash, generation) == Result::Busy);
    }
    {
        Journal journal(path, volume, fixture.crypto);
        assert(journal.open() == Result::Ready);
        assert(journal.nextRecovery(nullptr, entry) == Result::Ready && !entry.continue_intent && entry.phase == Phase::Prepared);
        assert(journal.rolledBack(task(1), 1, {wrong.data(), 32}) == Result::Conflict);
        assert(journal.rolledBack(task(1), 1, {}) == Result::Ready);
        assert(journal.nextRecovery(nullptr, entry) == Result::Missing);
        assert(journal.begin(task(2), one.id, one.hash, generation) == Result::Ready && generation == 2);
        assert(journal.prepare(task(1), 1, file_one, {}) == Result::Stale);
        assert(journal.prepare(task(2), 2, file_one, {}) == Result::Ready);
        sql(path, "CREATE TRIGGER fail_install_record BEFORE UPDATE ON linux_geocaching_install_tasks BEGIN SELECT RAISE(ABORT,'injected failure'); END");
        assert(journal.commit(task(2), 2, file_one) == Result::Error);
        assert(journal.read(task(2), entry) == Result::Ready && entry.phase == Phase::Prepared && !entry.has_current && !entry.highest_seen_revision);
        sql(path, "DROP TRIGGER fail_install_record");
        assert(journal.commit(task(2), 2, wrong) == Result::Conflict);
        assert(journal.commit(task(2), 2, file_one) == Result::Ready);
        assert(journal.commit(task(2), 2, file_one) == Result::Ready);
        assert(journal.read(task(2), entry) == Result::Ready && entry.phase == Phase::Installed && !entry.cleanup_complete && entry.has_current && entry.current_file_hash == file_one);
        assert(journal.begin(task(3), one.id, one.hash, generation) == Result::Busy);
    }
    {
        Journal journal(path, volume, fixture.crypto);
        assert(journal.open() == Result::Ready);
        assert(journal.nextRecovery(nullptr, entry) == Result::Ready && entry.task == task(2) && entry.phase == Phase::Installed);
        assert(journal.finishCleanup(task(2), 2, wrong) == Result::Conflict);
        assert(journal.finishCleanup(task(2), 2, file_one) == Result::Ready);
        assert(journal.nextRecovery(nullptr, entry) == Result::Missing);
        assert(journal.begin(task(3), one.id, one.hash, generation) == Result::Ready && generation == 3);
        assert(journal.cancel(task(3), 3) == Result::Ready);
        assert(journal.begin(task(3), one.id, one.hash, generation) == Result::Stale);
        assert(journal.begin(task(4), one.id, one.hash, generation) == Result::Ready && generation == 4);
        assert(records.accept(two.id, two.hash, two.view()) == Records::Result::Ready);
        assert(journal.cancel(task(4), 4) == Result::Ready);
        assert(journal.prepare(task(4), 4, file_one, {file_one.data(), 32}) == Result::Stale);
        assert(journal.begin(task(5), two.id, two.hash, generation) == Result::Ready && generation == 5);
        assert(journal.prepare(task(5), 5, file_two, {}) == Result::Conflict);
        assert(journal.prepare(task(5), 5, file_two, {file_one.data(), 32}) == Result::Ready);
        assert(records.accept(three.id, three.hash, three.view()) == Records::Result::Ready);
        assert(journal.commit(task(5), 5, file_two) == Result::Ready); // Ambient v3 observation does not cancel the active v2 intent.
        assert(journal.read(task(5), entry) == Result::Ready && entry.current_hash.bytes == two.hash.bytes && entry.highest_seen_revision == 2);
        assert(journal.finishCleanup(task(5), 5, file_two) == Result::Ready);
        assert(journal.begin(task(6), one.id, one.hash, generation) == Result::Stale); // Installed high-water mark blocks rollback.
        assert(journal.begin(task(7), three.id, three.hash, generation) == Result::Ready && generation == 6);
        assert(journal.prepare(task(7), 6, file_three, {file_two.data(), 32}) == Result::Ready);
        assert(journal.cancel(task(7), 6) == Result::Ready);
        assert(journal.commit(task(7), 6, file_three) == Result::Stale);
        assert(journal.rolledBack(task(7), 6, {file_two.data(), 32}) == Result::Ready);
        auto other_volume = volume;
        other_volume[0] = 0x22;
        Journal other(path, other_volume, fixture.crypto);
        assert(other.open() == Result::Ready);
        assert(other.begin(task(1), three.id, three.hash, generation) == Result::Ready && generation == 1);
        assert(other.prepare(task(1), 1, file_two, {}) == Result::Ready);
        assert(other.nextRecovery(nullptr, entry) == Result::Ready && entry.identity.hash.bytes == three.hash.bytes);
        assert(journal.nextRecovery(nullptr, entry) == Result::Missing);
    }
    {
        const auto failed_begin = root / "failed-begin.sqlite3";
        Journal journal(failed_begin, volume, fixture.crypto);
        assert(journal.open() == Result::Ready);
        sql(failed_begin, "CREATE TRIGGER fail_install_state BEFORE INSERT ON linux_geocaching_install_state BEGIN SELECT RAISE(ABORT,'injected failure'); END");
        assert(journal.begin(task(1), one.id, one.hash, generation) == Result::Error);
        assert(journal.read(task(1), entry) == Result::Missing);
        sql(failed_begin, "DROP TRIGGER fail_install_state");
        assert(journal.begin(task(1), one.id, one.hash, generation) == Result::Ready && generation == 1);
    }
    {
        const auto chain = root / "sequential-upgrade.sqlite3";
        Records local(chain, fixture.crypto);
        assert(local.open() == Records::Result::Ready);
        assert(local.accept(one.id, one.hash, one.view()) == Records::Result::Ready);
        assert(local.accept(two.id, two.hash, two.view()) == Records::Result::Ready);
        assert(local.accept(three.id, three.hash, three.view()) == Records::Result::Ready);
        Journal journal(chain, volume, fixture.crypto);
        assert(journal.open() == Result::Ready && journal.begin(task(1), one.id, one.hash, generation) == Result::Ready);
        assert(journal.prepare(task(1), 1, file_one, {}) == Result::Ready && journal.commit(task(1), 1, file_one) == Result::Ready);
        assert(journal.finishCleanup(task(1), 1, file_one) == Result::Ready);
        assert(journal.begin(task(2), three.id, three.hash, generation) == Result::Ready);
        assert(journal.prepare(task(2), 2, file_three, {file_one.data(), 32}) == Result::Conflict); // Core policy rejects a direct installed-v1 -> v3 jump.
        assert(journal.cancel(task(2), 2) == Result::Ready);
        assert(journal.begin(task(3), two.id, two.hash, generation) == Result::Ready);
        assert(journal.prepare(task(3), 3, file_two, {file_one.data(), 32}) == Result::Ready && journal.commit(task(3), 3, file_two) == Result::Ready);
        assert(journal.finishCleanup(task(3), 3, file_two) == Result::Ready);
        assert(journal.begin(task(4), three.id, three.hash, generation) == Result::Ready);
        assert(journal.prepare(task(4), 4, file_three, {file_two.data(), 32}) == Result::Ready && journal.commit(task(4), 4, file_three) == Result::Ready);
        assert(journal.finishCleanup(task(4), 4, file_three) == Result::Ready);
        assert(journal.read(task(4), entry) == Result::Ready && entry.highest_seen_revision == 3);
        // Simulate local-copy removal: immutable version evidence/high-water survives.
        sql(chain, "UPDATE linux_geocaching_install_state SET installed_hash=NULL,file_hash=NULL");
        assert(journal.begin(task(5), two.id, two.hash, generation) == Result::Stale);
        assert(journal.begin(task(6), three.id, three.hash, generation) == Result::Ready && generation == 5);
        assert(journal.prepare(task(6), 5, file_three, {}) == Result::Ready);
    }
    {
        const auto corrupt = root / "corrupt.sqlite3";
        Records local(corrupt, fixture.crypto);
        assert(local.open() == Records::Result::Ready && local.accept(one.id, one.hash, one.view()) == Records::Result::Ready);
        Journal journal(corrupt, volume, fixture.crypto);
        assert(journal.open() == Result::Ready && journal.begin(task(1), one.id, one.hash, generation) == Result::Ready);
        assert(journal.prepare(task(1), generation, file_one, {}) == Result::Ready);
        sql(corrupt, "UPDATE linux_geocaching_install_tasks SET record=x'00'");
        assert(journal.read(task(1), entry) == Result::Corrupt && !entry.has_record && entry.identity.generation == 0);
        assert(journal.nextRecovery(nullptr, entry) == Result::Corrupt);
        assert(journal.begin(task(2), one.id, one.hash, generation) == Result::Corrupt);
    }
    {
        const auto exhausted = root / "exhausted.sqlite3";
        Journal journal(exhausted, volume, fixture.crypto);
        assert(journal.open() == Result::Ready && journal.begin(task(1), one.id, one.hash, generation) == Result::Ready);
        sql(exhausted, "UPDATE linux_geocaching_install_state SET generation=x'ffffffffffffffff'; UPDATE linux_geocaching_install_tasks SET generation=x'ffffffffffffffff'");
        assert(journal.read(task(1), entry) == Result::Ready && entry.identity.generation == UINT64_MAX);
        assert(journal.begin(task(2), one.id, one.hash, generation) == Result::Invalid && generation == 0);
    }
    {
        const auto concurrent = root / "concurrent.sqlite3";
        const auto contender = [&](uint8_t id)
        {
            platform::linux_runtime::LinuxGeocachingCrypto crypto;
            Journal journal(concurrent, volume, crypto);
            assert(journal.open() == Result::Ready);
            uint64_t allocated = 0;
            assert(journal.begin(task(id), one.id, one.hash, allocated) == Result::Ready);
            return allocated;
        };
        uint64_t ga = 0, gb = 0;
        std::thread a([&]
                      { ga = contender(1); });
        std::thread b([&]
                      { gb = contender(2); });
        a.join();
        b.join();
        assert((ga == 1 && gb == 2) || (ga == 2 && gb == 1));
        Journal journal(concurrent, volume, fixture.crypto);
        assert(journal.open() == Result::Ready);
        assert(journal.read(task(ga == 1 ? 1 : 2), entry) == Result::Ready && !entry.active && !entry.continue_intent);
    }
    {
        const auto wide = root / "wide-counter.sqlite3";
        Records local(wide, fixture.crypto);
        assert(local.open() == Records::Result::Ready && local.accept(one.id, one.hash, one.view()) == Records::Result::Ready);
        Journal journal(wide, volume, fixture.crypto);
        assert(journal.open() == Result::Ready && journal.begin(task(1), one.id, one.hash, generation) == Result::Ready);
        sql(wide, "UPDATE linux_geocaching_install_state SET generation=x'8000000000000000'; UPDATE linux_geocaching_install_tasks SET generation=x'8000000000000000'");
        assert(journal.begin(task(2), one.id, one.hash, generation) == Result::Ready && generation == (uint64_t{1} << 63) + 1);
        assert(journal.prepare(task(2), generation, file_one, {}) == Result::Ready);
        assert(journal.read(task(2), entry) == Result::Ready && entry.identity.generation == generation && entry.has_record);
        assert(journal.commit(task(2), generation, file_one) == Result::Ready);
    }
    {
        const auto conflict = root / "external-conflict.sqlite3";
        Records local(conflict, fixture.crypto);
        assert(local.open() == Records::Result::Ready && local.accept(one.id, one.hash, one.view()) == Records::Result::Ready);
        Journal journal(conflict, volume, fixture.crypto);
        assert(journal.open() == Result::Ready && journal.begin(task(1), one.id, one.hash, generation) == Result::Ready);
        assert(journal.prepare(task(1), generation, file_one, {}) == Result::Ready);
        assert(journal.markConflict(task(1), generation) == Result::Ready);
        assert(journal.nextRecovery(nullptr, entry) == Result::Ready && entry.phase == Phase::ConflictExternal && !entry.continue_intent);
        assert(journal.begin(task(2), one.id, one.hash, generation) == Result::Busy);
    }
    Journal invalid_memory(":memory:", volume, fixture.crypto);
    assert(invalid_memory.open() == Result::Invalid);
    Journal invalid_volume(root / "invalid.sqlite3", {}, fixture.crypto);
    assert(invalid_volume.open() == Result::Invalid && !std::filesystem::exists(root / "invalid.sqlite3"));
    {
        struct Unavailable final : geocaching::protocol::RecordCrypto
        {
            bool sha256(geocaching::ByteView, uint8_t[32]) override { return false; }
            geocaching::protocol::VerificationResult verifyEd25519(geocaching::ByteView, geocaching::ByteView, geocaching::ByteView) override { return geocaching::protocol::VerificationResult::CryptoUnavailable; }
        } unavailable;
        const auto crypto_path = root / "unavailable.sqlite3";
        Records local(crypto_path, fixture.crypto);
        assert(local.open() == Records::Result::Ready && local.accept(one.id, one.hash, one.view()) == Records::Result::Ready);
        Journal enabled(crypto_path, volume, fixture.crypto);
        assert(enabled.open() == Result::Ready && enabled.begin(task(1), one.id, one.hash, generation) == Result::Ready);
        Journal disabled(crypto_path, volume, unavailable);
        assert(disabled.open() == Result::Ready);
        assert(disabled.prepare(task(1), 1, file_one, {}) == Result::Unavailable);
        assert(disabled.begin(task(2), one.id, one.hash, generation) == Result::Unavailable);
        assert(enabled.read(task(1), entry) == Result::Ready && entry.continue_intent && entry.active && !entry.has_record);
    }
    std::filesystem::remove_all(root);
#endif
}
