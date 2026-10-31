#include "platform/linux/packet_radio.h"
#include "platform/ui/lora_receive_session.h"
#include "platform/ui/radio_iq_runtime.h"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace iq = platform::ui::radio_iq;
namespace
{
struct Io
{
    std::mutex mutex;
    std::condition_variable wake;
    std::thread::id ui = std::this_thread::get_id();
    bool present = true, supported = true, acquire_ok = true, capture_ok = true, restore_ok = true, handoff_ok = true;
    bool block_handoff = true, handoff_entered = false, block_restore = true, restore_entered = false;
    size_t bytes = 1024;
    uint32_t word = 0x00004000;
    unsigned prepared = 0, acquired = 0, captured = 0, released = 0;
} io;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void offUi() { require(std::this_thread::get_id() != io.ui, "IQ hardware I/O reached UI"); }
template <typename Predicate>
void await(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!predicate())
    {
        require(std::chrono::steady_clock::now() < deadline, "IQ worker did not progress");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
template <typename Predicate>
auto inspect(Predicate predicate)
{
    std::lock_guard<std::mutex> lock(io.mutex);
    return predicate();
}
struct Radio final : platform::linux_runtime::IPacketRadio
{
    const char* name() const override { return "IQ fixture"; }
    bool hardwareCandidatePresent() const override
    {
        return inspect([]
                       { return io.present; });
    }
    bool supportsIqCapture() const override
    {
        return inspect([]
                       { return io.supported; });
    }
    bool acquire() override
    {
        offUi();
        std::lock_guard<std::mutex> lock(io.mutex);
        ++io.acquired;
        return io.acquire_ok;
    }
    void release() override { (void)releaseWithStatus(); }
    bool releaseWithStatus() override
    {
        offUi();
        std::unique_lock<std::mutex> lock(io.mutex);
        io.restore_entered = true;
        io.wake.wait(lock, []
                     { return !io.block_restore; });
        ++io.released;
        return io.restore_ok;
    }
    bool captureIq(uint32_t* words, uint32_t* scratch, size_t capacity, platform::linux_runtime::RadioIqCaptureResult& result) override
    {
        offUi();
        std::lock_guard<std::mutex> lock(io.mutex);
        require(words && scratch && words != scratch && capacity == 256, "invalid capture storage");
        ++io.captured;
        for (size_t n = 0; n < capacity; ++n) words[n] = io.word;
        result = {};
        result.bytes = io.bytes;
        if (!io.capture_ok) result.capture_error = -4;
        return io.capture_ok;
    }
    bool configureLoRa(const platform::linux_runtime::RadioSettings&) override { return true; }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t*, size_t) override { return false; }
    bool pollReceive(platform::linux_runtime::RadioPacket*) override { return false; }
    float readRssi() override { return -127; }
    bool isOnline() const override { return true; }
    const char* lastError() const override { return "Injected failure"; }
    platform::linux_runtime::RadioSettings appliedLoRaConfig() const override { return {}; }
    platform::linux_runtime::RadioStatistics stats() const override { return {}; }
} radio;
void finish(iq::Phase expected)
{
    iq::wait_until_finished();
    require(iq::snapshot().phase == expected, "unexpected IQ phase");
}
} // namespace
namespace platform::linux_runtime
{
IPacketRadio& foreground_packet_radio() { return radio; }
} // namespace platform::linux_runtime
namespace platform::ui::lora
{
namespace detail
{
void prepare_receive_session_worker() { ++io.prepared; }
} // namespace detail
bool wait_receive_session_released()
{
    offUi();
    std::unique_lock<std::mutex> lock(io.mutex);
    io.handoff_entered = true;
    io.wake.wait(lock, []
                 { return !io.block_handoff; });
    return io.handoff_ok;
}
} // namespace platform::ui::lora
int main()
{
    setenv("TRAIL_MATE_RUNTIME_MODE", "local", 1);
    unsetenv("TRAIL_MATE_LORA_SIMULATED");
    require(iq::is_supported(), "real-mode capability missing");
    setenv("TRAIL_MATE_RUNTIME_MODE", "demo", 1);
    require(!iq::is_supported(), "demo mode exposed IQ");
    setenv("TRAIL_MATE_RUNTIME_MODE", "local", 1);
    setenv("TRAIL_MATE_LORA_SIMULATED", "1", 1);
    require(!iq::is_supported(), "synthetic LoRa exposed IQ");
    unsetenv("TRAIL_MATE_LORA_SIMULATED");
    require(iq::start() && !iq::start(), "IQ admission is not single-owner");
    await([]
          { return inspect([]
                           { return io.handoff_entered; }); });
    require(inspect([]
                    { return !io.acquired; }),
            "IQ skipped asynchronous release barrier");
    auto joined = std::async(std::launch::async, []
                             { iq::wait_until_finished(); });
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.block_handoff = false;
        io.wake.notify_all();
    }
    await([]
          { return inspect([]
                           { return io.restore_entered; }); });
    const bool waited = joined.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    require(iq::snapshot().phase == iq::Phase::Capturing, "Ready preceded checked restoration");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.block_restore = false;
        io.wake.notify_all();
    }
    joined.get();
    require(waited, "page exit did not join restore");
    auto result = iq::snapshot();
    require(result.phase == iq::Phase::Ready && result.pairs == 256 && !result.nominal_rate_hz, "invalid successful capture metadata");
    require(std::abs(result.rms - 0.5f) < 1e-6f && result.dc_i == 0.5f && !result.dc_q, "wrong IQ metrics");
    require(inspect([]
                    { return io.prepared == 1 && io.acquired == 1 && io.released == 1; }),
            "owner lifecycle mismatch");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.word = 0xe000c000;
    }
    require(iq::start(), "negative-IQ capture rejected");
    finish(iq::Phase::Ready);
    result = iq::snapshot();
    require(result.dc_i == -0.5f && result.dc_q == -0.25f, "IQ sign extension failed");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.capture_ok = false;
    }
    require(iq::start(), "failure-case request rejected");
    finish(iq::Phase::Error);
    require(!iq::snapshot().pairs && iq::snapshot().capture_error == -4, "failed capture exposed samples");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.capture_ok = true;
        io.restore_ok = false;
    }
    require(iq::start(), "restore-case request rejected");
    finish(iq::Phase::Error);
    require(!iq::snapshot().pairs && iq::snapshot().recovery_error == -5, "failed restore exposed samples");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.restore_ok = true;
        io.bytes = 512;
    }
    require(iq::start(), "partial-case request rejected");
    finish(iq::Phase::Error);
    require(!iq::snapshot().pairs, "partial capture exposed samples");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.bytes = 1024;
        io.handoff_ok = false;
    }
    const auto acquired = inspect([]
                                  { return io.acquired; });
    require(iq::start(), "busy-case request rejected");
    finish(iq::Phase::Error);
    require(inspect([&]
                    { return io.acquired == acquired; }),
            "IQ stole another receiver");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.handoff_ok = true;
        io.acquire_ok = false;
    }
    const auto released = inspect([]
                                  { return io.released; });
    require(iq::start(), "acquire-case request rejected");
    finish(iq::Phase::Error);
    require(inspect([&]
                    { return io.released == released; }),
            "failed acquisition released another owner");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.present = false;
    }
    require(!iq::is_supported() && !iq::start(), "missing hardware exposed IQ");
    require(iq::snapshot().phase == iq::Phase::Error, "unsupported request retained old results");
}
