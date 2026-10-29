#include "chat/linux_mesh_worker.h"
#include "platform/linux/voice_service.h"
#include "platform/linux/voice_store.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace platform::linux_runtime;
namespace vmp = chat::voice::vmp;
using namespace std::chrono_literals;
struct NoRadio final : IPacketRadio
{
    const char* name() const override { return "MQTT only"; }
    bool hardwareCandidatePresent() const override { return false; }
    bool acquire() override { std::abort(); }
    void release() override { std::abort(); }
    bool configureLoRa(const RadioSettings&) override { std::abort(); }
    bool startReceive() override { std::abort(); }
    bool transmit(const uint8_t*, size_t) override { std::abort(); }
    bool pollReceive(RadioPacket*) override { std::abort(); }
    float readRssi() override { std::abort(); }
    bool isOnline() const override { return false; }
    const char* lastError() const override { return "forbidden"; }
    RadioSettings appliedLoRaConfig() const override { return {}; }
    RadioStatistics stats() const override { return {}; }
};
struct Pcm final : IVoicePcm
{
    bool beginCapture() override { return true; }
    size_t read(int16_t* samples, size_t count) override
    {
        for (size_t i = 0; i < count; ++i) samples[i] = int16_t((i % 32) * 100);
        return count;
    }
    bool captureActive() const override { return true; }
    uint64_t dropped() const override { return 0; }
    void endCapture() noexcept override {}
    bool beginPlayback() override { std::abort(); }
    bool writable() const override { std::abort(); }
    bool write(const int16_t*, size_t) override { std::abort(); }
    bool drain() override { std::abort(); }
    void endPlayback() noexcept override {}
};
template <class Predicate>
void wait(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 8s;
    while (!predicate())
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(2ms);
    }
}
int main(int argc, char** argv)
{
    assert(argc == 5);
    const bool sender = std::string(argv[3]) == "A";
    const uint32_t self = sender ? 300 : 200;
    const std::filesystem::path root(argv[4]);
    std::filesystem::create_directories(root);
    for (const char* name : {"TRAIL_MATE_SETTINGS_ROOT", "TRAIL_MATE_CACHE_ROOT", "TRAIL_MATE_STATE_ROOT"}) setenv(name, root.c_str(), 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    NoRadio radio;
    trailmate::linux_app::LinuxMeshWorker worker(self, &radio, nullptr, false);
    assert(worker.initialize());
    chat::MeshConfig config;
    config.tx_enabled = true;
    trailmate::linux_app::LinuxMeshWorker::MqttSettings mqtt;
    mqtt.enabled = true;
    mqtt.host = "127.0.0.1";
    mqtt.root = "fixture";
    mqtt.username = "u";
    mqtt.password = "p";
    mqtt.port = static_cast<uint16_t>(std::stoul(argv[1]));
    worker.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config, mqtt);
    vmp::FixedVerifiedContactSecretDirectory contacts;
    uint8_t secret[vmp::kPrivateKeySize];
    std::fill_n(secret, sizeof(secret), 0x39);
    assert(contacts.upsertVerifiedContactSecret(sender ? 200 : 300, secret));
    VoiceServiceOptions options;
    options.self = self;
    options.database = root / "voice.sqlite3";
    options.capture_available = []
    { return true; };
    options.pcm = []
    { return std::make_unique<Pcm>(); };
    options.contacts = [&](chat::MeshProtocol)
    { return &contacts; };
    options.is_verified = [](uint32_t, chat::MeshProtocol)
    { return true; };
    options.mqtt_publish = [&](const uint8_t* bytes, size_t size, const std::atomic_bool& cancel)
    { return worker.publishVoiceMqtt(bytes, size, cancel); };
    NativeVoiceService voice(radio, options);
    worker.setVoiceControlSink(&voice);
    wait([&]
         { return worker.isReady() && voice.snapshot().available; });
    voice.setMqttReceiveReady(true);
    voice.setMqttSendReady(true, true);
    std::ofstream(root / "mqtt-ready").put('1');
    wait([&]
         { return std::filesystem::exists(root.parent_path() / (sender ? "B" : "A") / "mqtt-ready"); });
    vmp::VoiceMessageMetadata rows[8];
    if (sender)
    {
        wait([&]
             { return voice.snapshot().can_record; });
        for (uint32_t peer : {0u, 200u})
        {
            assert(voice.record(peer, 1, 3) == IVoiceSource::Admission::Queued);
            wait([&]
                 { return !voice.snapshot().busy; });
            const auto count = voice.list(rows, 8);
            assert(count == (peer ? 2 : 1));
            for (size_t i = 0; i < count; ++i) assert(rows[i].delivery == vmp::VoiceDeliveryState::Sent && vmp::voiceMessageOutgoing(rows[i]));
        }
        std::this_thread::sleep_for(400ms);
    }
    else
    {
        wait([&]
             { return voice.list(rows, 8) == 2; });
        for (size_t i = 0; i < 2; ++i)
        {
            assert(!vmp::voiceMessageOutgoing(rows[i]) && rows[i].sender_id == 300 && rows[i].presentation_channel == 3);
            assert(vmp::voiceMessageSourceUnverified(rows[i]) == (rows[i].mode == vmp::DeliveryMode::Broadcast));
        }
    }
    worker.shutdown();
    worker.setVoiceControlSink(nullptr);
    voice.shutdown();
    LinuxVoiceStore store(options.database);
    assert(store.open() == LinuxVoiceStore::Result::Ready && store.list(rows, 8) == 2);
    for (size_t i = 0; i < 2; ++i)
    {
        VoiceClip clip;
        assert(store.media(rows[i].local_id, clip) && clip.valid() && clip.duration_ms() == 5000);
    }
}
