#include "platform/linux/oneseg_pipe_sink.h"
#include <array>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <signal.h>
#include <thread>
#include <unistd.h>

using namespace platform::linux_runtime;
struct Factory
{
    int fd;
    OneSegPipeSink::Factory port()
    {
        return {this, [](void* p, unsigned, uint64_t, const std::atomic_bool&)
                { auto& f=*static_cast<Factory*>(p); int fd=f.fd; f.fd=-1; return fd; }};
    }
};
int main()
{
    std::atomic_bool cancel{false};
    std::array<uint8_t, 2048> data{};
    int fds[2];
    assert(pipe(fds) == 0);
    Factory f{fds[1]};
    OneSegPipeSink sink(f.port());
    assert(sink.open(13, 1, cancel));
    assert(fcntl(fds[1], F_GETFL) & O_NONBLOCK);
    assert(fcntl(fds[1], F_GETFD) & FD_CLOEXEC);
    assert(sink.write(data.data(), data.size(), 0, 1, cancel));
    std::array<uint8_t, 2048> received;
    assert(read(fds[0], received.data(), received.size()) == 2048 && received == data);
    assert(!sink.write(data.data(), data.size(), 0, 1, cancel));   // Duplicate timeline.
    assert(!sink.write(data.data(), data.size(), 640, 2, cancel)); // Stale generation.
    assert(sink.finish() && sink.finish());
    close(fds[0]);
    assert(fcntl(fds[1], F_GETFD) == -1 && errno == EBADF);
    {
        Factory invalid{open("/dev/null", O_WRONLY | O_CLOEXEC)};
        OneSegPipeSink bad(invalid.port());
        assert(!bad.open(13, 2, cancel) && bad.finish());
    }
    // A decoder crash must neither kill the app nor change global SIGPIPE policy.
    auto original = signal(SIGPIPE, SIG_DFL);
    assert(pipe(fds) == 0);
    close(fds[0]);
    Factory closed{fds[1]};
    OneSegPipeSink broken(closed.port());
    assert(broken.open(13, 3, cancel));
    sigset_t before, after;
    assert(pthread_sigmask(SIG_SETMASK, nullptr, &before) == 0);
    assert(!broken.write(data.data(), data.size(), 0, 3, cancel));
    assert(pthread_sigmask(SIG_SETMASK, nullptr, &after) == 0);
    assert(sigismember(&before, SIGPIPE) == sigismember(&after, SIGPIPE));
    struct sigaction disposition
    {
    };
    assert(sigaction(SIGPIPE, nullptr, &disposition) == 0 && disposition.sa_handler == SIG_DFL);
    assert(broken.finish());
    signal(SIGPIPE, original);
    // No-reader backpressure is bounded; cancellation is faster than timeout.
    for (bool stop : {false, true})
    {
        assert(pipe(fds) == 0);
        Factory blocked{fds[1]};
        OneSegPipeSink pending(blocked.port());
        cancel = false;
        assert(pending.open(13, 4, cancel));
        std::array<uint8_t, 4096> fill{};
        while (write(fds[1], fill.data(), fill.size()) > 0)
        {
        }
        assert(errno == EAGAIN || errno == EWOULDBLOCK);
        std::thread stopper;
        if (stop) stopper = std::thread([&]
                                        { std::this_thread::sleep_for(std::chrono::milliseconds(20)); cancel=true; });
        const auto start = std::chrono::steady_clock::now();
        assert(!pending.write(data.data(), data.size(), 0, 4, cancel));
        const auto elapsed = std::chrono::steady_clock::now() - start;
        assert(elapsed < std::chrono::seconds(1));
        if (stop)
        {
            stopper.join();
            assert(elapsed < std::chrono::milliseconds(200));
        }
        else assert(elapsed >= std::chrono::milliseconds(200));
        assert(pending.finish());
        close(fds[0]);
    }
    // A probing decoder can pause beyond 250 ms, but only inside its explicit
    // startup window. Cancellation stays prompt and normal bounds return.
    for (bool stop : {false, true})
    {
        assert(pipe(fds) == 0);
        Factory delayed{fds[1]};
        auto port = delayed.port();
        port.startup_grace_ms = 600;
        OneSegPipeSink probing(port);
        cancel = false;
        assert(probing.open(13, 5, cancel));
        std::array<uint8_t, 4096> fill{};
        while (write(fds[1], fill.data(), fill.size()) > 0)
        {
        }
        const auto start = std::chrono::steady_clock::now();
        std::thread reader([&]
                           {
            std::this_thread::sleep_for(std::chrono::milliseconds(stop ? 20 : 350));
            if (stop) cancel = true;
            else assert(read(fds[0], fill.data(), fill.size()) == static_cast<ssize_t>(fill.size())); });
        assert(probing.write(data.data(), data.size(), 0, 5, cancel) == !stop);
        reader.join();
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (stop) assert(elapsed < std::chrono::milliseconds(200));
        else
        {
            assert(elapsed >= std::chrono::milliseconds(300) && elapsed < std::chrono::seconds(1));
            std::this_thread::sleep_until(start + std::chrono::milliseconds(650));
            while (write(fds[1], data.data(), data.size()) > 0)
            {
            }
            const auto normal = std::chrono::steady_clock::now();
            assert(!probing.write(data.data(), data.size(), data.size() / 2, 5, cancel));
            const auto stalled = std::chrono::steady_clock::now() - normal;
            assert(stalled >= std::chrono::milliseconds(200) && stalled < std::chrono::milliseconds(700));
        }
        assert(probing.finish());
        close(fds[0]);
    }
    {
        assert(pipe(fds) == 0);
        Factory absent{fds[1]};
        auto port = absent.port();
        port.startup_grace_ms = 600;
        OneSegPipeSink startup(port);
        cancel = false;
        assert(startup.open(13, 6, cancel));
        std::array<uint8_t, 4096> fill{};
        while (write(fds[1], fill.data(), fill.size()) > 0)
        {
        }
        const auto began = std::chrono::steady_clock::now();
        assert(!startup.write(data.data(), data.size(), 0, 6, cancel));
        const auto elapsed = std::chrono::steady_clock::now() - began;
        assert(elapsed >= std::chrono::milliseconds(500) && elapsed < std::chrono::milliseconds(900));
        assert(startup.finish());
        close(fds[0]);
    }
}
