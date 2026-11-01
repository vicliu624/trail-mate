#include "platform/linux/oneseg_media_worker.h"
#include "platform/shared/radio/transport_stream_programs.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

using namespace platform::linux_runtime;
struct Audio final : OneSegMediaAudio
{
    uint64_t samples = 0;
    bool begin(uint64_t) override { return true; }
    bool write(const int16_t*, size_t count, int64_t, uint64_t, const std::atomic_bool&) override
    {
        samples += count;
        return true;
    }
    bool end(bool) override { return true; }
};
int main(int argc, char** argv)
{
    assert(argc == 2 || argc == 3);
    const bool legacy = argc == 3 && !std::strcmp(argv[2], "legacy");
    using Programs = platform::shared::radio::TransportStreamPrograms;
    Programs programs(Programs::Discovery::AribPartial);
    std::ifstream input(argv[1], std::ios::binary);
    assert(input);
    std::vector<std::array<uint8_t, 188>> packets;
    std::array<uint8_t, 188> packet;
    while (input.read(reinterpret_cast<char*>(packet.data()), packet.size()))
    {
        const unsigned pid = (unsigned(packet[1] & 31) << 8) | packet[2];
        assert(packet[0] == 0x47 && pid != 0); // Genuine no-PAT received fixture.
        programs.accept(packet.data(), packet.size());
        packets.push_back(packet);
    }
    const auto table = programs.snapshot();
    assert(!table.pat_valid && table.pmt_valid && !table.crc_errors);
    OneSegMediaWorker::Config config;
    config.generation = 73;
    config.height = 240;
    for (size_t i = 0; i < table.stream_count; ++i)
    {
        if (programs.video(table.streams[i].type)) config.video_pid = table.streams[i].pid;
        if (programs.audio(table.streams[i].type)) config.audio_pid = table.streams[i].pid;
    }
    assert(config.video_pid != 0x1fff && config.audio_pid != 0x1fff);
    if (!legacy)
    {
        config.programme_number = table.selected_program;
        config.pmt_pid = table.pmt_pid;
        config.transport_id = table.transport_id;
    }
    Audio audio;
    OneSegMediaWorker media(&audio);
    assert(media.start(config));
    for (const auto& value : packets)
    {
        if (!media.acceptTs(value.data(), value.size(), config.generation)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    media.endInput();
    media.join();
    const auto state = media.snapshot();
    std::fprintf(stderr, "OFFLINE no-PAT mode=%s frames=%llu pcm=%llu error=%u message=%s\n",
                 legacy ? "legacy" : "received-PMT-bootstrap", (unsigned long long)state.video_frames,
                 (unsigned long long)audio.samples, unsigned(state.error), state.message);
    if (!legacy) assert(state.error == OneSegMediaWorker::Error::None && state.video_frames > 20 && audio.samples > 48000);
    assert(!programs.snapshot().pat_valid); // Internal bootstrap never changes RF evidence.
}
