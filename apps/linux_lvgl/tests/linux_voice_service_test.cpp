#include "app/linux_app_services.h"
#include "chat/infra/voice/vmp_control_auth.h"
#include "chat/infra/voice/vmp_media_frames.h"
#include "chat/linux_mesh_worker.h"
#include "chat/linux_raw_lora_mesh_adapter.h"
#include "chat/usecase/contact_service.h"
#include "platform/linux/mesh/linux_sqlite_mesh_identity_store.h"
#include "platform/linux/voice_service.h"
#include "platform/linux/voice_store.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "sys/event_bus.h"
#include "ui/voice_runtime_binding.h"
#include <cassert>
#include <chrono>
#include <deque>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unistd.h>

using namespace platform::linux_runtime;
using namespace std::chrono_literals;
namespace vmp = chat::voice::vmp;
template <class Predicate>
void wait(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!predicate())
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(2ms);
    }
}
struct Clock final : IVmpClock
{
    uint64_t time = 0;
    uint64_t nowMs() const override { return time; }
    void sleepMs(unsigned ms) override { time += ms; }
};
struct State
{
    bool online = false, media = false;
    RadioSettings settings;
    std::vector<RadioPacket> controls, frames;
    std::deque<RadioPacket> incoming;
    std::mutex control_mutex;
    std::deque<RadioPacket> control_incoming;
    bool fail_tx = false;
};
struct Radio final : IPacketRadio
{
    State& state;
    explicit Radio(State& value) : state(value) {}
    const char* name() const override { return "voice service fixture"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        state.online = true;
        return true;
    }
    void release() override { state.online = false; }
    bool configureLoRa(const RadioSettings& value) override
    {
        state.settings = value;
        state.media = false;
        return true;
    }
    bool supportsFsk() const override { return true; }
    bool supportsFskConfiguration(const FskSettings& value) const override { return valid_lr2021_fsk_settings(value); }
    bool configureFsk(const FskSettings&) override
    {
        state.media = true;
        return true;
    }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t* bytes, size_t size) override
    {
        RadioPacket packet;
        assert(size <= packet.data.size());
        packet.size = size;
        std::copy_n(bytes, size, packet.data.data());
        (state.media ? state.frames : state.controls).push_back(packet);
        return !state.fail_tx;
    }
    bool pollReceive(RadioPacket* packet) override
    {
        if (!state.media)
        {
            std::lock_guard<std::mutex> lock(state.control_mutex);
            if (state.control_incoming.empty()) return false;
            *packet = state.control_incoming.front();
            state.control_incoming.pop_front();
            return true;
        }
        if (state.incoming.empty()) return false;
        *packet = state.incoming.front();
        state.incoming.pop_front();
        return true;
    }
    float readRssi() override { return -100; }
    bool isOnline() const override { return state.online; }
    const char* lastError() const override { return "fixture failure"; }
    RadioSettings appliedLoRaConfig() const override { return state.settings; }
    RadioStatistics stats() const override { return {}; }
};
struct AudioState
{
    std::atomic_bool started{false}, paused{false};
    std::atomic_size_t played{0};
};
struct Sink final : trailmate::linux_app::ILinuxVoiceControlSink
{
    std::atomic_uint calls{0};
    uint32_t self = 0;
    chat::MeshProtocol protocol{};
    std::thread::id thread;
    bool enqueueVoiceControl(const uint8_t*, size_t size, const vmp::ControlRxMetadata&,
                             uint32_t id, chat::MeshProtocol selected) override
    {
        assert(size == vmp::kControlFrameSize);
        self = id;
        protocol = selected;
        thread = std::this_thread::get_id();
        ++calls;
        return true;
    }
};
struct Pcm final : IVoicePcm
{
    AudioState& state;
    size_t position = 0;
    bool active = false;
    explicit Pcm(AudioState& value) : state(value) {}
    bool beginCapture() override
    {
        state.started = active = true;
        return true;
    }
    size_t read(int16_t* samples, size_t count) override
    {
        if (state.paused) return 0;
        count = std::min<size_t>(count, 37);
        for (size_t i = 0; i < count; ++i) samples[i] = (int(position++ % 80) - 40) * 200;
        return count;
    }
    bool captureActive() const override { return active; }
    uint64_t dropped() const override { return 0; }
    void endCapture() noexcept override { active = false; }
    bool beginPlayback() override { return true; }
    bool writable() const override { return true; }
    bool write(const int16_t*, size_t count) override
    {
        state.played += count;
        return true;
    }
    bool drain() override { return true; }
    void endPlayback() noexcept override {}
};
int main()
{
    char directory[] = "/tmp/trailmate-voice-service-XXXXXX";
    assert(mkdtemp(directory));
    setenv("TRAIL_MATE_SETTINGS_ROOT", directory, 1);
    setenv("TRAIL_MATE_CACHE_ROOT", directory, 1);
    const auto database = std::filesystem::path(directory) / "chat.sqlite3";
    State state;
    PacketRadioCoordinator coordinator(std::make_unique<Radio>(state));
    assert(coordinator.mesh().acquire());
    RadioSettings baseline;
    baseline.freq_mhz = 433.175f;
    assert(coordinator.mesh().configureLoRa(baseline));
    AudioState audio;
    Clock clock;
    std::atomic_uint events{0};
    VoiceServiceOptions options;
    options.database = database;
    options.self = 100;
    options.clock = &clock;
    options.capture_available = []
    { return true; };
    options.pcm = [&]
    { return std::make_unique<Pcm>(audio); };
    options.changed = [&](uint8_t, uint64_t, bool)
    { ++events; throw std::runtime_error("UI wake failure"); };
    uint64_t outgoing = 0;
    {
        NativeVoiceService source(coordinator.foreground(), options);
        source.setContext(100, chat::MeshProtocol::Meshtastic, true);
        wait([&]
             { return source.snapshot().can_record; });
        source.setContext(100, chat::MeshProtocol::Reticulum, true);
        assert(!source.snapshot().can_record);
        source.setContext(100, chat::MeshProtocol::Meshtastic, true);
        wait([&]
             { return source.snapshot().can_record; });
        trailmate::linux_ui::VoiceRuntimeBinding ui(&source);
        assert(ui::chat_voice::isRuntimeBound() && ui::chat_voice::canRecordAndSend());
        assert(ui::chat_voice::requestRecordAndSend({200, 1, 0}) == ui::chat_voice::StartResult::PrivateContactUnverified);
        assert(ui::chat_voice::requestRecordAndSend({0, 2, 0}) == ui::chat_voice::StartResult::Unsupported);
        assert(ui::chat_voice::requestRecordAndSend({0, 1, 3}) == ui::chat_voice::StartResult::Queued);
        wait([&]
             { return !source.snapshot().busy; });
        ui::chat_voice::MessageSummary rows[8];
        assert(ui::chat_voice::listMessages(rows, 8) == 1);
        outgoing = rows[0].local_id;
        assert(rows[0].outgoing && rows[0].duration_ms == 5000 && rows[0].presentation_channel == 3);
        assert(rows[0].delivery == ui::chat_voice::DeliveryState::Sent && events == 2);
        assert(state.controls.size() == 1 && state.frames.size() == 13 && !state.media && state.settings.freq_mhz == baseline.freq_mhz);
        assert(ui::chat_voice::requestPlayback(outgoing));
        wait([&]
             { return !source.snapshot().busy; });
        assert(audio.played == 40000);

        // A loopback of our own object must not create a second timeline item.
        source.setContext(200, chat::MeshProtocol::Meshtastic, true);
        for (const auto& frame : state.frames) state.incoming.push_back(frame);
        const auto envelope = state.controls.front();
        const auto transmissions = state.frames.size() + state.controls.size();
        assert(!source.enqueueVoiceControl(envelope.data.data(), envelope.size, {}, 100, chat::MeshProtocol::Meshtastic));
        assert(source.enqueueVoiceControl(envelope.data.data(), envelope.size, {}, 200, chat::MeshProtocol::Meshtastic));
        wait([&]
             { return !source.snapshot().busy; });
        assert(ui::chat_voice::listMessages(rows, 8) == 1 && events == 2);
        VoiceClip clip;
        clip.size = 7;
        clip.bytes[0] = 0x53;
        NativeVmpSession peer(coordinator.foreground(), nullptr, &clock);
        assert(peer.prepare({300, 0, 3, 0}, clip) == NativeVmpSession::Result::Prepared);
        RadioPacket incoming;
        incoming.size = incoming.data.size();
        assert(vmp::encodePublicControlFrame(peer.control(), incoming.data.data(), &incoming.size));
        vmp::TransmitBlock block;
        assert(block.prepare(clip.bytes.data(), clip.size));
        for (uint8_t i = 0; i < 10; ++i)
        {
            RadioPacket shard;
            shard.size = shard.data.size();
            assert(block.buildPublicShardFrame(peer.control().session_id, i, shard.data.data(), &shard.size));
            state.incoming.push_back(shard);
        }
        assert(source.enqueueVoiceControl(incoming.data.data(), incoming.size, {}, 200, chat::MeshProtocol::Meshtastic));
        wait([&]
             { return !source.snapshot().busy; });
        assert(ui::chat_voice::listMessages(rows, 8) == 2 && !rows[0].outgoing && rows[0].source_unverified && !rows[0].read);
        assert(events == 3 && audio.played == 40000 && state.frames.size() + state.controls.size() == transmissions);
        assert(ui::chat_voice::markConversationRead(1, 3, 0, true));
        wait([&]
             { return ui::chat_voice::listMessages(rows, 8) == 2 && rows[0].read; });

        // A second application cannot hydrate/repair Sending while the first
        // still owns the attachment carrier lifecycle.
        NativeVoiceService other(coordinator.foreground(), options);
        wait([&]
             { return !other.snapshot().error.empty(); });
        assert(!other.snapshot().available);
        other.shutdown();

        audio.paused = true;
        audio.started = false;
        assert(ui::chat_voice::requestRecordAndSend({0, 1, 0}) == ui::chat_voice::StartResult::Queued);
        wait([&]
             { return audio.started.load(); });
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 1000; ++i)
        {
            source.snapshot();
            source.list(nullptr, 0);
        }
        assert(std::chrono::steady_clock::now() - start < 500ms);
        assert(ui::chat_voice::requestStopRecording());
        wait([&]
             { return !source.snapshot().busy; });
        assert(state.frames.size() + state.controls.size() == transmissions);
        audio.started = false;
        assert(ui::chat_voice::requestRecordAndSend({0, 1, 0}) == ui::chat_voice::StartResult::Queued);
        wait([&]
             { return audio.started.load(); });
        source.invalidateContext();
        wait([&]
             { return !source.snapshot().busy; });
        assert(!source.snapshot().can_record && state.frames.size() + state.controls.size() == transmissions);
        ui.release();
        assert(!ui::chat_voice::isRuntimeBound());
        source.shutdown();
        assert(!source.play(outgoing) && source.record(0, 1, 0) == IVoiceSource::Admission::Unavailable);
    }
    {
        LinuxVoiceStore store(database);
        assert(store.open() == LinuxVoiceStore::Result::Ready);
        vmp::VoiceMessageMetadata rows[8];
        assert(store.list(rows, 8) == 2 && vmp::voiceMessageHasFlag(rows[0], vmp::VoiceMessageFlagRead));
        assert(rows[1].local_id == outgoing && rows[1].delivery == vmp::VoiceDeliveryState::Sent);
    }
    // Exercise the actual mesh-worker -> raw ingress -> bounded sink bridge.
    // The context is stamped by the applied adapter, not read from UI intent.
    Sink sink;
    trailmate::linux_app::LinuxMeshWorker worker(coordinator.mesh(), 777);
    worker.setVoiceControlSink(&sink);
    assert(worker.initialize());
    worker.applyProtocolConfig(chat::MeshProtocol::Meshtastic, {});
    wait([&]
         { return worker.isReady(); });
    {
        std::lock_guard<std::mutex> lock(state.control_mutex);
        state.control_incoming.push_back(state.controls.front());
    }
    wait([&]
         { return sink.calls == 1; });
    assert(sink.self == 777 && sink.protocol == chat::MeshProtocol::Meshtastic && sink.thread != std::this_thread::get_id());
    worker.shutdown();
    worker.setVoiceControlSink(nullptr);
    {
        trailmate::linux_app::LinuxAppServicesOptions options;
        options.runtime_mode = LinuxRuntimeMode::DeviceRealMesh;
        options.packet_radio = &coordinator.mesh();
        options.foreground_radio = &coordinator.foreground();
        options.radio_power = &coordinator;
        vmp::EphemeralKeyPair peer_key;
        assert(vmp::generateEphemeralKeyPair(&peer_key));
        ::mesh::PeerPublicKey discovered;
        discovered.node_id = ::mesh::NodeId{200};
        std::copy_n(peer_key.public_key, 32, discovered.public_key);
        platform::linux_runtime::mesh::LinuxSqlitePeerKeyStore peer_store;
        assert(peer_store.put(discovered).ok);
        trailmate::linux_app::LinuxAppServices app(options);
        assert(app.initialize() && app.voiceSource());
        assert(app.switchMeshProtocol(chat::MeshProtocol::Meshtastic, false));
        wait([&]
             { app.updateCoreServices(); return app.voiceSource()->snapshot().available; });
        chat::MeshPeerRecord contact;
        wait([&]
             { app.updateCoreServices(); return app.voiceSource()->snapshot().can_record; });
        assert(platform::ui::reticulum_directory::mesh_peer_directory().findByNodeId(chat::MeshProtocol::Meshtastic, 200, contact).succeeded());
        assert(contact.meshtastic.has_public_key && !contact.meshtastic.key_manually_verified);
        assert(std::equal(std::begin(contact.meshtastic.public_key), std::end(contact.meshtastic.public_key), peer_key.public_key));
        assert(app.voiceSource()->record(200, 1, 0) == IVoiceSource::Admission::Unverified);
        assert(app.getContactService().setNodeKeyManuallyVerified(200, true));
        app.updateCoreServices();
        assert(app.voiceSource()->record(200, 1, 0) == IVoiceSource::Admission::Queued);
        assert(app.getContactService().setNodeKeyManuallyVerified(200, false));
        app.updateCoreServices(); // Must cancel an accepted pending/capturing job.
        wait([&]
             { return !app.voiceSource()->snapshot().busy; });
        assert(app.voiceSource()->record(200, 1, 0) == IVoiceSource::Admission::Unverified);
        assert(app.getContactService().setNodeIgnored(200, true));
        assert(sys::EventBus::publish(new sys::NodeInfoUpdateEvent(200, "B", "Bob", -1, -100, 1, 1, 0), 0));
        app.dispatchPendingEvents();
        const auto* ignored = app.getContactService().getPeerByNodeId(200);
        assert(ignored && ignored->is_ignored);
        app.shutdown();
        assert(!app.voiceSource());
        // Injecting only a mesh port must never borrow an unrelated global RF
        // foreground client, even when the injected driver supports VMP.
        options.foreground_radio = nullptr;
        trailmate::linux_app::LinuxAppServices mesh_only(options);
        assert(mesh_only.initialize() && mesh_only.voiceSource());
        wait([&]
             { mesh_only.updateCoreServices(); return mesh_only.voiceSource()->snapshot().available; });
        assert(!mesh_only.voiceSource()->snapshot().can_record);
        mesh_only.shutdown();
    }
    {
        // A cached MT key must never make the MT verification handshake run
        // after switching the same adapter to an unsupported identity family.
        trailmate::linux_app::LinuxRawLoraMeshAdapter adapter(coordinator.mesh(), 777);
        adapter.applyProtocolConfig(chat::MeshProtocol::Meshtastic, {});
        adapter.applyProtocolConfig(chat::MeshProtocol::MeshCore, {});
        const auto sent = state.controls.size() + state.frames.size();
        assert(!adapter.startKeyVerification(200));
        assert(!adapter.submitKeyVerificationNumber(200, 123, 456));
        assert(sent == state.controls.size() + state.frames.size());
    }
    coordinator.mesh().release();
    std::filesystem::remove_all(directory);
}
