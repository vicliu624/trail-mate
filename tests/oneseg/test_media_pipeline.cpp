#include "platform/audio/audio_focus.h"
#include "platform/linux/oneseg_decoder_process.h"
#include "platform/linux/oneseg_pcm_output.h"
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
int main(int argc, char** argv)
{
    assert(argc == 5);
    setenv("SDL_AUDIODRIVER", "dummy", 1); // Explicit offline fixture, no sound/RF device.
    platform::shared::radio::TransportStreamPrograms programmes(
        platform::shared::radio::TransportStreamPrograms::Discovery::AribPartial);
    std::ifstream transport(argv[4], std::ios::binary);
    assert(transport);
    std::array<uint8_t, 188> packet;
    while (!programmes.snapshot().pmt_valid && transport.read(reinterpret_cast<char*>(packet.data()), packet.size()))
        programmes.accept(packet.data(), packet.size());
    const auto table = programmes.snapshot();
    assert(table.pmt_valid);
    OneSegMediaWorker::Config media_config;
    media_config.generation = 42;
    for (size_t i = 0; i < table.stream_count; ++i)
    {
        if (programmes.video(table.streams[i].type)) media_config.video_pid = table.streams[i].pid;
        if (programmes.audio(table.streams[i].type)) media_config.audio_pid = table.streams[i].pid;
    }
    assert(media_config.video_pid != 0x1fff && media_config.audio_pid != 0x1fff);
    OneSegPcmOutput audio;
    OneSegMediaWorker media(&audio);
    assert(media.start(media_config));
    OneSegDecoderProcess::Config config;
    config.decoder = argv[1];
    std::string python, libraries;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[2]))
    {
        const auto name = entry.path().filename().string();
        if (entry.is_directory() && (name == "site-packages" || name == "dist-packages")) python += entry.path().string() + ":";
        if (entry.is_regular_file() && name.find("libgnuradio-isdbt.so") == 0) libraries += entry.path().parent_path().string() + ":";
    }
    assert(!python.empty() && !libraries.empty());
    config.environment = {"PYTHONPATH=" + python, "LD_LIBRARY_PATH=" + libraries};
    config.ts_context = &media;
    config.accept_ts = [](void* c, const uint8_t* p, size_t n, uint64_t generation)
    { return static_cast<OneSegMediaWorker*>(c)->acceptTs(p, n, generation); };
    OneSegDecoderProcess process(config);
    std::atomic_bool cancel{false};
    assert(process.open(13, 42, cancel));
    std::ifstream input(argv[3], std::ios::binary);
    assert(input);
    std::array<uint8_t, 1280> iq;
    uint64_t pairs = 0;
    while (input.read(reinterpret_cast<char*>(iq.data()), iq.size()) || input.gcount())
    {
        const size_t count = static_cast<size_t>(input.gcount());
        assert(process.write(iq.data(), count, pairs, 42, cancel));
        pairs += count / 2;
        // Keep the recorded IQ near real time while the real SDL dummy device
        // consumes PCM, rather than replacing playback with a memory sink.
        std::this_thread::sleep_for(std::chrono::microseconds(1100));
    }
    assert(process.finish());
    media.endInput();
    media.join();
    const auto state = media.snapshot();
    std::fprintf(stderr, "OFFLINE IQ + SDL DUMMY: frames=%llu pcm=%llu error=%u message=%s\n",
                 static_cast<unsigned long long>(state.video_frames), static_cast<unsigned long long>(state.pcm_samples),
                 unsigned(state.error), state.message);
    assert(state.phase == OneSegMediaWorker::Phase::Stopped && state.video_frames > 20 && state.pcm_samples > 48000);
    assert(!platform::audio::music_blocked());
    std::vector<uint8_t> pixels(OneSegMediaWorker::max_width * OneSegMediaWorker::max_height * 2);
    OneSegMediaWorker::FrameInfo info;
    assert(media.copyLatestFrame(42, pixels.data(), pixels.size(), info));
    assert(info.generation == 42 && info.ordinal == state.video_frames && info.pts_us != INT64_MIN);
    int status;
    assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
}
