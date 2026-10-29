#include "call_pcm_fixture.h"
#include "platform/audio/audio_focus.h"
#include <cassert>
#include <iostream>
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
call::Peer peer(uint8_t id)
{
    call::Peer value;
    value.link_id[0] = id;
    value.destination_hash[0] = id + 1;
    value.display_name = "Call audio fixture";
    return value;
}
void close(const call::Peer& value)
{
    call::hangup();
    uint8_t link[16];
    assert(call::consume_hangup_request(link));
    call::notify_link_closed(value.link_id);
    call::service_ui_runtime();
}
int main()
{
    auto state = std::make_shared<call_audio_test::State>();
    struct Focus
    {
        call_audio_test::State* state;
        unsigned pauses = 0, resumes = 0;
    } focus{state.get()};
    platform::audio::register_music_focus_client({&focus,
                                                  [](void* context)
                                                  { ++static_cast<Focus*>(context)->pauses; return true; },
                                                  [](void* context)
                                                  { auto& value=*static_cast<Focus*>(context); assert(value.state->live == 0); ++value.resumes; }});
    {
        native::NativeCallAudioBinding binding(call_audio_test::options(state));
        assert(binding.snapshot().built && binding.snapshot().installed && binding.snapshot().supported);
        {
            native::NativeCallAudioBinding duplicate(call_audio_test::options(state));
            assert(!duplicate.snapshot().installed);
        }
        const auto value = peer(1);
        assert(call::begin_outgoing(value));
        assert(platform::audio::music_blocked() && state->live == 0);
        call::mark_link_active(value.link_id);
        assert(call::prepare_media(value.link_id) && call::mark_call_active(value.link_id));
        until([&]
              { return binding.snapshot().statistics.queued_packets > 0; });
        call::AudioPacket packet;
        assert(call::dequeue_outbound_audio(&packet));
        assert(call::enqueue_inbound_audio(value.link_id, packet.data, packet.len));
        until([&]
              { return binding.snapshot().statistics.decoded_frames > 0; });
        call::set_microphone_muted(true);
        assert(!call::dequeue_outbound_audio(&packet));
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        assert(!call::dequeue_outbound_audio(&packet));
        call::set_speaker_muted(true);
        until([&]
              { return state->discarded > 1; });
        close(value);
        until([&]
              { return state->live == 0 && !platform::audio::music_blocked(); });
        assert(!binding.snapshot().running && binding.snapshot().failed == 0);
        binding.shutdown();
        assert(!binding.snapshot().installed && !call::snapshot().media_supported);
    }
    assert(focus.pauses == 1 && focus.resumes == 1 && state->live == 0);
    platform::audio::register_music_focus_client({});
    {
        auto failed = std::make_shared<call_audio_test::State>();
        failed->fail_open = true;
        native::NativeCallAudioBinding binding(call_audio_test::options(failed));
        const auto value = peer(3);
        assert(call::begin_outgoing(value));
        call::mark_link_active(value.link_id);
        assert(!call::prepare_media(value.link_id));
        assert(call::state() == call::State::Failed && failed->live == 0);
        close(value);
        until([&]
              { return !platform::audio::music_blocked(); });
    }
    {
        auto delayed = std::make_shared<call_audio_test::State>();
        delayed->block_open = true;
        auto options = call_audio_test::options(delayed);
        options.start_timeout_ms = 200;
        native::NativeCallAudioBinding binding(options);
        const auto value = peer(5);
        assert(call::begin_outgoing(value));
        call::mark_link_active(value.link_id);
        bool prepared = true;
        std::thread opening([&] { prepared = call::prepare_media(value.link_id); });
        until([&] { return delayed->entered.load(); });
        opening.join();
        assert(!prepared);
        assert(call::state() == call::State::Failed && !binding.snapshot().running);
        close(value);
        assert(platform::audio::music_blocked()); // The slow owner still holds its real resource.
        delayed->release();
        until([&]
              { return delayed->live == 0 && !platform::audio::music_blocked(); });
        assert(binding.snapshot().started == 0 && binding.snapshot().failed == 1);
    }
    for (unsigned index = 0; index < 20; ++index)
    {
        auto active = std::make_shared<call_audio_test::State>();
        {
            native::NativeCallAudioBinding binding(call_audio_test::options(active));
            const auto value = peer(uint8_t(10 + index));
            assert(call::begin_outgoing(value));
            call::mark_link_active(value.link_id);
            assert(call::prepare_media(value.link_id) && call::mark_call_active(value.link_id));
        }
        assert(active->live == 0 && !platform::audio::music_blocked());
        assert(call::realtime_phase() == call::RealtimePhase::Idle);
    }
    std::cout << "Native call media hooks, codec loopback, focus, mute, failure, timeout cancellation and active shutdown passed\n";
}
