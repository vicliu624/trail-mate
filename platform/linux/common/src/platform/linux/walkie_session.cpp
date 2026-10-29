#include "platform/linux/walkie_session.h"
#include "platform/audio/audio_focus.h"
#include "walkie/packet.h"
#include <algorithm>
#include <array>
#include <codec2.h>
#include <cstring>
#include <string>

namespace platform::linux_runtime
{
namespace wire = ::walkie::core;
namespace
{
struct Codec
{
    static CODEC2* create()
    {
        auto* state = codec2_create(CODEC2_MODE_3200);
        if (state) codec2_set_lpc_post_filter(state, 1, 0, 0.8f, 0.2f);
        return state;
    }
    CODEC2* value = create();
    ~Codec()
    {
        if (value) codec2_destroy(value);
    }
    bool valid() const { return value && codec2_samples_per_frame(value) == wire::samples_per_frame &&
                                codec2_bytes_per_frame(value) == wire::bytes_per_frame; }
    void reset()
    {
        if (value) codec2_destroy(value);
        value = create();
    }
};
uint8_t level(const int16_t* samples, std::size_t count)
{
    int peak = 0;
    for (std::size_t index = 0; index < count; ++index) peak = std::max(peak, std::abs(int(samples[index])));
    return static_cast<uint8_t>(std::min(100, (peak * 100 + 16383) / 32767));
}
} // namespace
struct WalkieSession::Impl
{
    Impl(IPacketRadio& port, IWalkieAudio& sound) : radio(port), audio(sound) {}
    bool fail(const char* message)
    {
        error = message && *message ? message : "Walkie operation failed";
        return false;
    }
    bool begin(const WalkieOptions& value)
    {
        end();
        error.clear();
        options = value;
        session = value.session_seed;
        if (!value.self_id || !encoder.valid() || !decoder.valid()) return fail("Identity or Codec2 is unavailable");
        if (!radio.supportsFsk()) return fail("Radio has no FSK backend");
        if (!radio.acquire()) return fail(radio.lastError());
        acquired = true;
        focus = std::make_unique<::platform::audio::FocusScope>(::platform::audio::FocusOwner::Walkie, true);
        if (!*focus)
        {
            end();
            return fail("Audio is already owned by another operation");
        }
        FskSettings config;
        config.freq_mhz = value.radio.freq_mhz;
        config.tx_power_dbm = value.radio.tx_power_dbm;
        if (!radio.configureFsk(config) || !radio.startReceive())
        {
            const std::string message = radio.lastError();
            end();
            return fail(message.c_str());
        }
        state.active = true;
        state.freq_mhz = config.freq_mhz;
        return true;
    }
    void end()
    {
        audio.stopCapture();
        audio.stopPlayback();
        if (acquired)
        {
            radio.release();
            acquired = false;
        }
        focus.reset();
        state = {};
        capture_count = pending_frames = 0;
        receiving.clear();
        playing = false;
    }
    bool sendFrame()
    {
        for (auto& sample : capture_pcm) sample = std::clamp(int(sample) * 8 / 5, -32768, 32767);
        state.tx_level = level(capture_pcm.data(), capture_pcm.size());
        if (!pending_frames) packet.header.first_frame = next_frame;
        codec2_encode(encoder.value, packet.payload.data() + pending_frames * wire::bytes_per_frame, capture_pcm.data());
        ++pending_frames;
        ++next_frame;
        capture_count = 0;
        if (pending_frames < wire::frames_per_packet) return true;
        packet.header.sender = options.self_id;
        packet.header.session = session;
        packet.header.sequence = sequence++;
        const auto bytes = wire::encode_packet(packet);
        pending_frames = 0;
        return radio.transmit(bytes.data(), bytes.size()) || fail(radio.lastError());
    }
    bool capture()
    {
        if (!audio.captureActive()) return fail("Microphone is no longer active");
        if (audio.captureDropped() != capture_dropped) return fail("Microphone capture overrun");
        for (unsigned batch = 0; batch < 32; ++batch)
        {
            const auto count = audio.capture(capture_pcm.data() + capture_count, wire::samples_per_frame - capture_count);
            if (!count) break;
            if (count > wire::samples_per_frame - capture_count) return fail("Invalid audio capture count");
            capture_count += count;
            if (capture_count != wire::samples_per_frame) continue;
            if (!sendFrame()) return false;
        }
        return true;
    }
    bool step(bool ptt, uint32_t now, uint8_t volume)
    {
        if (!state.active || !radio.isOnline()) return fail("Walkie radio is offline");
        if (ptt && !state.tx)
        {
            audio.stopPlayback();
            receiving.clear();
            playing = false;
            state.rx_level = 0;
            encoder.reset();
            if (!encoder.valid()) return fail("Cannot reset voice encoder");
            if (!audio.startCapture()) return fail(audio.lastError());
            capture_dropped = audio.captureDropped();
            capture_count = pending_frames = 0;
            if (++session == 0) ++session;
            sequence = next_frame = 0;
            state.tx = true;
        }
        if (state.tx)
        {
            if (!capture()) return false;
            if (ptt) return true;
            audio.stopCapture();
            if (capture_count)
            {
                std::fill(capture_pcm.begin() + capture_count, capture_pcm.end(), 0);
                if (!sendFrame()) return false;
            }
            while (pending_frames)
            {
                capture_pcm.fill(0);
                if (!sendFrame()) return false;
            }
            state.tx = false;
            state.tx_level = 0;
            if (!radio.startReceive()) return fail(radio.lastError());
        }
        for (unsigned packet_count = 0; packet_count < 8 && radio.pollReceive(&received); ++packet_count)
        {
            wire::Packet incoming;
            if (!wire::decode_packet(received.data.data(), received.size, incoming)) continue;
            const auto accepted = receiving.push(incoming, options.self_id);
            if (accepted == wire::ReceiveBuffer::Push::NewSession)
            {
                decoder.reset();
                if (!decoder.valid()) return fail("Cannot reset voice decoder");
                last_pcm.fill(0);
                playing = false;
                underruns = 0;
            }
        }
        if (!playing && receiving.size() >= 10)
        {
            playing = true;
            play_at = now;
            underruns = 0;
        }
        if (!playing)
        {
            state.rx_level = 0;
            return true;
        }
        for (unsigned catchup = 0; catchup < 4 && int32_t(now - play_at) >= 0 && audio.playbackReady(); ++catchup)
        {
            wire::Frame frame;
            const bool present = receiving.pop(frame);
            if (!present && ++underruns > 3)
            {
                playing = false;
                state.rx_level = 0;
                audio.stopPlayback();
                break;
            }
            if (present && !frame.missing)
            {
                codec2_decode(decoder.value, last_pcm.data(), frame.bytes.data());
                underruns = 0;
            }
            else
                for (auto& sample : last_pcm) sample = int(sample) * 4 / 5;
            state.rx_level = level(last_pcm.data(), last_pcm.size());
            for (unsigned index = 0; index < wire::samples_per_frame; ++index)
                playback_pcm[index] = std::clamp(int(last_pcm[index]) * 2 * std::min<int>(volume, 100) / 100, -32768, 32767);
            if (!audio.play(playback_pcm.data(), playback_pcm.size())) return fail(audio.lastError());
            play_at += 20;
            if (int32_t(now - play_at) > 100) play_at = now + 20;
        }
        return true;
    }
    IPacketRadio& radio;
    IWalkieAudio& audio;
    WalkieOptions options;
    bool acquired = false, playing = false;
    uint16_t session = 0, sequence = 0, next_frame = 0;
    unsigned capture_count = 0, pending_frames = 0, underruns = 0;
    uint64_t capture_dropped = 0;
    uint32_t play_at = 0;
    Codec encoder, decoder;
    std::unique_ptr<::platform::audio::FocusScope> focus;
    wire::Packet packet;
    wire::ReceiveBuffer receiving;
    RadioPacket received;
    std::array<int16_t, wire::samples_per_frame> capture_pcm{}, playback_pcm{}, last_pcm{};
    ::platform::ui::walkie::Status state;
    std::string error;
};
WalkieSession::WalkieSession(IPacketRadio& radio, IWalkieAudio& audio) : impl_(std::make_unique<Impl>(radio, audio)) {}
WalkieSession::~WalkieSession() { impl_->end(); }
bool WalkieSession::begin(const WalkieOptions& options) { return impl_->begin(options); }
bool WalkieSession::step(bool ptt, uint32_t now, uint8_t volume) { return impl_->step(ptt, now, volume); }
void WalkieSession::end() { impl_->end(); }
::platform::ui::walkie::Status WalkieSession::status() const { return impl_->state; }
const char* WalkieSession::lastError() const { return impl_->error.c_str(); }
} // namespace platform::linux_runtime
