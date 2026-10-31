#include "bsp/trail_mate_t_display_p4_runtime.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/idf_common/lr2021_radio.h"
#include "platform/shared/radio/fm_iq_stream.h"
#include "platform/ui/fm_runtime.h"
#include "platform/ui/lora_runtime.h"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace fm = platform::ui::fm;
namespace fixture
{
std::mutex tasks_mutex;
std::vector<std::thread> tasks;
std::atomic<unsigned> task_calls{0}, fail_task_on{0}, acquired{0}, released{0}, streams{0};
std::atomic<uint64_t> written{0};
std::atomic_bool lease{false}, paused{false}, radio_busy{false}, audio_busy{false}, audio_open{false};
std::atomic_bool audio_fail{false}, close_fail{false}, gap{false}, capture_fail{false}, recovery_fail{false};
std::atomic_bool hold_return{false}, returning{false};
std::atomic_bool hold_audio{false}, writing_audio{false};
void join()
{
    std::vector<std::thread> finished;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex);
        finished.swap(tasks);
    }
    for (auto& thread : finished) thread.join();
}
template <typename Predicate>
void await(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate())
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(1ms);
    }
}
void done()
{
    fm::wait_until_finished();
    join();
    assert(!fm::snapshot().active);
}
void stop()
{
    fm::request_stop();
    done();
}
void playing()
{
    await([]
          { return fm::snapshot().phase == fm::Phase::PcmSubmitted; });
}
} // namespace fixture
int xTaskCreatePinnedToCore(void (*entry)(void*), const char*, unsigned stack, void* context,
                            unsigned priority, TaskHandle_t*, int core)
{
    assert(stack == 6144 && ((priority == 5 && core == 1) || (priority == 4 && core == 0)));
    if (++fixture::task_calls == fixture::fail_task_on) return 0;
    std::lock_guard<std::mutex> lock(fixture::tasks_mutex);
    fixture::tasks.emplace_back([=]
                                { entry(context); });
    return pdPASS;
}
void vTaskDelete(void*) {}
void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks * 10)); }
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage) { return storage; }
int xSemaphoreGive(SemaphoreHandle_t sem)
{
    {
        std::lock_guard<std::mutex> lock(sem->mutex);
        sem->token = true;
    }
    sem->wake.notify_one();
    return pdTRUE;
}
int xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{
    std::unique_lock<std::mutex> lock(sem->mutex);
    if (!ticks && !sem->token) return 0;
    if (ticks == portMAX_DELAY) sem->wake.wait(lock, [&]
                                               { return sem->token; });
    else if (!sem->wake.wait_for(lock, std::chrono::milliseconds(ticks * 10), [&]
                                 { return sem->token; })) return 0;
    sem->token = false;
    return pdTRUE;
}
bool app::AppTasks::areRadioTasksPaused() { return fixture::paused; }
namespace platform::ui::lora
{
bool acquire()
{
    if (fixture::radio_busy || fixture::lease.exchange(true)) return false;
    fixture::paused = true;
    ++fixture::acquired;
    return true;
}
void release()
{
    assert(fixture::lease.exchange(false));
    fixture::paused = false;
    ++fixture::released;
}
} // namespace platform::ui::lora
namespace platform::esp::idf_common
{
Lr2021Radio& Lr2021Radio::instance()
{
    static Lr2021Radio radio;
    return radio;
}
bool Lr2021Radio::isOnline() const { return !fixture::recovery_fail; }
bool Lr2021Radio::streamFmIq(const platform::shared::radio::FmIqRequest& request,
                             platform::shared::radio::FmIqResult& result)
{
    assert(fixture::lease && fixture::paused && request.staging && request.staging_bytes >= 1280);
    ++fixture::streams;
    result = {};
    result.setup_attempted = true;
    result.setup.chip_errors = request.allow_image_calibration_warning ? 0x80 : 0;
    if (fixture::capture_fail)
    {
        result.capture_error = -5;
        return false;
    }
    std::array<uint8_t, 1280> iq{};
    for (size_t n = 0; n < iq.size(); n += 2) iq[n] = 31;
    uint64_t first = 0;
    while (!request.cancelled(request.context))
    {
        if (!request.accept(request.context, iq.data(), iq.size(), first))
        {
            result.sink_rejected = true;
            break;
        }
        result.validated_bytes += iq.size();
        first += 640 + (fixture::gap ? 1 : 0);
        std::this_thread::sleep_for(3ms);
    }
    fixture::returning = true;
    while (fixture::hold_return) std::this_thread::sleep_for(1ms);
    fixture::returning = false;
    result.cancelled = request.cancelled(request.context);
    result.recovery_error = fixture::recovery_fail ? -5 : 0;
    return !result.recovery_error;
}
} // namespace platform::esp::idf_common
bool trail_mate_t_display_p4_audio_begin(trail_mate_t_display_p4_audio_owner_t owner, uint32_t rate)
{
    assert(owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_FM && rate == 48000);
    if (fixture::audio_busy) return false;
    assert(!fixture::audio_open.exchange(true));
    return true;
}
bool trail_mate_t_display_p4_audio_write_mono(trail_mate_t_display_p4_audio_owner_t owner, const int16_t* data, size_t count)
{
    assert(owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_FM && fixture::audio_open && data && count);
    fixture::writing_audio = true;
    while (fixture::hold_audio) std::this_thread::sleep_for(1ms);
    fixture::writing_audio = false;
    if (fixture::audio_fail) return false;
    fixture::written += count;
    return true;
}
bool trail_mate_t_display_p4_audio_end_checked(trail_mate_t_display_p4_audio_owner_t owner)
{
    assert(owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_FM);
    if (fixture::close_fail && fixture::audio_open) return false;
    fixture::audio_open = false;
    return true;
}
int main()
{
    assert(fm::is_supported() && !fm::start(99999999) && !fixture::task_calls);
    fixture::fail_task_on = fixture::task_calls + 1;
    assert(!fm::start() && fm::snapshot().error == fm::Error::Task);
    fixture::fail_task_on = 0;
    fixture::radio_busy = true;
    assert(fm::start());
    fixture::done();
    assert(fm::snapshot().error == fm::Error::RadioBusy && !fixture::acquired);
    fixture::radio_busy = false;
    fixture::audio_busy = true;
    assert(fm::start());
    fixture::done();
    assert(fm::snapshot().error == fm::Error::Audio && !fm::snapshot().cleanup_failed);
    fixture::audio_busy = false;
    fixture::fail_task_on = fixture::task_calls + 2;
    assert(fm::start());
    fixture::done();
    assert(fm::snapshot().error == fm::Error::Task && !fixture::audio_open && !fixture::streams);
    fixture::fail_task_on = 0;
    assert(fm::start(100800000, true));
    fixture::playing();
    const auto session = fm::snapshot().session;
    assert(fm::snapshot().pcm_samples && fixture::written && fm::snapshot().calibration_exception_requested);
    fixture::hold_return = true;
    const auto before = std::chrono::steady_clock::now();
    fm::request_stop();
    fm::set_volume(80);
    (void)fm::snapshot();
    assert(std::chrono::steady_clock::now() - before < 100ms);
    fixture::await([]
                   { return fixture::returning.load(); });
    assert(fm::snapshot().active && fm::snapshot().phase == fm::Phase::Stopping);
    assert(!fm::start(101200000));
    fixture::hold_return = false;
    fixture::done();
    assert(fm::snapshot().error == fm::Error::None && fm::snapshot().chip_flags_valid && fm::snapshot().chip_flags == 0x80);
    assert(!fixture::audio_open && !fixture::lease && !fixture::paused);
    assert(fm::start(101200000));
    fixture::playing();
    assert(fm::snapshot().session > session && fm::snapshot().volume == 80);
    fixture::stop();
    fixture::gap = true;
    assert(fm::start());
    fixture::done();
    assert(fm::snapshot().error == fm::Error::InvalidIq);
    fixture::gap = false;
    fixture::audio_fail = true;
    assert(fm::start());
    fixture::done();
    assert(fm::snapshot().error == fm::Error::Audio);
    fixture::audio_fail = false;
    fixture::capture_fail = true;
    assert(fm::start());
    fixture::done();
    assert(fm::snapshot().error == fm::Error::Stream && fm::snapshot().capture_error == -5);
    fixture::capture_fail = false;
    fixture::recovery_fail = true;
    assert(fm::start());
    fixture::playing();
    fixture::stop();
    assert(fm::snapshot().error == fm::Error::Restore && fm::snapshot().recovery_error == -5);
    fixture::recovery_fail = false;
    fixture::hold_audio = true;
    assert(fm::start());
    fixture::await([]
                   { return fixture::writing_audio.load(); });
    fixture::await([]
                   { return fm::snapshot().error == fm::Error::QueueFull; });
    assert(fm::snapshot().active && fm::snapshot().phase == fm::Phase::Stopping);
    assert(fixture::audio_open && fixture::lease && !fm::start());
    fixture::hold_audio = false;
    fixture::done();
    assert(fm::snapshot().error == fm::Error::QueueFull && !fixture::audio_open && !fixture::lease);
    fixture::close_fail = true;
    assert(fm::start());
    fixture::playing();
    fixture::stop();
    assert(fm::snapshot().cleanup_failed && fm::snapshot().error == fm::Error::Cleanup && fixture::audio_open);
    assert(!fm::start());
    const auto acquisitions = fixture::acquired.load();
    fixture::close_fail = false;
    fm::request_stop();
    fixture::done();
    assert(!fm::snapshot().cleanup_failed && !fixture::audio_open && fixture::acquired == acquisitions);
    for (unsigned n = 0; n < 8; ++n)
    {
        assert(fm::start());
        fixture::stop();
    }
    assert(fixture::acquired == fixture::released && !fixture::lease && !fixture::audio_open && !fixture::paused);
}
