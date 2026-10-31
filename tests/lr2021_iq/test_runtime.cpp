#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/idf_common/lr2021_radio.h"
#include "platform/ui/radio_iq_runtime.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace iq = platform::ui::radio_iq;
namespace
{
void (*pending)(void*) = nullptr;
bool allocation_ok = true, acquire_ok = true, capture_ok = true, online = true, paused = false;
bool fail_mesh_restore = false;
size_t capture_bytes = 1024;
unsigned acquisitions = 0, releases = 0, captures = 0, deleted = 0;
} // namespace
int xTaskCreate(void (*entry)(void*), const char*, unsigned stack, void*, unsigned, void*)
{
    assert(stack >= 4096 && !pending);
    if (!allocation_ok) return 0;
    pending = entry;
    return 1;
}
void vTaskDelete(void*) { ++deleted; }
void vTaskDelay(unsigned)
{
    assert(pending);
    auto entry = pending;
    pending = nullptr;
    entry(nullptr);
}
bool app::AppTasks::areRadioTasksPaused() { return paused; }
namespace platform::ui::lora
{
bool acquire()
{
    ++acquisitions;
    return acquire_ok;
}
void release()
{
    assert(iq::snapshot().phase == iq::Phase::Capturing);
    ++releases;
    if (fail_mesh_restore) online = false;
}
} // namespace platform::ui::lora
namespace platform::esp::idf_common
{
Lr2021Radio& Lr2021Radio::instance()
{
    static Lr2021Radio radio;
    return radio;
}
bool Lr2021Radio::isOnline() const { return online; }
const char* Lr2021Radio::lastError() const { return "Injected capture failure"; }
bool Lr2021Radio::captureIq(uint32_t* words, uint32_t* scratch, size_t count, IqCaptureResult& result)
{
    assert(words && scratch && words != scratch && count == 256);
    ++captures;
    result = {};
    if (!capture_ok)
    {
        result.capture_error = -4;
        return false;
    }
    result.bytes = capture_bytes;
    result.elapsed_us = 3000;
    for (size_t n = 0; n < count; ++n) words[n] = 0x00004000; // I=0.5, Q=0.
    return true;
}
} // namespace platform::esp::idf_common
int main()
{
    assert(iq::is_supported());
    assert(iq::snapshot().phase == iq::Phase::Idle);
    assert(iq::start());
    assert(!iq::start()); // One worker and one buffer owner.
    assert(acquisitions == 0 && iq::snapshot().phase == iq::Phase::Capturing);
    iq::wait_until_finished(); // Joins work before next-page ownership.
    auto result = iq::snapshot();
    assert(result.phase == iq::Phase::Ready && releases == 1 && captures == 1);
    assert(result.pairs == 256 && result.nominal_rate_hz == 0 && result.clipped_pairs == 0);
    assert(std::abs(result.rms - 0.5f) < 1e-6f);
    assert(std::abs(result.dc_i - 0.5f) < 1e-6f && result.dc_q == 0);
    assert(result.elapsed_us == 3000);

    capture_ok = false;
    assert(iq::start());
    iq::wait_until_finished();
    result = iq::snapshot();
    assert(result.phase == iq::Phase::Error && result.pairs == 0 && result.capture_error == -4);
    assert(std::strstr(result.error, "Injected"));
    assert(releases == 2);

    capture_ok = true;
    fail_mesh_restore = true;
    assert(iq::start());
    iq::wait_until_finished();
    result = iq::snapshot();
    assert(result.phase == iq::Phase::Error && result.pairs == 0);
    assert(std::strstr(result.error, "mesh restore"));
    fail_mesh_restore = false;
    online = true;

    capture_bytes = 512;
    assert(iq::start());
    iq::wait_until_finished();
    result = iq::snapshot();
    assert(result.phase == iq::Phase::Error && result.pairs == 0);
    assert(std::strstr(result.error, "Incomplete"));
    capture_bytes = 1024;

    acquire_ok = false;
    const auto captured = captures;
    const auto released = releases;
    assert(iq::start());
    iq::wait_until_finished();
    assert(iq::snapshot().phase == iq::Phase::Error);
    assert(captures == captured && releases == released);

    paused = true;
    const auto acquired = acquisitions;
    assert(iq::start());
    iq::wait_until_finished();
    assert(iq::snapshot().phase == iq::Phase::Error);
    assert(acquisitions == acquired && releases == released);
    paused = false;
    allocation_ok = false;
    assert(!iq::start());
    assert(iq::snapshot().phase == iq::Phase::Error && !pending);
    assert(deleted == 6);
    std::puts("IQ runtime: one worker, actual sample metrics, cleanup-before-ready, failure invalidation, ownership rejection and exit join passed");
}
