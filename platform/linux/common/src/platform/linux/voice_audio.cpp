#include "platform/linux/voice_audio.h"
#include "platform/audio/audio_focus.h"
#include "platform/audio/media_volume.h"
#include "platform/linux/sdl_pcm_input.h"
#include "platform/linux/sdl_pcm_output.h"
#include <algorithm>
#include <chrono>
#include <codec2.h>
#include <cstdlib>
#include <thread>

namespace platform::linux_runtime
{
namespace
{
using Clock = std::chrono::steady_clock;
class Pcm final : public IVoicePcm
{
  public:
    bool beginCapture() override
    {
        input = std::make_unique<SdlPcmInput>();
        return input->begin(8000, std::getenv("TRAIL_MATE_VOICE_INPUT_DEVICE"));
    }
    size_t read(int16_t* out, size_t count) override { return input ? input->read(out, count) : 0; }
    bool captureActive() const override { return input && input->active(); }
    uint64_t dropped() const override { return input ? input->dropped_samples() : 0; }
    void endCapture() noexcept override { input.reset(); }
    bool beginPlayback() override
    {
        output = std::make_unique<SdlPcmOutput>(false);
        return output->begin(8000);
    }
    bool writable() const override { return output && !output->failed() && output->needs_frame(); }
    bool write(const int16_t* samples, size_t count) override { return output && output->write(samples, count); }
    bool drain() override { return output && !output->failed() && output->drain(); }
    void endPlayback() noexcept override { output.reset(); }

