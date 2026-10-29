#include "chat/infra/voice/vmp_mqtt_transport.h"
#include "platform/linux/voice_service.h"
#include "platform/linux/voice_store.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <thread>
#include <unistd.h>

using namespace platform::linux_runtime;
namespace vmp = chat::voice::vmp;
using namespace std::chrono_literals;
struct NoRadio final : IPacketRadio
{
    const char* name() const override { return "forbidden radio"; }
    bool hardwareCandidatePresent() const override { return false; }
    bool acquire() override { std::abort(); }
    void release() override { std::abort(); }
    bool configureLoRa(const RadioSettings&) override { std::abort(); }
    bool supportsFskConfiguration(const FskSettings&) const override { return true; }
    bool startReceive() override { std::abort(); }
    bool transmit(const uint8_t*, size_t) override { std::abort(); }
    bool pollReceive(RadioPacket*) override { std::abort(); }
    float readRssi() override { std::abort(); }
    bool isOnline() const override { return false; }
    const char* lastError() const override { return "forbidden"; }
    RadioSettings appliedLoRaConfig() const override { return {}; }
    RadioStatistics stats() const override { return {}; }
};
template <class Predicate>
void wait(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate())
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(2ms);
    }
}
int main()
{
    char directory[] = "/tmp/trailmate-mqtt-voice-XXXXXX";
    assert(mkdtemp(directory));
    const auto database = std::filesystem::path(directory) / "voice.sqlite3";
    NoRadio radio;
    vmp::FixedVerifiedContactSecretDirectory contacts;
    uint8_t secret[vmp::kPrivateKeySize];
    std::fill_n(secret, sizeof(secret), 0x51);
    assert(contacts.upsertVerifiedContactSecret(300, secret));
    assert(contacts.upsertVerifiedContactSecret(200, secret));
    VoiceServiceOptions options;
    options.database = database;
    options.self = 200;
    options.capture_available = []
    { return false; };
    options.contacts = [&](chat::MeshProtocol)
    { return &contacts; };
    {
        NativeVoiceService service(radio, options);
        wait([&]
             { return service.snapshot().available; });
        service.setContext(200, chat::MeshProtocol::Meshtastic, false);
        service.setMqttReceiveReady(true);
        assert(!service.snapshot().can_record);
        VoiceClip clip;
        clip.size = 14;
        for (size_t i = 0; i < clip.size; ++i) clip.bytes[i] = uint8_t(i + 1);
        for (bool private_mode : {false, true})
        {
            NativeVmpSession origin(radio, &contacts);
            assert(origin.prepare({300, private_mode ? 200u : 0u, 3, 0}, clip) == NativeVmpSession::Result::Prepared);
            vmp::MqttTransmitTransfer transmit;
            if (private_mode) assert(transmit.preparePrivate(origin.control(), secret, clip.bytes.data(), clip.size));
            else assert(transmit.prepareBroadcast(origin.control(), clip.bytes.data(), clip.size));
            std::array<std::array<uint8_t, vmp::kMaxMqttEnvelopeSize>, 11> envelopes{};
            std::array<size_t, 11> lengths{};
            size_t count = 0;
            while (transmit.hasNext())
            {
                assert(count < envelopes.size());
                lengths[count] = envelopes[count].size();
                assert(transmit.nextEnvelope(envelopes[count].data(), &lengths[count]));
                ++count;
            }
            const size_t expected = private_mode ? 2 : 1;
            // Omit two data shards: recovery must use parity, with no RF calls.
            for (size_t i = 0; i < count; ++i)
            {
                if (i == 2 || i == 4) continue;
                assert(service.enqueueVoiceMqttEnvelope(envelopes[i].data(), lengths[i], 200, chat::MeshProtocol::Meshtastic));
            }
            vmp::VoiceMessageMetadata rows[4];
            wait([&]
                 { return service.list(rows, 4) == expected; });
            // Replayed objects do not create another durable timeline entry.
            for (size_t i = 0; i < count; ++i)
                assert(service.enqueueVoiceMqttEnvelope(envelopes[i].data(), lengths[i], 200, chat::MeshProtocol::Meshtastic));
            std::this_thread::sleep_for(100ms);
            assert(service.list(rows, 4) == expected);
            service.setMqttReceiveReady(false);
            assert(!service.enqueueVoiceMqttEnvelope(envelopes[0].data(), lengths[0], 200, chat::MeshProtocol::Meshtastic));
            service.setMqttReceiveReady(true);
        }
        service.invalidateContext();
        service.shutdown();
    }
    LinuxVoiceStore store(database);
    assert(store.open() == LinuxVoiceStore::Result::Ready);
    vmp::VoiceMessageMetadata rows[4];
    assert(store.list(rows, 4) == 2);
    for (size_t i = 0; i < 2; ++i)
    {
        VoiceClip saved;
        assert(store.media(rows[i].local_id, saved) && saved.size == 14);
        for (size_t j = 0; j < saved.size; ++j) assert(saved.bytes[j] == uint8_t(j + 1));
    }
    std::filesystem::remove_all(directory);
}
