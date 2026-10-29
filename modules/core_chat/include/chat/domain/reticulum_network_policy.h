#pragma once
#include "chat/domain/chat_types.h"
#include "chat/domain/reticulum_network_validation.h"
#include <cstdio>

namespace chat::reticulum::network_config_policy
{
using NetworkConfig = ReticulumNetworkConfig;
using InterfaceConfig = NetworkInterfaceConfig;
// Existing factory seeds shared by all platform projections. They are logical
// endpoints, not Linux interface names or board wiring.
inline constexpr const char* kDefaultTcpHosts[] = {
    "sydney.reticulum.au", "node.reticulumnet.nl", "rmap.world"};

inline void reset_config(NetworkConfig* config)
{
    if (!config)
    {
        return;
    }
    config->version = NetworkConfig::kSchemaVersion;
    config->interface_count = 0U;
    for (auto& interface_config : config->interfaces)
    {
        interface_config = InterfaceConfig{};
    }
    config->propagation = chat::reticulum::LxmfPropagationClientConfig{};
}

inline bool append_default_interface(NetworkConfig* config,
                                     chat::reticulum::NetworkInterfaceType type,
                                     const char* id,
                                     InterfaceConfig** out = nullptr)
{
    if (!config || config->interface_count >= chat::reticulum::kMaxNetworkInterfaces)
    {
        return false;
    }
    InterfaceConfig* const interface_config = &config->interfaces[config->interface_count++];
    *interface_config = InterfaceConfig{};
    interface_config->type = type;
    interface_config->enabled = true;
    std::snprintf(interface_config->id, sizeof(interface_config->id), "%s", id ? id : "");
    if (out)
    {
        *out = interface_config;
    }
    return true;
}

inline bool build_defaults(const chat::MeshConfig& legacy_config, NetworkConfig* out)
{
    if (!out)
    {
        return false;
    }
    reset_config(out);
    const bool allow_lora = legacy_config.reticulum_lora_enabled &&
                            legacy_config.reticulum_interface_policy !=
                                chat::ReticulumInterfacePolicy::WifiGatewayOnly;
    const bool allow_ip = legacy_config.reticulum_wifi_gateway_enabled &&
                          legacy_config.reticulum_interface_policy !=
                              chat::ReticulumInterfacePolicy::LoRaOnly;
    if (allow_lora &&
        !append_default_interface(out,
                                  chat::reticulum::NetworkInterfaceType::IntegratedLoRa,
                                  "integrated-lora"))
    {
        return false;
    }
    if (allow_ip)
    {
        if (!append_default_interface(out,
                                      chat::reticulum::NetworkInterfaceType::Auto,
                                      "local-wifi"))
        {
            return false;
        }
        if (legacy_config.reticulum_wifi_gateway_host[0] != '\0')
        {
            InterfaceConfig* tcp = nullptr;
            if (!append_default_interface(out,
                                          chat::reticulum::NetworkInterfaceType::TcpClient,
                                          "primary-tcp",
                                          &tcp))
            {
                return false;
            }
            std::snprintf(tcp->target_host,
                          sizeof(tcp->target_host),
                          "%s",
                          legacy_config.reticulum_wifi_gateway_host);
            tcp->target_port = legacy_config.reticulum_wifi_gateway_port != 0U
                                   ? legacy_config.reticulum_wifi_gateway_port
                                   : 4242U;
        }
        else
        {
            // Factory seeds only: an explicit TMS configuration (including
            // removed or disabled entries) always takes precedence.
            const auto& hosts = kDefaultTcpHosts;
            static_assert(sizeof(hosts) / sizeof(hosts[0]) <= chat::reticulum::kMaxTcpClientInterfaces);
            for (size_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); ++i)
            {
                char id[24];
                std::snprintf(id, sizeof(id), "public-tcp-%u", static_cast<unsigned>(i + 1));
                InterfaceConfig* tcp = nullptr;
                if (!append_default_interface(out, chat::reticulum::NetworkInterfaceType::TcpClient, id, &tcp))
                {
                    return false;
                }
                std::snprintf(tcp->target_host, sizeof(tcp->target_host), "%s", hosts[i]);
                tcp->target_port = 4242U;
            }
        }
    }
    return true;
}

