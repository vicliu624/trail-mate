#include "chat/linux_mesh_worker.h"
#include "chat/infra/lxmf/lxmf_delivery_planner.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/infra/reticulum/interface_policy.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "chat/infra/voice/vmp_mqtt_transport.h"
#include "chat/linux_mesh_mqtt_bridge.h"
#include "chat/linux_raw_lora_mesh_adapter.h"
#include "chat/linux_reticulum_ingress.h"
#include "chat/linux_reticulum_links.h"
#include "chat/linux_reticulum_network.h"
#include "chat/linux_reticulum_propagation.h"
#include "chat/linux_reticulum_upload.h"
#include "chat/linux_track_directories.h"
#include "chat/linux_track_publication.h"
#include "chat/linux_track_read.h"
#include "chat/linux_voice_control_sink.h"
#include "platform/linux/mesh/linux_meshcore_identity.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "platform/linux/radio_power.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/track_directory_runtime.h"
#include "platform/ui/reticulum_call_runtime.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/settings_store.h"
#include "sys/event_bus.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <future>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/rand.h>
#endif

namespace trailmate::linux_app
{
namespace
{
using Failure = chat::delivery::SendFailureKind;
uint64_t trackDirectoryNow()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
uint32_t messageSeed()
{
    uint32_t id = 0;
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (RAND_bytes(reinterpret_cast<uint8_t*>(&id), sizeof(id)) != 1) throw std::runtime_error("Cannot initialize mesh message IDs");
#else
    id = std::random_device{}();
#endif
    return id ? id : 1;
}
Failure sendFailure(chat::MeshOperationFailure failure)
{
    switch (failure)
    {
    case chat::MeshOperationFailure::PeerKeyMissing:
        return Failure::PeerKeyMissing;
    case chat::MeshOperationFailure::ChannelKeyMissing:
        return Failure::ChannelKeyMissing;
    case chat::MeshOperationFailure::LocalIdentityMissing:
        return Failure::LocalIdentityMissing;
    default:
        return Failure::RadioSendFailed;
    }
}
chat::reticulum::ReticulumNetworkConfig legacyNetwork(const chat::MeshConfig& config)
{
    chat::reticulum::ReticulumNetworkConfig result;
    if (!config.reticulum_wifi_gateway_enabled || config.reticulum_interface_policy == chat::ReticulumInterfacePolicy::LoRaOnly ||
        !config.reticulum_wifi_gateway_host[0]) return result;
    auto& endpoint = result.interfaces[0];
    endpoint.type = chat::reticulum::NetworkInterfaceType::TcpClient;
    endpoint.enabled = true;
    std::snprintf(endpoint.id, sizeof(endpoint.id), "legacy-gateway");
    std::snprintf(endpoint.target_host, sizeof(endpoint.target_host), "%.*s", int(sizeof(config.reticulum_wifi_gateway_host) - 1), config.reticulum_wifi_gateway_host);
    endpoint.target_port = config.reticulum_wifi_gateway_port;
    result.interface_count = 1;
    return result;
}
LinuxMeshMqttBridge::Config mqttConfig(const LinuxMeshWorker::MqttSettings& settings, chat::MeshProtocol protocol, const chat::MeshConfig& mesh)
{
    LinuxMeshMqttBridge::Config result;
    result.protocol = protocol;
    result.client.enabled = settings.enabled && (protocol == chat::MeshProtocol::Meshtastic || protocol == chat::MeshProtocol::MeshCore);
    result.client.host = settings.host;
    result.client.port = settings.port ? settings.port : result.client.port;
    auto& host = result.client.host;
    for (const auto* scheme : {"mqtts://", "ssl://", "mqtt://", "tcp://"})
    {
        const std::string prefix(scheme);
        if (host.compare(0, prefix.size(), prefix) == 0)
        {
            result.client.tls = prefix == "mqtts://" || prefix == "ssl://";
            host.erase(0, prefix.size());
            break;
        }
    }
    if (!host.empty() && host.back() == '/') host.pop_back();
    const auto colon = host.find(':');
    const auto bracket = host.find(']');
    const auto port_at = !host.empty() && host.front() == '[' ? (bracket != host.npos && bracket + 1 < host.size() && host[bracket + 1] == ':' ? bracket + 1 : host.npos)
                                                              : (colon != host.npos && colon == host.rfind(':') ? colon : host.npos);
    if (port_at != host.npos)
    {
        const auto text = host.substr(port_at + 1);
        uint32_t port = 0;
        bool valid = !text.empty() && text.size() <= 5;
        for (const auto byte : text)
        {
            if (byte < '0' || byte > '9')
            {
                valid = false;
                break;
            }
            port = port * 10 + unsigned(byte - '0');
        }
        if (valid && port && port <= 65535)
        {
            result.client.port = static_cast<uint16_t>(port);
            host.resize(port_at);
        }
    }
    result.client.username = settings.username;
    result.client.password = settings.password;
    result.root = settings.root;
    result.uplink = settings.uplink;
    result.downlink = settings.downlink;
    auto& mt = result.meshtastic;
    mt.encryption_enabled = false; // Same standalone policy as the production client.
    mt.primary_channel_id = chat::meshtastic::primaryChannelName(mesh);
    mt.secondary_channel_id = chat::meshtastic::secondaryChannelName(mesh);
    mt.primary_uplink_enabled = mt.primary_downlink_enabled = true;
    mt.secondary_uplink_enabled = mt.secondary_downlink_enabled = !chat::meshtastic::isZeroKey(mesh.secondary_key, sizeof(mesh.secondary_key));
    return result;
}
} // namespace
struct LinuxMeshWorker::Impl
{
    static constexpr std::size_t command_limit = 32, incoming_limit = 64, outcome_limit = 128;
    enum class Kind
    {
        Text,
        Data,
        NodeInfo,
        Discovery,
        Verify,
        VerificationNumber,
        ReticulumPath,
        ReticulumText,
        ReticulumCall,
        VoiceMqtt
    };
    struct Command
    {
        Kind kind = Kind::Text;
        uint64_t generation = 0, nonce = 0;
        chat::MeshProtocol protocol = chat::MeshProtocol::Meshtastic;
        chat::ChannelId channel = chat::ChannelId::PRIMARY;
        chat::NodeId peer = 0;
        chat::MessageId message = 0;
        uint32_t port = 0, number = 0;
        bool ack = false, response = false;
        chat::MeshDiscoveryAction action = chat::MeshDiscoveryAction::ScanLocal;
        std::string text;
        std::vector<uint8_t> data;
        std::array<uint8_t, 16> destination{};
        chat::ReticulumPeerIdentity reticulum{};
        double timestamp = 0;
        std::shared_ptr<std::promise<bool>> completion;
        std::shared_ptr<std::atomic_bool> mqtt_cancel;
    };
    struct ReticulumPending
    {
        Command command;
        uint32_t started = 0, last_request = 0, retry_ms = 0;
        bool force_direct = false;
    };
    struct Desired
    {
        chat::NodeId base = 0;
        chat::MeshProtocol protocol = chat::MeshProtocol::Meshtastic;
        chat::MeshConfig config;
        std::string name, short_name;
        uint8_t privacy = 1, utilization = 0;
        bool duty_cycle = true;
        chat::reticulum::ReticulumNetworkConfig network;
        bool network_explicit = false;
        MqttSettings mqtt;
    };
    struct IncomingCommit
    {
        bool used = false, durable = false;
        uint64_t epoch = 0;
        chat::MeshIncomingText token{}; // Only full identity/hash metadata, never a second message body.
    };
    std::array<IncomingCommit, 32> incoming_commits{};
    struct Snapshot
    {
        chat::MeshProtocol protocol = chat::MeshProtocol::Meshtastic;
        uint64_t generation = 0;
        chat::MeshCapabilities capabilities;
        bool ready = false, pki = false, control_ready = false, network_ready = false, radio_ready = false;
        chat::ReticulumLocalIdentityInfo reticulum_identity;
        std::vector<chat::NodeId> keys;
        std::vector<::mesh::PeerPublicKey> public_keys;
        std::vector<chat::MessageId> reticulum_pending;
        std::vector<std::array<uint8_t, 16>> network_paths;
        std::string status = "Mesh worker not started", config, statistics;
        std::vector<std::string> diagnostics;
    };
    Impl(platform::linux_runtime::IPacketRadio* radio, chat::NodeId base,
         platform::linux_runtime::IRadioPowerControl* control = nullptr, bool allow_radio = true) : radio_(radio), power_controller_(control)
    {
        desired.base = base;
        if (!allow_radio)
        {
            unavailable_radio = platform::linux_runtime::make_unavailable_packet_radio("Physical radio disabled by composition");
            radio_ = unavailable_radio.get();
            power_controller_ = nullptr;
        }
    }
    ~Impl() { stop(); }
    bool start()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (thread.joinable()) return running && !stopping;
        stopping = false;
        bootstrapped = false;
        running = true;
        if (configured) dirty = radio_dirty = true;
        try
        {
            thread = std::thread([this]
                                 { run(); });
        }
        catch (...)
        {
            running = false;
            view.status = "Cannot start mesh worker";
            return false;
        }
        return true;
    }
    bool initialize()
    {
        if (!start()) return false;
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock, [&]
                  { return bootstrapped || !running; });
        return running && bootstrapped;
    }
    bool currentReady() const
    {
        return running && !stopping && configured && view.ready && (view.network_ready || !power->snapshot().changing) &&
               view.generation == generation && view.protocol == desired.protocol;
    }
    bool unsupported() const
    {
        return configured && view.generation == generation && view.protocol == desired.protocol &&
               !view.capabilities.supports_unicast_text && !view.capabilities.supports_broadcast_appdata &&
               !view.capabilities.supports_reticulum_destination_text;
    }
    bool pathControlReady() const
    {
        return running && !stopping && configured && desired.protocol == chat::MeshProtocol::Reticulum &&
               (view.network_ready || chat::reticulum::integratedLoRaAllowed(desired.config)) &&
               view.protocol == desired.protocol && view.generation == generation && view.reticulum_identity.ready &&
               view.control_ready && (view.network_ready || (!radio_dirty && !radio_inflight && !power->snapshot().changing));
    }
    void resultLocked(Outcome result)
    {
        if (!result.message) return;
        for (auto& previous : outcomes)
            if (previous.message == result.message && previous.protocol == result.protocol)
            {
                if (previous.status == chat::MessageStatus::Delivered) return;
                if (previous.status == chat::MessageStatus::Failed && result.status == chat::MessageStatus::Sent) return;
                if (previous.status == chat::MessageStatus::Failed && result.status == chat::MessageStatus::Failed &&
                    previous.failure != Failure::Unknown && result.failure == Failure::RadioSendFailed) return;
                previous = result;
                return;
            }
        // Admission counts undrained results as well as queued requests. Together
        // with the engine's 32 pending ACKs, unique outstanding outcomes stay <128.
        if (outcomes.size() >= outcome_limit) throw std::runtime_error("Mesh outcome capacity invariant failed");
        outcomes.push_back(result);
    }
    void result(Outcome value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        resultLocked(value);
    }
    bool acceptReticulumText(const chat::MeshIncomingText& message, uint64_t epoch, uint64_t receive_epoch)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping || generation != epoch || receive_generation != receive_epoch || desired.protocol != chat::MeshProtocol::Reticulum || texts.size() >= incoming_limit) return false;
        texts.push_back(message);
        return true;
    }
    static void sink(void* context, chat::MessageId id, chat::MeshProtocol protocol,
                     chat::MessageStatus status, Failure failure)
    {
        static_cast<Impl*>(context)->result({id, protocol, status, failure});
    }
    static uint32_t allocateId(void* context)
    {
        auto& counter = static_cast<Impl*>(context)->next_message;
        uint32_t id = counter.fetch_add(1);
        while (!id) id = counter.fetch_add(1);
        return id;
    }
    uint32_t reserveId(uint32_t forced)
    {
        if (!forced) return allocateId(this);
        auto current = next_message.load();
        const auto next = forced == 0xFFFFFFFFU ? 1U : forced + 1;
        while (forced >= current && !next_message.compare_exchange_weak(current, next))
        {
        }
        return forced;
    }
    void changed(bool invalidate_receive = false)
    {
        if (++generation == 0) ++generation;
        dirty = true;
        action_error.clear();
        if (invalidate_receive)
        {
            incoming_commits = {};
            for (const auto& command : commands)
                if (command.kind == Kind::Text || command.kind == Kind::ReticulumText) resultLocked({command.message, command.protocol, chat::MessageStatus::Failed, Failure::Unknown});
            commands.clear();
            if (++receive_generation == 0) ++receive_generation;
            texts.clear();
            data.clear();
            raw.clear();
        }
        else
            for (auto& command : commands) command.generation = generation;
        wake.notify_all();
    }
    bool enqueue(Command& command, bool allow_pending_metadata = false)
    {
        const bool metadata_ready = allow_pending_metadata && running && !stopping && configured &&
                                    view.ready && view.protocol == desired.protocol && !radio_dirty && !radio_inflight && !power->snapshot().changing;
        const bool control_ready = command.kind == Kind::ReticulumPath && pathControlReady();
        if ((!currentReady() && !metadata_ready && !control_ready) || commands.size() + outcomes.size() >= command_limit) return false;
        command.generation = generation;
        command.protocol = desired.protocol;
        commands.push_back(std::move(command));
        wake.notify_one();
        return true;
    }
    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            incoming_commits = {};
            power->deactivate();
            view.ready = false;
            for (const auto& command : commands)
                if (command.kind == Kind::Text || command.kind == Kind::ReticulumText) resultLocked({command.message, command.protocol, chat::MessageStatus::Failed, Failure::Unknown});
            commands.clear();
            wake.notify_all();
        }
        if (thread.joinable()) thread.join();
    }
    void collect(LinuxRawLoraMeshAdapter& engine, uint64_t epoch, chat::MeshProtocol protocol)
    {
        chat::MessageId id;
        bool ok;
        while (engine.takePendingSendResult(id, ok))
            result({id, protocol, ok ? chat::MessageStatus::Sent : chat::MessageStatus::Failed,
                    ok ? Failure::None : Failure::RadioSendFailed});
        // These engine poll methods may service hardware. They are worker-only.
        for (unsigned count = 0; count < 8; ++count)
        {
            chat::MeshIncomingText item;
            if (!engine.pollIncomingText(&item)) break;
            std::lock_guard<std::mutex> lock(mutex);
            if (receive_generation != epoch) continue;
            if (texts.size() >= incoming_limit) texts.pop_front();
            texts.push_back(std::move(item));
        }
        for (unsigned count = 0; count < 8; ++count)
        {
            chat::MeshIncomingData item;
            if (!engine.pollIncomingData(&item)) break;
            std::lock_guard<std::mutex> lock(mutex);
            if (receive_generation != epoch) continue;
            if (data.size() >= incoming_limit) data.pop_front();
            data.push_back(std::move(item));
        }
        for (unsigned count = 0; count < 8; ++count)
        {
            platform::linux_runtime::RadioPacket packet;
            if (!engine.pollIncomingRawPacket(packet.data.data(), packet.size, packet.data.size())) break;
            std::lock_guard<std::mutex> lock(mutex);
            if (receive_generation != epoch) continue;
            if (raw.size() >= incoming_limit) raw.pop_front();
            raw.push_back(std::move(packet));
        }
    }
    void execute(LinuxRawLoraMeshAdapter& engine, const Command& command)
    {
        bool ok = false;
        switch (command.kind)
        {
        case Kind::ReticulumPath:
        case Kind::ReticulumText:
        case Kind::ReticulumCall:
        case Kind::VoiceMqtt:
            return; // The path owner executes this intent through its own control effect.
        case Kind::Text:
        {
            const auto sent = engine.sendTextDetailed(command.channel, command.text, command.message, command.peer);
            if (!sent.ok) result({command.message, command.protocol, chat::MessageStatus::Failed, sendFailure(sent.failure)});
            else
            {
                std::lock_guard<std::mutex> lock(mutex);
                action_error.clear();
            }
            return;
        }
        case Kind::Data:
            ok = engine.sendAppData(command.channel, command.port, command.data.data(), command.data.size(), command.peer, command.ack, command.message, command.response);
            break;
        case Kind::NodeInfo:
            ok = engine.requestNodeInfo(command.peer, command.response);
            break;
        case Kind::Discovery:
            ok = engine.triggerDiscoveryActionDetailed(command.action).ok;
            break;
        case Kind::Verify:
            ok = engine.startKeyVerification(command.peer);
            break;
        case Kind::VerificationNumber:
            ok = engine.submitKeyVerificationNumber(command.peer, command.nonce, command.number);
            break;
        }
        if (!ok)
        {
            const auto error = engine.isReady() ? std::string("Queued mesh operation was rejected by the protocol") : engine.statusText();
            bool current, same_protocol;
            {
                std::lock_guard<std::mutex> lock(mutex);
                current = command.generation == generation;
                same_protocol = command.protocol == desired.protocol;
                if (current) action_error = error;
            }
            if (same_protocol && (command.kind == Kind::Verify || command.kind == Kind::VerificationNumber))
            {
                auto* failed = new sys::KeyVerificationFinalEvent(command.peer, command.nonce, command.kind == Kind::Verify, "");
                failed->runtime_failure = failed->has_protocol = true;
                failed->protocol = command.protocol;
                sys::EventBus::publish(failed, 0);
            }
        }
        else
        {
            std::lock_guard<std::mutex> lock(mutex);
            action_error.clear();
        }
    }
    void executePath(LinuxRawLoraMeshAdapter& engine, LinuxReticulumIngress& ingress, const Command& command)
    {
        struct SenderContext
        {
            Impl* owner;
            LinuxRawLoraMeshAdapter* engine;
            const Command* command;
        } context{this, &engine, &command};
        const auto send = [](void* pointer, const uint8_t* bytes, size_t size)
        {
            auto& ctx = *static_cast<SenderContext*>(pointer);
            {
                std::lock_guard<std::mutex> lock(ctx.owner->mutex);
                if (ctx.owner->stopping || ctx.owner->generation != ctx.command->generation ||
                    ctx.owner->desired.protocol != chat::MeshProtocol::Reticulum || !ctx.owner->desired.config.tx_enabled ||
                    !chat::reticulum::integratedLoRaAllowed(ctx.owner->desired.config)) return false;
            }
            return ctx.engine->sendReticulumControl(bytes, size);
        };
        std::array<uint8_t, 16> tag{};
        bool random_ready = false;
#if defined(TRAIL_MATE_HAS_OPENSSL)
        random_ready = RAND_bytes(tag.data(), tag.size()) == 1;
#endif
        const auto result = random_ready ? ingress.requestPath(command.destination.data(), tag.data(), ::sys::millis_now(), send, &context)
                                         : LinuxReticulumIngress::RequestResult::NotReady;
        std::lock_guard<std::mutex> lock(mutex);
        if (generation != command.generation || desired.protocol != command.protocol) return;
        if (result == LinuxReticulumIngress::RequestResult::Sent || result == LinuxReticulumIngress::RequestResult::CoolingDown) action_error.clear();
        else action_error = random_ready ? "Reticulum path request failed before physical acceptance" : "Reticulum path request requires secure randomness";
    }
    void run()
    {
        LinuxMeshMqttBridge mqtt;
        mqtt.setCancellation([this]
                             { std::lock_guard<std::mutex> lock(mutex); return stopping; });
        std::unique_ptr<LinuxRawLoraMeshAdapter> engine;
        struct VoiceIngress final : ::chat::voice::vmp::IControlEnvelopeSink
        {
            Impl& owner;
            LinuxRawLoraMeshAdapter* engine = nullptr;
            explicit VoiceIngress(Impl& value) : owner(value) {}
            bool enqueueControl(const uint8_t* bytes, size_t size,
                                const ::chat::voice::vmp::ControlRxMetadata& metadata) override
            {
                std::lock_guard<std::mutex> lock(owner.mutex);
                return owner.voice_sink && owner.voice_sink->enqueueVoiceControl(bytes, size, metadata,
                                                                                 engine->getNodeId(), engine->backendProtocol());
            }
        } voice_ingress(*this);
        chat::MeshProtocol applied_protocol = chat::MeshProtocol::Meshtastic;
        uint64_t applied_epoch = 0;
        uint64_t applied_receive_epoch = 0;
        try
        {
            chat::NodeId base;
            {
                std::lock_guard<std::mutex> lock(mutex);
                base = desired.base;
            }
            if (!base) throw std::runtime_error("Mesh worker requires a device identity");
            engine = radio_ ? std::make_unique<LinuxRawLoraMeshAdapter>(*radio_, base)
                            : std::make_unique<LinuxRawLoraMeshAdapter>(base);
            engine->setMqttBridge(&mqtt);
            engine->setMqttVoiceSink([this, &engine](const uint8_t* bytes, size_t size)
                                     {
                                         std::lock_guard<std::mutex> lock(mutex);
                                         return voice_sink && voice_sink->enqueueVoiceMqttEnvelope(bytes, size, engine->getNodeId(), engine->backendProtocol()); });
            engine->setSendResultSink(sink, this);
            voice_ingress.engine = engine.get();
            engine->setVoiceControlSink(&voice_ingress);
            auto& radio = radio_ ? *radio_ : platform::linux_runtime::mesh_packet_radio();
            auto* control = power_controller_ ? power_controller_ : (radio_ ? nullptr : &platform::linux_runtime::packet_radio_power());
            uint32_t meshcore_id = 0;
            {
                platform::linux_runtime::LinuxMeshCoreIdentity identity;
                if (identity.begin()) meshcore_id = identity.nodeId();
            }
            const auto seed = messageSeed();
            power->activate(control && radio.hardwareCandidatePresent(), control && control->enabled(), radio.isOnline());
            {
                std::lock_guard<std::mutex> lock(mutex);
                meshcore_node = meshcore_id;
                next_message = seed;
                bootstrapped = true;
                wake.notify_all();
            }
            engine->setMessageIdSource(allocateId, this);
            platform::linux_runtime::LinuxReticulumIdentity reticulum_identity(platform::linux_runtime::sqlite_database_path());
            std::array<uint8_t, 16> propagation_address{};
            auto& peer_directory = ::platform::ui::reticulum_directory::mesh_peer_directory();
            (void)peer_directory.begin();
            LinuxReticulumPropagation propagation(peer_directory);
            chat::reticulum::LxmfPropagationClientConfig propagation_config;
            bool propagation_allowed = false;
            struct PeerSink final : ::chat::lxmf::runtime::IPeerProjectionSink
            {
                float rssi = 0, snr = 0;
                void publishPeerUpdate(const ::chat::lxmf::runtime::PeerInfo& peer) override
                {
                    char short_name[10];
                    std::snprintf(short_name, sizeof(short_name), "%04lX", static_cast<unsigned long>(peer.node_id & 0xffff));
                    auto event = std::make_unique<sys::NodeInfoUpdateEvent>(peer.node_id, short_name, peer.display_name, snr, rssi,
                                                                            peer.last_seen_s, static_cast<uint8_t>(chat::contacts::NodeProtocolType::Reticulum),
                                                                            static_cast<uint8_t>(chat::contacts::NodeRoleType::Client), 0, 0, 0xff);
                    event->reticulum_identity = ::chat::lxmf::runtime::reticulumIdentityForPeer(peer);
                    if (!sys::EventBus::publish(event.release(), 0)) throw std::runtime_error("Reticulum peer projection queue is full");
                }
            } peer_sink, network_peer_sink;
            LinuxReticulumIngress reticulum_ingress(reticulum_identity, LinuxReticulumIngress::Options{}, &peer_directory);
            reticulum_ingress.setPeerProjectionSink(&peer_sink);
            reticulum_ingress.setPropagationObserver([](void* context, const LinuxReticulumIngress::PropagationAnnounce& event)
                                                     { static_cast<LinuxReticulumPropagation*>(context)->observe(event); },
                                                     &propagation);
            LinuxReticulumLinks reticulum_links(reticulum_identity);
            reticulum_links.setOutcomeSink([](void* pointer, chat::MessageId id, chat::MessageStatus status, Failure failure)
                                           { static_cast<Impl*>(pointer)->result({id, chat::MeshProtocol::Reticulum, status,
                                                                                  failure}); },
                                           this);
            std::deque<ReticulumPending> pending_texts;
            LinuxReticulumUpload propagation_upload;
            std::optional<Command> propagation_command;
            struct PropagationCommandGuard
            {
                Impl* owner;
                std::optional<Command>& command;
                ~PropagationCommandGuard()
                {
                    if (command)
                        try
                        {
                            owner->result({command->message, chat::MeshProtocol::Reticulum, chat::MessageStatus::Failed, Failure::Unknown});
                        }
                        catch (...)
                        {
                        }
                }
            } propagation_command_guard{this, propagation_command};
            struct PendingTextGuard
            {
                Impl* owner;
                std::deque<ReticulumPending>& pending;
                ~PendingTextGuard()
                {
                    for (const auto& item : pending)
                        try
                        {
                            owner->result({item.command.message, chat::MeshProtocol::Reticulum, chat::MessageStatus::Failed, Failure::Unknown});
                        }
                        catch (...)
                        {
                        }
                }
            } pending_text_guard{this, pending_texts};
            struct LinkSenderContext
            {
                Impl* owner;
                LinuxRawLoraMeshAdapter* engine;
                uint64_t epoch = 0;
                uint64_t receive_epoch = 0;
            } link_sender_context{this, engine.get()};
            const LinuxReticulumLinks::Sender link_sender = [](void* pointer, const uint8_t* bytes, size_t size)
            {
                auto& context = *static_cast<LinkSenderContext*>(pointer);
                {
                    std::lock_guard<std::mutex> lock(context.owner->mutex);
                    if (context.owner->stopping || context.owner->generation != context.epoch ||
                        context.owner->desired.protocol != chat::MeshProtocol::Reticulum || !context.owner->desired.config.tx_enabled ||
                        !chat::reticulum::integratedLoRaAllowed(context.owner->desired.config)) return false;
                }
                return context.engine->sendReticulumControl(bytes, size);
            };
            struct LinkIncomingContext
            {
                Impl* owner;
                chat::IMeshPeerDirectory* directory;
                LinkSenderContext* sender;
                LinuxReticulumIngress* ingress;
                LinuxReticulumPropagation* propagation;
                LinuxTrackDirectories* tracks = nullptr;
                LinuxTrackPublication* publication = nullptr;
                LinuxTrackRead* read = nullptr;
            } incoming_context{this, &peer_directory, &link_sender_context, &reticulum_ingress, &propagation};
            reticulum_links.setIncomingHooks([](void* pointer, const chat::MeshIncomingText& message)
                                             {
                auto& context = *static_cast<LinkIncomingContext*>(pointer);
                return context.owner->acceptReticulumText(message, context.sender->epoch, context.sender->receive_epoch); },
                                             [](void* pointer, const uint8_t destination[16], uint8_t public_key[64], bool* ignored)
                                             {
                                                 auto& context = *static_cast<LinkIncomingContext*>(pointer);
                                                 chat::MeshPeerRecord record;
                                                 const auto result = context.directory->find(chat::makeMeshPeerReticulumIdentity(chat::makeReticulumDestinationIdentity(destination)), record);
                                                 if (!result.succeeded())
                                                 {
                                                     *ignored = result.code != chat::MeshPeerDirectoryStatusCode::NotFound;
                                                     return false;
                                                 }
                                                 *ignored = record.flags.ignored;
                                                 if (!record.reticulum.has_public_keys) return false;
                                                 std::memcpy(public_key, record.reticulum.enc_pub, 32);
                                                 std::memcpy(public_key + 32, record.reticulum.sig_pub, 32);
                                                 return true;
                                             },
                                             &incoming_context, [](void* pointer, const uint8_t public_key[64], uint32_t now_s)
                                             { static_cast<LinkIncomingContext*>(pointer)->ingress->rememberAuthenticatedPeer(public_key, now_s); });
            LinuxReticulumNetwork::Hooks network_hooks;
            const auto observe_directory = [](void* pointer, const chat::lxmf::TrackDirectoryAnnouncementView& view)
            {
                auto* directories = static_cast<LinkIncomingContext*>(pointer)->tracks;
                return directories && directories->observe(view, trackDirectoryNow());
            };
            const auto receive_application = [](void* pointer, chat::lxmf::ByteSpan type, const chat::lxmf::CustomDeliveryView& view)
            {
                auto& context = *static_cast<LinkIncomingContext*>(pointer);
                if (context.tracks && context.tracks->receive(type, view, trackDirectoryNow())) return true;
                if (context.publication && context.publication->receive(type, view, trackDirectoryNow())) return true;
                return context.read && context.read->receive(type, view, trackDirectoryNow());
            };
            reticulum_ingress.setTrackDirectoryObserver(observe_directory, &incoming_context);
            reticulum_links.setCustomReceiver(receive_application, &incoming_context);
            network_hooks.track_directory = observe_directory;
            network_hooks.custom = receive_application;
            network_hooks.context = &incoming_context;
            network_hooks.peers = &network_peer_sink;
            network_hooks.propagation = [](void* pointer, const LinuxReticulumIngress::PropagationAnnounce& event)
            { static_cast<LinkIncomingContext*>(pointer)->propagation->observe(event); };
            network_hooks.incoming = [](void* pointer, const chat::MeshIncomingText& message)
            {
                auto& context = *static_cast<LinkIncomingContext*>(pointer);
                return context.owner->acceptReticulumText(message, context.sender->epoch, context.sender->receive_epoch);
            };
            network_hooks.outcome = [](void* pointer, chat::MessageId id, chat::MessageStatus status, Failure failure)
            {
                static_cast<LinkIncomingContext*>(pointer)->owner->result({id, chat::MeshProtocol::Reticulum, status, failure});
            };
            network_hooks.transmit_allowed = [](void* pointer)
            {
                auto& context = *static_cast<LinkIncomingContext*>(pointer);
                std::lock_guard<std::mutex> lock(context.owner->mutex);
                return !context.owner->stopping &&
                       context.owner->desired.protocol == chat::MeshProtocol::Reticulum && context.owner->desired.config.tx_enabled;
            };
            network_hooks.context_current = [](void* pointer)
            {
                auto& context = *static_cast<LinkIncomingContext*>(pointer);
                std::lock_guard<std::mutex> lock(context.owner->mutex);
                return !context.owner->stopping && context.owner->generation == context.sender->epoch &&
                       context.owner->desired.protocol == chat::MeshProtocol::Reticulum;
            };
            LinuxReticulumNetwork reticulum_network(reticulum_identity, peer_directory, network_hooks);
            const auto request_directory_path = [&](const uint8_t* destination)
            {
                if (reticulum_network.ready() && reticulum_network.requestPath(destination)) return true;
                if (!engine->isReady() || engine->backendProtocol() != chat::MeshProtocol::Reticulum) return false;
                std::array<uint8_t, 16> tag{};
#if defined(TRAIL_MATE_HAS_OPENSSL)
                if (RAND_bytes(tag.data(), tag.size()) != 1) return false;
#else
                return false;
#endif
                const auto result = reticulum_ingress.requestPath(destination, tag.data(), ::sys::millis_now(), link_sender, &link_sender_context);
                return result == LinuxReticulumIngress::RequestResult::Sent || result == LinuxReticulumIngress::RequestResult::CoolingDown;
            };
            LinuxTrackDirectories::Transport directory_transport;
            directory_transport.request_path = request_directory_path;
            directory_transport.next_message = [this]
            { return allocateId(this); };
            const auto send_application = [&](const uint8_t* destination, const uint8_t* public_key, chat::lxmf::ByteSpan data, chat::MessageId id)
            {
                chat::MeshPeerRecord peer;
                const auto found = peer_directory.find(chat::makeMeshPeerReticulumIdentity(chat::makeReticulumDestinationIdentity(destination)), peer);
                if (!found.succeeded() || peer.flags.ignored || !peer.reticulum.has_public_keys ||
                    std::memcmp(peer.reticulum.enc_pub, public_key, 32) || std::memcmp(peer.reticulum.sig_pub, public_key + 32, 32)) return false;
                uint8_t identity[16];
                chat::reticulum::computeIdentityHash(public_key, identity);
                const auto target = chat::makeReticulumPeerIdentity(destination, identity);
                const auto ip = reticulum_network.queueCustom(target, "trailmate.track.v1", data, id, ::sys::epoch_seconds_now());
                if (ip == LinuxReticulumLinks::QueueResult::Queued || ip == LinuxReticulumLinks::QueueResult::Pending) return true;
                if (!engine->isReady() || engine->backendProtocol() != chat::MeshProtocol::Reticulum) return false;
                const auto* path = reticulum_ingress.path(destination, ::sys::millis_now());
                if (!path && !reticulum_links.hasOpenDestination(destination))
                {
                    (void)request_directory_path(destination);
                    return false;
                }
                const auto sent = reticulum_links.queueCustom(destination, public_key, "trailmate.track.v1", data, id, ::sys::epoch_seconds_now(),
                                                              ::sys::millis_now(), link_sender, &link_sender_context, path ? path->hops : 1,
                                                              path && path->hops > 1 ? path->next_hop_transport : nullptr);
                return sent == LinuxReticulumLinks::QueueResult::Queued || sent == LinuxReticulumLinks::QueueResult::Pending;
            };
            directory_transport.send = send_application;
            LinuxTrackDirectories track_directories(platform::linux_runtime::sqlite_database_path(), reticulum_identity, std::move(directory_transport));
            LinuxTrackPublication track_publication(
                platform::linux_runtime::resolve_paths().sd_root, platform::linux_runtime::sqlite_database_path(), reticulum_identity,
                [](const uint8_t* session, geocaching::ByteView delivery, geocaching::ByteView epoch, track_sharing::PublicationDirectory& directory)
                { return platform::linux_runtime::pin_track_directory(platform::linux_runtime::sqlite_database_path(), session, delivery, epoch, directory); },
                send_application, [this]
                { return allocateId(this); });
            LinuxTrackRead track_read(
                platform::linux_runtime::resolve_paths().sd_root, platform::linux_runtime::sqlite_database_path(), reticulum_identity,
                [](const uint8_t* session, geocaching::ByteView delivery, geocaching::ByteView epoch, track_sharing::PublicationDirectory& directory)
                { return platform::linux_runtime::pin_track_directory(platform::linux_runtime::sqlite_database_path(), session, delivery, epoch, directory); },
                send_application, [this]
                { return allocateId(this); });
            incoming_context.read = &track_read;
            incoming_context.tracks = &track_directories;
            incoming_context.publication = &track_publication;
            struct DirectoryBinding
            {
                LinkIncomingContext& context;
                ~DirectoryBinding()
                {
                    context.tracks = nullptr;
                    context.publication = nullptr;
                    context.read = nullptr;
                }
            } directory_binding{incoming_context};
            struct PropagationTransport final : LinuxReticulumPropagation::Transport, LinuxReticulumUpload::Transport
            {
                enum class Bearer
                {
                    None,
                    Ip,
                    LoRa
                } bearer = Bearer::None;
                LinuxReticulumNetwork& network;
                LinuxReticulumLinks& radio_links;
                LinuxReticulumIngress& radio_ingress;
                LinkIncomingContext& incoming;
                LinuxReticulumLinks::Sender sender;
                PropagationTransport(LinuxReticulumNetwork& network, LinuxReticulumLinks& links, LinuxReticulumIngress& ingress,
                                     LinkIncomingContext& incoming, LinuxReticulumLinks::Sender sender)
                    : network(network), radio_links(links), radio_ingress(ingress), incoming(incoming), sender(sender) {}
                LinuxReticulumLinks::OpenResult open(const chat::lxmf::runtime::PropagationPeerState& peer) override
                {
                    const auto now = ::sys::millis_now();
                    const auto* route = radio_ingress.path(peer.propagation_hash, now);
                    if (bearer == Bearer::None)
                    {
                        if (network.hasRoute(peer.propagation_hash)) bearer = Bearer::Ip;
                        else if (route && incoming.sender->engine->isReady()) bearer = Bearer::LoRa;
                        else
                        {
                            discover(peer.propagation_hash);
                            return LinuxReticulumLinks::OpenResult::Pending;
                        }
                    }
                    uint8_t key[64];
                    std::memcpy(key, peer.enc_pub, 32);
                    std::memcpy(key + 32, peer.sig_pub, 32);
                    if (bearer == Bearer::Ip) return network.openPropagation(peer.propagation_hash, key);
                    if (!route && !radio_links.hasPropagationDestination(peer.propagation_hash)) return LinuxReticulumLinks::OpenResult::Pending;
                    return radio_links.openPropagation(peer.propagation_hash, key, now, sender, incoming.sender,
                                                       route ? route->hops : 1, route && route->hops > 1 ? route->next_hop_transport : nullptr);
                }
                LinuxReticulumLinks::ServiceRequestResult request(const uint8_t* destination, const uint8_t* path, const uint8_t* data,
                                                                  size_t size, double timestamp, uint8_t* id) override
                {
                    if (bearer == Bearer::Ip) return network.requestPropagation(destination, path, data, size, false, timestamp, id);
                    if (bearer == Bearer::LoRa) return radio_links.requestService(destination, path, data, size, false, timestamp,
                                                                                  ::sys::millis_now(), sender, incoming.sender, id);
                    return LinuxReticulumLinks::ServiceRequestResult::NotReady;
                }
                bool take(const uint8_t* destination, const uint8_t* id, LinuxReticulumLinks::ServiceResponse& response) override
                {
                    return bearer == Bearer::Ip ? network.takePropagationResponse(destination, id, response)
                                                : bearer == Bearer::LoRa && radio_links.takeServiceResponse(destination, id, response);
                }
                bool pending(const uint8_t* destination, const uint8_t* id) override
                {
                    return bearer == Bearer::Ip ? network.propagationRequestPending(destination, id)
                                                : bearer == Bearer::LoRa && radio_links.serviceRequestPending(destination, id);
                }
                void cancel(const uint8_t* destination) override
                {
                    if (bearer == Bearer::Ip) network.closePropagation(destination);
                    else if (bearer == Bearer::LoRa) radio_links.closePropagation(destination, ::sys::millis_now(), sender, incoming.sender);
                    bearer = Bearer::None;
                }
                bool decode(const uint8_t* bytes, size_t size, chat::MeshIncomingText& message) override
                {
                    uint8_t hash[32], missing[16]{};
                    const bool decoded = radio_links.decodePropagatedText(bytes, size, nullptr, message, hash, missing);
                    if (!decoded && std::any_of(std::begin(missing), std::end(missing), [](uint8_t byte)
                                                { return byte != 0; })) discover(missing);
                    return decoded;
                }
                void discover(const uint8_t* destination) override
                {
                    (void)network.requestPath(destination);
                    uint8_t tag[16]{};
#if defined(TRAIL_MATE_HAS_OPENSSL)
                    if (incoming.sender->engine->isReady() && RAND_bytes(tag, sizeof(tag)) == 1)
                        (void)radio_ingress.requestPath(destination, tag, ::sys::millis_now(), sender, incoming.sender);
#endif
                }
                bool deliver(const chat::MeshIncomingText& message) override
                {
                    return incoming.owner->acceptReticulumText(message, incoming.sender->epoch, incoming.sender->receive_epoch);
                }
                LinuxReticulumLinks::ServiceRequestResult upload(const uint8_t* destination, const uint8_t* data, size_t size, uint8_t* hash) override
                {
                    if (bearer == Bearer::Ip) return network.queuePropagationUpload(destination, data, size, hash);
                    if (bearer == Bearer::LoRa) return radio_links.queuePropagationUpload(destination, data, size, ::sys::millis_now(), sender, incoming.sender, hash);
                    return LinuxReticulumLinks::ServiceRequestResult::NotReady;
                }
                bool uploadResult(const uint8_t* hash, LinuxReticulumLinks::UploadResult& result) override
                {
                    return bearer == Bearer::Ip ? network.takePropagationUploadResult(hash, result)
                                                : bearer == Bearer::LoRa && radio_links.takePropagationUploadResult(hash, result);
                }
                bool hasUpload(const uint8_t* hash) override
                {
                    return bearer == Bearer::Ip ? network.hasPropagationUpload(hash) : bearer == Bearer::LoRa && radio_links.hasPropagationUpload(hash);
                }
            } propagation_transport(reticulum_network, reticulum_links, reticulum_ingress, incoming_context, link_sender);
            struct ReticulumRxContext
            {
                LinuxReticulumIngress& ingress;
                PeerSink& sink;
                LinuxReticulumLinks& links;
                LinuxReticulumLinks::Sender sender;
                void* sender_context;
            } receive_context{reticulum_ingress, peer_sink, reticulum_links, link_sender, &link_sender_context};
            engine->setReticulumPacketSink([](void* context, const LinuxRawLoraMeshAdapter::ReticulumPacket& packet)
                                           {
                auto& received = *static_cast<ReticulumRxContext*>(context);
                received.sink.rssi = packet.rssi_dbm;
                received.sink.snr = packet.snr_db;
                const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                chat::RxMeta meta;
                meta.origin = chat::RxOrigin::LoRa;
                meta.direct = true;
                meta.rx_timestamp_ms = ::sys::millis_now();
                meta.rx_timestamp_s = seconds > 0 && uint64_t(seconds) <= UINT32_MAX ? static_cast<uint32_t>(seconds) : 0;
                meta.time_source = meta.rx_timestamp_s ? chat::RxTimeSource::DeviceUtc : chat::RxTimeSource::Uptime;
                meta.rssi_dbm_x10 = static_cast<int16_t>(packet.rssi_dbm * 10);
                meta.snr_db_x10 = static_cast<int16_t>(packet.snr_db * 10);
                if (received.links.receive(packet.data.data(), packet.size, meta.rx_timestamp_ms, received.sender, received.sender_context, &meta)) return;
                received.ingress.receive(packet.data.data(), packet.size, ::sys::millis_now(),
                    seconds > 0 && uint64_t(seconds) <= UINT32_MAX ? static_cast<uint32_t>(seconds) : 0); },
                                           &receive_context);
            struct ReticulumBinding
            {
                LinuxRawLoraMeshAdapter* engine;
                ~ReticulumBinding() { engine->setReticulumPacketSink(nullptr, nullptr); }
            } reticulum_binding{engine.get()};
            for (;;)
            {
                platform::linux_runtime::RadioPowerWork power_work;
                if (power->take(power_work))
                {
                    bool applied = false;
                    std::string error;
                    if (!control || (power_work.enabled && !radio.hardwareCandidatePresent()))
                        error = "Radio endpoint is unavailable";
                    else
                        applied = control->enabled() == power_work.enabled || control->setEnabled(power_work.enabled);
                    if (applied)
                    {
                        const bool was_ready = engine->isReady();
                        if (!power_work.enabled && !mqtt.active())
                        {
                            engine->suspendRadio();
                            reticulum_links.reset();
                            reticulum_ingress.reset();
                            if (!reticulum_network.ready())
                            {
                                for (const auto& pending : pending_texts)
                                    result({pending.command.message, chat::MeshProtocol::Reticulum, chat::MessageStatus::Failed, Failure::Unknown});
                                pending_texts.clear();
                            }
                        }
                        if (!power_work.enabled || !was_ready)
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            if (power_work.enabled) radio_dirty = true;
                            changed(!reticulum_network.ready() && !mqtt.active());
                        }
                        if (power->current(power_work.generation))
                        {
                            platform::ui::settings_store::put_bool("settings", "radio_enabled", power_work.enabled);
                            if (platform::ui::settings_store::get_bool("settings", "radio_enabled", !power_work.enabled) != power_work.enabled)
                            {
                                applied = false;
                                error = "Radio changed, but its setting could not be saved";
                            }
                        }
                    }
                    else if (error.empty()) error = "Radio change failed or a foreground operation is in use";
                    power->complete(power_work, control && control->enabled(), radio.isOnline(), applied, error);
                    continue;
                }
                Desired settings;
                Command command;
                IncomingCommit incoming_commit;
                bool have_commit = false;
                bool apply = false, apply_radio = false, have_command = false;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait_for(lock, std::chrono::milliseconds(20), [&]
                                  { return stopping || (configured && (dirty || !commands.empty() ||
                                                                       std::any_of(incoming_commits.begin(), incoming_commits.end(), [](const auto& item)
                                                                                   { return item.used; }))); });
                    if (stopping) break;
                    if (!configured) continue;
                    // Only this protocol thread can start/admit network calls.
                    // Keep the current generation and its media/receipts usable
                    // until the call owner has finished its closing phase.
                    const auto call_state = platform::ui::reticulum_call::snapshot();
                    if (network_pending && call_state.realtime_phase == platform::ui::reticulum_call::RealtimePhase::Idle &&
                        call_state.state != platform::ui::reticulum_call::State::Incoming &&
                        call_state.state != platform::ui::reticulum_call::State::Outgoing &&
                        call_state.state != platform::ui::reticulum_call::State::Active)
                    {
                        desired.network = pending_network;
                        desired.network_explicit = pending_network_explicit;
                        network_pending = false;
                        changed();
                    }
                    if (dirty)
                    {
                        settings = desired;
                        applied_epoch = generation;
                        applied_receive_epoch = receive_generation;
                        apply = true;
                        apply_radio = radio_dirty;
                        radio_inflight = apply_radio;
                        dirty = radio_dirty = false;
                        action_error.clear();
                    }
                    else if (auto commit = std::find_if(incoming_commits.begin(), incoming_commits.end(), [](const auto& item)
                                                        { return item.used; });
                             commit != incoming_commits.end())
                    {
                        incoming_commit = std::move(*commit);
                        *commit = {};
                        have_commit = incoming_commit.epoch == applied_receive_epoch;
                    }
                    else if (!commands.empty())
                    {
                        command = std::move(commands.front());
                        commands.pop_front();
                        active_message = command.kind == Kind::Text || command.kind == Kind::ReticulumText ? command.message : 0;
                        call_inflight = command.kind == Kind::ReticulumCall;
                        active_protocol = command.protocol;
                        have_command = true;
                    }
                }
                if (apply)
                {
                    propagation_upload.cancel(propagation_transport);
                    if (apply_radio)
                    {
                        if (mqtt.active() || (settings.mqtt.enabled && (settings.protocol == chat::MeshProtocol::Meshtastic || settings.protocol == chat::MeshProtocol::MeshCore)))
                            engine->resetTransportContext();
                        (void)mqtt.configure(mqttConfig(settings.mqtt, settings.protocol, settings.config),
                                             settings.protocol == chat::MeshProtocol::MeshCore ? meshcore_id : settings.base);
                        for (const auto& pending : pending_texts)
                            result({pending.command.message, chat::MeshProtocol::Reticulum, chat::MessageStatus::Failed, Failure::Unknown});
                        pending_texts.clear();
                        reticulum_ingress.reset();
                        reticulum_links.reset();
                        reticulum_network.clear();
                        propagation.reset();
                        propagation_transport.bearer = PropagationTransport::Bearer::None;
                    }
                    link_sender_context.epoch = applied_epoch;
                    link_sender_context.receive_epoch = applied_receive_epoch;
                    if (settings.protocol == chat::MeshProtocol::Reticulum && reticulum_identity.begin())
                    {
                        uint8_t name[chat::reticulum::kNameHashSize];
                        chat::reticulum::computeNameHash("lxmf", "propagation", name);
                        chat::reticulum::computeDestinationHash(name, reticulum_identity.identityHash().data(), propagation_address.data());
                    }
                    auto network_config = settings.network_explicit ? settings.network : legacyNetwork(settings.config);
                    propagation_config = network_config.propagation;
                    propagation_allowed = settings.protocol == chat::MeshProtocol::Reticulum && settings.config.tx_enabled && !settings.config.reticulum_anonymous_peer;
                    if (settings.protocol != chat::MeshProtocol::Reticulum || !settings.config.reticulum_wifi_gateway_enabled ||
                        settings.config.reticulum_interface_policy == chat::ReticulumInterfacePolicy::LoRaOnly)
                        network_config = chat::reticulum::ReticulumNetworkConfig{};
                    if (!reticulum_network.configure(network_config, settings.config.reticulum_anonymous_peer, settings.name.c_str()))
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (generation == applied_epoch) action_error = "Invalid Reticulum network configuration";
                    }
                    engine->setSelfNodeId(settings.base);
                    engine->setUserInfo(settings.name.c_str(), settings.short_name.c_str());
                    engine->setNetworkLimits(settings.duty_cycle, settings.utilization);
                    engine->setPrivacyConfig(settings.privacy);
                    if (apply_radio) engine->applyProtocolConfig(settings.protocol, settings.config);
                    applied_protocol = settings.protocol;
                }
                if (have_command)
                {
                    if (command.kind == Kind::VoiceMqtt)
                    {
                        bool permitted;
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            permitted = !stopping && command.generation == generation && desired.protocol == chat::MeshProtocol::Meshtastic &&
                                        desired.mqtt.enabled && desired.mqtt.uplink && !command.mqtt_cancel->load();
                        }
                        const bool written = permitted && mqtt.publishVmp(command.data.data(), command.data.size());
                        command.completion->set_value(written && !command.mqtt_cancel->load());
                        command.completion.reset();
                    }
                    else if (command.kind == Kind::ReticulumPath)
                    {
                        const bool sent = reticulum_network.requestPath(command.destination.data());
                        if (chat::hasReticulumDestinationIdentity(command.reticulum) &&
                            std::any_of(std::begin(command.reticulum.identity_hash), std::end(command.reticulum.identity_hash), [](uint8_t value)
                                        { return value != 0; }))
                        {
                            uint8_t name[10], call_destination[16];
                            chat::reticulum::computeNameHash("lxst", "telephony", name);
                            chat::reticulum::computeDestinationHash(name, command.reticulum.identity_hash, call_destination);
                            (void)reticulum_network.requestPath(call_destination);
                        }
                        if (engine->isReady()) executePath(*engine, reticulum_ingress, command);
                        else
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            if (generation == command.generation) action_error = sent ? "" : "Reticulum path request unavailable";
                        }
                    }
                    else if (command.kind == Kind::ReticulumCall)
                    {
                        const auto called = reticulum_network.dial(command.reticulum);
                        bool current;
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            current = !stopping && command.generation == generation && desired.protocol == chat::MeshProtocol::Reticulum;
                            if (current) action_error = called.ok ? "" : "Reticulum call could not start";
                        }
                        if (!called.ok && current)
                        {
                            platform::ui::reticulum_call::Peer peer;
                            std::memcpy(peer.destination_hash, command.reticulum.destination_hash, 16);
                            std::memcpy(peer.identity_hash, command.reticulum.identity_hash, 16);
                            (void)platform::ui::reticulum_call::report_dial_failure(peer);
                        }
                    }
                    else if (command.kind == Kind::ReticulumText)
                    {
                        if (pending_texts.size() >= 8) result({command.message, chat::MeshProtocol::Reticulum, chat::MessageStatus::Failed, Failure::Unknown});
                        else pending_texts.push_back({std::move(command), ::sys::millis_now()});
                    }
                    else execute(*engine, command);
                }
                if (have_commit)
                {
                    propagation.commit(incoming_commit.token, incoming_commit.durable, ::sys::epoch_seconds_now());
                    reticulum_links.commitIncoming(incoming_commit.token, incoming_commit.durable, ::sys::millis_now(), link_sender, &link_sender_context);
                    reticulum_network.commitIncoming(incoming_commit.token, incoming_commit.durable);
                }
                mqtt.tick();
                const bool track_current = network_hooks.context_current && network_hooks.context_current(&incoming_context);
                track_directories.tick(trackDirectoryNow(), track_current, reticulum_network.ready() || engine->isReady(), applied_epoch);
                track_publication.tick(trackDirectoryNow(), track_current, applied_epoch);
                track_read.tick(trackDirectoryNow(), track_current, applied_epoch);
                engine->tick();
                if (applied_protocol == chat::MeshProtocol::Reticulum) reticulum_network.tick(::sys::millis_now(), ::sys::epoch_seconds_now());
                bool propagation_current;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    propagation_current = !stopping && generation == applied_epoch && desired.protocol == chat::MeshProtocol::Reticulum;
                }
                if (propagation_upload.active())
                {
                    chat::MeshPeerRecord recipient;
                    const auto found = propagation_command ? peer_directory.find(chat::makeMeshPeerReticulumIdentity(propagation_command->reticulum), recipient)
                                                           : chat::MeshPeerDirectoryStatus::fail(chat::MeshPeerDirectoryStatusCode::InvalidArgument);
                    const bool permitted = propagation_current && propagation_allowed && propagation_command && propagation_command->generation == applied_epoch &&
                                           found.succeeded() && !recipient.flags.ignored;
                    propagation_upload.tick(propagation_config, permitted, propagation.select(propagation_config, permitted, ::sys::millis_now()),
                                            ::sys::millis_now(), ::sys::epoch_seconds_now(), propagation_transport);
                }
                else propagation.tick(propagation_config, propagation_current && propagation_allowed, ::sys::millis_now(), ::sys::epoch_seconds_now(), propagation_transport);
                if (const auto uploaded = propagation_upload.takeResult(); uploaded && propagation_command)
                {
                    if (uploaded->uploaded) result({uploaded->message, chat::MeshProtocol::Reticulum, chat::MessageStatus::Sent, Failure::None});
                    else if (propagation_config.delivery == chat::reticulum::LxmfDeliveryPreference::Automatic &&
                             propagation_command->generation == applied_epoch && propagation_current && propagation_allowed)
                        pending_texts.push_back({std::move(*propagation_command), ::sys::millis_now(), 0, 0, true});
                    else result({uploaded->message, chat::MeshProtocol::Reticulum, chat::MessageStatus::Failed, Failure::RadioSendFailed});
                    propagation_command.reset();
                }
                if (!pending_texts.empty())
                {
                    auto& pending = pending_texts.front();
                    auto& message = pending.command;
                    const auto now = ::sys::millis_now();
                    chat::MeshPeerRecord peer;
                    const auto found = peer_directory.find(chat::makeMeshPeerReticulumIdentity(message.reticulum), peer);
                    const bool known = found.succeeded() && peer.reticulum.has_public_keys;
                    chat::lxmf::runtime::OutboundDeliveryPlanInput plan_input;
                    plan_input.has_active_link = reticulum_network.hasActiveDelivery(message.reticulum.destination_hash) || reticulum_links.hasActiveDelivery(message.reticulum.destination_hash);
                    plan_input.propagation_enabled = propagation_config.enabled && !pending.force_direct;
                    plan_input.propagation_preference = propagation_config.delivery;
                    plan_input.propagation_peer_available = propagation.selected() != nullptr;
                    const auto delivery_plan = chat::lxmf::runtime::ReticulumDeliveryPlanner::plan(plan_input);
                    const uint32_t admission_ttl = delivery_plan.propagation_first ? 15U * 60U * 1000U : 45000U;
                    if (message.generation != applied_epoch || uint32_t(now - pending.started) > admission_ttl || (found.succeeded() && peer.flags.ignored) ||
                        (!found.succeeded() && found.code != chat::MeshPeerDirectoryStatusCode::NotFound))
                    {
                        result({message.message, chat::MeshProtocol::Reticulum, chat::MessageStatus::Failed,
                                !known ? Failure::PeerKeyMissing : Failure::RadioSendFailed});
                        pending_texts.pop_front();
                    }
                    else if (known && delivery_plan.propagation_first)
                    {
                        if (propagation_upload.active() || propagation.syncStage() != chat::lxmf::runtime::PropagationSyncStage::Idle)
                        {
                            pending_texts.push_back(std::move(pending_texts.front()));
                            pending_texts.pop_front();
                        }
                        else
                        {
                            uint8_t key[64];
                            std::memcpy(key, peer.reticulum.enc_pub, 32);
                            std::memcpy(key + 32, peer.reticulum.sig_pub, 32);
                            const auto queued = propagation_upload.start(reticulum_links, message.reticulum.destination_hash, key, message.text,
                                                                         message.message, message.timestamp, now);
                            if (queued == LinuxReticulumLinks::QueueResult::Queued) propagation_command = std::move(message);
                            else result({message.message, chat::MeshProtocol::Reticulum, chat::MessageStatus::Failed, Failure::RadioSendFailed});
                            pending_texts.pop_front();
                        }
                    }
                    else if (known && reticulum_network.hasRoute(message.reticulum.destination_hash))
                    {
                        const auto queued = reticulum_network.queueText(message.reticulum, message.text, message.message, message.timestamp);
                        if (queued != LinuxReticulumLinks::QueueResult::Queued && queued != LinuxReticulumLinks::QueueResult::Pending)
                            result({message.message, chat::MeshProtocol::Reticulum, chat::MessageStatus::Failed, Failure::RadioSendFailed});
                        pending_texts.pop_front();
                    }
                    else if (const auto* path = reticulum_ingress.path(message.reticulum.destination_hash, now);
                             known && engine->isReady() && (path || reticulum_links.hasOpenDestination(message.reticulum.destination_hash)))
                    {
                        uint8_t public_key[64];
                        std::memcpy(public_key, peer.reticulum.enc_pub, 32);
                        std::memcpy(public_key + 32, peer.reticulum.sig_pub, 32);
                        const auto queued = reticulum_links.queueText(message.reticulum.destination_hash, public_key, message.text,
                                                                      message.message, message.timestamp, now, link_sender, &link_sender_context,
                                                                      path ? path->hops : 1, path && path->hops > 1 ? path->next_hop_transport : nullptr);
                        if (queued != LinuxReticulumLinks::QueueResult::Queued && queued != LinuxReticulumLinks::QueueResult::Pending)
                            result({message.message, chat::MeshProtocol::Reticulum, chat::MessageStatus::Failed, Failure::RadioSendFailed});
                        pending_texts.pop_front();
                    }
                    else
                    {
                        if (!pending.retry_ms || uint32_t(now - pending.last_request) >= pending.retry_ms)
                        {
                            uint8_t tag[16]{};
                            bool random = false;
#if defined(TRAIL_MATE_HAS_OPENSSL)
                            random = RAND_bytes(tag, sizeof(tag)) == 1;
#endif
                            const bool network_sent = reticulum_network.requestPath(message.reticulum.destination_hash);
                            const auto sent = random && engine->isReady() ? reticulum_ingress.requestPath(message.reticulum.destination_hash, tag, now,
                                                                                                          link_sender, &link_sender_context)
                                                                          : LinuxReticulumIngress::RequestResult::NotReady;
                            pending.last_request = now;
                            pending.retry_ms = network_sent || sent == LinuxReticulumIngress::RequestResult::Sent ? 20000 : 1000;
                        }
                        pending_texts.push_back(std::move(pending_texts.front()));
                        pending_texts.pop_front();
                    }
                }
                reticulum_links.processOutbound(::sys::millis_now(), link_sender, &link_sender_context);
                reticulum_links.retryAccepted(::sys::millis_now(), link_sender, &link_sender_context);
                collect(*engine, applied_receive_epoch, applied_protocol);
                Snapshot snapshot;
                snapshot.protocol = applied_protocol;
                snapshot.generation = applied_epoch;
                snapshot.network_ready = (applied_protocol == chat::MeshProtocol::Reticulum && reticulum_network.ready()) || (mqtt.active() && mqtt.ready());
                snapshot.radio_ready = !mqtt.active() && engine->isReady();
                snapshot.ready = snapshot.radio_ready || snapshot.network_ready;
                snapshot.control_ready = snapshot.ready;
                snapshot.pki = engine->isPkiReady();
                snapshot.capabilities = engine->getCapabilities();
                if (applied_protocol == chat::MeshProtocol::Reticulum)
                {
                    snapshot.capabilities.supports_reticulum_destination_text = true;
                    snapshot.capabilities.supports_protocol_ack_tracking = true;
                    snapshot.capabilities.supports_reticulum_audio_call = reticulum_network.canCall();
                }
                const bool protocol_supported = snapshot.capabilities.supports_unicast_text ||
                                                snapshot.capabilities.supports_broadcast_appdata || snapshot.capabilities.supports_reticulum_destination_text;
                snapshot.ready = snapshot.ready && protocol_supported;
                snapshot.keys = engine->encryptedPeerIds();
                snapshot.public_keys = engine->peerPublicKeys();
                snapshot.status = engine->statusText();
                if (snapshot.network_ready) snapshot.status = mqtt.active() ? "MQTT standalone transport online" : "Reticulum IP gateway online";
                if (!protocol_supported) snapshot.status = applied_protocol == chat::MeshProtocol::Reticulum
                                                               ? "Reticulum LXMF sending is not implemented"
                                                               : "Selected mesh protocol is not implemented by this Linux transport";
                if (applied_protocol == chat::MeshProtocol::Reticulum)
                {
                    auto& identity = snapshot.reticulum_identity;
                    identity.ready = reticulum_identity.ready();
                    snapshot.ready = snapshot.ready && identity.ready;
                    snapshot.pki = false;
                    snapshot.reticulum_pending = reticulum_links.pendingOutboundIds();
                    if (propagation_upload.active()) snapshot.reticulum_pending.push_back(propagation_upload.messageId());
                    const auto network_pending = reticulum_network.pendingOutboundIds();
                    snapshot.reticulum_pending.insert(snapshot.reticulum_pending.end(), network_pending.begin(), network_pending.end());
                    snapshot.network_paths = reticulum_network.pathDestinations();
                    for (const auto& pending : pending_texts) snapshot.reticulum_pending.push_back(pending.command.message);
                    if (identity.ready)
                    {
                        identity.node_id = reticulum_identity.nodeId();
                        std::memcpy(identity.identity_hash, reticulum_identity.identityHash().data(), sizeof(identity.identity_hash));
                        std::memcpy(identity.lxmf_address, reticulum_identity.destinationHash().data(), sizeof(identity.lxmf_address));
                        std::memcpy(identity.propagation_address, propagation_address.data(), sizeof(identity.propagation_address));
                    }
                    else snapshot.status = "Reticulum identity unavailable: " + reticulum_identity.lastError();
                }
                snapshot.config = engine->radioConfigText();
                snapshot.statistics = engine->radioStatsText();
                snapshot.diagnostics = engine->diagnosticLines();
                if (applied_protocol == chat::MeshProtocol::Reticulum)
                {
                    reticulum_ingress.tick(::sys::millis_now());
                    reticulum_links.tick(::sys::millis_now());
                    const auto links = reticulum_links.statistics();
                    snapshot.diagnostics.push_back("Reticulum links sessions=" + std::to_string(links.sessions) +
                                                   " active=" + std::to_string(links.active) + " proofs=" + std::to_string(links.proofs) +
                                                   " rejected=" + std::to_string(links.rejected) + " tx_errors=" + std::to_string(links.tx_errors));
                    snapshot.diagnostics.push_back("Reticulum incoming text=" + std::to_string(links.incoming_texts) +
                                                   " awaiting_commit=" + std::to_string(links.pending_incoming) + " proofs=" + std::to_string(links.delivery_proofs));
                    const auto received = reticulum_ingress.statistics();
                    snapshot.diagnostics.push_back("Reticulum RX paths=" + std::to_string(received.paths) +
                                                   " accepted=" + std::to_string(received.accepted) + " rejected=" + std::to_string(received.rejected) +
                                                   " replays=" + std::to_string(received.replays));
                    snapshot.diagnostics.push_back("Reticulum path requests sent=" + std::to_string(received.requests_sent) +
                                                   " failed=" + std::to_string(received.request_errors) + " responses=" + std::to_string(received.responses));
                    snapshot.diagnostics.push_back("Reticulum peers cached=" + std::to_string(received.peers) +
                                                   " directory_errors=" + std::to_string(received.directory_errors) + " projection_errors=" + std::to_string(received.projection_errors));
                    const auto network = reticulum_network.snapshot();
                    snapshot.diagnostics.push_back("Reticulum IP paths=" + std::to_string(network.ingress.paths) + " links=" + std::to_string(network.links.active) +
                                                   " announces=" + std::to_string(network.announcements) + " route_rejects=" + std::to_string(network.rejected_routes));
                    for (size_t index = 0; index < network.tcp.count; ++index)
                    {
                        const auto& endpoint = network.tcp.endpoints[index];
                        snapshot.diagnostics.push_back(std::string("Reticulum TCP ") + endpoint.id + " state=" + std::to_string(unsigned(endpoint.state)) +
                                                       " generation=" + std::to_string(endpoint.generation) + " failures=" + std::to_string(endpoint.failures));
                    }
                }
                power->refresh(control && radio.hardwareCandidatePresent(), control && control->enabled(), radio.isOnline());
                const auto active_node = engine->getNodeId();
                std::lock_guard<std::mutex> lock(mutex);
                if (applied_protocol == chat::MeshProtocol::MeshCore) meshcore_node = active_node;
                view = std::move(snapshot);
                radio_inflight = false;
                active_message = 0;
                call_inflight = false;
            }
        }
        catch (const std::exception& error)
        {
            std::lock_guard<std::mutex> lock(mutex);
            view.status = error.what();
            view.ready = false;
            for (const auto& command : commands)
                if (command.kind == Kind::Text || command.kind == Kind::ReticulumText) resultLocked({command.message, command.protocol, chat::MessageStatus::Failed, Failure::Unknown});
            commands.clear();
        }
        if (engine)
        {
            engine->cancelPendingSends();
            chat::MessageId id;
            bool ok;
            while (engine->takePendingSendResult(id, ok)) result({id, applied_protocol,
                                                                  ok ? chat::MessageStatus::Sent : chat::MessageStatus::Failed, ok ? Failure::None : Failure::RadioSendFailed});
            engine.reset(); // Release hardware on its owner thread before publishing stopped.
        }
        power->deactivate();
        std::lock_guard<std::mutex> lock(mutex);
        running = false;
        call_inflight = false;
        view.ready = false;
        if (stopping) view.status = "Mesh worker stopped";
        wake.notify_all();
    }
    mutable std::mutex mutex;
    ILinuxVoiceControlSink* voice_sink = nullptr;
    std::condition_variable wake;
    std::thread thread;
    platform::linux_runtime::IPacketRadio* radio_;
    platform::linux_runtime::IRadioPowerControl* power_controller_;
    std::unique_ptr<platform::linux_runtime::IPacketRadio> unavailable_radio;
    const std::shared_ptr<platform::linux_runtime::RadioPowerRequests> power = std::make_shared<platform::linux_runtime::RadioPowerRequests>();
    Desired desired;
    chat::reticulum::ReticulumNetworkConfig pending_network;
    bool network_pending = false, pending_network_explicit = false;
    Snapshot view;
    uint64_t generation = 0;
    uint64_t receive_generation = 0;
    std::atomic<uint32_t> next_message{1};
    uint32_t meshcore_node = 0;
    chat::MessageId active_message = 0;
    chat::MeshProtocol active_protocol = chat::MeshProtocol::Meshtastic;
    bool running = false, stopping = false, bootstrapped = false, call_inflight = false;
    bool configured = false, dirty = false, radio_dirty = false, radio_inflight = false;
    std::string action_error;
    std::deque<Command> commands;
    std::deque<Outcome> outcomes;
    std::deque<chat::MeshIncomingText> texts;
    std::deque<chat::MeshIncomingData> data;
    std::deque<platform::linux_runtime::RadioPacket> raw;
};

