#include "platform/linux/oneseg_receive_worker.h"
#include "platform/linux/oneseg_lr2021_rx.h"
#include "platform/linux/packet_radio.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>

namespace platform::linux_runtime
{
struct OneSegReceiveWorker::Impl
{
    static constexpr uint32_t kSlots = 8;
    static constexpr size_t kBlockBytes = 1280;
    static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic_bool::is_always_lock_free);
    static_assert(std::atomic<Error>::is_always_lock_free);
    struct Block
    {
        uint64_t first_pair = 0, generation = 0;
        size_t bytes = 0;
        std::array<uint8_t, kBlockBytes> data{};
    };
    IPacketRadio& radio;
    OneSegIqSink& sink;
    bool (*handoff)();
    mutable std::mutex mutex;
    std::condition_variable wake, done;
    Snapshot state;
    bool active = false, pending = false, shutdown = false;
    std::atomic_bool cancel{false}, producer_done{false};
    std::atomic<Error> fault{Error::None};
    std::atomic<uint32_t> write_index{0}, read_index{0}, high_water{0};
    std::array<Block, kSlots> blocks;
    std::array<uint8_t, kBlockBytes> staging{};
    uint64_t producer_next_pair = 0, session_generation = 0;
    char detail[128]{}; // Written only by the first failing thread; read after join.
    std::thread owner;

    Impl(IPacketRadio& r, OneSegIqSink& s, bool (*barrier)()) : radio(r), sink(s), handoff(barrier), owner([this]
                                                                                                           { loop(); }) {}
    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            shutdown = true;
            cancel = true;
        }
        wake.notify_one();
        owner.join();
    }
    void fail(Error reason, const char* explanation = nullptr)
    {
        Error expected = Error::None;
        if (fault.compare_exchange_strong(expected, reason) && explanation)
            std::snprintf(detail, sizeof(detail), "%s", explanation);
        cancel.store(true, std::memory_order_release);
    }
    // Only the producer calls this: bounded copy plus lock-free atomics.
    bool push(const uint8_t* data, size_t bytes, uint64_t first_pair)
    {
        if (cancel.load(std::memory_order_acquire)) return false;
        if (!data || !bytes || bytes > kBlockBytes || bytes % 2 || first_pair != producer_next_pair ||
            first_pair > std::numeric_limits<uint64_t>::max() - bytes / 2)
        {
            fail(Error::InvalidBlock);
            return false;
        }
        const auto write = write_index.load(std::memory_order_relaxed);
        const auto read = read_index.load(std::memory_order_acquire);
        const auto count = write - read;
        if (count >= kSlots)
        {
            fail(Error::QueueFull);
            return false;
        }
        auto& block = blocks[write % kSlots];
        block.bytes = bytes;
        block.first_pair = first_pair;
        block.generation = session_generation;
        std::memcpy(block.data.data(), data, bytes);
        producer_next_pair += bytes / 2;
        if (count + 1 > high_water.load(std::memory_order_relaxed)) high_water.store(count + 1, std::memory_order_relaxed);
        write_index.store(write + 1, std::memory_order_release);
        return true;
    }
    void consume()
    {
        uint64_t next_pair = 0;
        try
        {
            while (!cancel.load(std::memory_order_acquire))
            {
                const auto read = read_index.load(std::memory_order_relaxed);
                if (read == write_index.load(std::memory_order_acquire))
                {
                    if (producer_done.load(std::memory_order_acquire)) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                const auto& block = blocks[read % kSlots];
                if (block.generation != session_generation || block.first_pair != next_pair)
                {
                    fail(Error::InvalidBlock);
                    break;
                }
                if (!sink.write(block.data.data(), block.bytes, block.first_pair, block.generation, cancel))
                {
                    if (!cancel.load()) fail(Error::SinkWrite, sink.lastError());
                    break;
                }
                next_pair += block.bytes / 2;
                read_index.store(read + 1, std::memory_order_release);
                std::lock_guard<std::mutex> lock(mutex);
                state.forwarded_pairs = next_pair;
                if (state.phase == Phase::Starting) state.phase = Phase::ReceivingIq;
            }
        }
        catch (const std::exception& error)
        {
            fail(Error::Internal, error.what());
        }
        catch (...)
        {
            fail(Error::Internal);
        }
    }
    static const char* description(Error value)
    {
        switch (value)
        {
        case Error::None:
            return "";
        case Error::InvalidChannel:
            return "Invalid TV channel";
        case Error::Unavailable:
            return "Real CM0 TV IQ backend is unavailable";
        case Error::ReceiveBusy:
            return "Another packet receive session owns the radio";
        case Error::SinkOpen:
            return "Cannot open decoder IQ input";
        case Error::RadioAcquire:
            return "Cannot acquire the foreground radio";
        case Error::InvalidBlock:
            return "Invalid or discontinuous IQ block";
        case Error::QueueFull:
            return "IQ queue full; stream stopped without dropping silently";
        case Error::SinkWrite:
            return "Decoder IQ input failed or stalled";
        case Error::RadioStream:
            return "Radio IQ stream failed";
        case Error::RadioRestore:
            return "Radio configuration restoration failed";
        case Error::SinkFinish:
            return "Decoder IQ input cleanup failed";
        case Error::Internal:
            return "TV IQ worker failed";
        }
        return "TV IQ error";
    }
    void run(unsigned channel)
    {
        bool sink_attempted = false, acquired = false, restore_failed = false;
        std::thread consumer;
        OneSegIqResult result;
        try
        {
            if (!cancel.load() && (!handoff || !handoff())) fail(Error::ReceiveBusy);
            if (!cancel.load())
            {
                sink_attempted = true;
                if (!sink.open(channel, session_generation, cancel) && !cancel.load()) fail(Error::SinkOpen, sink.lastError());
            }
            if (!cancel.load())
            {
                acquired = radio.acquire();
                if (!acquired) fail(Error::RadioAcquire, radio.lastError());
            }
            if (!cancel.load())
            {
                consumer = std::thread([this]
                                       { consume(); });
                OneSegIqRequest request;
                request.channel = channel;
                request.staging = staging.data();
                request.staging_bytes = staging.size();
                request.context = this;
                request.cancelled = [](void* c)
                {
                    auto& self = *static_cast<Impl*>(c);
                    if (!self.sink.healthy()) self.fail(Error::SinkWrite);
                    return self.cancel.load(std::memory_order_acquire);
                };
                request.accept = [](void* c, const uint8_t* p, size_t n, uint64_t first)
                { return static_cast<Impl*>(c)->push(p, n, first); };
                const bool completed = radio.streamOneSegIq(request, result);
                if (result.recovery_error) fail(Error::RadioRestore, radio.lastError());
                else if (!completed || result.capture_error || result.sink_rejected || !cancel.load())
                    fail(Error::RadioStream, radio.lastError());
            }
        }
        catch (const std::exception& error)
        {
            fail(Error::Internal, error.what());
        }
        catch (...)
        {
            fail(Error::Internal);
        }
        producer_done.store(true, std::memory_order_release);
        if (consumer.joinable()) consumer.join();
        // The FIFO callback only publishes atomics. Fetch/format sink details
        // here, after the consumer has stopped, outside the producer hot path.
        if (fault.load() == Error::SinkWrite && !detail[0])
            std::snprintf(detail, sizeof(detail), "%s", sink.lastError());
        if (sink_attempted)
        {
            try
            {
                if (!sink.finish()) fail(Error::SinkFinish, sink.lastError());
            }
            catch (...)
            {
                fail(Error::SinkFinish);
            }
        }
        if (acquired)
        {
            try
            {
                restore_failed = !radio.releaseWithStatus();
                if (restore_failed) fail(Error::RadioRestore, radio.lastError());
            }
            catch (...)
            {
                restore_failed = true;
            }
            if (restore_failed) fail(Error::RadioRestore);
        }
        std::lock_guard<std::mutex> lock(mutex);
        state.radio = result;
        state.lease_restore_failed = restore_failed;
        state.error = fault.load();
        state.phase = state.error == Error::None ? Phase::Stopped : Phase::Error;
        std::snprintf(state.message, sizeof(state.message), "%s%s%.80s", description(state.error), detail[0] ? ": " : "", detail);
        active = false;
        done.notify_all();
    }
    void loop()
    {
        std::unique_lock<std::mutex> lock(mutex);
        for (;;)
        {
            wake.wait(lock, [this]
                      { return pending || shutdown; });
            if (shutdown && !pending) break;
            const unsigned channel = state.channel;
            pending = false;
            lock.unlock();
            run(channel);
            lock.lock();
        }
    }
};

