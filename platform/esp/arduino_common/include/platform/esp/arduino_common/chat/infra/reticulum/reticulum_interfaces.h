/**
 * @file reticulum_interfaces.h
 * @brief Reticulum carrier interface set for device-side LXMF runtime
 */

#pragma once

#include "board/LoraBoard.h"
#include "chat/domain/chat_types.h"
#include "chat/domain/reticulum_network_config.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "platform/esp/arduino_common/chat/infra/rnode/rnode_adapter.h"
#include "sys/ringbuf.h"

#include <array>
#include <cstddef>
#include <cstdint>

#if __has_include(<WiFi.h>)
#include "platform/esp/arduino_common/net/async_tcp_connector.h"
#include <WiFi.h>
#define TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE 1
#else
#define TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE 0
#endif

#if defined(ESP_PLATFORM) && !defined(ARDUINO)
#define TRAIL_MATE_RETICULUM_C6_TCP_AVAILABLE 1
#else
#define TRAIL_MATE_RETICULUM_C6_TCP_AVAILABLE 0
#endif

#define TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE \
    (TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE || TRAIL_MATE_RETICULUM_C6_TCP_AVAILABLE)

namespace chat::reticulum::interfaces
{

enum class InterfaceKind : uint8_t
{
    LoRa = 0,
    WifiGateway = 1,
    Auto = 2,
};

using InterfaceId = uint8_t;
constexpr InterfaceId kInvalidInterfaceId = 0;
constexpr InterfaceId kLoRaInterfaceId = 1;
constexpr InterfaceId kAutoInterfaceIdBase = 16;
constexpr InterfaceId kTcpClientInterfaceIdBase = 32;

struct RxPacket
{
    uint8_t data[reticulum::kReticulumMtu] = {};
    size_t len = 0;
    RxMeta rx_meta{};
    InterfaceKind interface_kind = InterfaceKind::LoRa;
    InterfaceId interface_id = kInvalidInterfaceId;
};

struct TxResult
{
    bool lora_required = false;
    bool lora_ready = false;
    bool lora_ok = false;
    bool wifi_required = false;
    bool wifi_ready = false;
    bool wifi_ok = false;
    InterfaceId sent_interface = kInvalidInterfaceId;
    uint8_t sent_count = 0;

    bool sent() const { return lora_ok || wifi_ok; }
    bool reachedRequiredInterfaces() const
    {
        return sent() &&
               (!lora_required || lora_ok) &&
               (!wifi_required || wifi_ok);
    }
};

class LoRaReticulumInterface
{
  public:
    explicit LoRaReticulumInterface(LoraBoard& board);

    void applyConfig(const MeshConfig& config, bool enabled);
    bool isReady() const;
    bool sendPacket(const uint8_t* data, size_t len);
    bool pollPacket(RxPacket* out);
    bool pollLegacyIncomingData(MeshIncomingData* out);
    void handleRawPacket(const uint8_t* data, size_t size);
    void setLastRxStats(float rssi, float snr);
    float lastRxRssi() const;
    float lastRxSnr() const;

  private:
    rnode::RNodeAdapter raw_;
    bool enabled_ = true;
};

class WifiGatewayReticulumInterface
{
  public:
    WifiGatewayReticulumInterface();

    void applyConfig(const reticulum::NetworkInterfaceConfig* config,
                     bool auto_connect_wifi,
                     InterfaceId interface_id);
    void setTransportEnabled(bool enabled);
    void maintain();
    bool isReady() const;
    bool isConfigured() const;
    bool sendPacket(const uint8_t* data,
                    size_t len,
                    const uint8_t* call_link_id = nullptr,
                    bool call_admission_control = false);
    bool pollPacket(RxPacket* out);
    InterfaceId interfaceId() const { return interface_id_; }
    const char* host() const { return host_; }
    uint16_t port() const { return port_; }

  private:
    struct QueuedPacket
    {
        uint8_t data[reticulum::kReticulumMtu] = {};
        size_t len = 0;
        RxMeta rx_meta{};
    };

