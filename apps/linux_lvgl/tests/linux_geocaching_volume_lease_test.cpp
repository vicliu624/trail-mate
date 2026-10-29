#include "platform/linux/geocaching_volume_lease.h"
#include <cassert>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using Lease = platform::linux_runtime::LinuxGeocachingVolumeLease;
using Result = Lease::Result;
std::string contents(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    return {std::istreambuf_iterator<char>(file), {}};
}
void overwrite(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), bytes.size());
    assert(file.good());
}
int main()
{
#if !defined(__linux__)
    return 77;
#else
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-volume-lease-" + std::to_string(getpid()));
    const auto cache = root / "cache";
    std::filesystem::create_directories(cache);
    Lease::Volume volume{}, observed{};
    volume[0] = 1;
    {
        Lease owner(cache), other(cache);
        assert(owner.acquire(observed) == Result::Missing && observed == Lease::Volume{});
        assert(owner.initialize({}) == Result::Invalid && !std::filesystem::exists(cache / ".state"));
        assert(owner.initialize(volume) == Result::Ready);
        const auto original = contents(cache / ".state/format.bin");
        auto wrong = volume;
        wrong[0] = 2;
        assert(owner.initialize(wrong) == Result::Exists && contents(cache / ".state/format.bin") == original);
        assert(owner.acquire(observed) == Result::Ready && observed == volume);
        assert(owner.check(volume) == Result::Ready && owner.rootDescriptor() >= 0);
        assert(owner.check(wrong) == Result::Mismatch);
        assert(other.acquire(observed) == Result::Busy && observed == Lease::Volume{});
        struct stat before
        {
        }, after{};
        assert(::stat((cache / ".state/owner.lock").c_str(), &before) == 0);
        owner.release();
        assert(owner.rootDescriptor() < 0 && other.acquire(observed) == Result::Ready);
        assert(::stat((cache / ".state/owner.lock").c_str(), &after) == 0 && before.st_ino == after.st_ino);
        // Normal directory writes do not invalidate the root identity.
        std::filesystem::create_directory(cache / ".state/staging");
        assert(other.check(volume) == Result::Ready);
        std::filesystem::remove(cache / ".state/owner.lock");
        Lease replacement(cache);
        assert(replacement.acquire(observed) == Result::Ready);
        assert(other.check(volume) == Result::Stale && other.rootDescriptor() < 0);
        other.release();
        Lease third(cache);
        assert(third.acquire(observed) == Result::Busy); // Releasing stale inode cannot unlock its replacement.
    }
    {
        Lease owner(cache);
        assert(owner.acquire(observed) == Result::Ready);
        auto header = contents(cache / ".state/format.bin");
        header.back() ^= 1;
        overwrite(cache / ".state/format.bin", header);
        assert(owner.check(volume) == Result::Corrupt);
        assert(owner.initialize(volume) == Result::Busy);
        owner.release();
        assert(owner.initialize(volume) == Result::Exists && contents(cache / ".state/format.bin") == header);
    }
    {
        const auto future_root = root / "future";
        std::filesystem::create_directory(future_root);
        Lease future(future_root);
        assert(future.initialize(volume) == Result::Ready);
        auto header = contents(future_root / ".state/format.bin");
        header[5] = 2;
        header += "future-format";
        overwrite(future_root / ".state/format.bin", header);
        assert(future.acquire(observed) == Result::Unsupported);
        assert(!std::filesystem::exists(future_root / ".state/owner.lock"));
        assert(contents(future_root / ".state/format.bin") == header);
    }
    {
        const auto incomplete = root / "incomplete";
        std::filesystem::create_directories(incomplete / ".state");
        overwrite(incomplete / ".state/history", "retained evidence");
        Lease owner(incomplete);
        assert(owner.acquire(observed) == Result::Corrupt && owner.initialize(volume) == Result::Exists);
        assert(contents(incomplete / ".state/history") == "retained evidence" && !std::filesystem::exists(incomplete / ".state/format.bin"));
    }
    {
        const auto fifo = root / "fifo";
        std::filesystem::create_directory(fifo);
        Lease owner(fifo);
        assert(owner.initialize(volume) == Result::Ready);
        assert(::mkfifo((fifo / ".state/owner.lock").c_str(), 0600) == 0);
        assert(owner.acquire(observed) == Result::Unsafe);
    }
    {
        const auto swapped = root / "swapped";
        std::filesystem::create_directory(swapped);
        Lease owner(swapped);
        assert(owner.initialize(volume) == Result::Ready && owner.acquire(observed) == Result::Ready);
        std::filesystem::rename(swapped, root / "original-volume");
        std::filesystem::create_directory(swapped);
        Lease replacement(swapped);
        auto another = volume;
        another[0] = 2;
        assert(replacement.initialize(another) == Result::Ready && replacement.acquire(observed) == Result::Ready);
        assert(owner.check(volume) == Result::Stale && owner.rootDescriptor() < 0);
    }
    {
        const auto crash = root / "crash";
        std::filesystem::create_directory(crash);
        Lease parent(crash);
        assert(parent.initialize(volume) == Result::Ready);
        int pipefd[2];
        assert(::pipe(pipefd) == 0);
        const auto pid = ::fork();
        assert(pid >= 0);
        if (pid == 0)
        {
            ::close(pipefd[0]);
            Lease child(crash);
            Lease::Volume actual{};
            const char acquired = child.acquire(actual) == Result::Ready ? 1 : 0;
            (void)::write(pipefd[1], &acquired, 1);
            for (;;) ::pause();
        }
        ::close(pipefd[1]);
        char acquired = 0;
        const auto received = ::read(pipefd[0], &acquired, 1);
        ::close(pipefd[0]);
        const auto contention = parent.acquire(observed);
        const auto killed = ::kill(pid, SIGKILL);
        int status = 0;
        const auto waited = ::waitpid(pid, &status, 0);
        // Reap the child before assertions, including a failed acquisition.
        assert(received == 1 && acquired == 1 && contention == Result::Busy);
        assert(killed == 0 && waited == pid && WIFSIGNALED(status));
        assert(parent.acquire(observed) == Result::Ready); // Kernel released the crashed writer's lease.
    }
    std::filesystem::remove_all(root);
#endif
}
