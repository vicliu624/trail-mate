#include "sstv/receiver.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
int main(int argc, char** argv)
{
    require(argc == 2, "PCM fixture path required");
    using Receiver = sstv::core::Receiver;
    auto receiver = std::make_unique<Receiver>();
    std::vector<int16_t> silence(150000);
    receiver->feed(silence.data(), silence.size());
    require(receiver->status().state == sstv::core::ReceiveState::Waiting && !receiver->status().has_image,
            "silence fabricated an SSTV image");
    receiver->reset();
    std::vector<int16_t> audio;
    double phase = 0;
    const auto tone = [&](double frequency)
    {
        phase += frequency * 6.283185307179586 / 15000;
        if (phase > 6.283185307179586) phase -= 6.283185307179586;
        audio.push_back(static_cast<int16_t>(12000 * std::sin(phase)));
    };
    // Independent BW8 scan train: 10 ms of 1200 Hz sync plus 57 ms of
    // luminance from 1500 (black) to 2300 Hz (white). Acquisition may begin
    // mid-transmission, so include enough complete scan lines after lock.
    for (unsigned line = 0; line < 130; ++line)
    {
        for (unsigned sample = 0; sample < 150; ++sample) tone(1200);
        for (unsigned sample = 0; sample < 855; ++sample) tone(1500 + 800.0 * sample / 854);
    }
    for (std::size_t offset = 0; offset < audio.size(); offset += 173)
        receiver->feed(audio.data() + offset, std::min<std::size_t>(173, audio.size() - offset));
    const auto status = receiver->status();
    std::fprintf(stderr, "SSTV state=%d line=%u progress=%f mode=%s\n", int(status.state), status.line, status.progress, receiver->mode_name());
    require(status.state == sstv::core::ReceiveState::Complete && status.has_image && status.progress == 1,
            "valid PCM scan did not complete");
    require(std::string(receiver->mode_name()) == "BW8", "wrong acquired mode");
    const auto black = receiver->frame()[96 * Receiver::width + 24 + 20];
    const auto white = receiver->frame()[96 * Receiver::width + 24 + 220];
    require(((black >> 11) & 31) < 10 && ((white >> 11) & 31) > 20, "PCM luminance was not decoded into pixels");
    const std::vector<uint16_t> first(receiver->frame(), receiver->frame() + Receiver::width * Receiver::height);
    receiver->reset();
    for (std::size_t offset = 0; offset < audio.size(); offset += 512)
        receiver->feed(audio.data() + offset, std::min<std::size_t>(512, audio.size() - offset));
    require(std::equal(first.begin(), first.end(), receiver->frame()), "decoder retained cross-session/block state");
    std::ofstream output(argv[1], std::ios::binary);
    for (const auto sample : audio)
    {
        output.put(static_cast<char>(uint16_t(sample) & 255));
        output.put(static_cast<char>(uint16_t(sample) >> 8));
    }
    require(bool(output), "cannot write PCM fixture");
}