  private:
    std::unique_ptr<SdlPcmInput> input;
    std::unique_ptr<SdlPcmOutput> output;
};
struct Codec
{
    CODEC2* value = codec2_create(CODEC2_MODE_1300);
    ~Codec()
    {
        if (value) codec2_destroy(value);
    }
    bool valid() const
    {
        return value && codec2_samples_per_frame(value) == VoiceClip::samples_per_frame &&
               codec2_bytes_per_frame(value) == VoiceClip::bytes_per_frame;
    }
};
struct Close
{
    IVoicePcm& pcm;
    bool capture;
    ~Close()
    {
        if (capture) pcm.endCapture();
        else pcm.endPlayback();
    }
};
struct ClearPcm
{
    std::array<int16_t, VoiceClip::samples_per_frame>& samples;
    ~ClearPcm()
    {
        volatile int16_t* values = samples.data();
        for (size_t i = 0; i < samples.size(); ++i) values[i] = 0;
    }
};
void pause() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
} // namespace
void VoiceClip::clear()
{
    volatile uint8_t* cursor = bytes.data();
    for (size_t i = 0; i < bytes.size(); ++i) cursor[i] = 0;
    size = 0;
}
struct NativeVoiceAudio::Impl
{
    explicit Impl(std::unique_ptr<IVoicePcm> value) : pcm(value ? std::move(value) : std::make_unique<Pcm>()) {}
    std::unique_ptr<IVoicePcm> pcm;
    VoiceClip clip;
    std::array<int16_t, VoiceClip::samples_per_frame> samples{};
};
NativeVoiceAudio::NativeVoiceAudio(std::unique_ptr<IVoicePcm> pcm) : impl_(std::make_unique<Impl>(std::move(pcm))) {}
NativeVoiceAudio::~NativeVoiceAudio() { impl_->clip.clear(); }
const VoiceClip& NativeVoiceAudio::captured() const { return impl_->clip; }
NativeVoiceAudio::Result NativeVoiceAudio::capture(const std::atomic_bool& stop, const std::atomic_bool& cancel)
{
    ClearPcm clear{impl_->samples};
    impl_->clip.clear();
    if (stop || cancel) return Result::Cancelled;
    ::platform::audio::FocusScope focus(::platform::audio::FocusOwner::VoiceMessage, true);
    if (!focus) return Result::Busy;
    Codec codec;
    if (!codec.valid()) return Result::CodecFailure;
    Close close{*impl_->pcm, true};
    Result result = Result::AudioFailure;
    try
    {
        if (!impl_->pcm->beginCapture()) return Result::AudioFailure;
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        size_t filled = 0;
        bool failed = false;
        while (impl_->clip.size < impl_->clip.bytes.size() && Clock::now() < deadline)
        {
            if (cancel)
            {
                result = Result::Cancelled;
                break;
            }
            if (stop)
            {
                result = impl_->clip.valid() ? Result::Complete : Result::Cancelled;
                break;
            }
            if (!impl_->pcm->captureActive() || impl_->pcm->dropped())
            {
                failed = true;
                break;
            }
            const auto remaining = impl_->samples.size() - filled;
            const auto count = impl_->pcm->read(impl_->samples.data() + filled, remaining);
            if (count > remaining || impl_->pcm->dropped())
            {
                failed = true;
                break;
            }
            filled += count;
            if (filled == impl_->samples.size())
            {
                codec2_encode(codec.value, impl_->clip.bytes.data() + impl_->clip.size, impl_->samples.data());
                impl_->clip.size += VoiceClip::bytes_per_frame;
                filled = 0;
            }
            if (impl_->clip.size == impl_->clip.bytes.size()) result = Result::Complete;
            if (!count) pause();
        }
        // A normal time limit preserves complete frames, while a device error
        // or overrun never turns a partial corrupt recording into valid media.
        if (!failed && Clock::now() >= deadline && impl_->clip.valid() && impl_->pcm->captureActive() && !impl_->pcm->dropped()) result = Result::Complete;
    }
    catch (...)
    {
        result = Result::AudioFailure;
    }
    if (cancel) result = Result::Cancelled;
    if (result != Result::Complete) impl_->clip.clear();
    impl_->samples.fill(0);
    return result;
}
NativeVoiceAudio::Result NativeVoiceAudio::play(const VoiceClip& clip, ::chat::voice::vmp::Codec kind,
                                                unsigned volume, const std::atomic_bool& cancel)
{
    ClearPcm clear{impl_->samples};
    if (!clip.valid() || kind != ::chat::voice::vmp::Codec::Codec2_1300) return Result::InvalidMedia;
    if (cancel) return Result::Cancelled;
    ::platform::audio::FocusScope focus(::platform::audio::FocusOwner::VoiceMessage, true);
    if (!focus) return Result::Busy;
    Codec codec;
    if (!codec.valid()) return Result::CodecFailure;
    Close close{*impl_->pcm, false};
    try
    {
        if (!impl_->pcm->beginPlayback()) return Result::AudioFailure;
        const auto deadline = Clock::now() + std::chrono::milliseconds(clip.duration_ms() + 2000);
        for (size_t offset = 0; offset < clip.size; offset += VoiceClip::bytes_per_frame)
        {
            if (Clock::now() >= deadline) return Result::AudioFailure;
            while (!impl_->pcm->writable())
            {
                if (cancel) return Result::Cancelled;
                if (Clock::now() >= deadline) return Result::AudioFailure;
                pause();
            }
            if (cancel) return Result::Cancelled;
            codec2_decode(codec.value, impl_->samples.data(), clip.bytes.data() + offset);
            const int gain = static_cast<int>(std::min(volume, 100u) * ::platform::audio::media_volume(100) / 100);
            for (auto& sample : impl_->samples) sample = static_cast<int16_t>(int(sample) * gain / 100);
            if (!impl_->pcm->write(impl_->samples.data(), impl_->samples.size())) return Result::AudioFailure;
        }
        while (!impl_->pcm->drain())
        {
            if (cancel) return Result::Cancelled;
            if (Clock::now() >= deadline) return Result::AudioFailure;
            pause();
        }
        return cancel ? Result::Cancelled : Clock::now() >= deadline ? Result::AudioFailure
                                                                     : Result::Complete;
    }
    catch (...)
    {
        return Result::AudioFailure;
    }
}
} // namespace platform::linux_runtime