OneSegReceiveWorker::OneSegReceiveWorker(IPacketRadio& radio, OneSegIqSink& sink, bool (*handoff)())
    : impl_(std::make_unique<Impl>(radio, sink, handoff)) {}
OneSegReceiveWorker::~OneSegReceiveWorker() = default;
bool OneSegReceiveWorker::start(unsigned channel)
{
    auto& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.active || s.shutdown) return false;
    const uint64_t generation = s.state.generation + 1;
    s.state = {};
    s.state.channel = channel;
    s.state.generation = generation ? generation : 1;
    s.detail[0] = 0;
    s.high_water = 0;
    if (!OneSegLr2021Rx::frequencyHz(channel) || !s.radio.supportsOneSegIq() || !s.radio.hardwareCandidatePresent())
    {
        s.state.error = OneSegLr2021Rx::frequencyHz(channel) ? Error::Unavailable : Error::InvalidChannel;
        s.state.phase = Phase::Error;
        std::snprintf(s.state.message, sizeof(s.state.message), "%s", Impl::description(s.state.error));
        return false;
    }
    s.write_index = 0;
    s.read_index = 0;
    s.high_water = 0;
    s.producer_next_pair = 0;
    s.session_generation = s.state.generation;
    s.cancel = false;
    s.producer_done = false;
    s.fault = Error::None;
    s.state.phase = Phase::Starting;
    s.active = s.pending = true;
    s.wake.notify_one();
    return true;
}
void OneSegReceiveWorker::requestStop()
{
    auto& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.active) return;
    s.state.phase = Phase::Stopping;
    s.cancel.store(true, std::memory_order_release);
}
OneSegReceiveWorker::Snapshot OneSegReceiveWorker::snapshot() const
{
    auto& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    auto result = s.state;
    result.queue_high_water = s.high_water.load(std::memory_order_relaxed);
    return result;
}
void OneSegReceiveWorker::waitUntilFinished()
{
    auto& s = *impl_;
    std::unique_lock<std::mutex> lock(s.mutex);
    s.done.wait(lock, [&s]
                { return !s.active; });
}
} // namespace platform::linux_runtime
