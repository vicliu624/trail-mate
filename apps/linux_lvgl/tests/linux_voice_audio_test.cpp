#include "platform/audio/audio_focus.h"
#include "platform/audio/media_volume.h"
#include "platform/linux/voice_audio.h"
#include <algorithm>
#include <cassert>
#include <codec2.h>
#include <stdexcept>
#include <vector>

using namespace platform::linux_runtime;
using Result = NativeVoiceAudio::Result;
namespace vmp = chat::voice::vmp;
struct Pcm final : IVoicePcm
{
    std::atomic_bool* stop = nullptr;
    std::atomic_bool* cancel = nullptr;
    size_t position = 0, stop_at = 0, overflow_at = 0, cancel_at = 0;
    unsigned capture_opens = 0, capture_closes = 0, playback_opens = 0, playback_closes = 0;
    bool fail_capture = false, fail_write = false, fail_read = false, capturing = false, playing = false;
    bool cancel_playback = false;
    std::vector<int16_t> played;
    static int16_t sample(size_t i) { return static_cast<int16_t>((int(i % 80) - 40) * 200); }
    bool beginCapture() override
    {
        ++capture_opens;
        position = 0;
        capturing = !fail_capture;
        return capturing;
    }
    size_t read(int16_t* out, size_t count) override
    {
        if (fail_read) throw std::runtime_error("input disconnected");
        count = std::min<size_t>(count, 37); // Exercise partial Codec2 frame assembly.
        for (size_t i = 0; i < count; ++i) out[i] = sample(position++);
        if (stop && stop_at && position >= stop_at) *stop = true;
        if (cancel && cancel_at && position >= cancel_at) *cancel = true;
        return count;
    }
    bool captureActive() const override { return capturing; }
    uint64_t dropped() const override { return overflow_at && position >= overflow_at ? 1 : 0; }
    void endCapture() noexcept override
    {
        ++capture_closes;
        capturing = false;
    }
    bool beginPlayback() override
    {
        ++playback_opens;
        played.clear();
        playing = true;
        return true;
    }
    bool writable() const override { return playing; }
    bool write(const int16_t* data, size_t count) override
    {
        assert(count == VoiceClip::samples_per_frame);
        if (fail_write) return false;
        played.insert(played.end(), data, data + count);
        if (cancel_playback && cancel) *cancel = true;
        return true;
    }
    bool drain() override { return true; }
    void endPlayback() noexcept override
    {
        ++playback_closes;
        playing = false;
    }
};
int main()
{
    platform::audio::publish_media_volume(100);
    std::atomic_bool stop{false}, cancel{false};
    auto backend = std::make_unique<Pcm>();
    auto* pcm = backend.get();
    pcm->stop = &stop;
    pcm->cancel = &cancel;
    NativeVoiceAudio audio(std::move(backend));
    assert(audio.capture(stop, cancel) == Result::Complete);
    const auto clip = audio.captured();
    assert(clip.valid() && clip.size == 875 && clip.duration_ms() == 5000);
    assert(pcm->capture_closes == 1 && !platform::audio::music_blocked());
    // Check frame boundaries against an independently assembled first frame.
    auto* encoder = codec2_create(CODEC2_MODE_1300);
    std::array<int16_t, 320> frame;
    for (size_t i = 0; i < frame.size(); ++i) frame[i] = Pcm::sample(i);
    std::array<uint8_t, 7> encoded{};
    codec2_encode(encoder, encoded.data(), frame.data());
    codec2_destroy(encoder);
    assert(std::equal(encoded.begin(), encoded.end(), clip.bytes.begin()));
    assert(audio.play(clip, vmp::Codec::Codec2_1300, 100, cancel) == Result::Complete);
    assert(pcm->played.size() == 40000 && pcm->playback_closes == 1 && !platform::audio::music_blocked());
    auto* decoder = codec2_create(CODEC2_MODE_1300);
    uint64_t reference_energy = 0, playback_energy = 0;
    for (size_t offset = 0; offset < clip.size; offset += 7)
    {
        codec2_decode(decoder, frame.data(), clip.bytes.data() + offset);
        for (const auto sample : frame) reference_energy += int64_t(sample) * sample;
    }
    for (const auto sample : pcm->played) playback_energy += int64_t(sample) * sample;
    codec2_destroy(decoder);
    // Codec2 synthesis consumes internal random phase, so independent decoders
    // need comparable signal energy rather than sample-for-sample identity.
    assert(reference_energy && playback_energy && reference_energy < playback_energy * 4 && playback_energy < reference_energy * 4);
    assert(audio.play(clip, vmp::Codec::Codec2_1300, 0, cancel) == Result::Complete);
    assert(std::all_of(pcm->played.begin(), pcm->played.end(), [](auto value)
                       { return value == 0; }));
    auto bad = clip;
    bad.size = 8;
    assert(audio.play(bad, vmp::Codec::Codec2_1300, 100, cancel) == Result::InvalidMedia);
    assert(audio.play(clip, vmp::Codec::Codec2_1200, 100, cancel) == Result::InvalidMedia);
    {
        platform::audio::FocusScope call(platform::audio::FocusOwner::Call, true);
        assert(call);
        assert(audio.capture(stop, cancel) == Result::Busy && !audio.captured().valid());
        assert(audio.play(clip, vmp::Codec::Codec2_1300, 100, cancel) == Result::Busy);
    }
    pcm->stop_at = 640;
    assert(audio.capture(stop, cancel) == Result::Complete);
    assert(audio.captured().size == 14 && audio.captured().duration_ms() == 80);
    stop = false;
    pcm->stop_at = 100;
    assert(audio.capture(stop, cancel) == Result::Cancelled && !audio.captured().valid());
    stop = false;
    pcm->stop_at = 0;
    pcm->overflow_at = 700;
    assert(audio.capture(stop, cancel) == Result::AudioFailure && !audio.captured().valid());
    assert(std::all_of(audio.captured().bytes.begin(), audio.captured().bytes.end(), [](auto byte)
                       { return byte == 0; }));
    pcm->overflow_at = 0;
    pcm->cancel_at = 700;
    assert(audio.capture(stop, cancel) == Result::Cancelled && !audio.captured().valid());
    assert(!pcm->capturing && !platform::audio::music_blocked());
    cancel = false;
    pcm->cancel_at = 0;
    pcm->fail_read = true;
    assert(audio.capture(stop, cancel) == Result::AudioFailure && !audio.captured().valid());
    pcm->fail_read = false;
    pcm->fail_capture = true;
    const auto closes = pcm->capture_closes;
    assert(audio.capture(stop, cancel) == Result::AudioFailure && pcm->capture_closes == closes + 1);
    pcm->fail_write = true;
    assert(audio.play(clip, vmp::Codec::Codec2_1300, 100, cancel) == Result::AudioFailure && !pcm->playing);
    pcm->fail_write = false;
    pcm->cancel_playback = true;
    assert(audio.play(clip, vmp::Codec::Codec2_1300, 100, cancel) == Result::Cancelled);
    assert(!pcm->playing && !platform::audio::music_blocked());
    const auto opens = pcm->capture_opens;
    assert(audio.capture(stop, cancel) == Result::Cancelled && pcm->capture_opens == opens);
    // Exercise the production SDL adapter too, with an explicitly dummy device.
    stop = false;
    cancel = false;
    NativeVoiceAudio native;
    const auto recorded = native.capture(stop, cancel);
    assert(recorded == Result::Complete && native.captured().valid());
    assert(native.captured().duration_ms() <= 5000);
    assert(native.play(native.captured(), vmp::Codec::Codec2_1300, 100, cancel) == Result::Complete);
    assert(!platform::audio::music_blocked());
}
