#include "platform/ui/reticulum_network_config_runtime.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace network = platform::ui::reticulum_network_config;
using chat::reticulum::NetworkInterfaceType;

int main()
{
    chat::MeshConfig legacy;
    legacy.reticulum_wifi_gateway_enabled = true;
    legacy.reticulum_lora_enabled = true;
    assert(network::reset(legacy));
    const auto& config = network::active();
    assert(config.interface_count == 5);
    assert(config.interfaces[0].type == NetworkInterfaceType::IntegratedLoRa);
    assert(config.interfaces[1].type == NetworkInterfaceType::Auto);
    for (unsigned i = 2; i < 5; ++i)
        assert(config.interfaces[i].type == NetworkInterfaceType::TcpClient &&
               config.interfaces[i].enabled && config.interfaces[i].target_port == 4242);

    assert(network::updateTcpEndpoint(1, "custom.example.net", 5252));
    assert(!std::strcmp(config.interfaces[3].target_host, "custom.example.net"));
    assert(config.interfaces[3].target_port == 5252);
    assert(!std::strcmp(config.interfaces[2].target_host, "sydney.reticulum.au"));
    assert(!std::strcmp(config.interfaces[4].target_host, "rmap.world"));
    const auto generation = network::status().generation;
    assert(!network::updateTcpEndpoint(3, "extra.example.net", 4242));
    assert(!network::updateTcpEndpoint(0, "bad host", 4242));
    assert(!network::updateTcpEndpoint(0, "tcp://bad", 4242));
    assert(!network::updateTcpEndpoint(0, "valid.example.net", 0));
    assert(network::status().generation == generation);

    assert(network::updateTcpEndpoint(1, "", 4242));
    assert(config.interface_count == 4);
    assert(!std::strcmp(config.interfaces[3].target_host, "rmap.world"));
    assert(network::updateTcpEndpoint(2, "replacement.example.net", 6000));
    assert(config.interface_count == 5);
    chat::reticulum::ReticulumNetworkConfig saved;
    assert(network::snapshotForTms(legacy, &saved));
    assert(network::reset(legacy));
    assert(network::setFromTms(saved));
    network::poll(legacy);
    assert(!std::strcmp(config.interfaces[4].target_host, "replacement.example.net"));
    assert(config.interfaces[4].target_port == 6000);
    for (unsigned i = 0; i < 3; ++i) assert(network::updateTcpEndpoint(0, "", 4242));
    assert(network::snapshotForTms(legacy, &saved));
    assert(saved.interface_count == 2);
    assert(network::reset(legacy));
    assert(network::setFromTms(saved));
    network::poll(legacy);
    assert(config.interface_count == 2); // Removed defaults stay removed.
    assert(!network::updateTcpEndpoint(2, "skip.example.net", 4242));
    assert(network::updateTcpEndpoint(0, "new.example.net", 4242));

    std::snprintf(legacy.reticulum_wifi_gateway_host, sizeof(legacy.reticulum_wifi_gateway_host), "legacy.example.net");
    legacy.reticulum_wifi_gateway_port = 9999;
    assert(network::reset(legacy));
    assert(config.interface_count == 3);
    assert(!std::strcmp(config.interfaces[2].target_host, "legacy.example.net"));
    assert(config.interfaces[2].target_port == 9999);
    assert(network::snapshotForTms(legacy, &saved));
    // A retained non-TCP ID may collide with factory TCP IDs.
    std::snprintf(saved.interfaces[0].id, sizeof(saved.interfaces[0].id), "public-tcp-1");
    assert(network::setFromTms(saved));
    const auto before_restore = network::status().generation;
    assert(network::restoreDefaultTcpEndpoints());
    assert(network::status().generation == before_restore + 1);
    assert(config.interface_count == 5);
    assert(!std::memcmp(&config.interfaces[0], &saved.interfaces[0], sizeof(saved.interfaces[0])));
    assert(!std::memcmp(&config.interfaces[1], &saved.interfaces[1], sizeof(saved.interfaces[1])));
    assert(!std::strcmp(config.interfaces[2].target_host, "sydney.reticulum.au"));
    assert(!std::strcmp(config.interfaces[3].target_host, "node.reticulumnet.nl"));
    assert(!std::strcmp(config.interfaces[4].target_host, "rmap.world"));
    assert(network::validateForTms(config));
    assert(network::snapshotForTms(legacy, &saved));
    assert(network::reset(legacy));
    assert(network::setFromTms(saved));
    network::poll(legacy);
    assert(!std::strcmp(config.interfaces[2].target_host, "sydney.reticulum.au"));
    legacy.reticulum_interface_policy = chat::ReticulumInterfacePolicy::LoRaOnly;
    assert(network::reset(legacy));
    assert(config.interface_count == 1);
    std::puts("TCP defaults, editing, removal, TMS snapshot restore, legacy override and LoRa-only policy passed");
}
