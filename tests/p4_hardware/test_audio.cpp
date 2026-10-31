#include "../../boards/t_display_p4/src/codec_compat.cpp"
#include "../../platform/esp/idf_components/t_display_p4/t_display_p4_audio_runtime.cpp"
#include <cassert>
#include <iostream>
int main()
{
    // Fail each allocation/init boundary, then retry without leaking I2S0.
    for (int failure = 1; failure <= 7; ++failure)
    {
        release_audio_resources_locked();
        fake::now_us += 3000000;
        fake::step = 0;
        fake::fail_step = failure;
        assert(!trail_mate_t_display_p4_audio_is_ready());
        assert(fake::live == 0 && fake::channel_live == 0);
        const int failed_step = fake::step;
        assert(!trail_mate_t_display_p4_audio_is_ready());
        assert(fake::step == failed_step); // Backoff prevents allocation storms.
        fake::fail_step = -1;
        fake::now_us += 3000000;
        assert(trail_mate_t_display_p4_audio_is_ready());
        assert(fake::live == 4 && fake::channel_live == 2);
    }
    using boards::t_display_p4::CodecCompat;
    constexpr auto walkie = TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_WALKIE;
    constexpr auto sstv = TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_SSTV;
    CodecCompat codec(walkie);
    codec.setGain(12);
    assert(codec.open(16, 1, 16000) == 0 && fake::gain == 12);
    assert(!trail_mate_t_display_p4_audio_begin(sstv, 16000));
    assert(!trail_mate_t_display_p4_audio_set_input_gain(sstv, 24));
    codec.setGain(99);
    assert(fake::gain == 42 && codec.getGain() == 42);
    fake::gain_ok = false;
    codec.setGain(0);
    assert(codec.getGain() == 42);
    fake::gain_ok = true;
    std::array<int16_t, 641> pcm{};
    pcm.fill(123);
    assert(trail_mate_t_display_p4_audio_write_mono(walkie, pcm.data(), pcm.size()));
    assert(fake::written.size() == pcm.size() * 2);
    for (auto sample : fake::written) assert(sample == 123);
    assert(trail_mate_t_display_p4_audio_read_mono(walkie, pcm.data(), pcm.size()));
    for (auto sample : pcm) assert(sample == 100);
    codec.close();
    constexpr auto music = TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_MUSIC;
    assert(trail_mate_t_display_p4_audio_begin(music, 44100) && fake::output_volume == 100);
    trail_mate_t_display_p4_audio_set_volume_percent(27);
    assert(fake::output_volume == 100 && s_audio.volume_percent == 27);
    trail_mate_t_display_p4_audio_end(music);
    assert(trail_mate_t_display_p4_audio_begin(walkie, 16000) && fake::output_volume == 27);
    trail_mate_t_display_p4_audio_end(walkie);
    constexpr auto call = TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_RETICULUM_CALL;
    assert(trail_mate_t_display_p4_audio_begin(call, 16000));
    assert(trail_mate_t_display_p4_audio_set_session_volume(call, 100));
    trail_mate_t_display_p4_audio_set_volume_percent(23);
    assert(fake::output_volume == 100 && s_audio.volume_percent == 23);
    assert(!trail_mate_t_display_p4_audio_set_session_volume(walkie, 50));
    trail_mate_t_display_p4_audio_end(call);
    assert(trail_mate_t_display_p4_audio_begin(walkie, 16000) && fake::output_volume == 23);
    trail_mate_t_display_p4_audio_end(walkie);
    constexpr auto fm = TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_FM;
    struct MusicWakeups
    {
        int suspended = 0;
        int resumed = 0;
    } wakeups;
    platform::audio::register_music_focus_client(
        {&wakeups,
         [](void* context)
         { ++static_cast<MusicWakeups*>(context)->suspended; return true; },
         [](void* context)
         { ++static_cast<MusicWakeups*>(context)->resumed; }});
    assert(trail_mate_t_display_p4_audio_begin(fm, 48000));
    assert(wakeups.suspended == 1 && wakeups.resumed == 0);
    assert(s_audio.sample_rate_hz == 48000 && fake::output_volume == 100);
    assert(platform::audio::music_blocked());
    assert(!trail_mate_t_display_p4_audio_begin(walkie, 16000));
    assert(!trail_mate_t_display_p4_audio_begin(music, 44100));
    assert(!trail_mate_t_display_p4_audio_write_mono(walkie, pcm.data(), pcm.size()));
    trail_mate_t_display_p4_audio_end(walkie);
    assert(s_audio.owner == fm); // Other pages cannot close FM's session.
    trail_mate_t_display_p4_audio_set_volume_percent(19);
    assert(fake::output_volume == 100 && s_audio.volume_percent == 19);
    fake::written.clear();
    pcm.fill(-1234);
    assert(trail_mate_t_display_p4_audio_write_mono(fm, pcm.data(), pcm.size()));
    assert(fake::written.size() == pcm.size() * 2);
    for (auto sample : fake::written) assert(sample == -1234);
    trail_mate_t_display_p4_audio_end(fm);
    assert(wakeups.resumed == 1);
    assert(trail_mate_t_display_p4_audio_end_checked(fm));
    assert(trail_mate_t_display_p4_audio_begin(fm, 48000));
    fake::mutex_ok = false;
    assert(!trail_mate_t_display_p4_audio_end_checked(fm));
    fake::mutex_ok = true;
    assert(s_audio.owner == fm && platform::audio::music_blocked());
    fake::codec_close_result = -7;
    assert(!trail_mate_t_display_p4_audio_end_checked(fm));
    assert(s_audio.owner == fm && s_audio.cleanup_required && wakeups.resumed == 1);
    assert(!trail_mate_t_display_p4_audio_begin(fm, 48000));
    assert(!trail_mate_t_display_p4_audio_begin(walkie, 16000));
    assert(!trail_mate_t_display_p4_audio_write_mono(fm, pcm.data(), pcm.size()));
    assert(trail_mate_t_display_p4_audio_end_checked(walkie));
    assert(s_audio.owner == fm && wakeups.resumed == 1);
    fake::codec_close_result = 0;
    assert(trail_mate_t_display_p4_audio_end_checked(fm));
    assert(wakeups.resumed == 2 && !platform::audio::music_blocked());
    for (const bool failed_open : {false, true})
    {
        fake::codec_open_result = failed_open ? -8 : 0;
        fake::gain_ok = failed_open;
        fake::codec_close_result = -9;
        assert(!trail_mate_t_display_p4_audio_begin(fm, 48000));
        assert(s_audio.owner == fm && s_audio.cleanup_required && platform::audio::music_blocked());
        assert(!trail_mate_t_display_p4_audio_begin(fm, 48000));
        fake::codec_close_result = 0;
        assert(trail_mate_t_display_p4_audio_end_checked(fm));
        assert(!platform::audio::music_blocked());
    }
    fake::codec_open_result = 0;
    fake::gain_ok = true;
    platform::audio::register_music_focus_client({});
    assert(!platform::audio::music_blocked());
    assert(!trail_mate_t_display_p4_audio_write_mono(fm, pcm.data(), pcm.size()));
    assert(trail_mate_t_display_p4_audio_begin(walkie, 16000));
    assert(fake::output_volume == 19);
    trail_mate_t_display_p4_audio_end(walkie);
    fake::gain_ok = false;
    assert(!trail_mate_t_display_p4_audio_begin(walkie, 16000));
    assert(s_audio.owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NONE);
    release_audio_resources_locked();
    assert(fake::live == 0 && fake::channel_live == 0);
    std::cout << "P4 audio allocation recovery, ownership, gain and PCM passed\n";
}
