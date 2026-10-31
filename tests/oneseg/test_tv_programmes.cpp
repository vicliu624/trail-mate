#include "platform/audio/audio_focus.h"
#include "platform/linux/oneseg_tv_session.h"
#include "platform/linux/packet_radio.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <thread>

using namespace platform::linux_runtime;
using Session = OneSegTvSession;
using Clock = std::chrono::steady_clock;
struct SyntheticRadioFixture final : IPacketRadio
{
    const std::thread::id ui = std::this_thread::get_id();
    std::atomic_uint acquired{0}, restored{0};
    const char* name() const override { return "TEST FIXTURE - NO RF / SYNTHETIC TS"; }
    bool supportsOneSegIq() const override { return true; }
    bool hardwareCandidatePresent() const override
    {
        assert(std::this_thread::get_id() != ui);
        return true;
    }
    bool acquire() override
    {
        assert(std::this_thread::get_id() != ui);
        ++acquired;
        return true;
    }
    void release() override { (void)releaseWithStatus(); }
    bool releaseWithStatus() override
    {
        ++restored;
        return true;
    }
    bool configureLoRa(const RadioSettings&) override { return true; }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t*, size_t) override
    {
        assert(false);
        return false;
    }
    bool pollReceive(RadioPacket*) override { return false; }
    float readRssi() override { return 0; }
    bool isOnline() const override { return true; }
    const char* lastError() const override { return "Synthetic transport fixture"; }
    RadioSettings appliedLoRaConfig() const override { return {}; }
    RadioStatistics stats() const override { return {}; }
    bool streamOneSegIq(const OneSegIqRequest& request, OneSegIqResult& result) override
    {
        std::memset(request.staging, 0, 1280);
        while (!request.cancelled(request.context))
        {
            if (!request.accept(request.context, request.staging, 1280, result.validated_bytes / 2)) break;
            result.validated_bytes += 1280;
            std::this_thread::sleep_for(std::chrono::microseconds(1100));
        }
        result.cancelled = true;
        return true;
    }
};
template <typename Predicate>
static void await(Session& session, Predicate predicate)
{
    const auto deadline = Clock::now() + std::chrono::seconds(15);
    bool satisfied = predicate();
    while (!satisfied && session.snapshot().active && Clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        satisfied = predicate();
    }
    if (!satisfied)
    {
        const auto s = session.snapshot();
        std::fprintf(stderr, "TEST FIXTURE failed: phase=%u error=%u service=%u epoch=%llu message=%s\n",
                     unsigned(s.phase), unsigned(s.error), s.decoder.programmes.selected_program,
                     static_cast<unsigned long long>(s.media_epoch), s.message);
        std::fprintf(stderr, "TEST FIXTURE child exit=%d signal=%d diagnostic=%s receive=%s\n",
                     s.decoder.child_exit_code, s.decoder.child_signal, s.decoder.stderr_tail, s.receive.message);
        std::fprintf(stderr, "TEST FIXTURE media: phase=%u error=%u timing=%u video=%llu audio=%llu ts=%llu decode_errors=%llu queue=%u video_age=%u presentation_age=%u message=%s\n",
                     unsigned(s.media.phase), unsigned(s.media.error), unsigned(s.timing),
                     static_cast<unsigned long long>(s.media.video_frames), static_cast<unsigned long long>(s.media.audio_frames),
                     static_cast<unsigned long long>(s.media.ts_packets), static_cast<unsigned long long>(s.media.decode_errors),
                     s.media.queue_high_water, s.video_age_ms, s.presentation_age_ms, s.media.message);
        std::fprintf(stderr, "TEST FIXTURE generations: session=%llu receive=%llu decoder=%llu media=%llu clock=%llu valid=%u starved=%u queued_us=%u pmt=%u version=%u streams=%zu\n",
                     static_cast<unsigned long long>(s.generation), static_cast<unsigned long long>(s.receive.generation),
                     static_cast<unsigned long long>(s.decoder.generation), static_cast<unsigned long long>(s.media.generation),
                     static_cast<unsigned long long>(s.audio_clock.generation), unsigned(s.audio_clock.valid),
                     unsigned(s.audio_clock.starved), s.audio_clock.queued_us, s.decoder.programmes.pmt_pid,
                     unsigned(s.decoder.programmes.pmt_version), s.decoder.programmes.stream_count);
        for (size_t index = 0; index < s.decoder.programmes.stream_count; ++index)
            std::fprintf(stderr, "TEST FIXTURE PMT stream: pid=%u type=%u\n", s.decoder.programmes.streams[index].pid,
                         unsigned(s.decoder.programmes.streams[index].type));
        assert(false);
    }
}
static void phase(const std::filesystem::path& root, const char* name)
{
    {
        std::ofstream output(root / "phase", std::ios::app);
        output << name << '\n';
        assert(output);
    }
}
static void cleanup(Session& session)
{
    session.requestStop();
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (session.snapshot().active && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(!session.snapshot().active && session.snapshot().error == Session::Error::None);
    assert(!platform::audio::music_blocked());
    int status;
    assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
}
static uint64_t frame(Session& session, uint16_t programme, uint64_t previous_epoch = 0, bool video_only = false, uint16_t* colour = nullptr)
{
    std::vector<uint8_t> pixels(320 * 240 * 2);
    OneSegMediaWorker::FrameInfo info;
    await(session, [&]
          {
        const auto s = session.snapshot();
        return s.decoder.programmes.selected_program == programme && s.media_epoch > previous_epoch &&
               session.copyLatestFrame(s.generation, s.media_epoch, pixels.data(), pixels.size(), info); });
    const auto s = session.snapshot();
    assert(s.active && s.error == Session::Error::None);
    assert(session.acknowledgeFrame(s.generation, s.media_epoch, info.ordinal));
    assert(session.snapshot().timing == (video_only ? Session::Timing::VideoMonotonic : Session::Timing::AudioQueueEstimate));
    if (colour) *colour = uint16_t(pixels[0]) | (uint16_t(pixels[1]) << 8);
    std::fprintf(stderr, "TEST FIXTURE: programme=%u epoch=%llu PTS=%lld video_only=%u\n", programme,
                 static_cast<unsigned long long>(s.media_epoch), static_cast<long long>(info.pts_us), unsigned(video_only));
    return s.media_epoch;
}
int main(int argc, char** argv)
{
    assert(argc == 3); // Transport-child fixture and generated TS fixture directory.
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    const std::filesystem::path root = std::filesystem::absolute(argv[2]);
    Session::Config config;
    config.decoder = std::filesystem::absolute(argv[1]);
    config.environment = {"TRAIL_MATE_ONESEG_TRANSPORT_FIXTURES=" + root.string()};
    SyntheticRadioFixture radio;
    Session session(radio, config, []
                    { return true; });
    // Frame retrieval uses try_lock: a successful observation is not repeatable.
    unsigned one_shot_calls = 0;
    await(session, [&]
          { return ++one_shot_calls == 1; });
    assert(one_shot_calls == 1);
    phase(root, "a");
    assert(session.start(13, 320, 240));
    const auto generation = session.snapshot().generation;
    uint16_t red = 0, blue = 0;
    const uint64_t first = frame(session, 8, 0, false, &red);
    await(session, [&]
          { return session.snapshot().decoder.programmes.program_count == 2; });
    assert(session.selectProgramme(generation, 9));
    const uint64_t second = frame(session, 9, first, false, &blue);
    assert(red != blue); // Actual media PID switch changes decoded programme pixels.
    std::vector<uint8_t> obsolete(320 * 240 * 2);
    OneSegMediaWorker::FrameInfo old;
    assert(!session.copyLatestFrame(generation, first, obsolete.data(), obsolete.size(), old));
    phase(root, "b");
    const uint64_t remapped = frame(session, 9, second);
    assert(session.snapshot().decoder.programmes.streams[0].pid == 768);
    phase(root, "c");
    const uint64_t fallback = frame(session, 8, remapped);
    assert(fallback > remapped && !session.snapshot().requested_programme);
    assert(session.snapshot().generation == generation && radio.acquired == 1);
    cleanup(session);
    assert(radio.restored == 1);
    phase(root, "video");
    assert(session.start(13, 320, 240));
    const uint64_t video_epoch = frame(session, 8, 0, true);
    const auto pts = session.snapshot().presented_pts_us;
    const auto before_jump = session.snapshot().video_clock_reanchors;
    phase(root, "jump");
    std::vector<uint8_t> pixels(320 * 240 * 2);
    OneSegMediaWorker::FrameInfo info;
    await(session, [&]
          {
        auto s = session.snapshot();
        return session.copyLatestFrame(s.generation, s.media_epoch, pixels.data(), pixels.size(), info) && info.pts_us > pts + 5000000; });
    assert(session.snapshot().media_epoch == video_epoch);
    assert(session.snapshot().timing == Session::Timing::VideoMonotonic);
    assert(session.snapshot().video_clock_reanchors > before_jump);
    const auto after_jump = session.snapshot().video_clock_reanchors;
    const auto jumped_pts = info.pts_us;
    phase(root, "video");
    await(session, [&]
          {
        auto s = session.snapshot();
        return session.copyLatestFrame(s.generation, s.media_epoch, pixels.data(), pixels.size(), info) && info.pts_us < jumped_pts - 5000000; });
    assert(session.snapshot().media_epoch == video_epoch && session.snapshot().video_clock_reanchors > after_jump);
    cleanup(session);
    assert(radio.acquired == 2 && radio.restored == 2);
}
