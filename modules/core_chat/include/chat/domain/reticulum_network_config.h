/**
 * @file reticulum_network_config.h
 * @brief Bounded Reticulum interface and LXMF client configuration
 */

#pragma once

#include "chat/infra/reticulum/reticulum_wire.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chat::reticulum
{

constexpr std::size_t kMaxNetworkInterfaces = 6;
constexpr std::size_t kMaxTcpClientInterfaces = 3;
constexpr std::size_t kInterfaceIdMaxLen = 23;
constexpr std::size_t kInterfaceHostMaxLen = 63;
constexpr std::size_t kAutoInterfaceGroupMaxLen = 31;

struct InterfaceAccessConfig
{
    char network_name[64] = {};
    char passphrase[64] = {};
    uint16_t ifac_size_bits = 0;

    bool enabled() const { return network_name[0] || passphrase[0]; }
    bool valid() const
    {
        if (!std::memchr(network_name, 0, sizeof(network_name)) || !std::memchr(passphrase, 0, sizeof(passphrase))) return false;
        return enabled() ? ifac_size_bits >= 8 && ifac_size_bits <= 512 && ifac_size_bits % 8 == 0 : ifac_size_bits == 0;
    }
    bool operator==(const InterfaceAccessConfig& other) const
    {
        return ifac_size_bits == other.ifac_size_bits &&
               !std::memcmp(network_name, other.network_name, sizeof(network_name)) &&
               !std::memcmp(passphrase, other.passphrase, sizeof(passphrase));
    }
};

enum class NetworkInterfaceType : uint8_t
{
    IntegratedLoRa = 0,
    Auto = 1,
    TcpClient = 2,
};

enum class LxmfDeliveryPreference : uint8_t
{
    Direct = 0,
    Propagated = 1,
    Automatic = 2,
};

struct NetworkInterfaceConfig
{
    char id[kInterfaceIdMaxLen + 1] = {};
    NetworkInterfaceType type = NetworkInterfaceType::IntegratedLoRa;
    bool enabled = false;
    char target_host[kInterfaceHostMaxLen + 1] = {};
    uint16_t target_port = 4242;
    char group_id[kAutoInterfaceGroupMaxLen + 1] = "reticulum";
    uint16_t discovery_port = 29716;
    uint16_t data_port = 42671;
    InterfaceAccessConfig access;
};

struct LxmfPropagationClientConfig
{
    bool enabled = true;
    bool service_enabled = false;
    LxmfDeliveryPreference delivery = LxmfDeliveryPreference::Automatic;
    bool automatic_node = true;
    uint8_t node_hash[kTruncatedHashSize] = {};
    bool sync_on_start = true;
    uint32_t sync_interval_s = 15U * 60U;
    uint8_t max_messages_per_sync = 32;
};

struct ReticulumNetworkConfig
{
    static constexpr uint16_t kSchemaVersion = 1;

    uint16_t version = kSchemaVersion;
    NetworkInterfaceConfig interfaces[kMaxNetworkInterfaces] = {};
    uint8_t interface_count = 0;
    LxmfPropagationClientConfig propagation{};
};

} // namespace chat::reticulum
