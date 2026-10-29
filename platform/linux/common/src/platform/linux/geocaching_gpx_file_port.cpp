#include "platform/linux/geocaching_gpx_file_port.h"
#include "geocaching/gpx/write_record.h"
#include "platform/linux/geocaching_gpx_stage.h"
#include <algorithm>
#include <vector>
#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace platform::linux_runtime
{
namespace gc = ::geocaching;
namespace
{
using Journal = LinuxGeocachingInstallJournal;
using Records = LinuxGeocachingRecordStore;
using Result = Journal::Result;
using Effect = gc::InstallEffectResult;
using Phase = gc::storage::InstallPhase;
template <size_t N>
std::string hex(const std::array<uint8_t, N>& bytes)
{
    constexpr char alphabet[] = "0123456789abcdef";
    std::string out;
    out.reserve(N * 2);
    for (auto byte : bytes)
    {
        out += alphabet[byte >> 4];
        out += alphabet[byte & 15];
    }
    return out;
}
struct Bytes final : gc::gpx::OutputSink
{
    std::vector<uint8_t> value;
    bool write(std::string_view text) override
    {
        if (text.size() > gc::kMaxGpxBytes - value.size()) return false;
        value.insert(value.end(), text.begin(), text.end());
        return true;
    }
};
struct Expected
{
    Records::Record record;
    Bytes gpx;
    Journal::Hash file_hash{};
};
Result map(Records::Result result)
{
    if (result == Records::Result::Ready) return Result::Ready;
    if (result == Records::Result::Missing) return Result::Missing;
    if (result == Records::Result::CryptoUnavailable) return Result::Unavailable;
    if (result == Records::Result::Conflict) return Result::Conflict;
    return result == Records::Result::Error ? Result::Error : Result::Corrupt;
}
Result map(LinuxGeocachingGpxStage::Result result)
{
    using S = LinuxGeocachingGpxStage::Result;
    if (result == S::Ready) return Result::Ready;
    if (result == S::Missing) return Result::Missing;
    if (result == S::Unavailable || result == S::CryptoUnavailable) return Result::Unavailable;
    return result == S::IoError ? Result::Error : Result::Conflict;
}
Effect effect(Result result, bool pending = false)
{
    return result == Result::Ready ? (pending ? Effect::Pending : Effect::Complete) : result == Result::Stale ? Effect::StaleIntent
                                                                                                              : Effect::Failed;
}
Result renderExpected(Records& records, gc::protocol::RecordCrypto& crypto, const gc::GeocacheId& id,
                      const gc::RevisionHash& hash, std::array<uint8_t, 4200>& scratch, Expected& out)
{
    auto result = map(records.read(id, hash, out.record));
    if (result != Result::Ready) return result;
    gc::protocol::VerifiedRecordView verified;
    const auto checked = gc::protocol::verifyGeocache({out.record.bytes.data(), out.record.bytes.size()}, crypto,
                                                      scratch.data(), scratch.size(), verified, &id, &hash);
    if (checked != gc::protocol::VerificationResult::Valid) return checked == gc::protocol::VerificationResult::CryptoUnavailable ? Result::Unavailable : Result::Corrupt;
    if (!gc::gpx::writeGeocacheGpx(verified, crypto, out.gpx) || !crypto.sha256({out.gpx.value.data(), out.gpx.value.size()}, out.file_hash.data())) return Result::Error;
    return Result::Ready;
}
#if defined(__linux__)
struct Fd
{
    int value = -1;
    Fd() = default;
    explicit Fd(int fd) : value(fd) {}
    ~Fd()
    {
        if (value >= 0) ::close(value);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};
bool same(const struct stat& a, const struct stat& b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size && a.st_nlink == b.st_nlink &&
           a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
           a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
Result error()
{
    if (errno == ENOENT) return Result::Missing;
    if (errno == EEXIST || errno == ELOOP || errno == ENOTDIR) return Result::Conflict;
    if (errno == ENOSYS || errno == EOPNOTSUPP || errno == EXDEV || errno == EINVAL) return Result::Unavailable;
    return Result::Error;
}
struct Directories
{
    int root;
    Fd state, caches, staging, history;
    explicit Directories(int fd) : root(fd) {}
    Result child(int parent, const char* name, Fd& out, dev_t device)
    {
        if (::mkdirat(parent, name, 0700) && errno != EEXIST) return error();
        out.value = ::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (out.value < 0) return error();
        struct stat info
        {
        };
        if (::fstat(out.value, &info)) return Result::Error;
        return info.st_dev == device ? Result::Ready : Result::Unavailable;
    }
    Result open()
    {
        struct stat info
        {
        };
        if (::fstat(root, &info)) return Result::Error;
        state.value = ::openat(root, ".state", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (state.value < 0) return error();
        auto result = child(root, "caches", caches, info.st_dev);
        if (result == Result::Ready) result = child(state.value, "staging", staging, info.st_dev);
        if (result == Result::Ready) result = child(state.value, "history", history, info.st_dev);
        return result == Result::Ready ? sync() : result;
    }
    Result sync()
    {
        return ::fsync(root) || ::fsync(state.value) || ::fsync(caches.value) || ::fsync(staging.value) || ::fsync(history.value) ? Result::Error : Result::Ready;
    }
};
struct Observed
{
    bool exists = false;
    struct stat info
    {
    };
    std::vector<uint8_t> bytes;
    Journal::Hash hash{};
};
Result inspect(int directory, const std::string& name, gc::protocol::RecordCrypto& crypto, Observed& out)
{
    out = {};
    Fd file(::openat(directory, name.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
    if (file.value < 0) return errno == ENOENT ? Result::Ready : error();
    if (::fstat(file.value, &out.info)) return Result::Error;
    if (!S_ISREG(out.info.st_mode) || out.info.st_nlink != 1 || out.info.st_size < 0 || out.info.st_size > static_cast<off_t>(gc::kMaxGpxBytes)) return Result::Conflict;
    out.bytes.resize(static_cast<size_t>(out.info.st_size));
    size_t offset = 0;
    while (offset < out.bytes.size())
    {
        const auto count = ::read(file.value, out.bytes.data() + offset, out.bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return Result::Error;
        offset += static_cast<size_t>(count);
    }
    uint8_t extra = 0;
    ssize_t count;
    do
    {
        count = ::read(file.value, &extra, 1);
    } while (count < 0 && errno == EINTR);
    struct stat after
    {
    };
    if (count != 0 || ::fstat(file.value, &after)) return Result::Error;
    if (!same(out.info, after)) return Result::Conflict;
    if (!crypto.sha256({out.bytes.data(), out.bytes.size()}, out.hash.data())) return Result::Unavailable;
    out.exists = true;
    return Result::Ready;
}
bool matches(const Observed& observed, const Expected& expected)
{
    return observed.exists && observed.hash == expected.file_hash && observed.bytes == expected.gpx.value;
}
Result move(int from, const std::string& source, int to, const std::string& target, const Observed& observed)
{
    struct stat current
    {
    };
    if (::fstatat(from, source.c_str(), &current, AT_SYMLINK_NOFOLLOW)) return error();
    if (!same(current, observed.info)) return Result::Conflict;
#if defined(SYS_renameat2)
    if (::syscall(SYS_renameat2, from, source.c_str(), to, target.c_str(), RENAME_NOREPLACE)) return error();
    return ::fsync(from) || ::fsync(to) ? Result::Error : Result::Ready;
#else
    (void)to;
    (void)target;
    return Result::Unavailable;
#endif
}
Result remove(int directory, const std::string& name, const Observed& observed)
{
    struct stat current
    {
    };
    if (::fstatat(directory, name.c_str(), &current, AT_SYMLINK_NOFOLLOW)) return error();
    if (!same(current, observed.info)) return Result::Conflict;
    if (::unlinkat(directory, name.c_str(), 0)) return error();
    return ::fsync(directory) ? Result::Error : Result::Ready;
}
#endif
} // namespace
struct LinuxGeocachingGpxFilePort::Impl
{
    enum class Operation
    {
        Stage,
        Prepare,
        Replace,
        ValidateTarget,
        Rollback,
        Cleanup
    };
    Impl(Journal::TaskId id, LinuxGeocachingVolumeLease& v, Journal& j, Records& r, gc::protocol::RecordCrypto& c)
        : task(id), lease(v), journal(j), records(r), crypto(c) {}
    struct Work
    {
        Work(Impl* value, const gc::InstallIdentity& id, Operation op) : owner(value), identity(id), operation(op) {}
        Impl* owner;
        gc::InstallIdentity identity;
        Operation operation;
        Expected wanted, old;
        bool has_old = false, pending = false;
        Journal::Hash observed{};
        std::string target, staged, backup, retired, retained;
    };
    Result snapshot(const gc::InstallIdentity& identity, Journal::Entry& entry)
    {
        const auto result = journal.read(task, entry);
        if (result != Result::Ready) return result;
        return entry.active && entry.identity.generation == identity.generation && entry.identity.id.bytes == identity.id.bytes &&
                       entry.identity.hash.bytes == identity.hash.bytes
                   ? Result::Ready
                   : Result::Stale;
    }
    Result expected(const gc::GeocacheId& id, const gc::RevisionHash& hash, Expected& out)
    {
        return renderExpected(records, crypto, id, hash, scratch, out);
    }
    Result load(const gc::InstallIdentity& identity, Work& work)
    {
        Journal::Entry entry;
        auto result = snapshot(identity, entry);
        if (result != Result::Ready) return result;
        result = expected(identity.id, identity.hash, work.wanted);
        if (result != Result::Ready) return result;
        if (entry.has_record && entry.new_file_hash != work.wanted.file_hash) return Result::Corrupt;
        work.has_old = entry.has_record ? entry.has_old_file : entry.has_current;
        if (work.has_old)
        {
            const auto hash = entry.has_record ? entry.previous_revision : entry.current_hash;
            result = expected(identity.id, hash, work.old);
            if (result != Result::Ready) return result;
            if (work.old.file_hash != (entry.has_record ? entry.old_file_hash : entry.current_file_hash)) return Result::Corrupt;
            work.retained = hex(hash.bytes) + ".gpx";
        }
        work.target = hex(identity.id.bytes) + ".gpx";
        work.staged = hex(task) + ".gpx";
        work.backup = hex(task) + ".old.gpx";
        work.retired = hex(task) + ".new.gpx";
        return Result::Ready;
    }
    static Result callback(void* pointer, const Journal::Entry& entry, int root)
    {
        auto& work = *static_cast<Work*>(pointer);
        auto& self = *work.owner;
        if (entry.identity.generation != work.identity.generation || entry.identity.hash.bytes != work.identity.hash.bytes ||
            (entry.has_record && entry.new_file_hash != work.wanted.file_hash)) return Result::Stale;
        const bool has_old = entry.has_record ? entry.has_old_file : entry.has_current;
        if (has_old != work.has_old || (has_old && (entry.has_record ? entry.previous_revision.bytes : entry.current_hash.bytes) != work.old.record.summary.hash)) return Result::Stale;
#if defined(__linux__)
        Directories dirs(root);
        auto result = dirs.open();
        if (result != Result::Ready) return result;
        Observed target, staged, backup, retired;
        if ((result = inspect(dirs.caches.value, work.target, self.crypto, target)) != Result::Ready ||
            (result = inspect(dirs.staging.value, work.staged, self.crypto, staged)) != Result::Ready ||
            (result = inspect(dirs.staging.value, work.backup, self.crypto, backup)) != Result::Ready ||
            (result = inspect(dirs.staging.value, work.retired, self.crypto, retired)) != Result::Ready) return result;
        if (backup.exists && (!has_old || !matches(backup, work.old))) return Result::Conflict;
        if (retired.exists && !matches(retired, work.wanted)) return Result::Conflict;
        if (staged.exists && !matches(staged, work.wanted)) return Result::Conflict;
        const bool target_new = matches(target, work.wanted), target_old = has_old && matches(target, work.old);
        if (target.exists && !target_new && !target_old) return Result::Conflict;
        if (work.operation == Operation::Stage)
        {
            if (entry.has_record && target_new && (!has_old || backup.exists || work.old.file_hash == work.wanted.file_hash)) return Result::Ready;
            if (!staged.exists)
            {
                LinuxGeocachingGpxStage staging(root, self.crypto);
                LinuxGeocachingGpxStage::Proof proof;
                result = map(staging.stage(self.task, work.identity.id, work.identity.hash,
                                           {work.wanted.record.bytes.data(), work.wanted.record.bytes.size()}, proof));
                if (result != Result::Ready) return result;
                if (proof.file_hash != work.wanted.file_hash) return Result::Corrupt;
            }
            return Result::Ready;
        }
        if (work.operation == Operation::Prepare)
        {
            if (retired.exists) return Result::Conflict;
            if (!entry.has_record)
            {
                if (backup.exists || (has_old ? !target_old : target.exists)) return Result::Conflict;
                return staged.exists ? Result::Ready : Result::Missing;
            }
            if (has_old && !target_old && !backup.exists) return Result::Conflict;
            return staged.exists || target_new ? Result::Ready : Result::Missing;
        }
        if (work.operation == Operation::Replace)
        {
            if (retired.exists) return Result::Conflict;
            if (target_new)
                return has_old && !backup.exists && work.old.file_hash != work.wanted.file_hash ? Result::Conflict : Result::Ready;
            if (target_old)
            {
                if (backup.exists) return Result::Conflict;
                result = move(dirs.caches.value, work.target, dirs.staging.value, work.backup, target);
                if (result == Result::Ready) work.pending = true;
                return result;
            }
            if (has_old && !backup.exists) return Result::Conflict;
            if (!staged.exists) return Result::Missing;
            return move(dirs.staging.value, work.staged, dirs.caches.value, work.target, staged);
        }
        if (work.operation == Operation::ValidateTarget)
        {
            if (!target_new) return Result::Conflict;
            work.observed = target.hash;
            return dirs.sync();
        }
        if (work.operation == Operation::Rollback)
        {
            if (target_new && !target_old)
            {
                if (retired.exists) return Result::Conflict;
                result = move(dirs.caches.value, work.target, dirs.staging.value, work.retired, target);
                if (result == Result::Ready) work.pending = true;
                return result;
            }
            if (has_old && !target_old)
            {
                if (!backup.exists || target.exists) return Result::Conflict;
                result = move(dirs.staging.value, work.backup, dirs.caches.value, work.target, backup);
                if (result == Result::Ready) work.pending = true;
                return result;
            }
            if (!has_old && target.exists) return Result::Conflict;
            if (backup.exists)
            {
                result = remove(dirs.staging.value, work.backup, backup);
                work.pending = result == Result::Ready;
                return result;
            }
            if (staged.exists)
            {
                result = remove(dirs.staging.value, work.staged, staged);
                work.pending = result == Result::Ready;
                return result;
            }
            if (retired.exists)
            {
                result = remove(dirs.staging.value, work.retired, retired);
                work.pending = result == Result::Ready;
                return result;
            }
            if (has_old) work.observed = target.hash;
            return dirs.sync();
        }
        if (work.operation == Operation::Cleanup)
        {
            if (!target_new) return Result::Conflict;
            if (has_old && work.old.file_hash != work.wanted.file_hash)
            {
                Observed history;
                result = inspect(dirs.history.value, work.retained, self.crypto, history);
                if (result != Result::Ready) return result;
                if (history.exists && !matches(history, work.old)) return Result::Conflict;
                if (backup.exists)
                {
                    result = history.exists ? remove(dirs.staging.value, work.backup, backup) : move(dirs.staging.value, work.backup, dirs.history.value, work.retained, backup);
                    work.pending = result == Result::Ready;
                    return result;
                }
                if (!history.exists) return Result::Conflict; // Missing backup is not proof of retained history.
            }
            else if (backup.exists)
            {
                result = remove(dirs.staging.value, work.backup, backup);
                work.pending = result == Result::Ready;
                return result;
            }
            if (staged.exists)
            {
                result = remove(dirs.staging.value, work.staged, staged);
                work.pending = result == Result::Ready;
                return result;
            }
            if (retired.exists)
            {
                result = remove(dirs.staging.value, work.retired, retired);
                work.pending = result == Result::Ready;
                return result;
            }
            work.observed = target.hash;
            return dirs.sync();
        }
        return Result::Invalid;
#else
        (void)root;
        return Result::Unavailable;
#endif
    }
    Result run(const gc::InstallIdentity& identity, Operation operation, Journal::FileEffectMode mode, Work& work)
    {
        auto result = load(identity, work);
        if (result != Result::Ready) return last = result;
        work.operation = operation;
        return last = journal.withFileEffect(task, identity.generation, mode, lease, callback, &work);
    }
    Journal::TaskId task;
    LinuxGeocachingVolumeLease& lease;
    Journal& journal;
    Records& records;
    gc::protocol::RecordCrypto& crypto;
    std::array<uint8_t, 4200> scratch{};
    Result last = Result::Ready;
};
LinuxGeocachingGpxFilePort::LinuxGeocachingGpxFilePort(Journal::TaskId task, LinuxGeocachingVolumeLease& lease, Journal& journal, Records& records, gc::protocol::RecordCrypto& crypto)
    : impl_(std::make_unique<Impl>(task, lease, journal, records, crypto)) {}
LinuxGeocachingGpxFilePort::~LinuxGeocachingGpxFilePort() = default;
Effect LinuxGeocachingGpxFilePort::stage(const gc::InstallIdentity& identity)
{
    Impl::Work work{impl_.get(), identity, Impl::Operation::Stage};
    return effect(impl_->run(identity, Impl::Operation::Stage, Journal::FileEffectMode::Stage, work));
}
Effect LinuxGeocachingGpxFilePort::execute(gc::InstallStep step, const gc::InstallIdentity& identity)
{
    Impl::Work work{impl_.get(), identity, Impl::Operation::Stage};
    using Step = gc::InstallStep;
    if (step == Step::CheckIntent || step == Step::ValidateStagedFile || step == Step::PrepareJournal)
    {
        auto result = impl_->run(identity, Impl::Operation::Prepare, Journal::FileEffectMode::Stage, work);
        if (result == Result::Ready && step == Step::PrepareJournal)
            result = impl_->last = impl_->journal.prepare(impl_->task, identity.generation, work.wanted.file_hash,
                                                          work.has_old ? gc::ByteView{work.old.file_hash.data(), 32} : gc::ByteView{});
        return effect(result);
    }
    if (step == Step::ReplaceFile)
    {
        const auto result = impl_->run(identity, Impl::Operation::Replace, Journal::FileEffectMode::Install, work);
        return effect(result, work.pending);
    }
    if (step == Step::ValidateInstalledFile || step == Step::CommitJournal)
    {
        auto result = impl_->run(identity, Impl::Operation::ValidateTarget, Journal::FileEffectMode::Install, work);
        if (result == Result::Ready && step == Step::CommitJournal) result = impl_->last = impl_->journal.commit(impl_->task, identity.generation, work.observed);
        return effect(result);
    }
    impl_->last = Result::Invalid;
    return Effect::Failed;
}
Effect LinuxGeocachingGpxFilePort::rollback(const gc::InstallIdentity& identity)
{
    Journal::Entry entry;
    auto loaded = impl_->snapshot(identity, entry);
    if (loaded != Result::Ready) return effect(impl_->last = loaded);
    if (entry.has_record && entry.phase == Phase::RolledBack) return effect(impl_->last = Result::Ready);
    auto result = impl_->journal.cancel(impl_->task, identity.generation);
    if (result != Result::Ready) return effect(impl_->last = result);
    if (!entry.has_record) return effect(impl_->last = Result::Ready); // No final-cache mutation was authorized.
    Impl::Work work{impl_.get(), identity, Impl::Operation::Rollback};
    result = impl_->run(identity, Impl::Operation::Rollback, Journal::FileEffectMode::Rollback, work);
    if (result == Result::Ready && !work.pending)
        result = impl_->last = impl_->journal.rolledBack(impl_->task, identity.generation, work.has_old ? gc::ByteView{work.observed.data(), 32} : gc::ByteView{});
    return effect(result, work.pending);
}
Effect LinuxGeocachingGpxFilePort::cleanup(const gc::InstallIdentity& identity)
{
    Journal::Entry entry;
    const auto loaded = impl_->snapshot(identity, entry);
    if (loaded != Result::Ready) return effect(impl_->last = loaded);
    if (entry.has_record && entry.phase == Phase::Installed && entry.cleanup_complete) return effect(impl_->last = Result::Ready);
    Impl::Work work{impl_.get(), identity, Impl::Operation::Cleanup};
    auto result = impl_->run(identity, Impl::Operation::Cleanup, Journal::FileEffectMode::Cleanup, work);
    if (result == Result::Ready && !work.pending) result = impl_->last = impl_->journal.finishCleanup(impl_->task, identity.generation, work.observed);
    return effect(result, work.pending);
}
Journal::Result LinuxGeocachingGpxFilePort::lastResult() const { return impl_->last; }
Result LinuxGeocachingGpxFilePort::readInstalled(const Journal::InstalledReference& reference, LinuxGeocachingVolumeLease& lease,
                                                 Records& records, gc::protocol::RecordCrypto& crypto, Records::Record& out)
{
    out = {};
    if (!reference.valid) return Result::Corrupt;
    if (lease.check(reference.volume) != LinuxGeocachingVolumeLease::Result::Ready) return Result::Unavailable;
    Expected expected;
    std::array<uint8_t, 4200> scratch{};
    auto result = renderExpected(records, crypto, reference.id, reference.hash, scratch, expected);
    if (result != Result::Ready) return result;
    if (expected.record.summary.revision != reference.highest_seen_revision || expected.file_hash != reference.file_hash) return Result::Corrupt;
#if defined(__linux__)
    const int root = lease.rootDescriptor();
    if (root < 0) return Result::Unavailable;
    Fd directory(::openat(root, "caches", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (directory.value < 0) return error();
    struct stat a
    {
    }, b{};
    if (::fstat(root, &a) || ::fstat(directory.value, &b)) return Result::Error;
    if (a.st_dev != b.st_dev) return Result::Unavailable;
    Observed observed;
    result = inspect(directory.value, hex(reference.id.bytes) + ".gpx", crypto, observed);
    if (result != Result::Ready) return result;
    if (!matches(observed, expected)) return observed.exists ? Result::Conflict : Result::Missing;
    if (lease.check(reference.volume) != LinuxGeocachingVolumeLease::Result::Ready) return Result::Unavailable;
    out = std::move(expected.record);
    return Result::Ready;
#else
    return Result::Unavailable;
#endif
}
} // namespace platform::linux_runtime
