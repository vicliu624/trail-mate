#pragma once

#include "chat/domain/reticulum_network_config.h"

namespace chat::reticulum
{

// Configuration admission shared by platform repositories. Transport-specific
// address parsing remains the responsibility of the selected bearer.
inline bool validateNetworkConfig(const ReticulumNetworkConfig& config)
{
    const auto text = [](const char* value, std::size_t capacity)
    { return value[0] && std::memchr(value, 0, capacity); };
    bool node_set = false;
    for (const auto byte : config.propagation.node_hash) node_set |= byte != 0;
    if (config.version != ReticulumNetworkConfig::kSchemaVersion ||
        config.interface_count > kMaxNetworkInterfaces ||
        static_cast<uint8_t>(config.propagation.delivery) > static_cast<uint8_t>(LxmfDeliveryPreference::Automatic) ||
        config.propagation.sync_interval_s < 60U || config.propagation.sync_interval_s > 24U * 60U * 60U ||
        config.propagation.max_messages_per_sync == 0U || config.propagation.max_messages_per_sync > 64U ||
        config.propagation.automatic_node == node_set)
        return false;

    uint8_t lora_count = 0, auto_count = 0, tcp_count = 0;
    for (std::size_t index = 0; index < config.interface_count; ++index)
    {
        const auto& entry = config.interfaces[index];
        if (!entry.access.valid() || (entry.access.enabled() && entry.type != NetworkInterfaceType::TcpClient && entry.type != NetworkInterfaceType::Auto) ||
            !text(entry.id, sizeof(entry.id)))
            return false;
        for (std::size_t prior = 0; prior < index; ++prior)
            if (!std::strcmp(entry.id, config.interfaces[prior].id)) return false;
        switch (entry.type)
        {
        case NetworkInterfaceType::IntegratedLoRa:
            if (++lora_count > 1U) return false;
            break;
        case NetworkInterfaceType::Auto:
            if (++auto_count > 1U || !text(entry.group_id, sizeof(entry.group_id)) ||
                !entry.discovery_port || entry.discovery_port == UINT16_MAX || !entry.data_port ||
                entry.data_port == entry.discovery_port || entry.data_port == entry.discovery_port + 1)
                return false;
            break;
        case NetworkInterfaceType::TcpClient:
            if (++tcp_count > kMaxTcpClientInterfaces || !text(entry.target_host, sizeof(entry.target_host)) || !entry.target_port)
                return false;
            break;
        default:
            return false;
        }
    }
    return true;
}

} // namespace chat::reticulum
