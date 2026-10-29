#include "chat/infra/reticulum/audio_call_wire.h"
#include "platform/linux/call_audio_session.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <deque>
#include <iostream>
#include <vector>

namespace call = platform::ui::reticulum_call;
namespace native = platform::linux_runtime;
namespace wire = chat::reticulum::audio_call;
struct Pcm final : native::ICallPcm
{
    bool open(uint32_t rate) override
    {
        assert(rate == 8000);
        opened = true;
        return !fail_open;
    }
    bool captureActive() const override { return opened && active; }
    uint64_t captureDropped() const override { return dropped; }
    size_t read(int16_t* out, size_t count) override
    {
        if (stall) return 0;
        count = std::min(count, maximum_read);
        for (size_t index = 0; index < count; ++index) out[index] = int16_t(6000 * std::sin(double(position++) * .15));
        return count;
    }
    bool writable() const override { return opened && writable_now; }
    void discardCapture() override { ++capture_discarded; }
    bool write(const int16_t* values, size_t count) override
    {
        rendered.assign(values, values + count);
        return !fail_write;
    }
    void discardPlayback() override
    {
        rendered.clear();
        ++discarded;
    }
    void close() noexcept override
    {
        if (opened) ++closed;
        opened = false;
    }
    bool opened = false, active = true, fail_open = false, fail_write = false, writable_now = true, stall = false;
    size_t position = 0, maximum_read = 320;
    uint64_t dropped = 0;
    unsigned discarded = 0, capture_discarded = 0, closed = 0;
    std::vector<int16_t> rendered;
};
struct Packets final : native::ICallAudioPackets
{
    bool receive(call::AudioPacket& out) override
    {
        if (incoming.empty()) return false;
        out = incoming.front();
        incoming.pop_front();
        return true;
    }
    bool transmit(const call::AudioPacket& value) override
    {
        outgoing.push_back(value);
        return true;
    }
    std::deque<call::AudioPacket> incoming;
    std::vector<call::AudioPacket> outgoing;
};
call::Snapshot state(call::Codec2Mode mode)
{
    call::Snapshot value;
    value.link_id[0] = 1;
    value.accepted = value.link_active = value.media_active = true;
    value.state = call::State::Active;
    value.realtime_phase = call::RealtimePhase::ActiveCall;
    value.codec2_mode = mode;
    return value;
}
int main()
{
    for (const auto mode : {call::Codec2Mode::Mode3200, call::Codec2Mode::Mode1600, call::Codec2Mode::Mode1200, call::Codec2Mode::Mode700C})
    {
        Pcm pcm;
        Packets packets;
        native::CallAudioSession session(pcm, packets);
        auto controls = state(mode);
        uint32_t now = UINT32_MAX - 1000;
        assert(session.begin(controls, now));
        for (unsigned tick = 0; tick < 50; ++tick)
        {
            assert(session.step(controls, now, 100));
            now += 40;
        }
        assert(session.statistics().captured_frames == 50 && packets.outgoing.size() >= 3);
        const auto packet = packets.outgoing.front();
        wire::DecodedPayload decoded;
        assert(wire::decodePayload(packet.data, packet.len, &decoded));
        const auto expected = mode == call::Codec2Mode::Mode3200 ? wire::Codec2Mode::Mode3200 : mode == call::Codec2Mode::Mode1600 ? wire::Codec2Mode::Mode1600
                                                                                            : mode == call::Codec2Mode::Mode1200   ? wire::Codec2Mode::Mode1200
                                                                                                                                   : wire::Codec2Mode::Mode700C;
        assert(decoded.mode == expected && decoded.encoded_len && packet.len <= call::kAudioPacketMaxLen);
        controls.microphone_muted = true;
        const auto transmitted = packets.outgoing.size();
        for (unsigned tick = 0; tick < 10; ++tick)
        {
            assert(session.step(controls, now, 100));
            now += 40;
        }
        assert(packets.outgoing.size() == transmitted);
        for (unsigned index = 0; index < 6; ++index) packets.incoming.push_back(packet);
        for (unsigned tick = 0; tick < 10; ++tick)
        {
            assert(session.step(controls, now, 0));
            now += 40;
        }
        assert(session.statistics().decoded_frames > 0 && session.statistics().jitter_dropped > 0);
        for (auto sample : pcm.rendered) assert(sample == 0);
        controls.speaker_muted = true;
        const auto frames = session.statistics().decoded_frames;
        packets.incoming.push_back(packet);
        assert(session.step(controls, now, 100));
        now += 40;
        assert(session.statistics().decoded_frames == frames && pcm.discarded > 0);
        for (auto sample : pcm.rendered) assert(sample == 0);
        controls.speaker_muted = false;
        auto foreign = packet;
        foreign.link_id[0] = 2;
        packets.incoming.push_back(foreign);
        auto truncated = packet;
        truncated.len = 1;
        packets.incoming.push_back(truncated);
        assert(session.step(controls, now, 100));
        now += 40;
        assert(session.statistics().rejected_packets == 2);
        controls.microphone_muted = false;
        controls.duplex_mode = call::DuplexMode::Half;
        controls.ptt_pressed = false;
        for (unsigned tick = 0; tick < 20; ++tick)
        {
            assert(session.step(controls, now, 100));
            now += 40;
        }
        assert(packets.outgoing.size() == transmitted);
        controls.ptt_pressed = true;
        for (unsigned tick = 0; tick < 20; ++tick)
        {
            assert(session.step(controls, now, 100));
            now += 40;
        }
        assert(packets.outgoing.size() > transmitted);
        pcm.dropped = 1;
        assert(!session.step(controls, now, 100));
        session.end();
        assert(!pcm.opened && pcm.closed == 1);
        pcm.dropped = 0;
        pcm.maximum_read = 17;
        assert(session.begin(controls, 0));
        for (unsigned tick = 0; tick < 200; ++tick) assert(session.step(controls, tick * 2, 100));
        assert(session.statistics().captured_frames > 0);
        pcm.stall = true;
        assert(!session.step(controls, 2000, 100));
        session.end();
        assert(!pcm.opened);
        pcm.fail_open = true;
        assert(!session.begin(controls, 0) && !pcm.opened);
    }
    std::cout << "Native call codecs, bounded jitter, session guards, mute/PTT, partial capture, clock wrap and cleanup passed\n";
}