    static constexpr size_t kRxQueueDepth = 8;
    static constexpr size_t kRxPriorityQueueDepth = 4;
    static constexpr uint8_t kHdlcFlag = 0x7E;
    static constexpr uint8_t kHdlcEscape = 0x7D;
    static constexpr uint8_t kHdlcEscapeMask = 0x20;
    static constexpr uint32_t kReconnectIntervalMs = 10000;
    static constexpr uint32_t kRxStatsLogIntervalMs = 5000;
    static constexpr int32_t kSocketConnectTimeoutMs = 5000;

    bool enabled_ = false;
    bool transport_enabled_ = true;
    bool auto_connect_wifi_ = true;
    InterfaceId interface_id_ = kInvalidInterfaceId;
    char host_[kReticulumGatewayHostMaxLen + 1] = {};
    uint16_t port_ = 4242;
    bool socket_online_ = false;
    bool socket_open_pending_ = false;
    uint32_t last_reconnect_ms_ = 0;
    uint32_t last_socket_read_ms_ = 0;
    bool hdlc_in_frame_ = false;
    bool hdlc_escape_ = false;
    size_t hdlc_frame_len_ = 0;
    uint32_t rx_stats_last_log_ms_ = 0;
    uint32_t rx_stats_frames_ = 0;
    uint32_t rx_stats_priority_frames_ = 0;
    uint32_t rx_stats_drops_ = 0;
    uint32_t rx_stats_bytes_ = 0;
    uint32_t rx_stats_read_skips_ = 0;
    uint8_t hdlc_frame_[reticulum::kReticulumMtu] = {};
    uint8_t tx_frame_[(reticulum::kReticulumMtu * 2U) + 2U] = {};
    uint8_t socket_rx_scratch_[256] = {};
    QueuedPacket poll_scratch_{};
    QueuedPacket enqueue_scratch_{};
    sys::RingBuffer<QueuedPacket, kRxPriorityQueueDepth> rx_priority_queue_;
    sys::RingBuffer<QueuedPacket, kRxQueueDepth> rx_queue_;

#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE
    WiFiClient client_;
    platform::esp::arduino_common::net::AsyncTcpConnector connector_;
#endif

    void stop();
    bool connected() const;
    void syncSocketState();
    bool ensureSocket();
    void readAvailable();
    void feedHdlcByte(uint8_t byte);
    void enqueueFrame(const uint8_t* data, size_t len);
    void fillRxMeta(RxMeta* out) const;
};

class AutoReticulumInterface
{
  public:
    AutoReticulumInterface();

    void applyConfig(const reticulum::NetworkInterfaceConfig* config,
                     bool auto_connect_wifi);
    void setTransportEnabled(bool enabled);
    void maintain();
    bool isReady() const;
    bool isConfigured() const;
    bool sendPacket(const uint8_t* data,
                    size_t len,
                    const uint8_t* call_link_id = nullptr,
                    bool call_admission_control = false);
    bool sendPacketOn(InterfaceId interface_id,
                      const uint8_t* data,
                      size_t len,
                      const uint8_t* call_link_id = nullptr,
                      bool call_admission_control = false);
    bool pollPacket(RxPacket* out);
    bool owns(InterfaceId interface_id) const;
    uint8_t peerCount() const;

  private:
    struct Peer
    {
        uint8_t address[16] = {};
        uint32_t scope_id = 0;
        uint32_t last_seen_ms = 0;
        uint32_t last_reverse_announce_ms = 0;
        InterfaceId interface_id = kInvalidInterfaceId;
        bool active = false;
    };

    static constexpr size_t kMaxPeers = 6;
    static constexpr size_t kRxQueueDepth = 8;
    static constexpr uint32_t kAnnounceIntervalMs = 1600;
    static constexpr uint32_t kPeerTimeoutMs = 22000;
    static constexpr uint32_t kReverseAnnounceIntervalMs = 5200;
    static constexpr uint32_t kWifiConnectRetryIntervalMs = 10000;

