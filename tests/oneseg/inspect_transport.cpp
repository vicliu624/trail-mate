#include "platform/shared/radio/transport_stream_programs.h"
#include <array>
#include <iostream>
#include <memory>

int main()
{
    using Reader = platform::shared::radio::TransportStreamPrograms;
    auto reader = std::make_unique<Reader>(Reader::Discovery::AribPartial);
    std::array<uint8_t, 188> packet{};
    size_t count = 0;
    while (std::cin.read(reinterpret_cast<char*>(packet.data()), packet.size()))
    {
        if (++count > 1000000) return 2;
        reader->accept(packet.data(), packet.size());
    }
    if (std::cin.gcount()) return 2;
    const auto& state = reader->snapshot();
    bool video = false, audio = false;
    for (size_t i = 0; i < state.stream_count; ++i)
    {
        video |= Reader::video(state.streams[i].type);
        audio |= Reader::audio(state.streams[i].type);
    }
    std::cout << std::boolalpha << "{\"programme\":" << state.selected_program
              << ",\"pmt_valid\":" << state.pmt_valid << ",\"pmt_pid\":" << state.pmt_pid
              << ",\"source\":" << static_cast<unsigned>(state.source)
              << ",\"video\":" << video << ",\"audio\":" << audio
              << ",\"video_packets\":" << state.selected_video_packets
              << ",\"audio_packets\":" << state.selected_audio_packets
              << ",\"crc_errors\":" << state.crc_errors << "}\n";
    return state.pmt_valid && video && audio && state.selected_video_packets && state.selected_audio_packets ? 0 : 1;
}
