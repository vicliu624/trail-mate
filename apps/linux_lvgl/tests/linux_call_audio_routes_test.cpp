#include "platform/linux/call_audio_service.h"
#include "platform/linux/native_audio_service.h"
#include "platform/linux/sdl_pcm_output.h"
#include <SDL.h>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

namespace call = platform::ui::reticulum_call;
namespace native = platform::linux_runtime;
template <class Predicate>
void until(Predicate ready)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready())
    {
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
int main()
{
    native::SdlPcmOutput output(false);
    assert(SDL_GetNumAudioDevices(0) > 0 && SDL_GetNumAudioDevices(1) > 0);
    const std::string out = SDL_GetAudioDeviceName(0, 0), in = SDL_GetAudioDeviceName(0, 1);
    setenv("TRAIL_MATE_AUDIO_OUTPUT_DEVICE", "missing-global-output", 1);
    assert(output.begin(8000, out.c_str()));
    const int16_t silence[160]{};
    assert(output.write(silence, 160));
    output.discard_pending();
    assert(output.pending_ms() <= output.period_ms());
    assert(!output.begin(8000, "missing-explicit-output"));
    output.end();
    setenv("TRAIL_MATE_AUDIO_INPUT_DEVICE", "missing-global-input", 1);
    setenv("TRAIL_MATE_CALL_INPUT_DEVICE", in.c_str(), 1);
    setenv("TRAIL_MATE_CALL_OUTPUT_DEVICE", out.c_str(), 1);
    {
        native::NativeCallAudioBinding binding;
        until([&]
              { return binding.snapshot().supported; });
        call::Peer peer;
        peer.link_id[0] = 1;
        peer.destination_hash[0] = 2;
        assert(call::begin_outgoing(peer));
        call::mark_link_active(peer.link_id);
        assert(call::prepare_media(peer.link_id) && call::mark_call_active(peer.link_id));
        until([&]
              { return binding.snapshot().statistics.queued_packets > 0; });
        call::hangup();
        uint8_t link[16];
        assert(call::consume_hangup_request(link));
        call::notify_link_closed(link);
        until([&]
              { return !binding.snapshot().running; });
    }
    setenv("TRAIL_MATE_AUDIO_OUTPUT_DEVICE", out.c_str(), 1);
    setenv("TRAIL_MATE_CALL_OUTPUT_DEVICE", "missing-call-output", 1);
    {
        native::NativeCallAudioBinding binding;
        until([&]
              { return binding.snapshot().supported; });
        call::Peer peer;
        peer.link_id[0] = 3;
        peer.destination_hash[0] = 4;
        assert(call::begin_outgoing(peer));
        call::mark_link_active(peer.link_id);
        assert(!call::prepare_media(peer.link_id) && call::state() == call::State::Failed);
        call::notify_link_closed(peer.link_id);
    }
    native::shutdown_native_audio();
}