    bool enabled_ = false;
    bool transport_enabled_ = true;
    bool auto_connect_wifi_ = true;
    bool sockets_ready_ = false;
    char group_id_[reticulum::kAutoInterfaceGroupMaxLen + 1] = "reticulum";
    uint16_t discovery_port_ = 29716;
    uint16_t data_port_ = 42671;
    int discovery_socket_ = -1;
    int unicast_discovery_socket_ = -1;
    int data_socket_ = -1;
    uint32_t interface_index_ = 0;
    uint8_t local_address_[16] = {};
    uint8_t multicast_address_[16] = {};
    uint8_t discovery_token_[reticulum::kFullHashSize] = {};
    uint32_t last_announce_ms_ = 0;
    uint32_t last_socket_attempt_ms_ = 0;
    uint32_t wifi_connect_retry_not_before_ms_ = 0;
    bool wifi_connect_suspended_ = false;
    std::array<Peer, kMaxPeers> peers_{};
    RxPacket rx_scratch_{};
    sys::RingBuffer<RxPacket, kRxQueueDepth> rx_queue_;

    void stop();
    bool ensureSockets();
    void closeSockets();
    void calculateDiscoveryIdentity();
    void sendPeerAnnounce();
    void sendReverseAnnounce(Peer& peer);
    void receiveDiscovery(int socket_fd);
    void receiveData();
    void cullPeers(uint32_t now_ms);
    Peer* upsertPeer(const uint8_t address[16], uint32_t scope_id);
    Peer* findPeer(InterfaceId interface_id);
};

class ReticulumInterfaceSet
{
  public:
    explicit ReticulumInterfaceSet(LoraBoard& board, bool owns_integrated_radio = true);

    void applyConfig(const MeshConfig& config,
                     const reticulum::ReticulumNetworkConfig& network_config);
    void setWifiTransportEnabled(bool enabled);
    void maintain();
    bool hasReadyInterface() const;
    bool hasReadyWifiGateway() const;
    bool wifiGatewayConfigured() const;
    bool isInterfaceSelected(InterfaceId interface_id) const;
    bool sendPacket(const uint8_t* data, size_t len);
    bool sendPacketOn(InterfaceId interface_id,
                      const uint8_t* data,
                      size_t len,
                      const uint8_t* call_link_id = nullptr,
                      bool call_admission_control = false);
    bool sendPacketWifiOnly(const uint8_t* data,
                            size_t len,
                            const uint8_t* call_link_id = nullptr,
                            bool call_admission_control = false,
                            InterfaceId interface_id = kInvalidInterfaceId);
    bool pollIncomingPacket(RxPacket* out);
    bool pollLegacyIncomingData(MeshIncomingData* out);
    void handleRawPacket(const uint8_t* data, size_t size);
    void setLastRxStats(float rssi, float snr);
    float lastRxRssi() const;
    float lastRxSnr() const;
    const RxMeta& lastRxMeta() const { return last_rx_meta_; }
    const TxResult& lastTxResult() const { return last_tx_result_; }

  private:
    LoRaReticulumInterface lora_;
    const bool owns_integrated_radio_;
    AutoReticulumInterface auto_;
    std::array<WifiGatewayReticulumInterface,
               reticulum::kMaxTcpClientInterfaces>
        tcp_{};
    MeshConfig config_{};
    reticulum::ReticulumNetworkConfig network_config_{};
    RxMeta last_rx_meta_{};
    TxResult last_tx_result_{};
    bool has_last_rx_meta_ = false;
    uint8_t tcp_count_ = 0;
    uint8_t next_poll_index_ = 0;
    bool shared_lora_rx_suppressed_ = false;

    bool loraAllowed() const;
    bool wifiAllowed() const;
    bool loraSelectedForRuntime() const;
    bool wifiSelectedForRuntime() const;
    bool hasConfiguredIpInterface() const;
    void syncSharedLoRaRxGate();
};

} // namespace chat::reticulum::interfaces
