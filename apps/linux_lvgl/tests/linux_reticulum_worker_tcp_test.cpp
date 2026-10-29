#include "app/app_facade_access.h"
#include "app/linux_app_facade.h"
#include "app/linux_app_services.h"
#include "call_pcm_fixture.h"
#include "chat/linux_mesh_worker.h"
#include "chat/linux_reticulum_network_store.h"
#include "chat/ports/i_mesh_peer_directory.h"
#include "chat/usecase/chat_service.h"
#include "local_tcp_relay_fixture.h"
#include "platform/audio/audio_focus.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "platform/linux/music_service.h"
#include "platform/linux/native_audio_service.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/radio_power.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "sys/event_bus.h"
#include "ui/presentation_sources/runtime_mesh_status_source.h"
#include <atomic>
#include <filesystem>
#include <new>
#include <sys/mman.h>

namespace native = platform::linux_runtime;
namespace call = platform::ui::reticulum_call;
namespace fs = std::filesystem;
using Worker = trailmate::linux_app::LinuxMeshWorker;
using Identity = native::LinuxReticulumIdentity;
struct Shared
{
    std::atomic<bool> caller_done{false}, peer_done{false};
};
static_assert(std::atomic<bool>::is_always_lock_free);
struct Radio final : native::IPacketRadio
{
    bool candidate;
    std::atomic<unsigned> io{0};
    explicit Radio(bool candidate) : candidate(candidate) {}
    const char* name() const override { return "Worker TCP fixture radio"; }
    bool hardwareCandidatePresent() const override { return candidate; }
    bool acquire() override
    {
        ++io;
        return false;
    }
    void release() override { ++io; }
    bool configureLoRa(const native::RadioSettings&) override
    {
        ++io;
        return false;
    }
    bool startReceive() override
    {
        ++io;
        return false;
    }
    bool transmit(const uint8_t*, size_t) override
    {
        ++io;
        return false;
    }
    bool pollReceive(native::RadioPacket*) override
    {
        ++io;
        return false;
    }
    float readRssi() override { return 0; }
    bool isOnline() const override { return false; }
    const char* lastError() const override { return "No radio transport in this fixture"; }
    native::RadioSettings appliedLoRaConfig() const override { return {}; }
    native::RadioStatistics stats() const override { return {}; }
};
struct Power final : native::IRadioPowerControl
{
    std::atomic<bool> value{true};
    bool setEnabled(bool enabled) override
    {
        value = enabled;
        return true;
    }
    bool enabled() const override { return value; }
};
struct PublicIdentity
{
    std::array<uint8_t, 16> destination{}, identity{};
};
int endpoint(const fs::path& root, bool caller, uint16_t port, const PublicIdentity& peer, Shared& shared, bool facade)
{
    const auto own = root / (caller ? "caller" : "peer");
    setenv("TRAIL_MATE_SETTINGS_ROOT", own.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", own.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (own / "sd").c_str(), 1);
    setenv("TRAIL_MATE_GPS_AUTO_SERIAL", "0", 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-worker-test-missing", 1);
    setenv("TRAIL_MATE_DEMO_WORLD", "0", 1);
    if (facade) setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    sys::EventBus::init(64);
    Radio radio(caller);
    Power power;
    auto pcm = std::make_shared<call_audio_test::State>();
    native::NativeCallAudioBinding media(call_audio_test::options(pcm));
    assert(media.snapshot().installed);
    std::unique_ptr<trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade> services;
    std::unique_ptr<Worker> owned_worker;
    chat::MeshConfig config;
    config.reticulum_lora_enabled = false;
    config.reticulum_wifi_gateway_enabled = true;
    config.reticulum_interface_policy = chat::ReticulumInterfacePolicy::WifiGatewayOnly;
    config.reticulum_anonymous_peer = false;
    std::snprintf(config.reticulum_wifi_gateway_host, sizeof(config.reticulum_wifi_gateway_host), "127.0.0.1");
    config.reticulum_wifi_gateway_port = port;
    Worker* selected = nullptr;
    if (facade)
    {
        chat::reticulum::ReticulumNetworkConfig network;
        network.interface_count = 1;
        auto& endpoint = network.interfaces[0];
        endpoint.type = chat::reticulum::NetworkInterfaceType::TcpClient;
        endpoint.enabled = true;
        std::strcpy(endpoint.id, "persisted-tcp");
        std::strcpy(endpoint.target_host, "127.0.0.1");
        endpoint.target_port = port;
        assert(native::LinuxReticulumNetworkStore::save(network, app::AppConfig{}));
        config.reticulum_wifi_gateway_host[0] = 0;
        trailmate::linux_app::LinuxAppServicesOptions options;
        options.runtime_mode = native::LinuxRuntimeMode::DeviceLocal;
        options.packet_radio = &radio;
        options.radio_power = caller ? &power : nullptr;
        services = std::make_unique<trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade>(options);
        assert(services->initialize());
        auto edit = services->beginConfigEdit();
        assert(edit);
        edit.config().mesh_protocol = chat::MeshProtocol::Reticulum;
        edit.config().reticulumConfig() = config;
        edit.commit(app::AppConfigChangeSet::allPersisted());
        services->applyMeshConfig();
        selected = dynamic_cast<Worker*>(services->getMeshAdapter());
        assert(selected && "DeviceLocal must retain its network worker when LoRa is disabled");
    }
    else
    {
        owned_worker = std::make_unique<Worker>(caller ? 0x1001 : 0x1002, &radio, caller ? &power : nullptr);
        selected = owned_worker.get();
        selected->applyProtocolConfig(chat::MeshProtocol::Reticulum, config);
    }
    Worker& worker = *selected;
    worker.setUserInfo(caller ? "Worker Alice" : "Worker Bob", caller ? "AL" : "BO");
    assert(worker.initialize());
    const auto target = chat::makeReticulumPeerIdentity(peer.destination.data(), peer.identity.data());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    chat::MessageId first = 0, second = 0, final = 0;
    bool rejected_reply = false, second_delivered = false, dialled = false, active = false, hung_up = false;
    bool rejected_first = false, committed_second = false, power_requested = false, power_off = false;
    bool observed_ip = false, observed_ui_ip = false;
    bool network_update_queued = false;
    uint32_t network_update_ms = 0, update_rx_packets = 0, update_tx_packets = 0;
    uint32_t last_call_try = 0;
    for (;;)
    {
        worker.tick();
        call::service_ui_runtime();
        chat::MeshTransportStatus transport;
        assert(worker.getTransportStatus(&transport));
        if (transport.ready)
        {
            assert(transport.network_ready && !transport.radio_ready);
            observed_ip = true;
            if (services)
            {
                ui::mesh::MeshStatusSnapshot mesh;
                assert(ui::presentation_sources::runtime_mesh_status_source().buildMeshStatusSnapshot(mesh));
                // The deferred edit can advance the generation between these
                // two independent reads. Check the presentation's own sample
                // and require an actual ready IP projection during the run.
                if (mesh.state == ui::mesh::MeshDisplayState::Ready)
                {
                    assert(mesh.radio_ready && !std::strcmp(mesh.radio_label.c_str(), "IP mesh path"));
                    observed_ui_ip = true;
                }
                else assert(mesh.state == ui::mesh::MeshDisplayState::Off && !mesh.radio_ready &&
                            !std::strcmp(mesh.radio_label.c_str(), "Transport unavailable"));
            }
        }
        sys::Event* event = nullptr;
        while (sys::EventBus::subscribe(&event, 0)) delete event;
        chat::MeshPeerRecord known;
        const bool peer_ready = platform::ui::reticulum_directory::mesh_peer_directory().find(chat::makeMeshPeerReticulumIdentity(target), known).succeeded() && known.reticulum.has_public_keys &&
                                !std::strcmp(known.display_name, caller ? "Worker Bob" : "Worker Alice");
        if (caller && worker.isReady() && peer_ready && !first)
        {
            const auto result = worker.sendTextToReticulumDestination(chat::ChannelId::PRIMARY, "reject-first", 0, target);
            assert(result.ok);
            first = result.msg_id;
        }
        chat::MeshIncomingText message;
        while (worker.pollIncomingText(&message))
        {
            assert(message.encrypted && !message.source_unverified && message.has_reticulum_lxmf_hash && message.rx_meta.origin == chat::RxOrigin::External);
            if (!caller && message.text == "reject-first")
            {
                rejected_first = true;
                worker.commitIncomingText(message, false);
                assert(worker.sendTextToReticulumDestination(chat::ChannelId::PRIMARY, "rejected-first", 0, target).ok);
            }
            else if (caller && message.text == "rejected-first")
            {
                rejected_reply = true;
                worker.commitIncomingText(message, true);
            }
            else if (!caller && message.text == "commit-second")
            {
                committed_second = true;
                worker.commitIncomingText(message, true);
            }
            else if (caller && message.text == "call-ended")
            {
                worker.commitIncomingText(message, true);
                shared.caller_done = true;
            }
            else assert(false && "Unexpected production worker message");
        }
        if (caller && rejected_reply && !second)
        {
            const auto result = worker.sendTextToReticulumDestination(chat::ChannelId::PRIMARY, "commit-second", 0, target);
            assert(result.ok);
            second = result.msg_id;
        }
        Worker::Outcome outcome;
        while (worker.takeOutcome(outcome))
        {
            if (caller && outcome.message == first) assert(outcome.status != chat::MessageStatus::Delivered);
            if (caller && second && outcome.message == second && outcome.status == chat::MessageStatus::Delivered) second_delivered = true;
            if (!caller && final && outcome.message == final && outcome.status == chat::MessageStatus::Delivered) shared.peer_done = true;
        }
        const auto now = sys::millis_now();
        if (caller && second_delivered && !dialled && uint32_t(now - last_call_try) > 100)
        {
            last_call_try = now;
            const auto result = services ? services->getChatService().startReticulumAudioCall(target) : worker.startReticulumAudioCall(target);
            if (result.ok) dialled = true;
            else assert(result.failure == chat::MeshOperationFailure::NotReady);
        }
        const auto state = call::snapshot();
        assert(state.failed_dial_revision == 0 && "Accepted worker dial unexpectedly failed asynchronously");
        if (!caller && state.state == call::State::Incoming && state.realtime_phase == call::RealtimePhase::IncomingRinging && !state.accepted) assert(call::accept());
        if (state.state == call::State::Active)
        {
            active = true;
            if (caller && facade)
            {
                assert(!worker.powerRequests()->snapshot().available);
                power_off = true;
            }
            if (caller && !facade && !power_requested)
            {
                assert(worker.powerRequests()->request(false));
                power_requested = true;
            }
            if (caller && power_requested && !worker.powerRequests()->snapshot().changing && !worker.powerRequests()->snapshot().enabled)
            {
                assert(!worker.powerRequests()->snapshot().failed);
                power_off = true;
            }
            if (caller && power_off && state.rx_packets > 1 && state.tx_packets > 1 && !network_update_queued)
            {
                chat::reticulum::ReticulumNetworkConfig replacement;
                if (facade) assert(native::LinuxReticulumNetworkStore::load(replacement) == native::LinuxReticulumNetworkStore::Result::Ok);
                else
                {
                    replacement.interface_count = 1;
                    auto& first = replacement.interfaces[0];
                    first.type = chat::reticulum::NetworkInterfaceType::TcpClient;
                    first.enabled = true;
                    std::strcpy(first.id, "legacy-gateway");
                    std::strcpy(first.target_host, "127.0.0.1");
                    first.target_port = port;
                }
                auto& extra = replacement.interfaces[replacement.interface_count++];
                extra.type = chat::reticulum::NetworkInterfaceType::TcpClient;
                std::strcpy(extra.id, "deferred-disabled");
                std::strcpy(extra.target_host, "127.0.0.1");
                extra.target_port = port;
                // The first edit would remove the live bearer. Coalescing must
                // keep it unapplied, then install only the last edit after close.
                worker.applyReticulumNetworkConfig(chat::reticulum::ReticulumNetworkConfig{});
                worker.applyReticulumNetworkConfig(replacement);
                assert(worker.reticulumNetworkConfigPending() && worker.isReady());
                network_update_queued = true;
                network_update_ms = now;
                update_rx_packets = state.rx_packets;
                update_tx_packets = state.tx_packets;
            }
            if (caller && network_update_queued && !hung_up)
                assert(worker.reticulumNetworkConfigPending() && worker.isReady());
            if (caller && network_update_queued && uint32_t(now - network_update_ms) >= 120 &&
                state.rx_packets > update_rx_packets && state.tx_packets > update_tx_packets && !hung_up)
            {
                call::hangup();
                hung_up = true;
            }
        }
        if (!caller && active && state.realtime_phase == call::RealtimePhase::Idle && !final)
        {
            const auto result = worker.sendTextToReticulumDestination(chat::ChannelId::PRIMARY, "call-ended", 0, target);
            assert(result.ok);
            final = result.msg_id;
        }
        if (shared.caller_done && shared.peer_done && !pcm->live && state.realtime_phase == call::RealtimePhase::Idle &&
            !worker.reticulumNetworkConfigPending()) break;
        if (std::chrono::steady_clock::now() >= deadline)
        {
            std::fprintf(stderr, "caller=%d ready=%d known=%d first=%u second=%u final=%u active=%d phase=%u delivered=%d\n", caller, worker.isReady(), peer_ready, first, second, final, active, unsigned(state.realtime_phase), second_delivered);
            for (const auto& line : worker.diagnosticLines()) std::fprintf(stderr, "%s\n", line.c_str());
            assert(false && "Production TCP worker fixture timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (services)
    {
        services->saveConfig();
        services->shutdown();
        trailmate::linux_app::LinuxAppServicesOptions options;
        options.runtime_mode = native::LinuxRuntimeMode::DeviceLocal;
        options.packet_radio = &radio;
        options.radio_power = caller ? &power : nullptr;
        trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade reload(options);
        assert(reload.initialize());
        const auto& restored = app::configFacade().readConfig();
        assert(restored.mesh_protocol == chat::MeshProtocol::Reticulum &&
               !restored.reticulumConfig().reticulum_wifi_gateway_host[0] &&
               restored.reticulumConfig().reticulum_wifi_gateway_port == port && !restored.reticulumConfig().reticulum_lora_enabled);
        reload.shutdown();
    }
    else worker.shutdown();
    media.shutdown();
    native::shutdown_native_audio();
    native::shutdown_linux_music();
    call::service_ui_runtime();
    sys::Event* event = nullptr;
    while (sys::EventBus::subscribe(&event, 0)) delete event;
    chat::MeshTransportStatus stopped;
    assert(worker.getTransportStatus(&stopped) && !stopped.ready);
    assert(active && observed_ip && !pcm->live && pcm->written > 0 && !platform::audio::music_blocked() && radio.io == 0);
    if (facade) assert(observed_ui_ip);
    if (caller) assert(first && second && second_delivered && hung_up && power_off && network_update_queued);
    else assert(rejected_first && committed_second && final);
    return 0;
}
int main()
{
    const auto root = fs::temp_directory_path() / ("trailmate-worker-tcp-" + std::to_string(getpid()));
    fs::create_directories(root / "caller");
    fs::create_directories(root / "peer");
    PublicIdentity identities[2];
    for (unsigned index = 0; index < 2; ++index)
    {
        Identity identity(root / (index ? "peer/trailmate.sqlite3" : "caller/trailmate.sqlite3"));
        assert(identity.begin());
        identities[index] = {identity.destinationHash(), identity.identityHash()};
    }
    void* memory = mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(memory != MAP_FAILED);
    auto* shared = new (memory) Shared;
    bool passed = true;
    for (const bool facade : {false, true})
    {
        shared->caller_done = shared->peer_done = false;
        passed = tcp_test::pair([&](bool caller, uint16_t port)
                                { return endpoint(root, caller, port, identities[caller ? 1 : 0], *shared, facade); });
        if (!passed) break;
    }
    shared->~Shared();
    munmap(memory, sizeof(Shared));
    fs::remove_all(root);
    assert(passed);
    std::puts("Production MeshWorker TCP: signed discovery, rejected/durable text commits, bidirectional Codec2, radio-off independence and clean shutdown passed");
}