// Edits operate on caller-owned scratch. Commit only on true; false may leave
// scratch changed. Platform runtimes retain their previous active snapshot.
inline bool updateTcpEndpoint(NetworkConfig& candidate, std::size_t slot, const char* host, uint16_t port)
{
    if (!host || slot >= chat::reticulum::kMaxTcpClientInterfaces || !port || !validateNetworkConfig(candidate)) return false;
    NetworkConfig* s_active = &candidate;
    for (std::size_t i = 0; host[i]; ++i)
        if (i >= chat::reticulum::kInterfaceHostMaxLen || static_cast<unsigned char>(host[i]) <= 32 ||
            host[i] == '/' || host[i] == '\\') return false;
    std::size_t ordinal = 0;
    std::size_t index = 0;
    for (; index < s_active->interface_count; ++index)
    {
        if (s_active->interfaces[index].type != chat::reticulum::NetworkInterfaceType::TcpClient) continue;
        if (ordinal++ == slot) break;
    }
    const bool append = index == s_active->interface_count;
    if (append && (ordinal != slot || index >= chat::reticulum::kMaxNetworkInterfaces || !host[0])) return false;
    if (!host[0])
    {
        for (std::size_t i = index + 1; i < s_active->interface_count; ++i)
            s_active->interfaces[i - 1] = s_active->interfaces[i];
        s_active->interfaces[--s_active->interface_count] = InterfaceConfig{};
    }
    else
    {
        auto& entry = s_active->interfaces[index];
        if (append)
        {
            entry = InterfaceConfig{};
            entry.type = chat::reticulum::NetworkInterfaceType::TcpClient;
            entry.enabled = true;
            // Select an unused ID even after earlier entries were removed.
            for (unsigned suffix = 1; suffix <= chat::reticulum::kMaxNetworkInterfaces + 1; ++suffix)
            {
                std::snprintf(entry.id, sizeof(entry.id), "user-tcp-%u", suffix);
                bool duplicate = false;
                for (std::size_t i = 0; i < index; ++i)
                    duplicate |= std::strcmp(entry.id, s_active->interfaces[i].id) == 0;
                if (!duplicate) break;
            }
            ++s_active->interface_count;
        }
        std::snprintf(entry.target_host, sizeof(entry.target_host), "%s", host);
        entry.target_port = port;
    }
    return validateNetworkConfig(candidate);
}

inline bool restoreDefaultTcpEndpoints(NetworkConfig& target)
{
    if (!validateNetworkConfig(target)) return false;
    auto* candidate = &target;
    size_t kept = 0;
    for (size_t i = 0; i < candidate->interface_count; ++i)
    {
        if (candidate->interfaces[i].type != chat::reticulum::NetworkInterfaceType::TcpClient)
            candidate->interfaces[kept++] = candidate->interfaces[i];
    }
    candidate->interface_count = kept;
    for (size_t i = kept; i < chat::reticulum::kMaxNetworkInterfaces; ++i)
        candidate->interfaces[i] = InterfaceConfig{};
    bool valid = true;
    for (size_t i = 0; i < sizeof(kDefaultTcpHosts) / sizeof(kDefaultTcpHosts[0]); ++i)
    {
        char id[24];
        // Avoid collisions with preserved non-TCP interface IDs.
        unsigned suffix = 1;
        bool duplicate;
        do
        {
            std::snprintf(id, sizeof(id), "public-tcp-%u", suffix++);
            duplicate = false;
            for (size_t j = 0; j < candidate->interface_count; ++j)
                duplicate |= std::strcmp(id, candidate->interfaces[j].id) == 0;
        } while (duplicate);
        InterfaceConfig* tcp = nullptr;
        if (!append_default_interface(candidate, chat::reticulum::NetworkInterfaceType::TcpClient, id, &tcp))
        {
            valid = false;
            break;
        }
        std::snprintf(tcp->target_host, sizeof(tcp->target_host), "%s", kDefaultTcpHosts[i]);
        tcp->target_port = 4242;
    }
    return valid && validateNetworkConfig(*candidate);
}

} // namespace chat::reticulum::network_config_policy
