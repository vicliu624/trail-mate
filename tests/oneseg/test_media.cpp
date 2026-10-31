#include "platform/linux/oneseg_media_worker.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

using Media = platform::linux_runtime::OneSegMediaWorker;
struct Audio final : platform::linux_runtime::OneSegMediaAudio
{
    uint64_t generation = 0, samples = 0;
    unsigned begins = 0, ends = 0;
    bool reject = false;
    bool begin_ok = true, end_throws = false;
    std::atomic_bool entered{false}, release{true};
    int64_t first_pts = INT64_MIN;
    bool begin(uint64_t value) override
    {
        generation = value;
        ++begins;
        entered = true;
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return begin_ok;
    }
    bool write(const int16_t* data, size_t count, int64_t pts, uint64_t value, const std::atomic_bool&) override
    {
        assert(data && count && count <= 2048 && value == generation);
        if (reject) return false;
        if (first_pts == INT64_MIN) first_pts = pts;
        samples += count;
        return true;
    }
    bool end(bool) override
    {
        ++ends;
        if (end_throws) throw 1;
        return true;
    }
};
static void feed(Media& media, const char* path, uint64_t generation)
{
    std::ifstream input(path, std::ios::binary);
    assert(input);
    std::array<uint8_t, 188> packet;
    while (input.read(reinterpret_cast<char*>(packet.data()), packet.size()))
    {
        assert(media.acceptTs(packet.data(), packet.size(), generation));
        // Reproduce a bounded live TS producer; this is offline fixture input.
        std::this_thread::sleep_for(std::chrono::microseconds(400));
    }
    assert(input.gcount() == 0);
    media.endInput();
    media.join();
}
static void lifecycle()
{
    Audio audio;
    Media media(&audio);
    assert(!media.start({}));
    assert(media.snapshot().error == Media::Error::Configuration);
    assert(media.start({1, 256, 257, 320, 180}));
    assert(!media.start({2, 256, 257, 320, 180}));
    media.requestStop();
    media.join();
    assert(media.snapshot().phase == Media::Phase::Stopped && audio.begins == 0);
    assert(media.start({2, 256, 257, 320, 180}));
    std::array<uint8_t, 188> packet{};
    packet[0] = 0x47;
    assert(!media.acceptTs(packet.data(), packet.size(), 1));
    media.join();
    assert(media.snapshot().error == Media::Error::Input && !media.snapshot().pcm_samples);
}
static void failure_paths(const char* path)
{
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        Audio audio;
        audio.begin_ok = mode != 0;
        audio.reject = mode == 1;
        audio.end_throws = mode == 2;
        Media media(&audio);
        assert(media.start({10 + mode, 256, 257, 320, 180}));
        std::ifstream input(path, std::ios::binary);
        std::array<uint8_t, 188> packet;
        while (input.read(reinterpret_cast<char*>(packet.data()), packet.size()))
        {
            if (!media.acceptTs(packet.data(), packet.size(), 10 + mode)) break;
            std::this_thread::sleep_for(std::chrono::microseconds(400));
        }
        media.endInput();
        media.join();
        assert(media.snapshot().phase == Media::Phase::Error);
        assert(media.snapshot().error == (mode == 2 ? Media::Error::Internal : Media::Error::Audio));
        assert(audio.begins == 1 && audio.ends == 1);
    }
    Audio blocked;
    blocked.release = false;
    Media media(&blocked);
    assert(media.start({20, 256, 257, 320, 180}));
    std::ifstream input(path, std::ios::binary);
    std::array<uint8_t, 188> packet;
    while (!blocked.entered && input.read(reinterpret_cast<char*>(packet.data()), packet.size()))
    {
        assert(media.acceptTs(packet.data(), packet.size(), 20));
        std::this_thread::sleep_for(std::chrono::microseconds(400));
    }
    assert(blocked.entered);
    packet.fill(0xff);
    packet[0] = 0x47;
    packet[1] = 0x1f;
    packet[2] = 0xff;
    packet[3] = 0x10;
    bool full = false;
    for (unsigned i = 0; i <= 512; ++i)
        if (!media.acceptTs(packet.data(), packet.size(), 20))
        {
            full = true;
            break;
        }
    assert(full);
    blocked.release = true;
    media.join();
    assert(media.snapshot().error == Media::Error::QueueFull && blocked.ends == 1);
}
int main(int argc, char** argv)
{
    assert(argc == 2 || argc == 3);
    lifecycle();
    Audio audio;
    Media media(&audio);
    assert(media.start({3, 256, 257, 320, 180}));
    feed(media, argv[1], 3);
    const auto state = media.snapshot();
    std::fprintf(stderr, "OFFLINE TS: phase=%u error=%u video=%llu audio=%llu pcm=%llu decode_errors=%llu message=%s\n",
                 unsigned(state.phase), unsigned(state.error), static_cast<unsigned long long>(state.video_frames),
                 static_cast<unsigned long long>(state.audio_frames), static_cast<unsigned long long>(state.pcm_samples),
                 static_cast<unsigned long long>(state.decode_errors), state.message);
    assert(state.phase == Media::Phase::Stopped && state.video_frames > 20 && state.audio_frames > 50);
    assert(state.pcm_samples == audio.samples && audio.samples > 48000 && audio.first_pts != INT64_MIN);
    assert(audio.begins == 1 && audio.ends == 1);
    std::vector<uint8_t> frame(Media::max_width * Media::max_height * 2);
    Media::FrameInfo info;
    assert(!media.copyLatestFrame(2, frame.data(), frame.size(), info));
    assert(media.copyLatestFrame(3, frame.data(), frame.size(), info));
    assert(info.width > 0 && info.width <= 320 && info.height <= 180 && info.ordinal == state.video_frames);
    assert(info.pts_us != INT64_MIN && info.stride == info.width * 2);
    assert(!media.copyLatestFrame(3, frame.data(), 1, info));
    Media::FrameInfo selected;
    const auto final_pts = info.pts_us;
    assert(media.copyFrameAt(3, final_pts - 150000, frame.data(), frame.size(), selected));
    assert(selected.ordinal < state.video_frames && selected.pts_us <= final_pts - 120000 && selected.pts_us >= final_pts - 400000);
    std::fill(frame.begin(), frame.end(), 0xa5);
    assert(!media.copyFrameAt(3, final_pts - 2000000, frame.data(), frame.size(), selected));
    assert(!media.copyFrameAt(3, final_pts + 300001, frame.data(), frame.size(), selected));
    assert(!media.copyFrameAt(3, INT64_MIN, frame.data(), frame.size(), selected));
    assert(!media.copyFrameAt(2, final_pts, frame.data(), frame.size(), selected));
    assert(std::all_of(frame.begin(), frame.end(), [](uint8_t byte)
                       { return byte == 0xa5; }));
    if (argc == 3) assert(state.pcm_samples == std::filesystem::file_size(argv[2]) / 2);
    failure_paths(argv[1]);
}
