#include "platform/audio/audio_focus.h"
#include "platform/linux/oneseg_tv_session.h"
#include "platform/linux/packet_radio.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <thread>
#include <vector>

using namespace platform::linux_runtime;
using Session = OneSegTvSession;
using Clock = std::chrono::steady_clock;
template <typename Predicate>
static void await(Predicate predicate)
{
    const auto end = Clock::now() + std::chrono::seconds(15);
    while (!predicate() && Clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(predicate());
}
struct Radio final : IPacketRadio
{
    const std::thread::id ui = std::this_thread::get_id();
    std::atomic_bool present{true};
    std::atomic_uint acquisitions{0}, restored{0};
    std::string input;
    const char* name() const override { return "TEST FIXTURE - NO RF"; }
    bool supportsOneSegIq() const override { return true; }
    bool hardwareCandidatePresent() const override
    {
        assert(std::this_thread::get_id() != ui);
        return present;
    }
    bool acquire() override
    {
        assert(std::this_thread::get_id() != ui);
        ++acquisitions;
        return true;
    }
    void release() override { (void)releaseWithStatus(); }
    bool releaseWithStatus() override
    {
        int status;
        assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
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
    const char* lastError() const override { return "test radio"; }
    RadioSettings appliedLoRaConfig() const override { return {}; }
    RadioStatistics stats() const override { return {}; }
    bool streamOneSegIq(const OneSegIqRequest& request, OneSegIqResult& result) override
    {
        std::ifstream stream(input, std::ios::binary);
        assert(stream);
        while (!request.cancelled(request.context) &&
               (stream.read(reinterpret_cast<char*>(request.staging), 1280) || stream.gcount()))
        {
            const size_t count = static_cast<size_t>(stream.gcount());
            if (!request.accept(request.context, request.staging, count, result.validated_bytes / 2)) break;
            result.validated_bytes += count;
            std::this_thread::sleep_for(std::chrono::microseconds(1100));
        }
        // Empty FIFO remains cancellable after the finite recorded input.
        while (!request.cancelled(request.context)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        result.cancelled = true;
        return true;
    }
};
static bool handoff() { return true; }
static void no_children()
{
    int status;
    assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    assert(!platform::audio::music_blocked());
}
static void commands_and_failure(const char* fixture)
{
    Radio radio;
    radio.present = false;
    Session::Config config;
    config.decoder = fixture;
    config.environment = {"TRAIL_MATE_ONESEG_TEST_MODE=early-exit"};
    Session session(radio, config, handoff);
    assert(!session.start(12, 320, 180));
    assert(session.snapshot().error == Session::Error::Configuration);
    const auto before = Clock::now();
    assert(session.start(13, 320, 180));
    assert(Clock::now() - before < std::chrono::milliseconds(100));
    assert(!session.start(14, 320, 180));
    await([&]
          { return !session.snapshot().active; });
    assert(session.snapshot().error == Session::Error::Radio && radio.acquisitions == 0);
    radio.present = true;
    assert(session.start(13, 320, 180));
    await([&]
          { return !session.snapshot().active; });
    assert(session.snapshot().phase == Session::Phase::Error && radio.acquisitions == 0);
    no_children();
    config.environment = {"TRAIL_MATE_ONESEG_TEST_MODE=no-ready"};
    Session startup(radio, config, handoff);
    assert(startup.start(13, 320, 180));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto stopped = Clock::now();
    startup.requestStop(startup.snapshot().generation);
    assert(Clock::now() - stopped < std::chrono::milliseconds(100));
    await([&]
          { return !startup.snapshot().active; });
    assert(startup.snapshot().phase == Session::Phase::Stopped && startup.snapshot().error == Session::Error::None);
    assert(radio.acquisitions == 0);
    no_children();
}
static Session::Config native_config(const char* decoder, const char* prefix)
{
    Session::Config config;
    config.decoder = decoder;
    std::string python, library;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(prefix))
    {
        const auto name = entry.path().filename().string();
        if (entry.is_directory() && (name == "site-packages" || name == "dist-packages")) python += entry.path().string() + ":";
        if (entry.is_regular_file() && name.find("libgnuradio-isdbt.so") == 0) library += entry.path().parent_path().string() + ":";
    }
    config.environment = {"PYTHONPATH=" + python, "LD_LIBRARY_PATH=" + library};
    return config;
}
static void native_session(const Session::Config& config, const char* iq)
{
    Radio radio;
    radio.input = iq;
    Session session(radio, config, handoff);
    session.setVolume(35);
    assert(session.start(13, 320, 180));
    const uint64_t generation = session.snapshot().generation;
    assert(!session.acknowledgeFrame(generation, 1, 1));
    await([&]
          { return session.snapshot().media.video_frames > 0 || !session.snapshot().active; });
    auto state = session.snapshot();
    assert(state.active && state.error == Session::Error::None && state.phase != Session::Phase::FramePresented);
    std::vector<uint8_t> pixels(OneSegMediaWorker::max_width * OneSegMediaWorker::max_height * 2);
    OneSegMediaWorker::FrameInfo info;
    assert(!session.copyLatestFrame(generation - 1, state.media_epoch, pixels.data(), pixels.size(), info));
    await([&]
          { return session.copyLatestFrame(generation, state.media_epoch, pixels.data(), pixels.size(), info); });
    assert(session.acknowledgeFrame(generation, state.media_epoch, info.ordinal));
    assert(session.snapshot().timing == Session::Timing::AudioQueueEstimate);
    assert(session.snapshot().av_delta_us >= -250000 && session.snapshot().av_delta_us <= 30000);
    assert(!session.acknowledgeFrame(generation, state.media_epoch, info.ordinal));
    await([&]
          { return session.snapshot().phase == Session::Phase::FramePresented; });
    assert(!session.selectProgramme(generation - 1, state.decoder.programmes.selected_program));
    assert(session.selectProgramme(generation, state.decoder.programmes.selected_program));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(session.snapshot().media_epoch == state.media_epoch);
    const auto before = Clock::now();
    session.requestStop(generation);
    assert(Clock::now() - before < std::chrono::milliseconds(100));
    assert(!session.copyLatestFrame(generation, state.media_epoch, pixels.data(), pixels.size(), info));
    await([&]
          { return !session.snapshot().active; });
    state = session.snapshot();
    assert(state.phase == Session::Phase::Stopped && state.error == Session::Error::None && state.volume == 35);
    assert(radio.acquisitions == 1 && radio.restored == 1);
    no_children();
    assert(session.start(14, 240, 120));
    const auto next = session.snapshot();
    assert(next.generation != generation && !next.decoder.programmes.program_count && !next.media.video_frames && !next.media_epoch);
    session.requestStop(generation);
    assert(session.snapshot().phase != Session::Phase::Stopping);
    session.requestStop(next.generation);
    await([&]
          { return !session.snapshot().active; });
    assert(session.snapshot().phase == Session::Phase::Stopped);
    no_children();
}
static void metadata_session(const char* fixture, const char* iq)
{
    Radio radio;
    radio.input = iq;
    Session::Config config;
    config.decoder = fixture;
    config.environment = {"TRAIL_MATE_ONESEG_TEST_MODE=metadata-multi"};
    Session session(radio, config, handoff);
    assert(session.start(13, 320, 240));
    await([&]
          { const auto current = session.snapshot(); return (current.service_text.present_valid && current.decoder.programmes.program_count == 2) || !current.active; });
    const auto received = session.snapshot();
    assert(received.active && received.service_text.service == 24 && received.service_text.service_valid);
    assert(!received.media.video_frames && !received.presented_frames); // Text alone never attests playback.
    assert(received.service_text.name && received.service_text.name.text == u8"ＴＥＳＴ");
    assert(received.service_text.present_title && received.service_text.present_title.text == u8"ＮＥＷＳ");
    assert(received.decoder.service_information.service == received.service_text.service);
    assert(session.selectProgramme(received.generation, 25));
    assert(session.snapshot().service_text.name.text.empty()); // Hide old name during an in-flight selection.
    await([&]
          { return session.snapshot().decoder.programmes.selected_program == 25; });
    assert(!session.snapshot().service_text.service_valid && session.snapshot().service_text.present_title.text.empty());
    session.requestStop(received.generation);
    assert(!session.snapshot().service_text.service_valid && session.snapshot().service_text.name.text.empty());
    await([&]
          { return !session.snapshot().active; });
    assert(!session.snapshot().service_text.present_valid);
    no_children();
}
int main(int argc, char** argv)
{
    assert(argc == 2 || argc == 5);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    commands_and_failure(argv[1]);
    if (argc == 5) native_session(native_config(argv[2], argv[3]), argv[4]);
    else
    {
        // TEST FIXTURE - NO RF: the fixture child ignores these synthetic IQ bytes.
        FILE* input = std::tmpfile();
        assert(input);
        std::array<uint8_t, 1280> bytes{};
        assert(std::fwrite(bytes.data(), 1, bytes.size(), input) == bytes.size());
        assert(std::fflush(input) == 0);
        const auto path = "/proc/self/fd/" + std::to_string(fileno(input));
        metadata_session(argv[1], path.c_str());
        assert(std::fclose(input) == 0);
    }
}
