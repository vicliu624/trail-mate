#include "bsp/trail_mate_t_display_p4_runtime.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/idf_common/lr2021_radio.h"
#include "platform/shared/radio/fm_iq_queue.h"
#include "platform/shared/radio/fm_iq_stream.h"
#include "platform/ui/fm_runtime.h"
#include "platform/ui/lora_runtime.h"
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <new>

namespace platform::ui::fm
{
namespace
{
using namespace platform::shared::radio;
using Radio = platform::esp::idf_common::Lr2021Radio;
constexpr auto kAudioOwner = TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_FM;
struct State
{
    std::mutex mutex;
    Snapshot state;
    std::atomic_bool cancel{false}, producer_done{false};
    std::atomic<Error> fault{Error::None};
    std::atomic<unsigned> volume{50};
    StaticSemaphore_t done_storage{};
    SemaphoreHandle_t consumer_done = nullptr;
    std::unique_ptr<FmIqQueue> queue;
    FmIqQueue::Block block;
    FmIqConsumer decoder;
    std::array<uint8_t, 1280> staging{};
    std::array<int16_t, FmIqConsumer::kMaxPcmSamples> pcm{};
    FmIqResult result;
    uint64_t session = 0, next_pair = 0;
    bool cleanup_only = false;
};
// All protocol/DSP/byte buffers remain in persistent storage, never task locals.
State s;
void fail(Error error)
{
    Error expected = Error::None;
    s.fault.compare_exchange_strong(expected, error);
    s.cancel.store(true, std::memory_order_release);
}
bool accept(void*, const uint8_t* data, size_t bytes, uint64_t first)
{
    if (s.cancel.load(std::memory_order_acquire)) return false;
    if (first != s.next_pair)
    {
        fail(Error::InvalidIq);
        return false;
    }
    const auto result = s.queue->push(s.session, data, bytes, 250000, 1, first);
    if (result != FmIqQueue::PushResult::Accepted)
    {
        fail(result == FmIqQueue::PushResult::Full ? Error::QueueFull : Error::InvalidIq);
        return false;
    }
    s.next_pair += bytes / 2;
    return true;
}
void consume(void*)
{
    bool connected = false;
    while (!s.cancel.load(std::memory_order_acquire))
    {
        if (!s.queue->pop(s.block))
        {
            if (s.producer_done.load(std::memory_order_acquire)) break;
            vTaskDelay(1);
            continue;
        }
        const auto result = s.decoder.consume(s.block.data, s.block.bytes, s.block.rate,
                                              s.block.generation, s.block.first_pair,
                                              s.pcm.data(), s.pcm.size(), s.block.format);
        if (s.block.session != s.session ||
            (result.status != FmIqConsumer::Status::Accepted &&
             !(result.status == FmIqConsumer::Status::GapReset && !connected)))
        {
            fail(Error::InvalidIq);
            break;
        }
        connected = true;
        const auto volume = s.volume.load(std::memory_order_relaxed);
        for (size_t n = 0; n < result.samples; ++n)
            s.pcm[n] = static_cast<int16_t>(static_cast<int32_t>(s.pcm[n]) * volume / 100);
        if (s.cancel.load(std::memory_order_acquire)) break;
        if (result.samples && !trail_mate_t_display_p4_audio_write_mono(kAudioOwner, s.pcm.data(), result.samples))
        {
            if (!s.cancel.load()) fail(Error::Audio);
            break;
        }
        std::lock_guard<std::mutex> lock(s.mutex);
        s.state.processed_pairs = s.block.first_pair + s.block.bytes / 2;
        s.state.pcm_samples += result.samples;
        if (result.samples && s.state.phase == Phase::Starting) s.state.phase = Phase::PcmSubmitted;
    }
    // No queue, decoder, codec or State access after signaling completion.
    xSemaphoreGive(s.consumer_done);
    vTaskDelete(nullptr);
}
bool close_audio()
{
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        if (trail_mate_t_display_p4_audio_end_checked(kAudioOwner)) return true;
        vTaskDelay(1);
    }
    return false;
}
void run(void*)
{
    uint32_t hz;
    bool exception, cleanup_only;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        hz = s.state.frequency_hz;
        exception = s.state.calibration_exception_requested;
        cleanup_only = s.cleanup_only;
    }
    bool acquired = false, audio_attempted = cleanup_only, consumer_started = false;
    bool cleanup_failed = false;
    s.result = {};
    if (!cleanup_only)
    {
        s.queue.reset(new (std::nothrow) FmIqQueue);
        s.decoder.reset();
        s.next_pair = 0;
        if (!s.queue) fail(Error::Memory);
        if (!s.cancel.load())
        {
            if (::app::AppTasks::areRadioTasksPaused() || !(acquired = lora::acquire())) fail(Error::RadioBusy);
        }
        if (!s.cancel.load())
        {
            audio_attempted = true;
            if (!trail_mate_t_display_p4_audio_begin(kAudioOwner, 48000)) fail(Error::Audio);
        }
        if (!s.cancel.load())
        {
            consumer_started = xTaskCreatePinnedToCore(consume, "fm_dsp", 6144, nullptr, 4, nullptr, 0) == pdPASS;
            if (!consumer_started) fail(Error::Task);
        }
        if (!s.cancel.load())
        {
            FmIqRequest request;
            request.frequency_hz = hz;
            request.allow_image_calibration_warning = exception;
            request.staging = s.staging.data();
            request.staging_bytes = s.staging.size();
            request.accept = accept;
            request.cancelled = [](void*)
            { return s.cancel.load(std::memory_order_acquire); };
            const bool ok = Radio::instance().streamFmIq(request, s.result);
            if (s.result.recovery_error) fail(Error::Restore);
            else if (!ok || s.result.capture_error || s.result.sink_rejected || !s.cancel.load()) fail(Error::Stream);
        }
    }
    s.producer_done.store(true, std::memory_order_release);
    if (consumer_started) xSemaphoreTake(s.consumer_done, portMAX_DELAY);
    s.queue.reset();
    if (audio_attempted && !close_audio())
    {
        cleanup_failed = true;
        fail(Error::Cleanup);
    }
    if (acquired)
    {
        lora::release();
        if (!Radio::instance().isOnline()) fail(Error::Restore);
    }
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!cleanup_only)
        {
            s.state.capture_error = s.result.capture_error;
            s.state.recovery_error = s.result.recovery_error;
            s.state.chip_flags_valid = s.result.setup_attempted &&
                                       (s.result.setup.error == FmLr2021Rx::Error::None || s.result.setup.error == FmLr2021Rx::Error::ChipErrors);
            s.state.chip_flags = s.result.setup.chip_errors;
        }
        s.state.cleanup_failed = cleanup_failed;
        s.state.error = s.fault.load();
        s.state.phase = s.state.error == Error::None ? Phase::Stopped : Phase::Error;
        s.state.active = false;
    }
    vTaskDelete(nullptr);
}
// Called only under the short control mutex. No hardware I/O or joining here.
bool launch(bool cleanup_only)
{
    if (!s.consumer_done) s.consumer_done = xSemaphoreCreateBinaryStatic(&s.done_storage);
    if (!s.consumer_done)
    {
        s.state.phase = Phase::Error;
        s.state.error = Error::Memory;
        return false;
    }
    while (xSemaphoreTake(s.consumer_done, 0) == pdTRUE)
    {
    }
    s.cleanup_only = cleanup_only;
    s.cancel = cleanup_only;
    s.producer_done = false;
    s.fault = Error::None;
    s.state.active = true;
    s.state.phase = cleanup_only ? Phase::Stopping : Phase::Starting;
    if (xTaskCreatePinnedToCore(run, "fm_rx", 6144, nullptr, 5, nullptr, 1) == pdPASS) return true;
    s.state.active = false;
    s.state.phase = Phase::Error;
    s.state.error = Error::Task;
    return false;
}
} // namespace
bool is_supported() { return true; }
bool start(uint32_t hz, bool exception)
{
    if (hz < 100000000 || hz > 108000000) return false;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.state.active || s.state.cleanup_failed) return false;
    s.state = {};
    s.state.frequency_hz = hz;
    s.state.calibration_exception_requested = exception;
    if (!++s.session) ++s.session;
    s.state.session = s.session;
    return launch(false);
}
void request_stop()
{
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.state.active)
    {
        s.state.phase = Phase::Stopping;
        s.cancel.store(true, std::memory_order_release);
    }
    else if (s.state.cleanup_failed) (void)launch(true);
}
void set_volume(unsigned percent) { s.volume.store(std::min(percent, 100u), std::memory_order_relaxed); }
Snapshot snapshot()
{
    std::lock_guard<std::mutex> lock(s.mutex);
    auto result = s.state;
    result.volume = static_cast<uint8_t>(s.volume.load(std::memory_order_relaxed));
    if (result.active && s.fault.load() != Error::None)
    {
        result.error = s.fault.load();
        result.phase = Phase::Stopping;
    }
    return result;
}
void wait_until_finished()
{
    while (snapshot().active) vTaskDelay(1);
}
} // namespace platform::ui::fm
