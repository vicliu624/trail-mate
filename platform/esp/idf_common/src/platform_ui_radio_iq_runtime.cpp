#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/idf_common/lr2021_radio.h"
#include "platform/ui/lora_runtime.h"
#include "platform/ui/radio_iq_runtime.h"
#include <cmath>
#include <cstdio>
#include <mutex>

namespace platform::ui::radio_iq
{
namespace
{
using Radio = platform::esp::idf_common::Lr2021Radio;
std::mutex s_mutex;
Snapshot s_snapshot;
// One worker owns both internal-memory buffers until recovery and release.
DMA_ATTR uint32_t s_words[256];
DMA_ATTR uint32_t s_scratch[256];
Radio::IqCaptureResult s_capture;

int32_t signed16(uint32_t word)
{
    const int32_t value = static_cast<int32_t>(word & 0xffffu);
    return value >= 32768 ? value - 65536 : value;
}
void publish(const Snapshot& value)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_snapshot = value;
}
void capture_task(void*)
{
    Snapshot report;
    report.phase = Phase::Error;
    if (::app::AppTasks::areRadioTasksPaused())
    {
        std::snprintf(report.error, sizeof(report.error), "Radio already owned by another operation");
        publish(report);
        vTaskDelete(nullptr);
        return;
    }
    if (!platform::ui::lora::acquire())
    {
        std::snprintf(report.error, sizeof(report.error), "Cannot acquire exclusive radio");
        publish(report);
        vTaskDelete(nullptr);
        return;
    }
    const bool captured = Radio::instance().captureIq(s_words, s_scratch, 256, s_capture);
    report.capture_error = s_capture.capture_error;
    report.restore_error = s_capture.restore_error;
    report.recovery_error = s_capture.recovery_error;
    report.elapsed_us = s_capture.elapsed_us;
    if (!captured) std::snprintf(report.error, sizeof(report.error), "%s", Radio::instance().lastError());
    // Restore the application's configured PHY and release paused tasks before
    // publishing Ready. No next-page radio owner can race this restoration.
    platform::ui::lora::release();
    if (captured && s_capture.bytes == sizeof(s_words) && Radio::instance().isOnline())
    {
        report.phase = Phase::Ready;
        report.pairs = static_cast<uint32_t>(s_capture.bytes / 4);
        report.nominal_rate_hz = s_capture.nominal_rate_hz;
        double sum_i = 0, sum_q = 0, power = 0;
        for (uint32_t n = 0; n < report.pairs; ++n)
        {
            const auto i = signed16(s_words[n]);
            const auto q = signed16(s_words[n] >> 16);
            sum_i += i;
            sum_q += q;
            power += static_cast<double>(i) * i + static_cast<double>(q) * q;
            if (i <= -32760 || i >= 32760 || q <= -32760 || q >= 32760) ++report.clipped_pairs;
        }
        if (report.pairs)
        {
            report.rms = static_cast<float>(std::sqrt(power / report.pairs) / 32768);
            report.dc_i = static_cast<float>(sum_i / report.pairs / 32768);
            report.dc_q = static_cast<float>(sum_q / report.pairs / 32768);
        }
    }
    else if (captured)
        std::snprintf(report.error, sizeof(report.error), "%s", s_capture.bytes != sizeof(s_words) ? "Incomplete IQ capture" : "Radio unavailable after mesh restore");
    publish(report);
    vTaskDelete(nullptr);
}
} // namespace

bool is_supported() { return true; }
Snapshot snapshot()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_snapshot;
}
bool start()
{
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_snapshot.phase == Phase::Capturing) return false;
        s_snapshot = {};
        s_snapshot.phase = Phase::Capturing;
    }
    if (xTaskCreate(capture_task, "probe_iq", 6144, nullptr, 4, nullptr) == pdPASS) return true;
    Snapshot failure;
    failure.phase = Phase::Error;
    std::snprintf(failure.error, sizeof(failure.error), "Cannot allocate IQ worker");
    publish(failure);
    return false;
}
void wait_until_finished()
{
    while (snapshot().phase == Phase::Capturing) vTaskDelay(1);
}
} // namespace platform::ui::radio_iq
