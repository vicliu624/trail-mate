#include "app/linux_app_services.h"
#include "chat/linux_reticulum_network_store.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/runtime_packet_log.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
namespace port = platform::linux_runtime;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
struct NoRadio final : port::IPacketRadio
{
    const char* name() const override { return "no physical radio in MQTT test"; }
    bool hardwareCandidatePresent() const override { return false; }
    bool acquire() override { throw std::runtime_error("MQTT acquired physical radio"); }
    void release() override { throw std::runtime_error("MQTT released unowned physical radio"); }
    bool configureLoRa(const port::RadioSettings&) override { throw std::runtime_error("MQTT configured physical radio"); }
    bool startReceive() override { throw std::runtime_error("MQTT started RF receive"); }
    bool transmit(const uint8_t*, size_t) override { throw std::runtime_error("MQTT transmitted RF"); }
    bool pollReceive(port::RadioPacket*) override { throw std::runtime_error("MQTT polled physical RF"); }
    float readRssi() override { throw std::runtime_error("MQTT sampled physical RF"); }
    bool isOnline() const override { return false; }
    const char* lastError() const override { return "physical radio forbidden"; }
    port::RadioSettings appliedLoRaConfig() const override { return {}; }
    port::RadioStatistics stats() const override { return {}; }
};
} // namespace
int main(int argc, char** argv)
{
    if (argc != 5) return 2;
    const std::string mode = argv[2], role = argv[3];
    const auto protocol = mode == "mc" ? chat::MeshProtocol::MeshCore : chat::MeshProtocol::Meshtastic;
    const auto port_number = static_cast<uint16_t>(std::stoul(argv[1]));
    const std::filesystem::path root(argv[4]);
    std::filesystem::create_directories(root);
    for (const char* key : {"TRAIL_MATE_SETTINGS_ROOT", "TRAIL_MATE_CACHE_ROOT", "TRAIL_MATE_STATE_ROOT", "TRAIL_MATE_SD_ROOT"}) setenv(key, root.c_str(), 1);
    setenv("TRAIL_MATE_DESKTOP_NOTIFICATIONS", "0", 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-no-test-gps", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    require(port::LinuxReticulumNetworkStore::save(chat::reticulum::ReticulumNetworkConfig{}, app::AppConfig{}), "isolated network state failed");
    NoRadio radio;
    trailmate::linux_app::LinuxAppServicesOptions options;
    options.runtime_mode = port::LinuxRuntimeMode::DeviceRealMesh;
    options.packet_radio = &radio;
    const std::string own_text = "MQTT application " + role;
    const std::string other_text = "MQTT application " + std::string(role == "A" ? "B" : "A");
    chat::MessageId sent = 0;
    chat::MessageId private_sent = 0;
    chat::NodeId peer = 0;
    bool announced = false, private_received = false, private_written = false;
    std::chrono::steady_clock::time_point settled{};
    bool received = false, written = false;
    bool node_info_queried = false, node_info_received = protocol != chat::MeshProtocol::MeshCore;
    {
        trailmate::linux_app::LinuxAppServices services(options);
        require(services.initialize(), "application services initialization failed");
        {
            auto edit = services.beginConfigEdit();
            auto& config = edit.config();
            config.mesh_protocol = protocol;
            std::snprintf(config.node_name, sizeof(config.node_name), "MQTT node %s long", role.c_str());
            std::snprintf(config.short_name, sizeof(config.short_name), "%s", role.c_str());
            auto& mesh = protocol == chat::MeshProtocol::MeshCore ? config.meshcore_config : config.meshtastic_config;
            mesh.tx_enabled = true;
            mesh.ignore_mqtt = true; // RF forwarded-MQTT filter must not disable explicit broker downlink.
            std::snprintf(mesh.primary_channel_name, sizeof(mesh.primary_channel_name), "primary");
            for (unsigned index = 0; index < 16; ++index) mesh.meshcore_channels[0].key[index] = static_cast<uint8_t>(index);
            mesh.meshcore_client_repeat = true; // Must not turn broker input into RF relay.
            if (protocol == chat::MeshProtocol::Meshtastic)
            {
                config.meshtastic_mqtt_enabled = true;
                config.meshtastic_mqtt_uplink_enabled = config.meshtastic_mqtt_downlink_enabled = true;
                std::snprintf(config.meshtastic_mqtt_host, sizeof(config.meshtastic_mqtt_host), "127.0.0.1");
                config.meshtastic_mqtt_port = port_number;
                std::snprintf(config.meshtastic_mqtt_root, sizeof(config.meshtastic_mqtt_root), "fixture");
                std::snprintf(config.meshtastic_mqtt_username, sizeof(config.meshtastic_mqtt_username), "u");
                std::snprintf(config.meshtastic_mqtt_password, sizeof(config.meshtastic_mqtt_password), "p");
            }
            else
            {
                mesh.meshcore_mqtt_enabled = true;
                mesh.meshcore_mqtt_uplink_enabled = mesh.meshcore_mqtt_downlink_enabled = true;
                std::snprintf(mesh.meshcore_mqtt_host, sizeof(mesh.meshcore_mqtt_host), "127.0.0.1");
                mesh.meshcore_mqtt_port = port_number;
                std::snprintf(mesh.meshcore_mqtt_root, sizeof(mesh.meshcore_mqtt_root), "fixture");
                std::snprintf(mesh.meshcore_mqtt_username, sizeof(mesh.meshcore_mqtt_username), "u");
                std::snprintf(mesh.meshcore_mqtt_password, sizeof(mesh.meshcore_mqtt_password), "p");
            }
            edit.commit(app::AppConfigChangeSet::mesh().merged(app::AppConfigChangeSet::network()).merged(app::AppConfigChangeSet::channels()));
        }
        services.applyUserInfo();
        services.applyMeshConfig();
        const auto started = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - started < std::chrono::seconds(8))
        {
            services.tick(128);
            chat::MeshTransportStatus transport;
            auto* adapter = services.getMeshAdapter();
            if (adapter && adapter->getTransportStatus(&transport) && transport.ready)
            {
                std::ofstream(root / "mqtt-ready").put('1');
                if (!std::filesystem::exists(root.parent_path() / (role == "A" ? "B" : "A") / "mqtt-ready"))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
            }
            if (!announced && adapter && adapter->getTransportStatus(&transport) && transport.ready)
            {
                require(adapter->requestNodeInfo(0, false), "MQTT NodeInfo announcement not queued");
                announced = true;
            }
            if (!sent && adapter && adapter->getTransportStatus(&transport) && transport.ready)
            {
                require(transport.network_ready && !transport.radio_ready, "MQTT readiness was projected as RF");
                sent = services.getChatService().sendText(chat::ChannelId::PRIMARY, own_text);
                require(sent, "application message was not queued");
            }
            if (sent)
            {
                const auto* message = services.getChatService().getMessageForProtocol(sent, protocol);
                if (message && message->status == chat::MessageStatus::Sent) written = true;
                require(!message || message->status != chat::MessageStatus::Delivered, "broker write falsely delivered");
            }
            const auto messages = services.getChatService().getRecentMessages(chat::ConversationId(chat::ChannelId::PRIMARY, 0, protocol), 16);
            for (const auto& message : messages)
                if (message.text.find(other_text) != std::string::npos)
                {
                    require(message.status == chat::MessageStatus::Incoming && message.rx_origin == chat::RxOrigin::External, "MQTT receive provenance/status lost");
                    received = true;
                }
            if (!peer)
                for (const auto& item : services.getContactService().getAllPeers())
                    if (item.node_id != services.getSelfNodeId() && item.has_public_key && !item.is_ignored)
                    {
                        peer = item.node_id;
                        break;
                    }
            if (peer && received && written && !private_sent)
            {
                private_sent = services.getChatService().sendText(chat::ChannelId::PRIMARY, "MQTT private " + role, peer);
                require(private_sent, "MQTT private message not queued after key discovery");
            }
            if (protocol == chat::MeshProtocol::MeshCore && peer && !node_info_queried)
            {
                require(adapter->requestNodeInfo(peer, true), "MQTT NodeInfo query not queued");
                node_info_queried = true;
            }
            if (node_info_queried)
            {
                const auto* info = services.getContactService().getPeerByNodeId(peer);
                const auto expected = "MQTT node " + std::string(role == "A" ? "B" : "A") + " long";
                if (info && info->long_name == expected)
                {
                    require(info->via_mqtt && !info->has_rssi && !info->has_snr && info->has_public_key, "MQTT NodeInfo provenance/key state changed");
                    node_info_received = true;
                }
            }
            if (private_sent)
            {
                const auto* message = services.getChatService().getMessageForProtocol(private_sent, protocol);
                private_written = message && (message->status == chat::MessageStatus::Sent || message->status == chat::MessageStatus::Delivered);
                for (const auto& item : services.getChatService().getRecentMessages(chat::ConversationId(chat::ChannelId::PRIMARY, peer, protocol), 16))
                    if (item.text.find("MQTT private " + std::string(role == "A" ? "B" : "A")) != std::string::npos)
                    {
                        require(item.rx_origin == chat::RxOrigin::External && item.status == chat::MessageStatus::Incoming && !item.source_unverified,
                                "MQTT private identity/provenance lost");
                        private_received = true;
                    }
            }
            if (received && written && private_received && private_written && node_info_received)
            {
                if (settled == std::chrono::steady_clock::time_point{}) settled = std::chrono::steady_clock::now();
                if (std::chrono::steady_clock::now() - settled > std::chrono::milliseconds(400)) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!(received && written && private_received && private_written))
        {
            std::cerr << "MQTT_DIAGNOSTIC role=" << role << " public=" << received << '/' << written << " peer=" << peer
                      << " private=" << private_sent << '/' << private_received << '/' << private_written << '\n';
            if (const auto* message = services.getChatService().getMessageForProtocol(private_sent, protocol))
                std::cerr << "MQTT_PRIVATE_STATUS " << static_cast<unsigned>(message->status) << '\n';
            for (const auto& item : services.getContactService().getAllPeers())
                std::cerr << "MQTT_PEER " << item.node_id << " key=" << item.has_public_key << " ignored=" << item.is_ignored << '\n';
            for (const auto& entry : port::recent_packet_logs(port::PacketLogSource::Lora, 12))
                std::cerr << "MQTT_ENGINE " << entry.title << ": " << entry.summary << '\n';
        }
        require(received && written && private_received && private_written && node_info_received, "application MQTT chat/private/NodeInfo roundtrip timed out");
        services.shutdown();
    }
    {
        trailmate::linux_app::LinuxAppServices reopened(options);
        require(reopened.initialize(), "application reopen failed");
        const auto* outgoing = reopened.getChatService().getMessageForProtocol(sent, protocol);
        require(outgoing && outgoing->status == chat::MessageStatus::Sent, "MQTT sent status did not persist");
        bool restored = false;
        for (const auto& message : reopened.getChatService().getRecentMessages(chat::ConversationId(chat::ChannelId::PRIMARY, 0, protocol), 16))
            restored |= message.text.find(other_text) != std::string::npos && message.rx_origin == chat::RxOrigin::External;
        require(restored, "MQTT inbox/provenance did not persist");
        bool private_restored = false;
        for (const auto& message : reopened.getChatService().getRecentMessages(chat::ConversationId(chat::ChannelId::PRIMARY, peer, protocol), 16))
            private_restored |= message.text.find("MQTT private " + std::string(role == "A" ? "B" : "A")) != std::string::npos && message.rx_origin == chat::RxOrigin::External;
        require(private_restored, "MQTT private inbox did not persist");
        if (protocol == chat::MeshProtocol::MeshCore)
        {
            const auto* info = reopened.getContactService().getPeerByNodeId(peer);
            require(info && std::string(info->long_name) == "MQTT node " + std::string(role == "A" ? "B" : "A") + " long", "MQTT NodeInfo name did not persist");
        }
        reopened.shutdown();
    }
    std::cout << "MQTT_APPLICATION_PASSED " << mode << ' ' << role << '\n';
}
