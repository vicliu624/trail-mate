#include "platform/audio/audio_focus.h"
#include "platform/linux/walkie_session.h"
#include "walkie/packet.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <stdexcept>
#include <vector>

using namespace platform::linux_runtime;
namespace wire = walkie::core;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
class Radio final : public IPacketRadio
{
  public:
    const char* name() const override { return "test"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        online = true;
        ++opens;
        return true;
    }
    void release() override
    {
        online = false;
        ++closes;
    }
    bool configureLoRa(const RadioSettings& value) override
    {
        settings = value;
        fsk = false;
        return !restore_fail;
    }
    bool supportsFsk() const override { return true; }
    bool configureFsk(const FskSettings& value) override
    {
        fsk = true;
        return valid_fsk_settings(value) && !fsk_fail;
    }
    bool startReceive() override { return online; }
    bool transmit(const uint8_t* bytes, std::size_t count) override
    {
        tx.emplace_back(bytes, bytes + count);
        return !tx_fail;
    }
    bool pollReceive(RadioPacket* packet) override
    {
        if (rx.empty()) return false;
        packet->size = rx.front().size();
        std::copy(rx.front().begin(), rx.front().end(), packet->data.begin());
        rx.pop_front();
        return true;
    }
    float readRssi() override { return -90; }
    bool isOnline() const override { return online; }
    const char* lastError() const override { return "test failure"; }
    RadioSettings appliedLoRaConfig() const override { return settings; }
    RadioStatistics stats() const override
    {
        RadioStatistics value;
        value.online = online;
        return value;
    }
    bool online = false, fsk = false, restore_fail = false, fsk_fail = false, tx_fail = false;
    unsigned opens = 0, closes = 0;
    RadioSettings settings;
    std::vector<std::vector<uint8_t>> tx;
    std::deque<std::vector<uint8_t>> rx;
};
class Audio final : public IWalkieAudio
{
  public:
    bool startCapture() override
    {
        capturing = true;
        return !fail;
    }
    void stopCapture() override { capturing = false; }
    std::size_t capture(int16_t* output, std::size_t capacity) override
    {
        const auto count = std::min(capacity, input.size() - offset);
        std::copy_n(input.begin() + offset, count, output);
        offset += count;
        return count;
    }
    uint64_t captureDropped() const override { return dropped; }
    bool captureActive() const override { return capturing; }
    bool play(const int16_t* samples, std::size_t count) override
    {
        played.insert(played.end(), samples, samples + count);
        return !fail;
    }
    bool playbackReady() const override { return true; }
    void stopPlayback() override {}
    const char* lastError() const override { return "test audio failure"; }
    bool capturing = false, fail = false;
    uint64_t dropped = 0;
    std::size_t offset = 0;
    std::vector<int16_t> input, played;
};
int main()
{
    auto driver = std::make_unique<Radio>();
    auto* physical = driver.get();
    PacketRadioCoordinator coordinator(std::move(driver));
    auto& mesh = coordinator.mesh();
    require(mesh.acquire(), "mesh acquire");
    RadioSettings settings;
    settings.freq_mhz = 433.175f;
    require(mesh.configureLoRa(settings), "mesh configure");
    Audio audio;
    for (unsigned sample = 0; sample < 3200; ++sample) audio.input.push_back(int16_t(6000 * std::sin(sample * 6.283185307179586 * 300 / 8000)));
    {
        WalkieSession voice(coordinator.foreground(), audio);
        require(voice.begin({settings, 10, 100}), "voice session begin");
        require(physical->fsk && platform::audio::music_blocked(), "session did not reserve FSK/audio");
        const uint8_t marker = 1;
        require(!mesh.transmit(&marker, 1), "mesh transmitted during voice lease");
        require(voice.step(true, 0, 100), "voice capture/encode/TX");
        require(voice.status().tx && voice.status().tx_level > 0 && physical->tx.size() == 4, "no actual voice packets");
        require(voice.step(false, 400, 100), "PTT release");
        require(!audio.capturing && !voice.status().tx, "PTT left capture active");
        settings.freq_mhz = 434.125f;
        require(mesh.configureLoRa(settings), "deferred mesh change");
    }
    require(!physical->fsk && physical->settings.freq_mhz == 434.125f && !platform::audio::music_blocked(), "session did not restore latest mesh/audio");
    wire::Packet parsed;
    require(wire::decode_packet(physical->tx[0].data(), physical->tx[0].size(), parsed), "invalid WT packet");
    require(parsed.header.sender == 10 && parsed.header.session == 101 && parsed.header.sequence == 0 && parsed.header.first_frame == 0, "WT header mismatch");
    Audio receiver;
    for (const auto& packet : physical->tx) physical->rx.push_back(packet);
    {
        WalkieSession voice(coordinator.foreground(), receiver);
        require(voice.begin({settings, 20, 200}), "RX begin");
        for (unsigned now = 0; now < 600; now += 20) require(voice.step(false, now, 100), "RX step");
        require(receiver.played.size() >= 3200, "voice was not decoded/played");
        require(std::any_of(receiver.played.begin(), receiver.played.end(), [](int16_t sample)
                            { return std::abs(int(sample)) > 500; }),
                "Codec2 RX produced no waveform");
        require(std::any_of(receiver.played.begin(), receiver.played.end(), [](int16_t sample)
                            { return sample < -500; }) &&
                    std::any_of(receiver.played.begin(), receiver.played.end(), [](int16_t sample)
                                { return sample > 500; }),
                "RX gain lost the signed waveform");
    }
    {
        platform::audio::FocusScope other(platform::audio::FocusOwner::Sstv, true);
        WalkieSession voice(coordinator.foreground(), audio);
        require(!voice.begin({settings, 10, 0}), "voice overlapped another audio owner");
    }
    physical->fsk_fail = true;
    {
        WalkieSession voice(coordinator.foreground(), audio);
        require(!voice.begin({settings, 10, 0}), "FSK failure accepted");
    }
    physical->fsk_fail = false;
    {
        Audio tail;
        tail.input.assign(audio.input.begin(), audio.input.begin() + 500);
        const auto before = physical->tx.size();
        WalkieSession voice(coordinator.foreground(), tail);
        require(voice.begin({settings, 10, 300}) && voice.step(true, 0, 100), "tail capture failed");
        require(physical->tx.size() == before, "incomplete packet transmitted early");
        require(voice.step(false, 100, 100) && physical->tx.size() == before + 1,
                "PTT release did not pad/flush trailing voice");
    }
    {
        Audio source;
        source.input = audio.input;
        WalkieSession voice(coordinator.foreground(), source);
        require(voice.begin({settings, 10, 0}), "overrun case begin");
        require(voice.step(true, 0, 100), "overrun case capture");
        ++source.dropped;
        require(!voice.step(true, 20, 100), "capture overrun was ignored");
    }
    physical->tx_fail = true;
    {
        Audio source;
        source.input = audio.input;
        WalkieSession voice(coordinator.foreground(), source);
        require(voice.begin({settings, 10, 0}) && !voice.step(true, 0, 100), "TX failure was ignored");
    }
    physical->tx_fail = false;
    require(mesh.isOnline() && !platform::audio::music_blocked(), "failed start leaked leases");
    mesh.release();
    require(physical->opens == 1 && physical->closes == 1, "physical radio lease imbalance");
}
