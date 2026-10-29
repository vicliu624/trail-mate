#include "platform/linux/call_audio_service.h"
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
#include "platform/audio/audio_focus.h"
#include "platform/linux/native_audio_service.h"
#include "platform/linux/sdl_pcm_input.h"
#include "platform/linux/sdl_pcm_output.h"
#include "platform/ui/audio/call_notification_tone.h"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace platform::linux_runtime
{
namespace call = ::platform::ui::reticulum_call;
namespace
{
using Clock = std::chrono::steady_clock;
uint32_t now_ms() { return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()); }
bool equal(const uint8_t* first, const uint8_t* second) { return std::memcmp(first, second, call::kHashSize) == 0; }
bool nonzero(const uint8_t* hash)
{
    return hash && std::any_of(hash, hash + call::kHashSize, [](uint8_t byte)
                               { return byte != 0; });
}
class Pcm final : public ICallPcm
{
  public:
    bool open(uint32_t rate) override
    {
        close();
        input_ = std::make_unique<SdlPcmInput>();
        output_ = std::make_unique<SdlPcmOutput>(false, nullptr, 160);
        if (!output_->begin(rate, std::getenv("TRAIL_MATE_CALL_OUTPUT_DEVICE")) ||
            !input_->begin(rate, std::getenv("TRAIL_MATE_CALL_INPUT_DEVICE")))
        {
            close();
            return false;
        }
        return true;
    }
    bool captureActive() const override { return input_ && input_->active(); }
    uint64_t captureDropped() const override { return input_ ? input_->dropped_samples() : 0; }
    size_t read(int16_t* data, size_t size) override { return input_ ? input_->read(data, size) : 0; }
    void discardCapture() override { if (input_) input_->discard_pending(); }
    bool writable() const override { return output_ && !output_->failed() && output_->pending_ms() < std::max<uint32_t>(80, output_->period_ms() * 2); }
    bool write(const int16_t* data, size_t size) override { return output_ && output_->write(data, size); }
    void discardPlayback() override
    {
        if (output_) output_->discard_pending();
    }
    void close() noexcept override
    {
        input_.reset();
        output_.reset();
    }

  private:
    std::unique_ptr<SdlPcmInput> input_;
    std::unique_ptr<SdlPcmOutput> output_;
};
class Packets final : public ICallAudioPackets
{
  public:
    bool receive(call::AudioPacket& packet) override { return call::dequeue_inbound_audio(&packet); }
    bool transmit(const call::AudioPacket& packet) override { return call::enqueue_outbound_audio(packet.link_id, packet.data, packet.len); }
};
} // namespace
struct NativeCallAudioBinding::Impl
{
    explicit Impl(CallAudioOptions options) : options(std::move(options)) { published.built = true; }
    struct Registry
    {
        std::mutex lifecycle, lookup;
        std::weak_ptr<Impl> active;
    };
    static Registry& registry()
    {
        static Registry value;
        return value;
    }
    static std::shared_ptr<Impl> owner()
    {
        auto& r = registry();
        std::lock_guard<std::mutex> lock(r.lookup);
        return r.active.lock();
    }
    static bool supportedHook()
    {
        const auto value = owner();
        return value && value->supported();
    }
    static uint8_t volumeHook()
    {
        const auto value = owner();
        return value ? uint8_t(value->volume.load()) : 0;
    }
    static void volumeHook(uint8_t volume)
    {
        if (const auto value = owner()) value->volume = std::min<unsigned>(100, volume);
    }
    static bool reserveHook(const uint8_t* link)
    {
        const auto value = owner();
        return value && value->reserve(link);
    }
    static bool ringHook(const uint8_t* link)
    {
        const auto value = owner();
        if (!value) return false;
        std::lock_guard<std::mutex> lock(value->mutex);
        if (!value->focus || !equal(value->link.data(), link) || value->stopping) return false;
        value->ringing = true;
        value->wake.notify_one();
        return true;
    }
    static bool startHook()
    {
        const auto value = owner();
        return value && value->start();
    }
    static void stopHook()
    {
        if (const auto value = owner()) value->stop(false);
    }
    static void closingHook(const uint8_t* link, bool keep)
    {
        if (const auto value = owner()) value->end(link, !keep);
    }
    static void endHook(const uint8_t* link)
    {
        if (const auto value = owner()) value->end(link, true);
    }
    static bool install(const std::shared_ptr<Impl>& value)
    {
        auto& r = registry();
        std::lock_guard<std::mutex> lifecycle(r.lifecycle);
        if (call::modal_active()) return false;
        {
            std::lock_guard<std::mutex> lock(r.lookup);
            if (!r.active.expired()) return false;
            r.active = value;
        }
        call::MediaHooks media;
        media.is_supported = supportedHook;
        media.start = startHook;
        media.stop = stopHook;
        media.speaker_volume = static_cast<uint8_t (*)()>(volumeHook);
        media.set_speaker_volume = static_cast<void (*)(uint8_t)>(volumeHook);
        call::set_media_hooks(media);
        call::RealtimeHooks realtime;
        realtime.begin_soft_preempt = reserveHook;
        realtime.begin_exclusive = reserveHook;
        realtime.begin_ringing_alert = ringHook;
        realtime.begin_closing = closingHook;
        realtime.end = endHook;
        call::set_realtime_hooks(realtime);
        std::lock_guard<std::mutex> lock(value->mutex);
        value->published.installed = true;
        return true;
    }
    static void uninstall(const std::shared_ptr<Impl>& value)
    {
        auto& r = registry();
        std::lock_guard<std::mutex> lifecycle(r.lifecycle);
        {
            std::lock_guard<std::mutex> lock(r.lookup);
            if (r.active.lock() != value) return;
        }
        const auto current = call::snapshot();
        if (nonzero(current.link_id)) call::notify_link_closed(current.link_id);
        {
            std::lock_guard<std::mutex> lock(r.lookup);
            r.active.reset();
        }
        call::set_media_hooks({});
        call::set_realtime_hooks({});
        call::service_ui_runtime();
        std::lock_guard<std::mutex> lock(value->mutex);
        value->published.installed = false;
    }
    bool supported() const
    {
        if (stopping) return false;
        try
        {
            if (options.available) return options.available();
            const auto audio = native_audio_snapshot();
            return audio.initialized && audio.capture_available && audio.playback_available;
        }
        catch (...)
        {
            return false;
        }
    }
    bool reserve(const uint8_t* requested)
    {
        if (!nonzero(requested) || !supported()) return false;
        uint64_t expected;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping || reserving) return false;
            if (focus) return equal(link.data(), requested) && !release_focus;
            reserving = true;
            expected = ++generation;
            std::memcpy(link.data(), requested, link.size());
        }
        auto token = ::platform::audio::request_exclusive_focus(::platform::audio::FocusOwner::Call);
        bool accepted = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            reserving = false;
            if (token && !stopping && expected == generation && equal(link.data(), requested))
            {
                try
                {
                    if (!worker.joinable()) worker = std::thread([this]
                                                                 { run(); });
                    focus = token;
                    release_focus = false;
                    stop_requested = false;
                    accepted = true;
                }
                catch (...)
                {
                }
            }
        }
        if (!accepted) ::platform::audio::release_focus(token);
        wake.notify_one();
        return accepted;
    }
    bool start()
    {
        const auto snapshot = call::snapshot();
        std::unique_lock<std::mutex> lock(mutex);
        if (stopping || !focus || !equal(link.data(), snapshot.link_id) || release_focus) return false;
        const auto expected = ++generation;
        requested = snapshot;
        published.statistics = {};
        start_requested = true;
        stop_requested = false;
        ringing = false;
        wake.notify_one();
        const auto timeout = std::chrono::milliseconds(std::clamp<uint32_t>(options.start_timeout_ms, 1, 5000));
        const bool completed = wake.wait_for(lock, timeout, [&]
                                             { return stopping || generation != expected || completed_generation == expected; });
        if (!completed || generation != expected || stopping)
        {
            if (generation == expected)
            {
                if (!completed && !stopping) ++published.failed;
                ++generation;
                stop_requested = true;
                start_requested = false;
            }
            wake.notify_one();
            return false;
        }
        return start_ok;
    }
    void stop(bool release)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++generation;
        stop_requested = true;
        start_requested = false;
        ringing = false;
        release_focus = release_focus || release;
        wake.notify_one();
    }
    void end(const uint8_t* expected, bool release)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!expected || !equal(link.data(), expected)) return;
        ++generation;
        stop_requested = true;
        start_requested = false;
        ringing = false;
        release_focus = release_focus || release;
        wake.notify_one();
    }
    void shutdown()
    {
        std::lock_guard<std::mutex> lifecycle(shutdown_mutex);
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            ++generation;
            stop_requested = true;
            release_focus = true;
        }
        wake.notify_one();
        if (worker.joinable()) worker.join();
    }
    void run()
    {
        struct Cleanup
        {
            ~Cleanup() { SDL_TLSCleanup(); }
        } cleanup;
        std::unique_ptr<ICallPcm> pcm;
        std::unique_ptr<CallAudioSession> session;
        std::unique_ptr<SdlPcmOutput> alert;
        Packets packets;
        ::platform::ui::audio::call_notification::AdpcmPlaybackState tone;
        std::array<int16_t, 256> ring_samples;
        uint32_t next_alert = 0;
        std::array<uint8_t, call::kHashSize> active_link{};
        bool active = false;
        for (;;)
        {
            call::Snapshot pending;
            uint64_t expected = 0;
            bool start_now = false, stop_now = false, ring = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (!active && !start_requested && !stop_requested && !ringing && !stopping)
                    wake.wait(lock, [&]
                              { return stopping || start_requested || stop_requested || ringing; });
                stop_now = stop_requested || stopping;
                if (stop_now) stop_requested = false;
                if (start_requested && !stopping)
                {
                    start_now = true;
                    start_requested = false;
                    pending = requested;
                    expected = generation;
                }
                ring = ringing && bool(focus) && !release_focus;
            }
            if (stop_now)
            {
                alert.reset();
                if (session) session->end();
                active = false;
                ::platform::audio::FocusToken token;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    published.running = false;
                    if (release_focus || stopping)
                    {
                        token = focus;
                        focus = {};
                        link.fill(0);
                        release_focus = false;
                    }
                }
                ::platform::audio::release_focus(token);
            }
            if (stopping) break;
            if (start_now)
            {
                alert.reset();
                bool opened = false;
                try
                {
                    if (!pcm) pcm = options.make_pcm ? options.make_pcm() : std::make_unique<Pcm>();
                    if (pcm && !session) session = std::make_unique<CallAudioSession>(*pcm, packets);
                    opened = session && session->begin(pending, now_ms());
                }
                catch (...)
                {
                    if (session) session->end();
                    else if (pcm) pcm->close();
                }
                const auto current = call::snapshot();
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    active = opened && !stopping && generation == expected && equal(current.link_id, pending.link_id) && current.accepted && current.media_active;
                    completed_generation = expected;
                    start_ok = active;
                    published.running = active;
                    if (active) ++published.started;
                    else if (generation == expected && !stopping) ++published.failed;
                }
                if (active) std::memcpy(active_link.data(), pending.link_id, active_link.size());
                if (!active && session) session->end();
                wake.notify_all();
            }
            const auto controls = call::snapshot();
            if (active)
            {
                bool ok = false;
                try
                {
                    ok = session->step(controls, now_ms(), uint8_t(volume.load()));
                }
                catch (...)
                {
                }
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    published.statistics = session->statistics();
                }
                if (!ok)
                {
                    session->end();
                    active = false;
                    const bool failure = controls.accepted && controls.link_active && controls.media_active && equal(controls.link_id, active_link.data());
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        published.running = false;
                        if (failure) ++published.failed;
                    }
                    if (failure) call::notify_media_failed_for_link(active_link.data());
                }
            }
            else if (ring && options.ring_alert && controls.realtime_phase == call::RealtimePhase::IncomingRinging && !controls.speaker_muted)
            {
                try
                {
                    const auto now = now_ms();
                    if (!alert && int32_t(now - next_alert) >= 0)
                    {
                        alert = std::make_unique<SdlPcmOutput>(false, nullptr, 256);
                        if (!alert->begin(::platform::ui::audio::call_notification::kPlaybackSampleRateHz, std::getenv("TRAIL_MATE_CALL_OUTPUT_DEVICE"))) alert.reset();
                        tone = {};
                        next_alert = now + 2000;
                    }
                    if (alert && alert->pending_ms() < 40)
                    {
                        size_t count = 0;
                        int16_t sample;
                        while (count < ring_samples.size() && ::platform::ui::audio::call_notification::nextPlaybackSample(tone, sample))
                            ring_samples[count++] = int16_t(int32_t(sample) * volume.load() / 100);
                        if (count)
                        {
                            if (!alert->write(ring_samples.data(), count)) alert.reset();
                        }
                        else if (alert->drain()) alert.reset();
                    }
                }
                catch (...)
                {
                    alert.reset();
                }
            }
            else alert.reset();
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock, std::chrono::milliseconds(2), [&]
                          { return stopping || stop_requested || start_requested; });
        }
        // Shutdown can arrive after the loop copied its stop flags. Close the
        // device owner before releasing focus on every exit, not just that tick.
        alert.reset();
        if (session) session->end();
        ::platform::audio::FocusToken token;
        {
            std::lock_guard<std::mutex> lock(mutex);
            token = focus;
            focus = {};
            link.fill(0);
            published.running = false;
        }
        ::platform::audio::release_focus(token);
    }
    CallAudioOptions options;
    mutable std::mutex mutex;
    std::mutex shutdown_mutex;
    std::condition_variable wake;
    std::thread worker;
    std::atomic_bool stopping{false};
    std::atomic<unsigned> volume{100};
    ::platform::audio::FocusToken focus;
    std::array<uint8_t, call::kHashSize> link{};
    call::Snapshot requested;
    uint64_t generation = 0, completed_generation = 0;
    bool reserving = false, release_focus = false, start_requested = false, stop_requested = false, ringing = false, start_ok = false;
    CallAudioSnapshot published;
};
NativeCallAudioBinding::NativeCallAudioBinding(CallAudioOptions options) : impl_(std::make_shared<Impl>(std::move(options)))
{
    if (!impl_->options.available) prepare_native_audio();
    Impl::install(impl_);
}
NativeCallAudioBinding::~NativeCallAudioBinding() { shutdown(); }
void NativeCallAudioBinding::shutdown()
{
    Impl::uninstall(impl_);
    impl_->shutdown();
}
CallAudioSnapshot NativeCallAudioBinding::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto value = impl_->published;
    value.supported = value.installed && impl_->supported();
    return value;
}
} // namespace platform::linux_runtime
#else
namespace platform::linux_runtime
{
struct NativeCallAudioBinding::Impl
{
};
NativeCallAudioBinding::NativeCallAudioBinding(CallAudioOptions) : impl_(std::make_shared<Impl>()) {}
NativeCallAudioBinding::~NativeCallAudioBinding() = default;
void NativeCallAudioBinding::shutdown() {}
CallAudioSnapshot NativeCallAudioBinding::snapshot() const { return {}; }
} // namespace platform::linux_runtime
#endif