LinuxMeshWorker::LinuxMeshWorker(chat::NodeId id) : impl_(std::make_unique<Impl>(nullptr, id)) {}
LinuxMeshWorker::LinuxMeshWorker(chat::NodeId id, platform::linux_runtime::IPacketRadio* radio,
                                 platform::linux_runtime::IRadioPowerControl* control) : impl_(std::make_unique<Impl>(radio, id, control)) {}
LinuxMeshWorker::LinuxMeshWorker(platform::linux_runtime::IPacketRadio& radio, chat::NodeId id) : impl_(std::make_unique<Impl>(&radio, id)) {}
LinuxMeshWorker::LinuxMeshWorker(chat::NodeId id, platform::linux_runtime::IPacketRadio* radio,
                                 platform::linux_runtime::IRadioPowerControl* control, bool allow_radio)
    : impl_(std::make_unique<Impl>(radio, id, control, allow_radio)) {}
LinuxMeshWorker::~LinuxMeshWorker() = default;
bool LinuxMeshWorker::initialize() { return impl_->initialize(); }
void LinuxMeshWorker::shutdown() { impl_->stop(); }
LinuxMeshWorker::PeerKeys LinuxMeshWorker::peerPublicKeys() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return {impl_->view.protocol, impl_->view.public_keys};
}
bool LinuxMeshWorker::getTransportStatus(chat::MeshTransportStatus* out) const
{
    if (!out) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    *out = {};
    if (impl_->configured && impl_->running && !impl_->stopping && impl_->view.generation == impl_->generation)
    {
        out->ready = impl_->currentReady();
        out->radio_ready = impl_->view.radio_ready;
        out->network_ready = impl_->view.network_ready;
    }
    return true;
}
void LinuxMeshWorker::setVoiceControlSink(ILinuxVoiceControlSink* sink)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->voice_sink = sink;
}
bool LinuxMeshWorker::publishVoiceMqtt(const uint8_t* bytes, size_t size, const std::atomic_bool& cancel)
{
    chat::voice::vmp::MqttEnvelopeView envelope;
    if (cancel || !chat::voice::vmp::parseMqttEnvelope(bytes, size, &envelope)) return false;
    Impl::Command command;
    command.kind = Impl::Kind::VoiceMqtt;
    command.data.assign(bytes, bytes + size);
    command.completion = std::make_shared<std::promise<bool>>();
    auto result = command.completion->get_future();
    auto cancelled = command.mqtt_cancel = std::make_shared<std::atomic_bool>(false);
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->desired.protocol != chat::MeshProtocol::Meshtastic || !impl_->desired.mqtt.enabled || !impl_->desired.mqtt.uplink || !impl_->enqueue(command)) return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (result.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready)
    {
        if (cancel || std::chrono::steady_clock::now() >= deadline)
        {
            *cancelled = true;
            return false;
        }
    }
    try
    {
        return !cancel && result.get();
    }
    catch (const std::future_error&)
    {
        return false;
    }
}
std::shared_ptr<platform::linux_runtime::RadioPowerRequests> LinuxMeshWorker::powerRequests() const { return impl_->power; }
void LinuxMeshWorker::tick() { impl_->wake.notify_one(); }
void LinuxMeshWorker::processSendQueue() { tick(); }
void LinuxMeshWorker::applyProtocolConfig(chat::MeshProtocol protocol, const chat::MeshConfig& config)
{
    MqttSettings mqtt;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (protocol == impl_->desired.protocol) mqtt = impl_->desired.mqtt;
    }
    if (protocol == chat::MeshProtocol::MeshCore)
    {
        mqtt.enabled = config.meshcore_mqtt_enabled;
        mqtt.uplink = config.meshcore_mqtt_uplink_enabled;
        mqtt.downlink = config.meshcore_mqtt_downlink_enabled;
        mqtt.host = config.meshcore_mqtt_host;
        mqtt.port = config.meshcore_mqtt_port;
        mqtt.root = config.meshcore_mqtt_root;
        mqtt.username = config.meshcore_mqtt_username;
        mqtt.password = config.meshcore_mqtt_password;
    }
    applyProtocolConfig(protocol, config, mqtt);
}
void LinuxMeshWorker::applyProtocolConfig(chat::MeshProtocol protocol, const chat::MeshConfig& config, const MqttSettings& mqtt)
{
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->desired.protocol = protocol;
        impl_->desired.config = config;
        impl_->desired.mqtt = mqtt;
        impl_->configured = impl_->radio_dirty = true;
        impl_->changed(true);
    }
    (void)impl_->start();
}
void LinuxMeshWorker::applyConfig(const chat::MeshConfig& config)
{
    chat::MeshProtocol protocol;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        protocol = impl_->desired.protocol;
    }
    applyProtocolConfig(protocol, config);
}
void LinuxMeshWorker::applyReticulumNetworkConfig(const chat::reticulum::ReticulumNetworkConfig& network)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pending_network = network;
    impl_->pending_network_explicit = true;
    impl_->network_pending = true;
    impl_->wake.notify_one();
}
void LinuxMeshWorker::clearReticulumNetworkOverride()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pending_network_explicit = false;
    impl_->network_pending = true;
    impl_->wake.notify_one();
}
bool LinuxMeshWorker::reticulumNetworkConfigPending() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->network_pending;
}
chat::MeshActionResult LinuxMeshWorker::startReticulumAudioCall(const chat::ReticulumPeerIdentity& target)
{
    using Result = chat::MeshActionResult;
    using Failure = chat::MeshOperationFailure;
    if (!chat::hasReticulumDestinationIdentity(target) ||
        std::all_of(std::begin(target.destination_hash), std::end(target.destination_hash), [](uint8_t value)
                    { return value == 0; })) return Result::fail(Failure::InvalidInput);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->desired.protocol != chat::MeshProtocol::Reticulum || impl_->desired.config.reticulum_anonymous_peer) return Result::fail(Failure::Unsupported);
    if (!impl_->currentReady() || !impl_->view.network_ready) return Result::fail(Failure::NotReady);
    if (!impl_->desired.config.tx_enabled) return Result::fail(Failure::TxDisabled);
    if (!impl_->view.capabilities.supports_reticulum_audio_call) return Result::fail(Failure::Unsupported);
    if (impl_->call_inflight || platform::ui::reticulum_call::modal_active() ||
        std::any_of(impl_->commands.begin(), impl_->commands.end(), [](const auto& item)
                    { return item.kind == Impl::Kind::ReticulumCall; })) return Result::fail(Failure::Busy);
    chat::MeshPeerRecord peer;
    const auto found = platform::ui::reticulum_directory::mesh_peer_directory().find(chat::makeMeshPeerReticulumIdentity(target), peer);
    if (found.succeeded() && peer.flags.ignored) return Result::fail(Failure::InvalidInput);
    if (!found.succeeded() && found.code != chat::MeshPeerDirectoryStatusCode::NotFound) return Result::fail(Failure::NotReady);
    Impl::Command command;
    command.reticulum = target;
    const bool identity_present = std::any_of(std::begin(target.identity_hash), std::end(target.identity_hash), [](uint8_t value)
                                              { return value != 0; });
    if (!found.succeeded() || !peer.reticulum.has_public_keys)
    {
        if (!identity_present)
        {
            command.kind = Impl::Kind::ReticulumPath;
            std::memcpy(command.destination.data(), target.destination_hash, 16);
            return impl_->enqueue(command) ? Result::fail(Failure::NotReady, 1) : Result::fail(Failure::Busy);
        }
    }
    uint8_t key[64], identity[16], name[10], destination[16];
    if (found.succeeded() && peer.reticulum.has_public_keys)
    {
        std::memcpy(key, peer.reticulum.enc_pub, 32);
        std::memcpy(key + 32, peer.reticulum.sig_pub, 32);
        chat::reticulum::computeIdentityHash(key, identity);
    }
    else std::memcpy(identity, target.identity_hash, 16);
    chat::reticulum::computeNameHash("lxmf", "delivery", name);
    chat::reticulum::computeDestinationHash(name, identity, destination);
    if (std::memcmp(target.destination_hash, destination, 16) || (identity_present && std::memcmp(target.identity_hash, identity, 16))) return Result::fail(Failure::InvalidInput);
    command.kind = Impl::Kind::ReticulumCall;
    return impl_->enqueue(command) ? Result::success() : Result::fail(Failure::Busy);
}
void LinuxMeshWorker::setSelfNodeId(chat::NodeId id)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (id && impl_->desired.base != id)
    {
        impl_->desired.base = id;
        impl_->changed(true);
    }
}
void LinuxMeshWorker::setUserInfo(const char* name, const char* short_name)
{
    const std::string full = name ? std::string(name, strnlen(name, 128)) : "";
    const std::string brief = short_name ? std::string(short_name, strnlen(short_name, 32)) : "";
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->desired.name != full || impl_->desired.short_name != brief)
    {
        impl_->desired.name = full;
        impl_->desired.short_name = brief;
        impl_->changed();
    }
}
void LinuxMeshWorker::setNetworkLimits(bool enabled, uint8_t percent)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->desired.duty_cycle != enabled || impl_->desired.utilization != percent)
    {
        impl_->desired.duty_cycle = enabled;
        impl_->desired.utilization = percent;
        impl_->changed();
    }
}
void LinuxMeshWorker::setPrivacyConfig(uint8_t mode)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->desired.privacy != mode)
    {
        impl_->desired.privacy = mode;
        impl_->changed(true);
    }
}
chat::MeshCapabilities LinuxMeshWorker::getCapabilities() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->view.protocol == impl_->desired.protocol ? impl_->view.capabilities : chat::MeshCapabilities{};
}
chat::MeshProtocol LinuxMeshWorker::backendProtocol() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->desired.protocol;
}
chat::NodeId LinuxMeshWorker::getNodeId() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->desired.protocol == chat::MeshProtocol::Reticulum)
        return impl_->view.protocol == chat::MeshProtocol::Reticulum ? impl_->view.reticulum_identity.node_id : 0;
    return impl_->desired.protocol == chat::MeshProtocol::MeshCore ? impl_->meshcore_node : impl_->desired.base;
}
bool LinuxMeshWorker::getReticulumLocalIdentityInfo(chat::ReticulumLocalIdentityInfo* out) const
{
    if (!out) return false;
    *out = {};
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->running || impl_->stopping || impl_->desired.protocol != chat::MeshProtocol::Reticulum ||
        impl_->view.protocol != chat::MeshProtocol::Reticulum || impl_->view.generation != impl_->generation) return false;
    *out = impl_->view.reticulum_identity;
    // Names/privacy are configuration projections, not identity storage facts.
    out->anonymous_peer = impl_->desired.config.reticulum_anonymous_peer;
    const auto& name = impl_->desired.name.empty() ? impl_->desired.short_name : impl_->desired.name;
    std::memcpy(out->display_name, name.data(), std::min(name.size(), sizeof(out->display_name) - 1));
    return out->ready;
}
bool LinuxMeshWorker::requestDirectoryPath(const uint8_t destination[16])
{
    if (!destination || !std::any_of(destination, destination + 16, [](uint8_t byte)
                                     { return byte != 0; })) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->pathControlReady() || !impl_->desired.config.tx_enabled) return false;
    for (const auto& queued : impl_->commands)
        if (queued.kind == Impl::Kind::ReticulumPath && !std::memcmp(queued.destination.data(), destination, 16)) return true;
    Impl::Command command;
    command.kind = Impl::Kind::ReticulumPath;
    std::memcpy(command.destination.data(), destination, 16);
    return impl_->enqueue(command);
}
bool LinuxMeshWorker::isReady() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->currentReady();
}
bool LinuxMeshWorker::isPkiReady() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->currentReady() && impl_->view.pki;
}
bool LinuxMeshWorker::hasPkiKey(chat::NodeId peer) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->desired.protocol == chat::MeshProtocol::Meshtastic && impl_->currentReady() &&
           std::find(impl_->view.keys.begin(), impl_->view.keys.end(), peer) != impl_->view.keys.end();
}
chat::MeshSendResult LinuxMeshWorker::sendTextDetailed(chat::ChannelId channel, const std::string& text, chat::MessageId forced, chat::NodeId peer)
{
    using F = chat::MeshOperationFailure;
    if (text.empty() || text.size() > 512) return chat::MeshSendResult::fail(F::InvalidInput);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->desired.protocol == chat::MeshProtocol::Reticulum) return chat::MeshSendResult::fail(F::Unsupported);
    if (impl_->unsupported()) return chat::MeshSendResult::fail(F::Unsupported);
    if (!impl_->currentReady()) return chat::MeshSendResult::fail(F::NotReady);
    if (!impl_->desired.config.tx_enabled) return chat::MeshSendResult::fail(F::TxDisabled);
    if (impl_->desired.protocol == chat::MeshProtocol::MeshCore && peer && peer != 0xFFFFFFFFU &&
        std::find(impl_->view.keys.begin(), impl_->view.keys.end(), peer) == impl_->view.keys.end()) return chat::MeshSendResult::fail(F::PeerKeyMissing);
    Impl::Command command;
    command.kind = Impl::Kind::Text;
    command.channel = channel;
    command.text = text;
    command.peer = peer;
    command.message = impl_->reserveId(forced);
    for (const auto& result : impl_->outcomes)
        if (result.message == command.message && result.protocol == impl_->desired.protocol) return chat::MeshSendResult::fail(F::Busy);
    for (const auto& queued : impl_->commands)
        if (queued.kind == Impl::Kind::Text && queued.message == command.message && queued.protocol == impl_->desired.protocol)
            return chat::MeshSendResult::fail(F::Busy);
    if (impl_->active_message == command.message && impl_->active_protocol == impl_->desired.protocol)
        return chat::MeshSendResult::fail(F::Busy);
    const auto id = command.message;
    return impl_->enqueue(command) ? chat::MeshSendResult::success(id) : chat::MeshSendResult::fail(F::Busy);
}
chat::MeshSendResult LinuxMeshWorker::sendTextToReticulumDestination(chat::ChannelId channel, const std::string& text,
                                                                     chat::MessageId forced, const chat::ReticulumPeerIdentity& destination)
{
    using F = chat::MeshOperationFailure;
    if (channel != chat::ChannelId::PRIMARY || !LinuxReticulumLinks::textFitsTransport(text) ||
        !chat::hasReticulumDestinationIdentity(destination)) return chat::MeshSendResult::fail(F::InvalidInput);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->desired.protocol != chat::MeshProtocol::Reticulum) return chat::MeshSendResult::fail(F::Unsupported);
    if (!impl_->currentReady()) return chat::MeshSendResult::fail(F::NotReady);
    if (!impl_->desired.config.tx_enabled) return chat::MeshSendResult::fail(F::TxDisabled);
    Impl::Command command;
    command.kind = Impl::Kind::ReticulumText;
    command.channel = channel;
    command.text = text;
    command.reticulum = destination;
    command.message = impl_->reserveId(forced);
    command.timestamp = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (const auto& previous : impl_->outcomes)
        if (previous.message == command.message && previous.protocol == chat::MeshProtocol::Reticulum) return chat::MeshSendResult::fail(F::Busy);
    for (const auto& queued : impl_->commands)
        if ((queued.kind == Impl::Kind::Text || queued.kind == Impl::Kind::ReticulumText) && queued.message == command.message)
            return chat::MeshSendResult::fail(F::Busy);
    if (std::find(impl_->view.reticulum_pending.begin(), impl_->view.reticulum_pending.end(), command.message) != impl_->view.reticulum_pending.end() ||
        (impl_->active_message == command.message && impl_->active_protocol == chat::MeshProtocol::Reticulum)) return chat::MeshSendResult::fail(F::Busy);
    const auto id = command.message;
    if (!impl_->enqueue(command)) return chat::MeshSendResult::fail(F::Busy);
    auto result = chat::MeshSendResult::success(id);
    result.reticulum_identity = destination;
    return result;
}
bool LinuxMeshWorker::sendText(chat::ChannelId channel, const std::string& text, chat::MessageId* id, chat::NodeId peer)
{
    return sendTextWithId(channel, text, 0, id, peer);
}
bool LinuxMeshWorker::sendTextWithId(chat::ChannelId channel, const std::string& text, chat::MessageId forced, chat::MessageId* id, chat::NodeId peer)
{
    const auto result = sendTextDetailed(channel, text, forced, peer);
    if (id) *id = result.msg_id;
    return result.ok;
}
bool LinuxMeshWorker::sendAppData(chat::ChannelId channel, uint32_t port, const uint8_t* bytes, std::size_t size,
                                  chat::NodeId peer, bool ack, chat::MessageId packet, bool response)
{
    if (!port || !bytes || !size || size > 255) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->desired.config.tx_enabled) return false;
    Impl::Command command;
    command.kind = Impl::Kind::Data;
    command.channel = channel;
    command.port = port;
    command.peer = peer;
    command.ack = ack;
    command.response = response;
    command.message = impl_->reserveId(packet);
    command.data.assign(bytes, bytes + size);
    return impl_->enqueue(command);
}
bool LinuxMeshWorker::requestNodeInfo(chat::NodeId peer, bool response)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->desired.config.tx_enabled || !impl_->view.capabilities.supports_node_info) return false;
    Impl::Command command;
    command.kind = Impl::Kind::NodeInfo;
    command.peer = peer;
    command.response = response;
    return impl_->enqueue(command, true);
}
bool LinuxMeshWorker::triggerDiscoveryAction(chat::MeshDiscoveryAction action) { return triggerDiscoveryActionDetailed(action).ok; }
chat::MeshActionResult LinuxMeshWorker::triggerDiscoveryActionDetailed(chat::MeshDiscoveryAction action)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->unsupported()) return chat::MeshActionResult::fail(chat::MeshOperationFailure::Unsupported);
    if (!impl_->currentReady()) return chat::MeshActionResult::fail(chat::MeshOperationFailure::NotReady);
    if (!impl_->desired.config.tx_enabled) return chat::MeshActionResult::fail(chat::MeshOperationFailure::TxDisabled);
    Impl::Command command;
    command.kind = Impl::Kind::Discovery;
    command.action = action;
    return impl_->enqueue(command) ? chat::MeshActionResult::success() : chat::MeshActionResult::fail(chat::MeshOperationFailure::Busy);
}
bool LinuxMeshWorker::startKeyVerification(chat::NodeId peer)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->desired.config.tx_enabled) return false;
    if (!impl_->view.pki || std::find(impl_->view.keys.begin(), impl_->view.keys.end(), peer) == impl_->view.keys.end()) return false;
    Impl::Command command;
    command.kind = Impl::Kind::Verify;
    command.peer = peer;
    return impl_->enqueue(command);
}
bool LinuxMeshWorker::submitKeyVerificationNumber(chat::NodeId peer, uint64_t nonce, uint32_t number)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->desired.config.tx_enabled) return false;
    Impl::Command command;
    command.kind = Impl::Kind::VerificationNumber;
    command.peer = peer;
    command.nonce = nonce;
    command.number = number;
    return impl_->enqueue(command);
}
bool LinuxMeshWorker::takeOutcome(Outcome& out)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->outcomes.empty()) return false;
    out = impl_->outcomes.front();
    impl_->outcomes.pop_front();
    return true;
}
bool LinuxMeshWorker::pollIncomingText(chat::MeshIncomingText* out)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->texts.empty()) return false;
    if (out) *out = std::move(impl_->texts.front());
    impl_->texts.pop_front();
    return true;
}
bool LinuxMeshWorker::pollIncomingData(chat::MeshIncomingData* out)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->data.empty()) return false;
    if (out) *out = std::move(impl_->data.front());
    impl_->data.pop_front();
    return true;
}
void LinuxMeshWorker::commitIncomingText(const chat::MeshIncomingText& message, bool durable)
{
    if (!chat::hasReticulumLxmfMessageHash(message) || !chat::hasReticulumDestinationIdentity(message.reticulum_identity)) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->running || impl_->stopping || !impl_->configured || impl_->desired.protocol != chat::MeshProtocol::Reticulum) return;
    Impl::IncomingCommit* available = nullptr;
    for (auto& entry : impl_->incoming_commits)
    {
        if (entry.used && entry.epoch == impl_->receive_generation &&
            !std::memcmp(entry.token.reticulum_lxmf_hash, message.reticulum_lxmf_hash, 32) &&
            chat::sameReticulumDestinationHash(entry.token.reticulum_identity, message.reticulum_identity) &&
            !std::memcmp(entry.token.reticulum_identity.identity_hash, message.reticulum_identity.identity_hash, 16))
        {
            entry.durable = entry.durable || durable;
            impl_->wake.notify_one();
            return;
        }
        if ((!entry.used || entry.epoch != impl_->receive_generation) && !available) available = &entry;
    }
    if (!available) return; // Matches the link owner's bounded 32-message acceptance capacity.
    *available = {};
    available->used = true;
    available->durable = durable;
    available->epoch = impl_->receive_generation;
    available->token.has_reticulum_lxmf_hash = true;
    available->token.reticulum_identity = message.reticulum_identity;
    std::memcpy(available->token.reticulum_lxmf_hash, message.reticulum_lxmf_hash, 32);
    impl_->wake.notify_one();
}
bool LinuxMeshWorker::pollIncomingRawPacket(uint8_t* bytes, std::size_t& size, std::size_t capacity)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    size = 0;
    if (impl_->raw.empty()) return false;
    size = std::min(capacity, impl_->raw.front().size);
    if (bytes && size) std::memcpy(bytes, impl_->raw.front().data.data(), size);
    impl_->raw.pop_front();
    return true;
}
std::string LinuxMeshWorker::statusText() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->action_error.empty()) return impl_->action_error;
    if (!impl_->running) return impl_->view.status;
    if (impl_->view.generation != impl_->generation) return "Applying mesh settings";
    return impl_->view.status;
}
std::string LinuxMeshWorker::radioConfigText() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->view.config;
}
std::string LinuxMeshWorker::radioStatsText() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->view.statistics;
}
std::vector<std::string> LinuxMeshWorker::diagnosticLines() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->view.diagnostics;
}
} // namespace trailmate::linux_app
