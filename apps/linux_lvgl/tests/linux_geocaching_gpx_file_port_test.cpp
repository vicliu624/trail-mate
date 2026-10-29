#include "geocaching/gpx/write_record.h"
#include "geocaching_signed_fixture.h"
#include "platform/linux/geocaching_gpx_file_port.h"
#include <cassert>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sys/wait.h>
#include <unistd.h>

namespace gc = geocaching;
using Journal = platform::linux_runtime::LinuxGeocachingInstallJournal;
using Records = platform::linux_runtime::LinuxGeocachingRecordStore;
using Lease = platform::linux_runtime::LinuxGeocachingVolumeLease;
using Port = platform::linux_runtime::LinuxGeocachingGpxFilePort;
using Effect = gc::InstallEffectResult;
using Step = gc::InstallStep;
template <size_t N>
std::string hex(const std::array<uint8_t, N>& value)
{
    constexpr char alphabet[] = "0123456789abcdef";
    std::string out;
    for (auto byte : value)
    {
        out += alphabet[byte >> 4];
        out += alphabet[byte & 15];
    }
    return out;
}
Journal::TaskId task(uint8_t value)
{
    Journal::TaskId out{};
    out[0] = value;
    return out;
}
std::string contents(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    return {std::istreambuf_iterator<char>(file), {}};
}
void write(const std::filesystem::path& path, std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    assert(file.good());
}
struct StringSink final : gc::gpx::OutputSink
{
    std::string text;
    bool write(std::string_view value) override
    {
        text.append(value);
        return true;
    }
};
#if defined(TRAIL_MATE_HAS_OPENSSL)
std::string canonical(const linux_geocaching_test::SignedVersion& value, gc::protocol::RecordCrypto& crypto)
{
    std::array<uint8_t, 4200> scratch{};
    gc::protocol::VerifiedRecordView verified;
    assert(gc::protocol::verifyGeocache(value.view(), crypto, scratch.data(), scratch.size(), verified) == gc::protocol::VerificationResult::Valid);
    StringSink sink;
    assert(gc::gpx::writeGeocacheGpx(verified, crypto, sink));
    return sink.text;
}
struct Environment
{
    std::filesystem::path root, cache;
    Lease lease;
    Records records;
    Journal journal;
    static Journal::Volume volume()
    {
        Journal::Volume out{};
        out[0] = 1;
        return out;
    }
    Environment(std::filesystem::path path, gc::protocol::RecordCrypto& crypto)
        : root(std::move(path)), cache(root / "cache"), lease(cache), records(root / "metadata.sqlite3", crypto), journal(root / "metadata.sqlite3", volume(), crypto)
    {
        std::filesystem::create_directories(cache);
        const auto initialized = lease.initialize(volume());
        assert(initialized == Lease::Result::Ready || initialized == Lease::Result::Exists);
        Journal::Volume actual{};
        assert(lease.acquire(actual) == Lease::Result::Ready && actual == volume());
        assert(records.open() == Records::Result::Ready && journal.open() == Journal::Result::Ready);
    }
    gc::InstallIdentity reserve(uint8_t number, const linux_geocaching_test::SignedVersion& value)
    {
        const auto accepted = records.accept(value.id, value.hash, value.view());
        assert(accepted == Records::Result::Ready || accepted == Records::Result::Identical || accepted == Records::Result::Older);
        uint64_t generation = 0;
        assert(journal.begin(task(number), value.id, value.hash, generation) == Journal::Result::Ready);
        return {value.id, value.hash, generation};
    }
    std::filesystem::path target(const gc::GeocacheId& id) const { return cache / "caches" / (hex(id.bytes) + ".gpx"); }
    std::filesystem::path artifact(uint8_t number, const char* suffix) const { return cache / ".state/staging" / (hex(task(number)) + suffix); }
};
void drive(Port& port, const gc::InstallIdentity& identity)
{
    assert(port.stage(identity) == Effect::Complete);
    gc::GpxInstall flow(identity);
    for (unsigned count = 0; count < 20 && flow.step() < Step::Complete; ++count) flow.advance(port);
    assert(flow.installed());
}
void clean(Port& port, const gc::InstallIdentity& identity)
{
    Effect result = Effect::Pending;
    for (unsigned count = 0; count < 12 && result == Effect::Pending; ++count) result = port.cleanup(identity);
    assert(result == Effect::Complete);
}
void rollback(Port& port, const gc::InstallIdentity& identity)
{
    Effect result = Effect::Pending;
    for (unsigned count = 0; count < 12 && result == Effect::Pending; ++count) result = port.rollback(identity);
    assert(result == Effect::Complete);
}
#endif
int main(int argc, char** argv)
{
#if !defined(__linux__) || !defined(TRAIL_MATE_HAS_OPENSSL)
    (void)argc;
    (void)argv;
    return 77;
#else
    assert(argc == 2);
    linux_geocaching_test::FixtureSigner fixture(argv[1]);
    const auto one = fixture.make(1), two = fixture.make(2, &one.hash);
    const auto xml_one = canonical(one, fixture.crypto), xml_two = canonical(two, fixture.crypto);
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-gpx-install-" + std::to_string(getpid()));
    {
        Environment env(root / "normal", fixture.crypto);
        auto identity = env.reserve(1, one);
        Port first(task(1), env.lease, env.journal, env.records, fixture.crypto);
        drive(first, identity);
        clean(first, identity);
        assert(first.cleanup(identity) == Effect::Complete);
        assert(contents(env.target(one.id)) == xml_one);
        Journal::Entry entry;
        assert(env.journal.read(task(1), entry) == Journal::Result::Ready && entry.cleanup_complete && entry.highest_seen_revision == 1);
        identity = env.reserve(2, two);
        Port second(task(2), env.lease, env.journal, env.records, fixture.crypto);
        drive(second, identity);
        clean(second, identity);
        assert(contents(env.target(two.id)) == xml_two);
        assert(contents(env.cache / ".state/history" / (hex(one.hash.bytes) + ".gpx")) == xml_one);
        assert(!std::filesystem::exists(env.artifact(2, ".old.gpx")));
        assert(env.journal.read(task(2), entry) == Journal::Result::Ready && entry.cleanup_complete && entry.previous_revision.bytes == one.hash.bytes);
    }
    {
        Environment env(root / "cancel", fixture.crypto);
        auto first_id = env.reserve(1, one);
        Port first(task(1), env.lease, env.journal, env.records, fixture.crypto);
        drive(first, first_id);
        clean(first, first_id);
        const auto identity = env.reserve(2, two);
        Port second(task(2), env.lease, env.journal, env.records, fixture.crypto);
        assert(second.stage(identity) == Effect::Complete);
        gc::GpxInstall flow(identity);
        while (flow.step() < Step::ValidateInstalledFile) flow.advance(second);
        assert(contents(env.target(two.id)) == xml_two && std::filesystem::exists(env.artifact(2, ".old.gpx")));
        assert(env.journal.cancel(task(2), identity.generation) == Journal::Result::Ready);
        flow.advance(second);
        assert(flow.step() == Step::Cancelled);
        rollback(second, identity);
        assert(second.rollback(identity) == Effect::Complete);
        assert(contents(env.target(one.id)) == xml_one);
        assert(!std::filesystem::exists(env.artifact(2, ".gpx")) && !std::filesystem::exists(env.artifact(2, ".new.gpx")));
        Journal::Entry entry;
        assert(env.journal.read(task(2), entry) == Journal::Result::Ready && entry.phase == gc::storage::InstallPhase::RolledBack && entry.current_hash.bytes == one.hash.bytes);
        const auto newer_identity = env.reserve(3, two);
        assert(second.rollback(identity) == Effect::StaleIntent);
        (void)newer_identity;
    }
    {
        const auto crash_root = root / "crash";
        int pipes[2];
        assert(::pipe(pipes) == 0);
        const auto pid = ::fork();
        assert(pid >= 0);
        if (!pid)
        {
            ::close(pipes[0]);
            Environment env(crash_root, fixture.crypto);
            const auto first_id = env.reserve(1, one);
            Port first(task(1), env.lease, env.journal, env.records, fixture.crypto);
            drive(first, first_id);
            clean(first, first_id);
            const auto identity = env.reserve(2, two);
            Port second(task(2), env.lease, env.journal, env.records, fixture.crypto);
            assert(second.stage(identity) == Effect::Complete);
            gc::GpxInstall flow(identity);
            while (flow.step() < Step::ReplaceFile) flow.advance(second);
            flow.advance(second); // Old file is durably backed up; replacement is pending.
            const char ready = flow.step() == Step::ReplaceFile && !std::filesystem::exists(env.target(one.id)) && std::filesystem::exists(env.artifact(2, ".old.gpx"));
            (void)::write(pipes[1], &ready, 1);
            for (;;) ::pause();
        }
        ::close(pipes[1]);
        char ready = 0;
        const auto received = ::read(pipes[0], &ready, 1);
        ::close(pipes[0]);
        const auto killed = ::kill(pid, SIGKILL);
        int status = 0;
        const auto waited = ::waitpid(pid, &status, 0);
        assert(received == 1 && ready && killed == 0 && waited == pid && WIFSIGNALED(status));
        Environment resumed(crash_root, fixture.crypto);
        Journal::Entry entry;
        assert(resumed.journal.nextRecovery(nullptr, entry) == Journal::Result::Ready && entry.task == task(2));
        Port port(task(2), resumed.lease, resumed.journal, resumed.records, fixture.crypto);
        drive(port, entry.identity);
        clean(port, entry.identity);
        assert(contents(resumed.target(two.id)) == xml_two && contents(resumed.cache / ".state/history" / (hex(one.hash.bytes) + ".gpx")) == xml_one);
    }
    {
        Environment env(root / "foreign", fixture.crypto);
        const auto identity = env.reserve(1, one);
        write(env.target(one.id), "foreign user data");
        Port port(task(1), env.lease, env.journal, env.records, fixture.crypto);
        assert(port.stage(identity) == Effect::Failed && port.lastResult() == Journal::Result::Conflict);
        assert(contents(env.target(one.id)) == "foreign user data");
        assert(port.rollback(identity) == Effect::Complete && contents(env.target(one.id)) == "foreign user data");
        Journal::Entry entry;
        assert(env.journal.read(task(1), entry) == Journal::Result::Ready && !entry.has_record && !entry.has_current);
    }
    {
        const auto cache_root = root / "cleanup-restart";
        gc::InstallIdentity second_id;
        {
            Environment env(cache_root, fixture.crypto);
            const auto first_id = env.reserve(1, one);
            Port first(task(1), env.lease, env.journal, env.records, fixture.crypto);
            drive(first, first_id);
            clean(first, first_id);
            second_id = env.reserve(2, two);
            Port second(task(2), env.lease, env.journal, env.records, fixture.crypto);
            drive(second, second_id);
        }
        Environment reopened(cache_root, fixture.crypto);
        Journal::Entry pending;
        assert(reopened.journal.nextRecovery(nullptr, pending) == Journal::Result::Ready && pending.phase == gc::storage::InstallPhase::Installed);
        Port second(task(2), reopened.lease, reopened.journal, reopened.records, fixture.crypto);
        write(reopened.cache / ".state/history" / (hex(one.hash.bytes) + ".gpx"), xml_one);
        clean(second, second_id);
        assert(contents(reopened.cache / ".state/history" / (hex(one.hash.bytes) + ".gpx")) == xml_one);
    }
    {
        Environment env(root / "untracked-canonical", fixture.crypto);
        const auto identity = env.reserve(1, one);
        write(env.target(one.id), xml_one); // Existing canonical data is still not this task's property.
        Port port(task(1), env.lease, env.journal, env.records, fixture.crypto);
        assert(port.stage(identity) == Effect::Complete);
        gc::GpxInstall flow(identity);
        flow.advance(port);
        assert(flow.step() == Step::Failed && port.rollback(identity) == Effect::Complete);
        assert(contents(env.target(one.id)) == xml_one);
    }
    {
        Environment env(root / "history-conflict", fixture.crypto);
        const auto first_id = env.reserve(1, one);
        Port first(task(1), env.lease, env.journal, env.records, fixture.crypto);
        drive(first, first_id);
        clean(first, first_id);
        const auto second_id = env.reserve(2, two);
        Port second(task(2), env.lease, env.journal, env.records, fixture.crypto);
        drive(second, second_id);
        const auto history = env.cache / ".state/history" / (hex(one.hash.bytes) + ".gpx");
        write(history, "unrecognized history file");
        assert(second.cleanup(second_id) == Effect::Failed && second.lastResult() == Journal::Result::Conflict);
        assert(contents(history) == "unrecognized history file" && contents(env.artifact(2, ".old.gpx")) == xml_one && contents(env.target(two.id)) == xml_two);
        Journal::Entry pending;
        assert(env.journal.nextRecovery(nullptr, pending) == Journal::Result::Ready && !pending.cleanup_complete);
    }
    std::filesystem::remove_all(root);
#endif
}
