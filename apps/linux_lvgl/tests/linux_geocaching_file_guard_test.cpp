#include "geocaching_signed_fixture.h"
#include "platform/linux/geocaching_install_journal.h"
#include "platform/linux/geocaching_record_store.h"
#include "platform/linux/geocaching_volume_lease.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unistd.h>

using Journal = platform::linux_runtime::LinuxGeocachingInstallJournal;
using Lease = platform::linux_runtime::LinuxGeocachingVolumeLease;
using Records = platform::linux_runtime::LinuxGeocachingRecordStore;
using Result = Journal::Result;
using Mode = Journal::FileEffectMode;
using namespace std::chrono_literals;
struct BlockingEffect
{
    Lease& lease;
    Lease::Volume volume;
    std::mutex mutex;
    std::condition_variable wake;
    bool entered = false, release = false;
    static Result execute(void* pointer, const Journal::Entry& entry, int root)
    {
        auto& effect = *static_cast<BlockingEffect*>(pointer);
        assert(entry.active && entry.continue_intent && entry.has_record);
        assert(effect.lease.check(effect.volume) == Lease::Result::Ready);
        const int fd = ::openat(root, "guarded.marker", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        assert(fd >= 0 && ::write(fd, "ok", 2) == 2 && ::fsync(fd) == 0 && ::close(fd) == 0);
        std::unique_lock<std::mutex> lock(effect.mutex);
        effect.entered = true;
        effect.wake.notify_all();
        effect.wake.wait(lock, [&]
                         { return effect.release; });
        return Result::Ready;
    }
};
int main(int argc, char** argv)
{
#if !defined(__linux__) || !defined(TRAIL_MATE_HAS_OPENSSL)
    (void)argc;
    (void)argv;
    return 77;
#else
    assert(argc == 2);
    linux_geocaching_test::FixtureSigner fixture(argv[1]);
    const auto signed_record = fixture.make(1);
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-file-guard-" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "cache");
    const auto database = root / "metadata.sqlite3";
    Lease lease(root / "cache");
    Lease::Volume volume{};
    volume[0] = 1;
    assert(lease.initialize(volume) == Lease::Result::Ready);
    Lease::Volume actual{};
    assert(lease.acquire(actual) == Lease::Result::Ready);
    Records records(database, fixture.crypto);
    assert(records.open() == Records::Result::Ready && records.accept(signed_record.id, signed_record.hash, signed_record.view()) == Records::Result::Ready);
    Journal journal(database, volume, fixture.crypto), peer(database, volume, fixture.crypto), observer(database, volume, fixture.crypto);
    assert(journal.open() == Result::Ready && peer.open() == Result::Ready && observer.open() == Result::Ready);
    Journal::TaskId task{};
    task[0] = 1;
    uint64_t generation = 0;
    assert(journal.begin(task, signed_record.id, signed_record.hash, generation) == Result::Ready);
    Journal::Hash metadata_hash{};
    metadata_hash.fill(0x33); // Marker operation, not an installed GPX proof.
    assert(journal.prepare(task, generation, metadata_hash, {}) == Result::Ready);
    unsigned calls = 0;
    const auto count = [](void* pointer, const Journal::Entry&, int)
    { ++*static_cast<unsigned*>(pointer); return Result::Ready; };
    Lease unheld(root / "cache");
    assert(journal.withFileEffect(task, generation, Mode::Install, unheld, count, &calls) == Result::Unavailable && !calls);
    std::filesystem::create_directory(root / "other-cache");
    Lease wrong_volume(root / "other-cache");
    auto other_id = volume;
    other_id[0] = 2;
    assert(wrong_volume.initialize(other_id) == Lease::Result::Ready && wrong_volume.acquire(actual) == Lease::Result::Ready);
    assert(journal.withFileEffect(task, generation, Mode::Install, wrong_volume, count, &calls) == Result::Stale && !calls);
    BlockingEffect effect{lease, volume};
    Result operation = Result::Error, cancellation = Result::Error;
    std::thread writer([&]
                       { operation = journal.withFileEffect(task, generation, Mode::Install, lease, BlockingEffect::execute, &effect); });
    {
        std::unique_lock<std::mutex> lock(effect.mutex);
        const bool entered = effect.wake.wait_for(lock, 2s, [&]
                                                  { return effect.entered; });
        if (!entered)
        {
            effect.release = true;
            effect.wake.notify_all();
            lock.unlock();
            writer.join();
            assert(entered);
        }
    }
    std::atomic<bool> started{false}, finished{false};
    std::thread cancel([&]
                       { started = true; cancellation = peer.cancel(task, generation); finished = true; });
    while (!started) std::this_thread::yield();
    std::this_thread::sleep_for(100ms);
    const bool blocked = !finished;
    Journal::Entry snapshot;
    assert(observer.read(task, snapshot) == Result::Ready && snapshot.continue_intent);
    {
        std::lock_guard<std::mutex> lock(effect.mutex);
        effect.release = true;
        effect.wake.notify_all();
    }
    writer.join();
    cancel.join();
    assert(blocked && operation == Result::Ready && cancellation == Result::Ready);
    assert(std::filesystem::file_size(root / "cache/guarded.marker") == 2);
    assert(journal.withFileEffect(task, generation, Mode::Install, lease, count, &calls) == Result::Stale && !calls);
    assert(journal.withFileEffect(task, generation + 1, Mode::Rollback, lease, count, &calls) == Result::Stale && !calls);
    assert(journal.withFileEffect(task, generation, Mode::Cleanup, lease, count, &calls) == Result::Conflict && !calls);
    assert(journal.withFileEffect(task, generation, Mode::Rollback, lease, count, &calls) == Result::Ready && calls == 1);
    const auto failure = [](void*, const Journal::Entry&, int)
    { return Result::Error; };
    assert(journal.withFileEffect(task, generation, Mode::Rollback, lease, failure, nullptr) == Result::Error);
    const auto exception = [](void*, const Journal::Entry&, int) -> Result
    { throw 1; };
    assert(journal.withFileEffect(task, generation, Mode::Rollback, lease, exception, nullptr) == Result::Error);
    struct Swap
    {
        std::filesystem::path current, moved;
    } swap{root / "cache", root / "moved-cache"};
    const auto replace_root = [](void* pointer, const Journal::Entry&, int)
    {
        auto& swap = *static_cast<Swap*>(pointer);
        std::filesystem::rename(swap.current, swap.moved);
        std::filesystem::create_directory(swap.current);
        return Result::Ready;
    };
    assert(journal.withFileEffect(task, generation, Mode::Rollback, lease, replace_root, &swap) == Result::Stale);
    std::filesystem::remove(swap.current);
    std::filesystem::rename(swap.moved, swap.current);
    assert(lease.check(volume) == Lease::Result::Ready);
    assert(journal.read(task, snapshot) == Result::Ready && snapshot.phase == geocaching::storage::InstallPhase::Prepared && !snapshot.continue_intent);
    assert(journal.rolledBack(task, generation, {}) == Result::Ready);
    assert(journal.withFileEffect(task, generation, Mode::Rollback, lease, count, &calls) == Result::Conflict && calls == 1);
    Journal::TaskId installed_task{};
    installed_task[0] = 2;
    assert(journal.begin(installed_task, signed_record.id, signed_record.hash, generation) == Result::Ready);
    assert(journal.prepare(installed_task, generation, metadata_hash, {}) == Result::Ready);
    assert(journal.commit(installed_task, generation, metadata_hash) == Result::Ready);
    const auto newer = fixture.make(2, &signed_record.hash);
    assert(records.accept(newer.id, newer.hash, newer.view()) == Records::Result::Ready);
    assert(journal.withFileEffect(installed_task, generation, Mode::Install, lease, count, &calls) == Result::Stale);
    assert(journal.withFileEffect(installed_task, generation, Mode::Rollback, lease, count, &calls) == Result::Conflict);
    assert(journal.withFileEffect(installed_task, generation, Mode::Cleanup, lease, count, &calls) == Result::Ready && calls == 2);
    assert(journal.finishCleanup(installed_task, generation, metadata_hash) == Result::Ready);
    assert(journal.withFileEffect(installed_task, generation, Mode::Cleanup, lease, count, &calls) == Result::Conflict && calls == 2);
    std::filesystem::remove_all(root);
#endif
}
