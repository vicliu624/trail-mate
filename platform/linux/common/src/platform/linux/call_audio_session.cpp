#include "platform/linux/call_audio_session.h"
#include "chat/infra/reticulum/audio_call_wire.h"
#include "platform/audio/call_echo_guard.h"
#include <algorithm>
#include <array>
#include <codec2.h>
#include <cstring>

namespace platform::linux_runtime
{
namespace call = ::platform::ui::reticulum_call;
namespace wire = ::chat::reticulum::audio_call;
namespace
{
struct Selection
{
    int codec;
    wire::Codec2Mode wire;
    uint32_t packet_ms;
};
bool selection(call::Codec2Mode mode, Selection& out)
{
    switch (mode)
    {
    case call::Codec2Mode::Mode3200:
        out = {CODEC2_MODE_3200, wire::Codec2Mode::Mode3200, 200};
        return true;
    case call::Codec2Mode::Mode1600:
        out = {CODEC2_MODE_1600, wire::Codec2Mode::Mode1600, 320};
        return true;
    case call::Codec2Mode::Mode1200:
        out = {CODEC2_MODE_1200, wire::Codec2Mode::Mode1200, 520};
        return true;
    case call::Codec2Mode::Mode700C:
        out = {CODEC2_MODE_700C, wire::Codec2Mode::Mode700C, 520};
        return true;
    }
    return false;
}
} // namespace
struct CallAudioSession::Impl
{
    Impl(ICallPcm& pcm, ICallAudioPackets& packets) : pcm(pcm), packets(packets) {}
    bool same(const call::Snapshot& state) const
    {
        return state.accepted && state.link_active && state.media_active &&
               (state.state == call::State::Incoming || state.state == call::State::Outgoing || state.state == call::State::Active) &&
               (state.realtime_phase == call::RealtimePhase::AcceptedStarting || state.realtime_phase == call::RealtimePhase::ActiveCall) &&
               state.codec2_mode == initial.codec2_mode && !std::memcmp(state.link_id, initial.link_id, call::kHashSize);
    }
    void clear()
    {
        pcm.close();
        if (encoder) codec2_destroy(encoder);
        if (decoder) codec2_destroy(decoder);
        encoder = decoder = nullptr;
        running = false;
        samples = bytes = fill = tx_count = head = count = 0;
        capture.fill(0);
        playback.fill(0);
        tx.fill(0);
        jitter = {};
        inbound = {};
        outbound = {};
        echo = platform::audio::CallEchoGuard(8000);
    }
    bool begin(const call::Snapshot& state, uint32_t now)
    {
        clear();
        stats = {};
        initial = state;
        if (!same(state) || !std::any_of(std::begin(state.link_id), std::end(state.link_id), [](uint8_t byte)
                                         { return byte != 0; }) ||
            !selection(state.codec2_mode, selected)) return false;
        encoder = codec2_create(selected.codec);
        decoder = codec2_create(selected.codec);
        if (!encoder || !decoder)
        {
            clear();
            return false;
        }
        samples = codec2_samples_per_frame(encoder);
        bytes = codec2_bytes_per_frame(encoder);
        if (!samples || samples > capture.size() || !bytes || bytes > 8 ||
            codec2_samples_per_frame(decoder) != int(samples) || codec2_bytes_per_frame(decoder) != int(bytes))
        {
            clear();
            return false;
        }
        codec2_set_lpc_post_filter(decoder, 1, 0, .8f, .2f);
        interval = samples * 1000 / 8000;
        per_packet = std::min<uint32_t>(13, std::max<uint32_t>(1, selected.packet_ms / interval));
        if (!pcm.open(8000) || !pcm.captureActive())
        {
            clear();
            return false;
        }
        pcm.discardCapture();
        dropped = pcm.captureDropped();
        last_capture = last_play = next_play = now;
        previous_tx = previous_rx = false;
        primed = false;
        first_step = true;
        running = true;
        return true;
    }
    bool step(const call::Snapshot& state, uint32_t now, uint8_t volume)
    {
        if (!running || !same(state) || !pcm.captureActive() || pcm.captureDropped() != dropped) return false;
        if (first_step)
        {
            // Device startup may have blocked. Watchdog time starts when this
            // owner can actually service the newly opened stream.
            last_capture = last_play = next_play = now;
            first_step = false;
        }
        const bool active = state.state == call::State::Active;
        const bool tx_allowed = active && !state.microphone_muted && (state.duplex_mode == call::DuplexMode::Full || state.ptt_pressed);
        const bool rx_allowed = active && !state.speaker_muted && !(state.duplex_mode == call::DuplexMode::Half && state.ptt_pressed);
        if (tx_allowed != previous_tx)
        {
            pcm.discardCapture();
            tx_count = fill = 0;
            capture.fill(0);
            tx.fill(0);
            previous_tx = tx_allowed;
        }
        if (rx_allowed != previous_rx)
        {
            head = count = 0;
            primed = false;
            pcm.discardPlayback();
            previous_rx = rx_allowed;
        }
        // Drain transport queues even while muted, so reopening never replays
        // an old remote burst. A mismatched session/codec is never decoded.
        for (unsigned drained = 0; drained < 4 && packets.receive(inbound); ++drained)
        {
            wire::DecodedPayload decoded;
            if (!rx_allowed) continue;
            if (std::memcmp(inbound.link_id, initial.link_id, call::kHashSize) || inbound.len > sizeof(inbound.data) ||
                !wire::decodePayload(inbound.data, inbound.len, &decoded) || !decoded.encoded || decoded.mode != selected.wire ||
                !decoded.encoded_len || decoded.encoded_len % bytes || decoded.encoded_len / bytes > 13)
            {
                ++stats.rejected_packets;
                continue;
            }
            for (size_t at = 0; at < decoded.encoded_len; at += bytes)
            {
                if (count == jitter.size())
                {
                    head = (head + 1) % jitter.size();
                    --count;
                    ++stats.jitter_dropped;
                }
                std::memcpy(jitter[(head + count) % jitter.size()].data(), decoded.encoded + at, bytes);
                ++count;
            }
        }
        const auto received = pcm.read(capture.data() + fill, samples - fill);
        if (received > samples - fill) return false;
        if (received)
        {
            fill += received;
            last_capture = now;
        }
        else if (uint32_t(now - last_capture) > 1000) return false;
        if (fill == samples)
        {
            if (tx_allowed)
            {
                if (echo.suppressCapture(capture.data(), samples)) ++stats.echo_suppressed;
                std::memset(tx.data() + tx_count * bytes, 0, bytes);
                codec2_encode(encoder, tx.data() + tx_count * bytes, capture.data());
                ++stats.captured_frames;
                if (++tx_count == per_packet)
                {
                    std::memcpy(outbound.link_id, initial.link_id, call::kHashSize);
                    outbound.len = sizeof(outbound.data);
                    if (!wire::encodePayload(selected.wire, tx.data(), tx_count * bytes, outbound.data, &outbound.len)) return false;
                    if (packets.transmit(outbound)) ++stats.queued_packets;
                    else ++stats.queue_dropped;
                    tx_count = 0;
                }
            }
            fill = 0;
        }
        if (int32_t(now - next_play) < 0) return true;
        // Never accelerate through an accumulated deadline after a scheduler
        // stall. The PCM port also bounds its own output queue.
        next_play = now + interval;
        if (!pcm.writable()) return uint32_t(now - last_play) <= 1000;
        playback.fill(0);
        if (rx_allowed)
        {
            if (!primed && count >= 3) primed = true;
            if (primed && count)
            {
                codec2_decode(decoder, playback.data(), jitter[head].data());
                head = (head + 1) % jitter.size();
                --count;
                ++stats.decoded_frames;
            }
            else if (primed)
            {
                primed = false;
                ++stats.underruns;
            }
        }
        const auto gain = std::min<unsigned>(100, volume);
        for (size_t index = 0; index < samples; ++index) playback[index] = int16_t(int32_t(playback[index]) * gain / 100);
        echo.observeRender(playback.data(), samples);
        if (!pcm.write(playback.data(), samples)) return false;
        last_play = now;
        return true;
    }
    ICallPcm& pcm;
    ICallAudioPackets& packets;
    call::Snapshot initial;
    Selection selected{};
    CODEC2 *encoder = nullptr, *decoder = nullptr;
    std::array<int16_t, 320> capture{}, playback{};
    std::array<uint8_t, 13 * 8> tx{};
    std::array<std::array<uint8_t, 8>, 32> jitter{};
    call::AudioPacket inbound, outbound;
    platform::audio::CallEchoGuard echo{8000};
    CallAudioStatistics stats;
    size_t samples = 0, bytes = 0, fill = 0, tx_count = 0, head = 0, count = 0;
    uint32_t interval = 0, per_packet = 0, next_play = 0, last_capture = 0, last_play = 0;
    uint64_t dropped = 0;
    bool running = false, previous_tx = false, previous_rx = false, primed = false, first_step = false;
};
CallAudioSession::CallAudioSession(ICallPcm& pcm, ICallAudioPackets& packets) : impl_(std::make_unique<Impl>(pcm, packets)) {}
CallAudioSession::~CallAudioSession() { impl_->clear(); }
bool CallAudioSession::begin(const call::Snapshot& state, uint32_t now) { return impl_->begin(state, now); }
bool CallAudioSession::step(const call::Snapshot& state, uint32_t now, uint8_t volume) { return impl_->step(state, now, volume); }
void CallAudioSession::end() { impl_->clear(); }
CallAudioStatistics CallAudioSession::statistics() const { return impl_->stats; }
} // namespace platform::linux_runtime
