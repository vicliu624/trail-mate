#include "platform/linux/fm_session.h"
#include "platform/linux/fm_pcm_output.h"
#include "platform/linux/packet_radio.h"
#include "platform/shared/radio/fm_iq_queue.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace platform::linux_runtime
{
using namespace platform::shared::radio;
struct FmSession::Impl
{
    IPacketRadio& radio;
    bool (*handoff)();
    mutable std::mutex mutex;
    std::condition_variable wake, done;
    Snapshot state;
    bool pending = false, shutdown = false, running = false;
    uint32_t requested_hz = 100800000;
    bool requested_warning = false;
    std::atomic_bool cancel{false}, producer_done{false};
    std::atomic<Error> fault{Error::None};
    FmPcmOutput audio;
    std::unique_ptr<FmIqQueue> queue;
    FmIqQueue::Block block;
    FmIqConsumer decoder;
    std::array<uint8_t, 1280> staging{};
    std::array<int16_t, FmIqConsumer::kMaxPcmSamples> pcm{};
    uint64_t session = 0, next_pair = 0;
    std::thread owner;

    Impl(IPacketRadio& r, bool (*barrier)()) : radio(r), handoff(barrier), owner([this]
                                                                                 { loop(); }) {}
    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            shutdown = true;
            pending = false;
            cancel.store(true, std::memory_order_release);
        }
        wake.notify_one();
        owner.join();
    }
    void fail(Error error)
    {
        Error expected = Error::None;
        fault.compare_exchange_strong(expected, error);
        cancel.store(true, std::memory_order_release);
    }
    bool push(const uint8_t* data, size_t bytes, uint64_t first)
    {
        if (cancel.load(std::memory_order_acquire)) return false;
        if (first != next_pair)
        {
            fail(Error::InvalidIq);
            return false;
        }
        const auto result = queue->push(session, data, bytes, 250000, 1, first);
        if (result != FmIqQueue::PushResult::Accepted)
        {
            fail(result == FmIqQueue::PushResult::Full ? Error::QueueFull : Error::InvalidIq);
            return false;
        }
        next_pair += bytes / 2;
        return true;
    }
    void consume()
    {
        bool connected = false;
        try
        {
            while (!cancel.load(std::memory_order_acquire))
            {
                if (!queue->pop(block))
                {
                    if (producer_done.load(std::memory_order_acquire)) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                const auto result = decoder.consume(block.data, block.bytes, block.rate, block.generation,
                                                    block.first_pair, pcm.data(), pcm.size(), block.format);
                if (block.session != session ||
                    (result.status != FmIqConsumer::Status::Accepted &&
                     !(result.status == FmIqConsumer::Status::GapReset && !connected)))
                {
                    fail(Error::InvalidIq);
                    break;
                }
                connected = true;
                if (result.samples && !audio.write(pcm.data(), result.samples, session, cancel))
                {
                    if (!cancel.load()) fail(Error::Audio);
                    break;
                }
                if (result.samples)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    state.pcm_samples += result.samples;
                    if (state.phase == Phase::Starting) state.phase = Phase::PcmQueued;
                }
            }
        }
        catch (...)
        {
            fail(Error::Internal);
        }
    }
    void run(uint32_t hz, bool warning)
    {
        bool acquired = false, restore_failed = false;
        FmIqResult result;
        std::thread consumer;
        try
        {
            // A fresh queue prevents old-station blocks surviving cancellation.
            queue = std::make_unique<FmIqQueue>();
            decoder.reset();
            next_pair = 0;
            if (!radio.supportsFmIq() || !radio.hardwareCandidatePresent()) fail(Error::RadioAcquire);
            if (!cancel.load() && (!handoff || !handoff())) fail(Error::Handoff);
            if (!cancel.load())
            {
                acquired = radio.acquire();
                if (!acquired) fail(Error::RadioAcquire);
            }
            if (!cancel.load() && !audio.begin(session)) fail(Error::Audio);
            if (!cancel.load())
            {
                consumer = std::thread([this]
                                       { consume(); });
                FmIqRequest request;
                request.frequency_hz = hz;
                request.allow_image_calibration_warning = warning;
                request.staging = staging.data();
                request.staging_bytes = staging.size();
                request.context = this;
                request.cancelled = [](void* c)
                { return static_cast<Impl*>(c)->cancel.load(std::memory_order_acquire); };
                request.accept = [](void* c, const uint8_t* p, size_t n, uint64_t first)
                { return static_cast<Impl*>(c)->push(p, n, first); };
                const bool ok = radio.streamFmIq(request, result);
                if (result.recovery_error) fail(Error::Restore);
                else if (!ok || result.capture_error || result.sink_rejected || !cancel.load()) fail(Error::Stream);
            }
        }
        catch (...)
        {
            fail(Error::Internal);
        }
        producer_done.store(true, std::memory_order_release);
        if (consumer.joinable()) consumer.join();
        audio.end();
        queue.reset();
        if (acquired)
        {
            try
            {
                restore_failed = !radio.releaseWithStatus();
            }
            catch (...)
            {
                restore_failed = true;
            }
            if (restore_failed) fail(Error::Restore);
        }
        std::lock_guard<std::mutex> lock(mutex);
        state.radio = result;
        state.lease_restore_failed = restore_failed;
        state.error = fault.load();
        state.phase = state.error == Error::None ? Phase::Stopped : Phase::Error;
        // Failed restoration must never be followed by an automatic retune.
        if (restore_failed || result.recovery_error) pending = false;
        state.active = pending;
        running = false;
        done.notify_all();
    }
    void loop()
    {
        std::unique_lock<std::mutex> lock(mutex);
        for (;;)
        {
            wake.wait(lock, [this]
                      { return pending || shutdown; });
            if (shutdown) break;
            const auto hz = requested_hz;
            const auto warning = requested_warning;
            pending = false;
            running = true;
            session++;
            if (!session) ++session;
            state = {};
            state.session = session;
            state.frequency_hz = hz;
            state.calibration_exception_requested = warning;
            state.phase = Phase::Starting;
            state.active = true;
            cancel = false;
            producer_done = false;
            fault = Error::None;
            lock.unlock();
            run(hz, warning);
            lock.lock();
        }
    }
};
FmSession::FmSession(IPacketRadio& radio, bool (*handoff)()) : impl_(std::make_unique<Impl>(radio, handoff)) {}
FmSession::~FmSession() = default;
bool FmSession::tune(uint32_t hz, bool warning)
{
    auto& s = *impl_;
    if (hz < 100000000 || hz > 108000000) return false;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.shutdown) return false;
    s.requested_hz = hz;
    s.requested_warning = warning;
    s.pending = true;
    s.state.active = true;
    s.cancel.store(true, std::memory_order_release);
    s.wake.notify_one();
    return true;
}
void FmSession::requestStop()
{
    auto& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    s.pending = false;
    if (s.state.active) s.state.phase = Phase::Stopping;
    s.cancel.store(true, std::memory_order_release);
    // If a request has not started, no worker run will publish completion.
    if (!s.running && s.state.active)
    {
        s.state.active = false;
        s.state.phase = Phase::Stopped;
        s.done.notify_all();
    }
}
void FmSession::setVolume(unsigned percent) { impl_->audio.setVolume(percent); }
FmSession::Snapshot FmSession::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state;
}
void FmSession::waitUntilFinished()
{
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->done.wait(lock, [this]
                     { return !impl_->state.active; });
}
} // namespace platform::linux_runtime
