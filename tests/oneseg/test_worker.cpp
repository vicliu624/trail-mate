#include "platform/linux/oneseg_pipe_sink.h"
#include "platform/linux/packet_radio.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>

using namespace platform::linux_runtime;
using Worker = OneSegReceiveWorker;
using namespace std::chrono_literals;
template <typename Predicate>
void await(Predicate predicate)
{
    const auto until = std::chrono::steady_clock::now() + 2s;
    while (!predicate() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(1ms);
    assert(predicate());
}
std::atomic_bool barrier_block{false}, barrier_release{true}, barrier_ok{true};
std::atomic_uint barrier_calls{0}, prepares{0};
bool barrier()
{
    ++barrier_calls;
    while (barrier_block && !barrier_release) std::this_thread::sleep_for(1ms);
    return barrier_ok;
}
struct State
{
    std::thread::id ui = std::this_thread::get_id();
    std::atomic_uint acquired{0}, released{0}, streams{0}, opens{0}, writes{0}, finishes{0};
    std::atomic_bool sink_finished{false};
    std::atomic_uint acquire_attempts{0};
    unsigned busy_attempts = 0;
    uint32_t nominal_rate = 576000;
    bool supported = true, present = true, acquire_ok = true, restore_ok = true;
    bool open_ok = true, write_ok = true, finish_ok = true, blocked_sink = false, throw_sink = false;
    enum Flow
    {
        Paced,
        Empty,
        Rapid,
        BadIndex,
        Failed
    } flow = Paced;
};
class Radio final : public IPacketRadio
{
  public:
    State& s;
    explicit Radio(State& state) : s(state) {}
    const char* name() const override { return "test-only"; }
    bool hardwareCandidatePresent() const override { return s.present; }
    bool supportsOneSegIq() const override { return s.supported; }
    bool acquire() override
    {
        assert(std::this_thread::get_id() != s.ui);
        if (++s.acquire_attempts <= s.busy_attempts) return false;
        if (!s.acquire_ok) return false;
        ++s.acquired;
        return true;
    }
    void release() override { (void)releaseWithStatus(); }
    bool releaseWithStatus() override
    {
        assert(s.sink_finished);
        ++s.released;
        return s.restore_ok;
    }
    bool configureLoRa(const RadioSettings&) override { return true; }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t*, size_t) override
    {
        assert(false);
        return false;
    }
    bool pollReceive(RadioPacket*) override
    {
        assert(false);
        return false;
    }
    float readRssi() override
    {
        assert(false);
        return 0;
    }
    bool isOnline() const override { return true; }
    const char* lastError() const override { return s.acquire_attempts <= s.busy_attempts && s.busy_attempts ? "" : "test"; }
    RadioSettings appliedLoRaConfig() const override { return {}; }
    RadioStatistics stats() const override { return {}; }
    bool streamOneSegIq(const OneSegIqRequest& request, OneSegIqResult& result) override
    {
        assert(std::this_thread::get_id() != s.ui);
        ++s.streams;
        result = {};
        std::array<uint8_t, 1280> data;
        data.fill(0x42);
        const unsigned count = s.flow == State::Rapid ? OneSegReceiveWorker::queue_slots + 2 : s.flow == State::Empty ? 0
                                                                                                                      : 4;
        for (unsigned n = 0; n < count && !request.cancelled(request.context); ++n)
        {
            const auto first = s.flow == State::BadIndex ? 1 : result.validated_bytes / 2;
            if (!request.accept(request.context, data.data(), data.size(), first)) break;
            result.validated_bytes += data.size();
            if (s.flow != State::Rapid) std::this_thread::sleep_for(2ms);
        }
        if (s.flow == State::Failed)
        {
            result.capture_error = -5;
            return false;
        }
        while (!request.cancelled(request.context)) std::this_thread::sleep_for(1ms);
        result.cancelled = true;
        result.nominal_rate_hz = s.nominal_rate;
        return true;
    }
};
class Sink final : public OneSegIqSink
{
  public:
    State& s;
    uint64_t generation = 0, next_pair = 0;
    explicit Sink(State& state) : s(state) {}
    bool open(unsigned channel, uint64_t gen, const std::atomic_bool&) override
    {
        assert(std::this_thread::get_id() != s.ui && channel >= 13 && channel <= 62 && gen);
        ++s.opens;
        s.sink_finished = false;
        generation = gen;
        next_pair = 0;
        return s.open_ok;
    }
    bool write(const uint8_t* data, size_t bytes, uint64_t first, uint64_t gen, const std::atomic_bool& cancel) override
    {
        assert(std::this_thread::get_id() != s.ui && data[0] == 0x42 && bytes == 1280);
        assert(gen == generation && first == next_pair);
        if (s.throw_sink) throw std::runtime_error("test sink");
        while (s.blocked_sink && !cancel.load()) std::this_thread::sleep_for(1ms);
        if (cancel.load() || !s.write_ok) return false;
        next_pair += bytes / 2;
        ++s.writes;
        return true;
    }
    bool finish() override
    {
        ++s.finishes;
        s.sink_finished = true;
        return s.finish_ok;
    }
    const char* lastError() const override { return "fixture sink detail"; }
};
IPacketRadio* bound_radio = nullptr;
namespace platform::linux_runtime
{
IPacketRadio& foreground_packet_radio()
{
    assert(bound_radio && prepares > 0);
    return *bound_radio;
}
} // namespace platform::linux_runtime
namespace platform::ui::lora
{
bool wait_receive_session_released() { return barrier(); }
namespace detail
{
void prepare_receive_session_worker() { ++prepares; }
} // namespace detail
} // namespace platform::ui::lora
int main()
{
    {
        State state;
        Radio radio(state);
        Sink sink(state);
        Worker worker(radio, sink, barrier);
        OneSegReceptionSettings reception;
        reception.enabled = true;
        Worker::Source source;
        source.recording_capacity = 2048;
        state.nominal_rate = 450000;
        assert(worker.start(13, reception, source));
        await([&]
              { return worker.snapshot().forwarded_pairs == 2560; });
        std::string error;
        const std::string path = "/tmp/trailmate-worker-iq-" + std::to_string(getpid()) + ".tmiq";
        assert(!worker.exportRecording(path, error));
        worker.requestStop();
        worker.waitUntilFinished();
        assert(worker.snapshot().recorded_bytes == 2048 && worker.snapshot().recording_full);
        assert(worker.exportRecording(path, error));
        OneSegIqArchive archive;
        assert(archive.load(path, error) && archive.size() == 2048);
        for (size_t i = 0; i < archive.size(); ++i) assert(archive.data()[i] == 0x42);
        assert(archive.metadata().rate == 450000 && archive.metadata().gain == 9);
        assert(::unlink(path.c_str()) == 0);
        state.nominal_rate = 0;
        assert(worker.start(13, reception, source));
        await([&]
              { return worker.snapshot().forwarded_pairs == 2560; });
        worker.requestStop();
        worker.waitUntilFinished();
        assert(!worker.exportRecording(path, error) && error.find("sample rate") != std::string::npos);
        // The source API rejects unsupported archive decoders before hardware.
        source.recording_capacity = 0;
        source.archive_path = "/not-present.tmiq";
        const auto leases = state.acquired.load();
        assert(worker.start(13, reception, source));
        worker.waitUntilFinished();
        assert(worker.snapshot().error == Worker::Error::InvalidBlock && state.acquired == leases);
        barrier_calls = 0;
    }
    {
        rlimit original{};
        assert(!getrlimit(RLIMIT_MEMLOCK, &original));
        auto denied = original;
        denied.rlim_cur = 0;
        assert(!setrlimit(RLIMIT_MEMLOCK, &denied));
        State state;
        Radio radio(state);
        Sink sink(state);
        {
            Worker worker(radio, sink, barrier);
            assert(worker.start(13));
            worker.waitUntilFinished();
            assert(worker.snapshot().error == Worker::Error::Unavailable);
            assert(!state.acquired && !state.streams && !state.opens && !barrier_calls);
            assert(std::strstr(worker.snapshot().message, "lock") != nullptr);
        }
        assert(!setrlimit(RLIMIT_MEMLOCK, &original));
    }
    {
        State s;
        Radio radio(s);
        Sink sink(s);
        Worker worker(radio, sink, barrier);
        assert(!worker.start(12) && worker.snapshot().error == Worker::Error::InvalidChannel);
        s.present = false;
        assert(!worker.start(13) && worker.snapshot().error == Worker::Error::Unavailable);
        assert(!s.acquired && !s.opens);
        s.present = true;
        assert(worker.start(13) && !worker.start(14));
        await([&]
              { return worker.snapshot().forwarded_pairs == 2560; });
        const auto generation = worker.snapshot().generation;
        assert(worker.snapshot().phase == Worker::Phase::ReceivingIq && worker.snapshot().forwarded_pairs == 2560);
        worker.requestStop();
        worker.waitUntilFinished();
        assert(worker.snapshot().phase == Worker::Phase::Stopped && s.released == 1 && s.finishes == 1);
        assert(worker.start(14));
        await([&]
              { return worker.snapshot().generation > generation && worker.snapshot().forwarded_pairs == 2560; });
        worker.requestStop();
        worker.waitUntilFinished();
        assert(worker.snapshot().generation > generation && s.released == 2);
    }
    for (unsigned scenario = 0; scenario < 9; ++scenario)
    {
        State s;
        Radio radio(s);
        Sink sink(s);
        Worker worker(radio, sink, barrier);
        if (scenario == 0) s.open_ok = false;
        if (scenario == 1) s.acquire_ok = false;
        if (scenario == 2)
        {
            s.flow = State::Rapid;
            s.blocked_sink = true;
        }
        if (scenario == 3) s.write_ok = false;
        if (scenario == 4) s.flow = State::BadIndex;
        if (scenario == 5) s.flow = State::Failed;
        if (scenario == 6) s.restore_ok = false;
        if (scenario == 7) s.finish_ok = false;
        if (scenario == 8) s.throw_sink = true;
        assert(worker.start(13));
        if (scenario == 6 || scenario == 7)
        {
            await([&]
                  { return s.writes == 4; });
            worker.requestStop();
        }
        worker.waitUntilFinished();
        const Worker::Error errors[] = {Worker::Error::SinkOpen, Worker::Error::RadioAcquire,
                                        Worker::Error::QueueFull, Worker::Error::SinkWrite, Worker::Error::InvalidBlock,
                                        Worker::Error::RadioStream, Worker::Error::RadioRestore, Worker::Error::SinkFinish, Worker::Error::Internal};
        const auto report = worker.snapshot();
        assert(report.phase == Worker::Phase::Error && report.error == errors[scenario]);
        if (scenario == 0 || scenario == 3 || scenario == 7) assert(std::strstr(report.message, "fixture sink detail"));
        assert(s.finishes == 1 && s.released == s.acquired);
        if (scenario == 2)
        {
            assert(report.queue_high_water == Worker::queue_slots);
            assert(!worker.start(0) && worker.snapshot().queue_high_water == 0);
        }
    }
    {
        State s;
        s.flow = State::Empty;
        Radio radio(s);
        Sink sink(s);
        {
            Worker worker(radio, sink, barrier);
            assert(worker.start(13));
            await([&]
                  { return s.streams == 1; });
        }
        assert(s.released == 1 && s.finishes == 1); // Destructor cancels/joins empty FIFO.
    }
    {
        State s;
        Radio radio(s);
        Sink sink(s);
        Worker worker(radio, sink, barrier);
        barrier_calls = 0;
        barrier_block = true;
        barrier_release = false;
        assert(worker.start(13));
        await([]
              { return barrier_calls > 0; });
        worker.requestStop();
        barrier_release = true;
        worker.waitUntilFinished();
        assert(!s.opens && !s.acquired && worker.snapshot().phase == Worker::Phase::Stopped);
        barrier_block = false;
        barrier_ok = false;
        assert(worker.start(13));
        worker.waitUntilFinished();
        assert(worker.snapshot().error == Worker::Error::ReceiveBusy && !s.acquired);
        barrier_ok = true;
    }
    {
        State s;
        Radio radio(s);
        Sink sink(s);
        bound_radio = &radio;
        setenv("TRAIL_MATE_RUNTIME_MODE", "demo", 1);
        assert(!make_oneseg_receive_worker(sink));
        setenv("TRAIL_MATE_RUNTIME_MODE", "mesh", 1);
        setenv("TRAIL_MATE_LORA_SIMULATED", "true", 1);
        assert(!make_oneseg_receive_worker(sink));
        unsetenv("TRAIL_MATE_LORA_SIMULATED");
        {
            auto worker = make_oneseg_receive_worker(sink);
            assert(worker && prepares == 1 && !s.acquired);
        }
        bound_radio = nullptr;
    }
    // Real pipe integration: source callbacks -> ring -> consumer -> exact IQ.
    {
        State s;
        Radio radio(s);
        int fds[2];
        assert(pipe(fds) == 0);
        struct Endpoint
        {
            int fd;
            State* state;
        } endpoint{fds[1], &s};
        OneSegPipeSink pipe_sink({&endpoint, [](void* p, unsigned, uint64_t, const std::atomic_bool&)
                                  { auto& e = *static_cast<Endpoint*>(p); const int fd = e.fd; e.fd = -1; return fd; }});
        // Observer preserves the cleanup-order assertion from the fake radio.
        class Observer final : public OneSegIqSink
        {
          public:
            OneSegPipeSink& pipe;
            State& state;
            Observer(OneSegPipeSink& p, State& s) : pipe(p), state(s) {}
            bool open(unsigned c, uint64_t g, const std::atomic_bool& cancel) override { return pipe.open(c, g, cancel); }
            bool write(const uint8_t* p, size_t n, uint64_t i, uint64_t g, const std::atomic_bool& cancel) override { return pipe.write(p, n, i, g, cancel); }
            bool finish() override
            {
                const bool ok = pipe.finish();
                state.sink_finished = true;
                return ok;
            }
        } sink(pipe_sink, s);
        std::atomic_size_t read_bytes{0};
        std::thread reader([&]
                           { uint8_t data[1024]; ssize_t n; while((n=read(fds[0],data,sizeof(data)))>0) { for(ssize_t j=0;j<n;++j) assert(data[j]==0x42); read_bytes+=n; } close(fds[0]); });
        Worker worker(radio, sink, barrier);
        assert(worker.start(13));
        await([&]
              { return read_bytes == 5120; });
        worker.requestStop();
        worker.waitUntilFinished();
        reader.join();
        assert(worker.snapshot().forwarded_pairs == 2560 && worker.snapshot().phase == Worker::Phase::Stopped);
        assert(s.released == 1);
    }
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        State s;
        s.busy_attempts = mode == 0 ? 3 : 1000;
        Radio radio(s);
        Sink sink(s);
        Worker worker(radio, sink, barrier);
        const auto started = std::chrono::steady_clock::now();
        assert(worker.start(13));
        if (mode == 0)
        {
            await([&]
                  { return worker.snapshot().forwarded_pairs == 2560; });
            worker.requestStop();
        }
        else if (mode == 2)
        {
            await([&]
                  { return s.acquire_attempts.load() != 0; });
            worker.requestStop();
        }
        worker.waitUntilFinished();
        const auto elapsed = std::chrono::steady_clock::now() - started;
        if (mode == 1)
        {
            assert(worker.snapshot().error == Worker::Error::RadioAcquire);
            assert(elapsed >= 200ms && elapsed < 1s && !s.acquired);
        }
        else assert(worker.snapshot().phase == Worker::Phase::Stopped);
        if (mode == 0) assert(s.acquire_attempts == 4 && s.acquired == 1 && s.released == 1);
        if (mode == 2) assert(elapsed < 100ms && !s.acquired && !s.streams);
    }
}
