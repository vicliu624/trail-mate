#include "platform/audio/audio_focus.h"
#include "platform/linux/fm_session.h"
#include "platform/linux/packet_radio.h"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

using namespace platform::linux_runtime;
using namespace std::chrono_literals;
static std::atomic_bool handoff_ok{true};
struct Radio final : IPacketRadio
{
    std::atomic<unsigned> acquisitions{0}, releases{0}, streams{0};
    std::atomic<uint32_t> hz{0};
    std::atomic_bool gap{false}, recovery_failure{false}, hold_restore{false}, restoring{false};
    const char* name() const override { return "TEST FIXTURE - NO RF"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        ++acquisitions;
        return true;
    }
    void release() override { ++releases; }
    bool releaseWithStatus() override
    {
        restoring = true;
        while (hold_restore.load()) std::this_thread::sleep_for(1ms);
        release();
        restoring = false;
        return !recovery_failure.load();
    }
    bool configureLoRa(const RadioSettings&) override { return true; }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t*, size_t) override
    {
        assert(false);
        return false;
    }
    bool pollReceive(RadioPacket*) override { return false; }
    float readRssi() override { return -100; }
    bool isOnline() const override { return true; }
    const char* lastError() const override { return "injected test failure"; }
    RadioSettings appliedLoRaConfig() const override { return {}; }
    RadioStatistics stats() const override { return {}; }
    bool supportsFmIq() const override { return true; }
    bool streamFmIq(const FmIqRequest& request, FmIqResult& result) override
    {
        ++streams;
        hz = request.frequency_hz;
        result = {};
        result.setup_attempted = true;
        result.setup.calibration_warning = request.allow_image_calibration_warning;
        result.setup.chip_errors = request.allow_image_calibration_warning ? 0x80 : 0;
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
            first += 640 + (gap.load() ? 1 : 0);
            std::this_thread::sleep_for(3ms);
        }
        result.cancelled = request.cancelled(request.context);
        result.nominal_rate_hz = 250000;
        return true;
    }
};
template <typename Predicate>
static void await(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate())
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(1ms);
    }
}
int main()
{
    Radio radio;
    FmSession fm(radio, []
                 { return handoff_ok.load(); });
    assert(!fm.tune(99999999) && !radio.acquisitions);
    assert(fm.tune(100800000));
    await([&]
          { return fm.snapshot().pcm_samples > 0; });
    auto first = fm.snapshot();
    assert(first.phase == FmSession::Phase::PcmQueued && first.active);
    assert(platform::audio::music_blocked());
    radio.hold_restore = true;
    assert(fm.tune(101000000, true));
    await([&]
          { return radio.restoring.load(); });
    auto query = std::chrono::steady_clock::now();
    (void)fm.snapshot();
    fm.setVolume(80);
    assert(fm.tune(101200000, true)); // Latest station wins while restoring.
    assert(std::chrono::steady_clock::now() - query < 100ms);
    radio.hold_restore = false;
    await([&]
          { return radio.hz == 101200000 && fm.snapshot().pcm_samples > 0; });
    assert(fm.snapshot().session > first.session);
    assert(fm.snapshot().calibration_exception_requested);
    fm.requestStop();
    fm.waitUntilFinished();
    auto final = fm.snapshot();
    assert(final.phase == FmSession::Phase::Stopped && !final.active);
    assert(final.radio.setup.calibration_warning && final.radio.setup.chip_errors == 0x80);
    assert(radio.acquisitions == radio.releases && !platform::audio::music_blocked());
    radio.gap = true;
    assert(fm.tune(100800000));
    fm.waitUntilFinished();
    assert(fm.snapshot().error == FmSession::Error::InvalidIq);
    radio.gap = false;
    handoff_ok = false;
    const auto acquired = radio.acquisitions.load();
    assert(fm.tune(100800000));
    fm.waitUntilFinished();
    assert(fm.snapshot().error == FmSession::Error::Handoff && radio.acquisitions == acquired);
    handoff_ok = true;
    radio.recovery_failure = true;
    assert(fm.tune(100800000));
    await([&]
          { return fm.snapshot().pcm_samples > 0; });
    fm.requestStop();
    fm.waitUntilFinished();
    assert(fm.snapshot().error == FmSession::Error::Restore && fm.snapshot().lease_restore_failed);
    assert(radio.acquisitions == radio.releases && !platform::audio::music_blocked());
    radio.recovery_failure = false;
    // Stop a pending request both before and after an earlier completed session.
    for (unsigned n = 0; n < 30; ++n)
    {
        assert(fm.tune(100800000));
        fm.requestStop();
        fm.waitUntilFinished();
        assert(!fm.snapshot().active);
    }
}
