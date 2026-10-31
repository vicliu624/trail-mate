// The platform library itself is UI-profile neutral; this file supplies the
// optional port even when its target does not define the UI's Linux macro.
#ifndef TRAIL_MATE_EXTERNAL_RADIO_IQ_RUNTIME
#define TRAIL_MATE_EXTERNAL_RADIO_IQ_RUNTIME 1
#endif
#include "platform/ui/radio_iq_runtime.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/runtime_mode.h"
#include "platform/ui/lora_receive_session.h"
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace platform::ui::radio_iq
{
namespace
{
class IqWorker;
std::atomic<IqWorker*> instance{nullptr};
int32_t signed16(uint32_t word)
{
    const auto value = static_cast<int32_t>(word & 0xffffu);
    return value >= 32768 ? value - 65536 : value;
}
class IqWorker
{
  public:
    IqWorker()
    {
        // Establish lifetime order without touching hardware. This worker is
        // destroyed/joined before the receive owner and concrete radio owner.
        lora::detail::prepare_receive_session_worker();
        worker_ = std::thread([this]
                              { loop(); });
        instance = this;
    }
    ~IqWorker()
    {
        instance = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_one();
        worker_.join();
    }
    bool start()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || state_.phase == Phase::Capturing) return false;
        state_ = {};
        if (!is_supported())
        {
            state_.phase = Phase::Error;
            std::snprintf(state_.error, sizeof(state_.error), "Real IQ backend is unavailable");
            return false;
        }
        state_.phase = Phase::Capturing;
        pending_ = true;
        wake_.notify_one();
        return true;
    }
    Snapshot snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }
    void waitFinished()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [this]
                   { return state_.phase != Phase::Capturing; });
    }

  private:
    Snapshot capture()
    {
        using namespace platform::linux_runtime;
        Snapshot report;
        report.phase = Phase::Error;
        report.capture_error = static_cast<int>(IqCaptureError::State);
        if (!lora::wait_receive_session_released())
        {
            std::snprintf(report.error, sizeof(report.error), "Another receive session owns the radio");
            return report;
        }
        auto& radio = foreground_packet_radio();
        if (!radio.acquire())
        {
            std::snprintf(report.error, sizeof(report.error), "%s", radio.lastError());
            return report;
        }
        RadioIqCaptureResult result;
        const bool captured = radio.captureIq(words_.data(), scratch_.data(), words_.size(), result);
        report.capture_error = result.capture_error;
        report.restore_error = result.restore_error;
        report.recovery_error = result.recovery_error;
        report.elapsed_us = result.elapsed_us;
        if (!captured) std::snprintf(report.error, sizeof(report.error), "%s", radio.lastError());
        const bool restored = radio.releaseWithStatus();
        if (!restored)
        {
            if (!report.recovery_error) report.recovery_error = static_cast<int>(IqCaptureError::Io);
            std::snprintf(report.error, sizeof(report.error), "Radio restore failed: %s", radio.lastError());
            return report;
        }
        if (!captured || result.bytes != sizeof(words_))
        {
            if (!report.error[0]) std::snprintf(report.error, sizeof(report.error), "Incomplete or rejected IQ capture");
            return report;
        }
        report.phase = Phase::Ready;
        report.pairs = static_cast<uint32_t>(words_.size());
        report.nominal_rate_hz = result.nominal_rate_hz;
        double sum_i = 0, sum_q = 0, power = 0;
        for (const auto word : words_)
        {
            const auto i = signed16(word);
            const auto q = signed16(word >> 16);
            sum_i += i;
            sum_q += q;
            power += static_cast<double>(i) * i + static_cast<double>(q) * q;
            if (i <= -32760 || i >= 32760 || q <= -32760 || q >= 32760) ++report.clipped_pairs;
        }
        report.rms = static_cast<float>(std::sqrt(power / report.pairs) / 32768);
        report.dc_i = static_cast<float>(sum_i / report.pairs / 32768);
        report.dc_q = static_cast<float>(sum_q / report.pairs / 32768);
        return report;
    }
    void loop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;)
        {
            wake_.wait(lock, [this]
                       { return stopping_ || pending_; });
            if (stopping_ && !pending_) break;
            pending_ = false;
            lock.unlock();
            const auto report = capture();
            lock.lock();
            state_ = report;
            done_.notify_all();
        }
    }
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    bool stopping_ = false, pending_ = false;
    Snapshot state_;
    std::array<uint32_t, 256> words_{}, scratch_{};
    std::thread worker_;
};
IqWorker& worker()
{
    static IqWorker value;
    return value;
}
} // namespace
bool is_supported()
{
    using namespace platform::linux_runtime;
    if (resolve_runtime_mode() == LinuxRuntimeMode::SimulatorDemo) return false;
    const char* simulated = std::getenv("TRAIL_MATE_LORA_SIMULATED");
    if (simulated && (!std::strcmp(simulated, "1") || !std::strcmp(simulated, "true") ||
                      !std::strcmp(simulated, "TRUE") || !std::strcmp(simulated, "yes") || !std::strcmp(simulated, "YES"))) return false;
    auto& radio = foreground_packet_radio();
    return radio.supportsIqCapture() && radio.hardwareCandidatePresent();
}
bool start() { return worker().start(); }
Snapshot snapshot()
{
    auto* value = instance.load();
    return value ? value->snapshot() : Snapshot{};
}
void wait_until_finished()
{
    if (auto* value = instance.load()) value->waitFinished();
}
} // namespace platform::ui::radio_iq
