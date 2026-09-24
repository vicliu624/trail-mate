/**
 * @file reticulum_interfaces.cpp
 * @brief Reticulum carrier interface set for device-side LXMF runtime
 */

#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_interfaces.h"

#include "chat/time_utils.h"
#if defined(ARDUINO)
#include "platform/esp/arduino_common/app_tasks.h"
#endif
#include "platform/esp/common/reticulum_runtime_compat.h"
#include "platform/ui/reticulum_call_runtime.h"
#include "platform/ui/wifi_access_runtime.h"
#include "platform/ui/wifi_runtime.h"

#if TRAIL_MATE_RETICULUM_C6_TCP_AVAILABLE
#include "platform/esp/idf_common/wireless_companion/c6_companion.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <lwip/netif.h>
#include <lwip/sockets.h>

namespace chat::reticulum::interfaces
{

namespace
{

const char* boolLabel(bool value)
{
    return value ? "true" : "false";
}

const char* txBearerName(const TxResult& result)
{
    if (result.lora_ok && result.wifi_ok)
    {
        return "lora+wifi";
    }
    if (result.lora_ok)
    {
        return "lora";
    }
    if (result.wifi_ok)
    {
        return "wifi";
    }
    return "none";
}

bool isPriorityRxFrame(const uint8_t* data, size_t len)
{
    if (!data || len == 0)
    {
        return false;
    }

    ::chat::reticulum::ParsedPacket parsed{};
    if (!::chat::reticulum::parsePacket(data, len, &parsed))
    {
        return false;
    }

    if (parsed.packet_type == ::chat::reticulum::PacketType::LinkRequest ||
        parsed.packet_type == ::chat::reticulum::PacketType::Proof ||
        parsed.destination_type == ::chat::reticulum::DestinationType::Link ||
        (parsed.packet_type == ::chat::reticulum::PacketType::Announce &&
         parsed.context == static_cast<uint8_t>(
                               ::chat::reticulum::PacketContext::PathResponse)))
    {
        return true;
    }

    if (parsed.packet_type != ::chat::reticulum::PacketType::Data ||
        parsed.destination_type != ::chat::reticulum::DestinationType::Plain ||
        !parsed.destination_hash)
    {
        return false;
    }

    static const auto path_request_hash = []
    {
        std::array<uint8_t, ::chat::reticulum::kTruncatedHashSize> hash{};
        ::chat::reticulum::computePathRequestDestinationHash(hash.data());
        return hash;
    }();
    return std::memcmp(parsed.destination_hash,
                       path_request_hash.data(),
                       path_request_hash.size()) == 0;
}

bool copyHost(char* out, size_t out_len, const char* value)
{
    if (!out || out_len == 0)
    {
        return false;
    }
    out[0] = '\0';
    if (!value)
    {
        return true;
    }
    std::strncpy(out, value, out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

void fillTimestamp(RxMeta* out)
{
    if (!out)
    {
        return;
    }
    out->rx_timestamp_ms = millis();
    const uint32_t epoch_s = now_epoch_seconds();
    if (is_valid_epoch(epoch_s))
    {
        out->rx_timestamp_s = epoch_s;
        out->time_source = RxTimeSource::DeviceUtc;
    }
    else
    {
        out->rx_timestamp_s = out->rx_timestamp_ms / 1000U;
        out->time_source = RxTimeSource::Uptime;
    }
}

} // namespace

LoRaReticulumInterface::LoRaReticulumInterface(LoraBoard& board)
    : raw_(board)
{
}

void LoRaReticulumInterface::applyConfig(const MeshConfig& config, bool enabled)
{
    enabled_ = enabled;
    if (enabled_)
    {
        raw_.applyConfig(config);
    }
    Serial.printf("[Reticulum][IF][LoRa] enabled=%s ready=%s\n",
                  boolLabel(enabled_),
                  boolLabel(isReady()));
}

bool LoRaReticulumInterface::isReady() const
{
    return enabled_ && raw_.isReady();
}

bool LoRaReticulumInterface::sendPacket(const uint8_t* data, size_t len)
{
    if (!isReady())
    {
        return false;
    }
    return raw_.sendAppData(ChannelId::PRIMARY, 0, data, len);
}

bool LoRaReticulumInterface::pollPacket(RxPacket* out)
{
    if (!out || !enabled_)
    {
        return false;
    }

    size_t len = 0;
    if (!raw_.pollIncomingRawPacket(out->data, len, sizeof(out->data)))
    {
        return false;
    }

    out->len = len;
    out->interface_kind = InterfaceKind::LoRa;
    out->interface_id = kLoRaInterfaceId;
    fillTimestamp(&out->rx_meta);
    out->rx_meta.origin = RxOrigin::LoRa;
    out->rx_meta.direct = true;
    out->rx_meta.from_is = false;
    out->rx_meta.rssi_dbm_x10 = static_cast<int16_t>(std::lround(raw_.lastRxRssi() * 10.0f));
    out->rx_meta.snr_db_x10 = static_cast<int16_t>(std::lround(raw_.lastRxSnr() * 10.0f));
    return true;
}

bool LoRaReticulumInterface::pollLegacyIncomingData(MeshIncomingData* out)
{
    return enabled_ && raw_.pollIncomingData(out);
}

void LoRaReticulumInterface::handleRawPacket(const uint8_t* data, size_t size)
{
    if (enabled_)
    {
        raw_.handleRawPacket(data, size);
    }
}

void LoRaReticulumInterface::setLastRxStats(float rssi, float snr)
{
    raw_.setLastRxStats(rssi, snr);
}

float LoRaReticulumInterface::lastRxRssi() const
{
    return raw_.lastRxRssi();
}

float LoRaReticulumInterface::lastRxSnr() const
{
    return raw_.lastRxSnr();
}

WifiGatewayReticulumInterface::WifiGatewayReticulumInterface() = default;

void WifiGatewayReticulumInterface::applyConfig(
    const reticulum::NetworkInterfaceConfig* config,
    bool auto_connect_wifi,
    InterfaceId interface_id)
{
    const bool next_enabled = config && config->enabled &&
                              config->type ==
                                  reticulum::NetworkInterfaceType::TcpClient;
    char next_host[kReticulumGatewayHostMaxLen + 1] = {};
    copyHost(next_host,
             sizeof(next_host),
             next_enabled ? config->target_host : nullptr);
    const uint16_t next_port =
        next_enabled && config->target_port != 0 ? config->target_port : 4242;

    const bool changed = next_enabled != enabled_ ||
                         interface_id != interface_id_ ||
                         next_port != port_ ||
                         std::strcmp(next_host, host_) != 0;

    enabled_ = next_enabled;
    auto_connect_wifi_ = auto_connect_wifi;
    interface_id_ = next_enabled ? interface_id : kInvalidInterfaceId;
    copyHost(host_, sizeof(host_), next_host);
    port_ = next_port;

    if (changed)
    {
        stop();
        last_reconnect_ms_ = 0;
        hdlc_in_frame_ = false;
        hdlc_escape_ = false;
        hdlc_frame_len_ = 0;
        rx_queue_.clear();
    }

    Serial.printf("[Reticulum][IF][TCP] id=%u enabled=%s host=%s port=%u auto_wifi=%s available=%s\n",
                  static_cast<unsigned>(interface_id_),
                  boolLabel(enabled_),
                  host_[0] != '\0' ? host_ : "<unset>",
                  static_cast<unsigned>(port_),
                  boolLabel(auto_connect_wifi_),
                  boolLabel(TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE != 0));
}

void WifiGatewayReticulumInterface::setTransportEnabled(bool enabled)
{
    transport_enabled_ = enabled;
    if (!transport_enabled_)
    {
        stop();
        last_reconnect_ms_ = 0;
    }
}

void WifiGatewayReticulumInterface::maintain()
{
    if (!transport_enabled_ || !enabled_)
    {
        stop();
        return;
    }

    if (host_[0] == '\0')
    {
        stop();
        return;
    }

    syncSocketState();
    if (connected())
    {
        readAvailable();
        return;
    }

#if !TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE
    if (socket_open_pending_)
    {
        return;
    }
#endif

    (void)ensureSocket();
}

bool WifiGatewayReticulumInterface::isReady() const
{
    return transport_enabled_ && enabled_ && host_[0] != '\0' && socket_online_;
}

bool WifiGatewayReticulumInterface::isConfigured() const
{
    return enabled_ && host_[0] != '\0';
}

bool WifiGatewayReticulumInterface::sendPacket(const uint8_t* data,
                                               size_t len,
                                               const uint8_t* call_link_id,
                                               bool call_admission_control)
{
    if (!data || len == 0 || len > reticulum::kReticulumMtu)
    {
        return false;
    }
    if (!isReady())
    {
        maintain();
    }
    if (!isReady())
    {
        return false;
    }
    const auto budget = platform::ui::wifi_access::traffic_budget(
        platform::ui::wifi_access::Client::ReticulumGateway,
        platform::ui::wifi_access::Priority::Messaging,
        call_link_id,
        call_admission_control
            ? platform::ui::wifi_access::AccessKind::ReticulumGatewayCallControl
            : (call_link_id
                   ? platform::ui::wifi_access::AccessKind::ReticulumGatewayCallAudio
                   : platform::ui::wifi_access::AccessKind::LongLivedSocket));
    if (!budget.allow_write || budget.tx_byte_budget == 0)
    {
        return false;
    }

    size_t tx_len = 0;
    tx_frame_[tx_len++] = kHdlcFlag;
    for (size_t i = 0; i < len && tx_len + 2U < sizeof(tx_frame_); ++i)
    {
        const uint8_t byte = data[i];
        if (byte == kHdlcFlag || byte == kHdlcEscape)
        {
            tx_frame_[tx_len++] = kHdlcEscape;
            tx_frame_[tx_len++] = byte ^ kHdlcEscapeMask;
        }
        else
        {
            tx_frame_[tx_len++] = byte;
        }
    }
    if (tx_len + 1U > sizeof(tx_frame_))
    {
        return false;
    }
    tx_frame_[tx_len++] = kHdlcFlag;

#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE
    const size_t written = client_.write(tx_frame_, tx_len);
    if (written != tx_len)
    {
        Serial.printf("[Reticulum][IF][WiFi][TX] failed raw_len=%u frame_len=%u written=%u\n",
                      static_cast<unsigned>(len),
                      static_cast<unsigned>(tx_len),
                      static_cast<unsigned>(written));
        stop();
        return false;
    }
    Serial.printf("[Reticulum][IF][WiFi][TX] ok raw_len=%u frame_len=%u host=%s:%u\n",
                  static_cast<unsigned>(len),
                  static_cast<unsigned>(tx_len),
                  host_,
                  static_cast<unsigned>(port_));
    return true;
#elif TRAIL_MATE_RETICULUM_C6_TCP_AVAILABLE
    auto& transport =
        ::platform::esp::idf_common::wireless_companion::c6_wifi_tcp_transport();
    if (!transport.writeTcp(tx_frame_, tx_len))
    {
        const auto status = transport.tcpStatus();
        Serial.printf("[Reticulum][IF][WiFi][TX] failed raw_len=%u frame_len=%u state=%u error=%u detail=%s\n",
                      static_cast<unsigned>(len),
                      static_cast<unsigned>(tx_len),
                      static_cast<unsigned>(status.state),
                      static_cast<unsigned>(status.error_code),
                      status.detail ? status.detail : "-");
        stop();
        return false;
    }
    Serial.printf("[Reticulum][IF][WiFi][TX] ok raw_len=%u frame_len=%u host=%s:%u transport=c6\n",
                  static_cast<unsigned>(len),
                  static_cast<unsigned>(tx_len),
                  host_,
                  static_cast<unsigned>(port_));
    return true;
#else
    (void)tx_len;
    return false;
#endif
}

bool WifiGatewayReticulumInterface::pollPacket(RxPacket* out)
{
    if (!out)
    {
        return false;
    }
    maintain();

    poll_scratch_ = QueuedPacket{};
    if (!rx_priority_queue_.popOldest(&poll_scratch_) &&
        !rx_queue_.popOldest(&poll_scratch_))
    {
        return false;
    }

    std::memcpy(out->data, poll_scratch_.data, poll_scratch_.len);
    out->len = poll_scratch_.len;
    out->rx_meta = poll_scratch_.rx_meta;
    out->interface_kind = InterfaceKind::WifiGateway;
    out->interface_id = interface_id_;
    return true;
}

void WifiGatewayReticulumInterface::stop()
{
#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE
    connector_.cancel();
    client_.stop();
#elif TRAIL_MATE_RETICULUM_C6_TCP_AVAILABLE
    if (socket_online_ || socket_open_pending_)
    {
        ::platform::esp::idf_common::wireless_companion::c6_wifi_tcp_transport().closeTcp();
    }
#endif
    socket_online_ = false;
    socket_open_pending_ = false;
}

bool WifiGatewayReticulumInterface::connected() const
{
    return socket_online_;
}

void WifiGatewayReticulumInterface::syncSocketState()
{
#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE
    if (socket_online_ && !client_.connected())
    {
        Serial.printf("[Reticulum][IF][WiFi] gateway disconnected host=%s port=%u\n",
                      host_,
                      static_cast<unsigned>(port_));
        client_.stop();
        socket_online_ = false;
        socket_open_pending_ = false;
    }
#elif TRAIL_MATE_RETICULUM_C6_TCP_AVAILABLE
    using ::platform::esp::idf_common::wireless_companion::WifiTcpState;
    auto& transport =
        ::platform::esp::idf_common::wireless_companion::c6_wifi_tcp_transport();
    const auto status = transport.tcpStatus();
    if (status.state == WifiTcpState::Connected)
    {
        if (!socket_online_)
        {
            hdlc_in_frame_ = false;
            hdlc_escape_ = false;
            hdlc_frame_len_ = 0;
            Serial.printf("[Reticulum][IF][WiFi] gateway connected host=%s port=%u transport=c6\n",
                          host_,
                          static_cast<unsigned>(port_));
        }
        socket_online_ = true;
        socket_open_pending_ = false;
        return;
    }
    if (status.state == WifiTcpState::Opening)
    {
        socket_online_ = false;
        socket_open_pending_ = true;
        return;
    }

    if (socket_online_ || socket_open_pending_)
    {
        Serial.printf("[Reticulum][IF][WiFi] gateway disconnected host=%s port=%u state=%u error=%u detail=%s\n",
                      host_,
                      static_cast<unsigned>(port_),
                      static_cast<unsigned>(status.state),
                      static_cast<unsigned>(status.error_code),
                      status.detail ? status.detail : "-");
    }
    socket_online_ = false;
    socket_open_pending_ = false;
#endif
}

bool WifiGatewayReticulumInterface::ensureSocket()
{
    if (!transport_enabled_ || !enabled_ || host_[0] == '\0')
    {
        return false;
    }

#if !TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE
    return false;
#else
    const uint32_t now_ms = millis();
    const bool socket_connect_pending =
#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE
        connector_.pending();
#elif TRAIL_MATE_RETICULUM_C6_TCP_AVAILABLE
        socket_open_pending_;
#else
        false;
#endif
    if (!socket_connect_pending &&
        last_reconnect_ms_ != 0 &&
        (now_ms - last_reconnect_ms_) < kReconnectIntervalMs)
    {
        return false;
    }

    platform::ui::wifi::Status wifi_status = platform::ui::wifi::status();
    if (!wifi_status.connected)
    {
        if (auto_connect_wifi_)
        {
            Serial.printf("[Reticulum][IF][WiFi] requesting station before gateway host=%s:%u\n",
                          host_,
                          static_cast<unsigned>(port_));
            platform::ui::wifi_access::Request request{};
            request.client = platform::ui::wifi_access::Client::ReticulumGateway;
            request.kind = platform::ui::wifi_access::AccessKind::WifiConnect;
            request.priority = platform::ui::wifi_access::Priority::Messaging;
            request.allow_connect = true;
            request.reason = "reticulum_gateway";
            platform::ui::wifi_access::ConnectResult connect_result{};
            if (!platform::ui::wifi_access::ensure_connected(request,
                                                             &connect_result))
            {
                Serial.printf("[Reticulum][IF][WiFi] station denied decision=%s host=%s:%u\n",
                              platform::ui::wifi_access::decision_name(
                                  connect_result.decision),
                              host_,
                              static_cast<unsigned>(port_));
            }
            wifi_status = platform::ui::wifi::status();
        }

        if (!wifi_status.connected)
        {
            last_reconnect_ms_ = now_ms;
            return false;
        }
    }

    last_reconnect_ms_ = now_ms;
    platform::ui::wifi_access::Request socket_request{};
    socket_request.client = platform::ui::wifi_access::Client::ReticulumGateway;
    socket_request.kind = platform::ui::wifi_access::AccessKind::LongLivedSocket;
    socket_request.priority = platform::ui::wifi_access::Priority::Messaging;
    socket_request.reason = "reticulum_gateway_socket";
    const auto lease = platform::ui::wifi_access::acquire(socket_request);
    if (!lease.granted)
    {
        socket_online_ = false;
        socket_open_pending_ = false;
        Serial.printf("[Reticulum][IF][WiFi] gateway connect deferred decision=%s host=%s port=%u\n",
                      platform::ui::wifi_access::decision_name(lease.decision),
                      host_,
                      static_cast<unsigned>(port_));
        return false;
    }

#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE
    using platform::esp::arduino_common::net::TcpConnectPhase;
    if (!connector_.pending() &&
        connector_.status().phase != TcpConnectPhase::Connected)
    {
        client_.stop();
        Serial.printf("[Reticulum][IF][WiFi] gateway connect start host=%s port=%u\n",
                      host_,
                      static_cast<unsigned>(port_));
        if (!connector_.start(host_,
                              port_,
                              now_ms,
                              static_cast<uint32_t>(kSocketConnectTimeoutMs)))
        {
            const auto status = connector_.status();
            Serial.printf("[Reticulum][IF][WiFi] gateway connect failed host=%s port=%u stage=%s err=%d\n",
                          host_,
                          static_cast<unsigned>(port_),
                          platform::esp::arduino_common::net::
                              tcpConnectFailureName(status.failure),
                          status.detail);
            return false;
        }
    }

    const auto connect_status = connector_.poll(now_ms);
    if (connect_status.phase == TcpConnectPhase::Resolving ||
        connect_status.phase == TcpConnectPhase::Connecting)
    {
        socket_open_pending_ = true;
        return true;
    }
    if (connect_status.phase != TcpConnectPhase::Connected)
    {
        connector_.cancel();
        socket_open_pending_ = false;
        Serial.printf("[Reticulum][IF][WiFi] gateway connect failed host=%s port=%u stage=%s err=%d\n",
                      host_,
                      static_cast<unsigned>(port_),
                      platform::esp::arduino_common::net::
                          tcpConnectFailureName(connect_status.failure),
                      connect_status.detail);
        return false;
    }

    const int socket = connector_.takeSocket();
    if (socket < 0)
    {
        socket_open_pending_ = false;
        return false;
    }
    client_ = WiFiClient(socket);
    client_.setNoDelay(true);
    socket_online_ = true;
    hdlc_in_frame_ = false;
    hdlc_escape_ = false;
    hdlc_frame_len_ = 0;
    Serial.printf("[Reticulum][IF][WiFi] gateway connected host=%s port=%u\n",
                  host_,
                  static_cast<unsigned>(port_));
    return true;
#elif TRAIL_MATE_RETICULUM_C6_TCP_AVAILABLE
    auto& transport =
        ::platform::esp::idf_common::wireless_companion::c6_wifi_tcp_transport();
    const auto tcp_status = transport.tcpStatus();
    using ::platform::esp::idf_common::wireless_companion::WifiTcpState;
    if (tcp_status.state == WifiTcpState::Connected)
    {
        socket_online_ = true;
        socket_open_pending_ = false;
        return true;
    }
    if (tcp_status.state == WifiTcpState::Opening)
    {
        socket_open_pending_ = true;
        return true;
    }

    Serial.printf("[Reticulum][IF][WiFi] gateway connect host=%s port=%u transport=c6\n",
                  host_,
                  static_cast<unsigned>(port_));
    if (!transport.openTcp(host_, port_))
    {
        const auto failed = transport.tcpStatus();
        socket_online_ = false;
        socket_open_pending_ = false;
        Serial.printf("[Reticulum][IF][WiFi] gateway connect failed host=%s port=%u state=%u error=%u detail=%s\n",
                      host_,
                      static_cast<unsigned>(port_),
                      static_cast<unsigned>(failed.state),
                      static_cast<unsigned>(failed.error_code),
                      failed.detail ? failed.detail : "-");
        return false;
    }
    socket_open_pending_ = true;
    return true;
#else
    return false;
#endif
#endif
}

void WifiGatewayReticulumInterface::readAvailable()
{
#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE
    if (!socket_online_)
    {
        return;
    }

    if (!client_.connected())
    {
        Serial.printf("[Reticulum][IF][WiFi] gateway disconnected host=%s port=%u\n",
                      host_,
                      static_cast<unsigned>(port_));
        stop();
        return;
    }

    int remaining = client_.available();
    const uint32_t now_ms = millis();
    const auto budget = platform::ui::wifi_access::traffic_budget(
        platform::ui::wifi_access::Client::ReticulumGateway,
        platform::ui::wifi_access::Priority::Messaging);
    if (!budget.allow_read || budget.rx_byte_budget == 0)
    {
        if (remaining > 0)
        {
            ++rx_stats_read_skips_;
        }
        if (!budget.allow_connect && !budget.allow_write)
        {
            stop();
            last_reconnect_ms_ = now_ms;
        }
        return;
    }
    if (remaining > 0 &&
        last_socket_read_ms_ != 0 &&
        (now_ms - last_socket_read_ms_) < budget.min_read_interval_ms)
    {
        ++rx_stats_read_skips_;
        return;
    }
    if (remaining > 0)
    {
        last_socket_read_ms_ = now_ms;
    }

    int processed = 0;
    while (remaining > 0 && processed < static_cast<int>(budget.rx_byte_budget))
    {
        const int value = client_.read();
        if (value < 0)
        {
            break;
        }
        feedHdlcByte(static_cast<uint8_t>(value));
        ++processed;
        --remaining;
    }
    rx_stats_bytes_ += static_cast<uint32_t>(processed);
#elif TRAIL_MATE_RETICULUM_C6_TCP_AVAILABLE
    if (!socket_online_)
    {
        return;
    }

    auto& transport =
        ::platform::esp::idf_common::wireless_companion::c6_wifi_tcp_transport();
    const auto tcp_status = transport.tcpStatus();
    const int remaining = static_cast<int>(tcp_status.buffered_bytes);
    const uint32_t now_ms = millis();
    const auto budget = platform::ui::wifi_access::traffic_budget(
        platform::ui::wifi_access::Client::ReticulumGateway,
        platform::ui::wifi_access::Priority::Messaging);
    if (!budget.allow_read || budget.rx_byte_budget == 0)
    {
        if (remaining > 0)
        {
            ++rx_stats_read_skips_;
        }
        if (!budget.allow_connect && !budget.allow_write)
        {
            stop();
            last_reconnect_ms_ = now_ms;
        }
        return;
    }
    if (remaining > 0 && last_socket_read_ms_ != 0 &&
        (now_ms - last_socket_read_ms_) < budget.min_read_interval_ms)
    {
        ++rx_stats_read_skips_;
        return;
    }
    if (remaining > 0)
    {
        last_socket_read_ms_ = now_ms;
    }

    const size_t read_budget = std::min<size_t>(budget.rx_byte_budget,
                                                sizeof(socket_rx_scratch_));
    const size_t read = transport.readTcp(socket_rx_scratch_, read_budget);
    for (size_t index = 0; index < read; ++index)
    {
        feedHdlcByte(socket_rx_scratch_[index]);
    }
    rx_stats_bytes_ += static_cast<uint32_t>(read);
#endif
}

void WifiGatewayReticulumInterface::feedHdlcByte(uint8_t byte)
{
    if (byte == kHdlcFlag)
    {
        if (hdlc_in_frame_ && hdlc_frame_len_ > 0)
        {
            enqueueFrame(hdlc_frame_, hdlc_frame_len_);
        }
        hdlc_in_frame_ = true;
        hdlc_escape_ = false;
        hdlc_frame_len_ = 0;
        return;
    }

    if (!hdlc_in_frame_)
    {
        return;
    }

    if (byte == kHdlcEscape)
    {
        hdlc_escape_ = true;
        return;
    }

    if (hdlc_escape_)
    {
        byte ^= kHdlcEscapeMask;
        hdlc_escape_ = false;
    }

    if (hdlc_frame_len_ >= sizeof(hdlc_frame_))
    {
        Serial.printf("[Reticulum][IF][WiFi][RX] drop reason=frame_too_large len=%u\n",
                      static_cast<unsigned>(hdlc_frame_len_));
        hdlc_in_frame_ = false;
        hdlc_escape_ = false;
        hdlc_frame_len_ = 0;
        return;
    }

    hdlc_frame_[hdlc_frame_len_++] = byte;
}

void WifiGatewayReticulumInterface::enqueueFrame(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > reticulum::kReticulumMtu)
    {
        return;
    }

    enqueue_scratch_ = QueuedPacket{};
    std::memcpy(enqueue_scratch_.data, data, len);
    enqueue_scratch_.len = len;
    fillRxMeta(&enqueue_scratch_.rx_meta);
    bool dropped = false;
    const bool priority = isPriorityRxFrame(data, len);
    if (priority)
    {
        rx_priority_queue_.append(enqueue_scratch_, &dropped);
        ++rx_stats_priority_frames_;
    }
    else
    {
        rx_queue_.append(enqueue_scratch_, &dropped);
    }

    ++rx_stats_frames_;
    if (dropped)
    {
        ++rx_stats_drops_;
    }
    const uint32_t now_ms = millis();
    if (rx_stats_last_log_ms_ == 0 ||
        (now_ms - rx_stats_last_log_ms_) >= kRxStatsLogIntervalMs)
    {
        Serial.printf("[Reticulum][IF][WiFi][RX] stats frames=%u priority=%u drops=%u bytes=%u read_skips=%u depth=%u prio_depth=%u last_len=%u\n",
                      static_cast<unsigned>(rx_stats_frames_),
                      static_cast<unsigned>(rx_stats_priority_frames_),
                      static_cast<unsigned>(rx_stats_drops_),
                      static_cast<unsigned>(rx_stats_bytes_),
                      static_cast<unsigned>(rx_stats_read_skips_),
                      static_cast<unsigned>(rx_queue_.size()),
                      static_cast<unsigned>(rx_priority_queue_.size()),
                      static_cast<unsigned>(len));
        rx_stats_last_log_ms_ = now_ms;
        rx_stats_frames_ = 0;
        rx_stats_priority_frames_ = 0;
        rx_stats_drops_ = 0;
        rx_stats_bytes_ = 0;
        rx_stats_read_skips_ = 0;
    }
}

void WifiGatewayReticulumInterface::fillRxMeta(RxMeta* out) const
{
    if (!out)
    {
        return;
    }
    fillTimestamp(out);
    out->origin = RxOrigin::WiFi;
    out->direct = true;
    out->from_is = false;
    out->rssi_dbm_x10 = 0;
    out->snr_db_x10 = 0;
    out->freq_hz = 0;
    out->bw_hz = 0;
    out->sf = 0;
    out->cr = 0;
}

AutoReticulumInterface::AutoReticulumInterface() = default;

void AutoReticulumInterface::applyConfig(
    const reticulum::NetworkInterfaceConfig* config,
    bool auto_connect_wifi)
{
    const bool next_enabled = config && config->enabled &&
                              config->type ==
                                  reticulum::NetworkInterfaceType::Auto;
    const char* next_group = next_enabled ? config->group_id : "reticulum";
    const uint16_t next_discovery_port =
        next_enabled && config->discovery_port != 0 ? config->discovery_port : 29716;
    const uint16_t next_data_port =
        next_enabled && config->data_port != 0 ? config->data_port : 42671;
    const bool changed = next_enabled != enabled_ ||
                         std::strcmp(next_group, group_id_) != 0 ||
                         next_discovery_port != discovery_port_ ||
                         next_data_port != data_port_;

    enabled_ = next_enabled;
    auto_connect_wifi_ = auto_connect_wifi;
    copyHost(group_id_, sizeof(group_id_), next_group);
    discovery_port_ = next_discovery_port;
    data_port_ = next_data_port;
    if (changed)
    {
        stop();
        last_socket_attempt_ms_ = 0;
        wifi_connect_retry_not_before_ms_ = 0;
        wifi_connect_suspended_ = false;
        rx_queue_.clear();
    }
    Serial.printf("[Reticulum][IF][Auto] enabled=%s group=%s discovery=%u data=%u available=%s\n",
                  boolLabel(enabled_),
                  group_id_,
                  static_cast<unsigned>(discovery_port_),
                  static_cast<unsigned>(data_port_),
                  boolLabel(TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE != 0));
}

void AutoReticulumInterface::setTransportEnabled(bool enabled)
{
    transport_enabled_ = enabled;
    if (transport_enabled_)
    {
        wifi_connect_retry_not_before_ms_ = 0;
        wifi_connect_suspended_ = false;
    }
    if (!transport_enabled_)
    {
        stop();
    }
}

void AutoReticulumInterface::maintain()
{
    if (!enabled_ || !transport_enabled_)
    {
        stop();
        return;
    }

    platform::ui::wifi::Status wifi_status = platform::ui::wifi::status();
    const uint32_t now_ms = millis();
    if (wifi_status.connected)
    {
        wifi_connect_retry_not_before_ms_ = 0;
        wifi_connect_suspended_ = false;
    }
    if (!wifi_status.connected && auto_connect_wifi_)
    {
        if (wifi_connect_suspended_)
        {
            stop();
            return;
        }
        if (wifi_connect_retry_not_before_ms_ != 0 &&
            static_cast<int32_t>(wifi_connect_retry_not_before_ms_ - now_ms) > 0)
        {
            stop();
            return;
        }

        platform::ui::wifi_access::Request request{};
        request.client = platform::ui::wifi_access::Client::ReticulumGateway;
        request.kind = platform::ui::wifi_access::AccessKind::WifiConnect;
        request.priority = platform::ui::wifi_access::Priority::Messaging;
        request.allow_connect = true;
        request.reason = "reticulum_auto_interface";
        platform::ui::wifi_access::ConnectResult connect_result{};
        const bool connect_allowed =
            platform::ui::wifi_access::ensure_connected(request, &connect_result);
        wifi_status = platform::ui::wifi::status();
        if (!connect_allowed && !wifi_status.connected)
        {
            if (connect_result.decision ==
                platform::ui::wifi_access::Decision::WifiDisabled)
            {
                // Wi-Fi disabled is a policy state, not a transient connect
                // failure. Wait for the explicit enable transition instead of
                // polling the policy on every maintain() tick.
                wifi_connect_suspended_ = true;
            }
            else
            {
                const uint32_t retry_after_ms =
                    connect_result.retry_after_ms != 0
                        ? connect_result.retry_after_ms
                        : kWifiConnectRetryIntervalMs;
                wifi_connect_retry_not_before_ms_ = now_ms + retry_after_ms;
            }
        }
        else if (wifi_status.connected)
        {
            wifi_connect_retry_not_before_ms_ = 0;
            wifi_connect_suspended_ = false;
        }
    }
    if (!wifi_status.connected)
    {
        stop();
        return;
    }

    if (!sockets_ready_ && !ensureSockets())
    {
        return;
    }

    receiveDiscovery(discovery_socket_);
    receiveDiscovery(unicast_discovery_socket_);
    receiveData();
    cullPeers(now_ms);
    if (last_announce_ms_ == 0 || now_ms - last_announce_ms_ >= kAnnounceIntervalMs)
    {
        last_announce_ms_ = now_ms;
        sendPeerAnnounce();
    }
    for (auto& peer : peers_)
    {
        if (peer.active &&
            (peer.last_reverse_announce_ms == 0 ||
             now_ms - peer.last_reverse_announce_ms >=
                 kReverseAnnounceIntervalMs))
        {
            peer.last_reverse_announce_ms = now_ms;
            sendReverseAnnounce(peer);
        }
    }
}

bool AutoReticulumInterface::isReady() const
{
    return enabled_ && transport_enabled_ && sockets_ready_ && peerCount() != 0;
}

bool AutoReticulumInterface::isConfigured() const
{
    return enabled_;
}

bool AutoReticulumInterface::sendPacket(const uint8_t* data,
                                        size_t len,
                                        const uint8_t* call_link_id,
                                        bool call_admission_control)
{
    if (!data || len == 0 || len > reticulum::kReticulumMtu)
    {
        return false;
    }
    maintain();
    bool sent = false;
    for (const auto& peer : peers_)
    {
        if (peer.active)
        {
            sent = sendPacketOn(peer.interface_id,
                                data,
                                len,
                                call_link_id,
                                call_admission_control) ||
                   sent;
        }
    }
    return sent;
}

bool AutoReticulumInterface::sendPacketOn(InterfaceId interface_id,
                                          const uint8_t* data,
                                          size_t len,
                                          const uint8_t* call_link_id,
                                          bool call_admission_control)
{
    if (!data || len == 0 || len > reticulum::kReticulumMtu)
    {
        return false;
    }
    Peer* peer = findPeer(interface_id);
    if (!peer || data_socket_ < 0)
    {
        return false;
    }
    const auto budget = platform::ui::wifi_access::traffic_budget(
        platform::ui::wifi_access::Client::ReticulumGateway,
        platform::ui::wifi_access::Priority::Messaging,
        call_link_id,
        call_admission_control
            ? platform::ui::wifi_access::AccessKind::ReticulumGatewayCallControl
            : (call_link_id
                   ? platform::ui::wifi_access::AccessKind::ReticulumGatewayCallAudio
                   : platform::ui::wifi_access::AccessKind::LongLivedSocket));
    if (!budget.allow_write || budget.tx_byte_budget == 0)
    {
        return false;
    }

#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE && LWIP_IPV6
    sockaddr_in6 destination{};
    destination.sin6_family = AF_INET6;
    destination.sin6_port = htons(data_port_);
    destination.sin6_scope_id = peer->scope_id;
    std::memcpy(&destination.sin6_addr,
                peer->address,
                sizeof(peer->address));
    const int written = lwip_sendto(data_socket_,
                                    data,
                                    len,
                                    0,
                                    reinterpret_cast<sockaddr*>(&destination),
                                    sizeof(destination));
    if (written == static_cast<int>(len))
    {
        return true;
    }
    Serial.printf("[Reticulum][IF][Auto][TX] failed id=%u len=%u errno=%d\n",
                  static_cast<unsigned>(interface_id),
                  static_cast<unsigned>(len),
                  errno);
#else
    (void)interface_id;
#endif
    return false;
}

bool AutoReticulumInterface::pollPacket(RxPacket* out)
{
    if (!out)
    {
        return false;
    }
    maintain();
    return rx_queue_.popOldest(out);
}

bool AutoReticulumInterface::owns(InterfaceId interface_id) const
{
    return interface_id >= kAutoInterfaceIdBase &&
           interface_id < kAutoInterfaceIdBase + kMaxPeers;
}

uint8_t AutoReticulumInterface::peerCount() const
{
    uint8_t count = 0;
    for (const auto& peer : peers_)
    {
        if (peer.active)
        {
            ++count;
        }
    }
    return count;
}

void AutoReticulumInterface::stop()
{
    closeSockets();
    for (auto& peer : peers_)
    {
        peer = Peer{};
    }
}

bool AutoReticulumInterface::ensureSockets()
{
#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE && LWIP_IPV6
    const uint32_t now_ms = millis();
    if (last_socket_attempt_ms_ != 0 &&
        now_ms - last_socket_attempt_ms_ < 5000U)
    {
        return false;
    }
    last_socket_attempt_ms_ = now_ms;
    closeSockets();

    netif* selected = nullptr;
    for (netif* item = netif_list; item != nullptr; item = item->next)
    {
        if (!netif_is_up(item) || !netif_is_link_up(item) ||
            !ip6_addr_isvalid(netif_ip6_addr_state(item, 0)))
        {
            continue;
        }
        char address[INET6_ADDRSTRLEN] = {};
        if (!ip6addr_ntoa_r(netif_ip6_addr(item, 0), address, sizeof(address)) ||
            std::strncmp(address, "fe80:", 5) != 0)
        {
            continue;
        }
        selected = item;
        break;
    }
    if (!selected)
    {
        return false;
    }

    interface_index_ = netif_get_index(selected);
    char local_text[INET6_ADDRSTRLEN] = {};
    if (!ip6addr_ntoa_r(netif_ip6_addr(selected, 0),
                        local_text,
                        sizeof(local_text)) ||
        lwip_inet_pton(AF_INET6, local_text, local_address_) != 1)
    {
        return false;
    }
    calculateDiscoveryIdentity();

    auto open_udp_socket = []() -> int
    {
        const int socket_fd = lwip_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_fd >= 0)
        {
            const int reuse = 1;
            lwip_setsockopt(socket_fd,
                            SOL_SOCKET,
                            SO_REUSEADDR,
                            &reuse,
                            sizeof(reuse));
            const int flags = lwip_fcntl(socket_fd, F_GETFL, 0);
            lwip_fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
        }
        return socket_fd;
    };

    discovery_socket_ = open_udp_socket();
    unicast_discovery_socket_ = open_udp_socket();
    data_socket_ = open_udp_socket();
    if (discovery_socket_ < 0 || unicast_discovery_socket_ < 0 ||
        data_socket_ < 0)
    {
        closeSockets();
        return false;
    }

    sockaddr_in6 discovery_bind{};
    discovery_bind.sin6_family = AF_INET6;
    discovery_bind.sin6_port = htons(discovery_port_);
    discovery_bind.sin6_addr = in6addr_any;
    ipv6_mreq membership{};
    std::memcpy(&membership.ipv6mr_multiaddr,
                multicast_address_,
                sizeof(multicast_address_));
    membership.ipv6mr_interface = interface_index_;
    if (lwip_bind(discovery_socket_,
                  reinterpret_cast<sockaddr*>(&discovery_bind),
                  sizeof(discovery_bind)) != 0 ||
        lwip_setsockopt(discovery_socket_,
                        IPPROTO_IPV6,
                        IPV6_JOIN_GROUP,
                        &membership,
                        sizeof(membership)) != 0)
    {
        closeSockets();
        return false;
    }

    sockaddr_in6 unicast_bind{};
    unicast_bind.sin6_family = AF_INET6;
    unicast_bind.sin6_port = htons(static_cast<uint16_t>(discovery_port_ + 1U));
    unicast_bind.sin6_scope_id = interface_index_;
    std::memcpy(&unicast_bind.sin6_addr,
                local_address_,
                sizeof(local_address_));
    sockaddr_in6 data_bind = unicast_bind;
    data_bind.sin6_port = htons(data_port_);
    if (lwip_bind(unicast_discovery_socket_,
                  reinterpret_cast<sockaddr*>(&unicast_bind),
                  sizeof(unicast_bind)) != 0 ||
        lwip_bind(data_socket_,
                  reinterpret_cast<sockaddr*>(&data_bind),
                  sizeof(data_bind)) != 0)
    {
        closeSockets();
        return false;
    }

    sockets_ready_ = true;
    last_announce_ms_ = 0;
    Serial.printf("[Reticulum][IF][Auto] online ifindex=%u peers=0\n",
                  static_cast<unsigned>(interface_index_));
    return true;
#else
    return false;
#endif
}

void AutoReticulumInterface::closeSockets()
{
#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE && LWIP_IPV6
    if (discovery_socket_ >= 0)
    {
        lwip_close(discovery_socket_);
    }
    if (unicast_discovery_socket_ >= 0)
    {
        lwip_close(unicast_discovery_socket_);
    }
    if (data_socket_ >= 0)
    {
        lwip_close(data_socket_);
    }
#endif
    discovery_socket_ = -1;
    unicast_discovery_socket_ = -1;
    data_socket_ = -1;
    sockets_ready_ = false;
}

void AutoReticulumInterface::calculateDiscoveryIdentity()
{
    uint8_t group_hash[reticulum::kFullHashSize] = {};
    reticulum::fullHash(reinterpret_cast<const uint8_t*>(group_id_),
                        std::strlen(group_id_),
                        group_hash);
    std::memset(multicast_address_, 0, sizeof(multicast_address_));
    multicast_address_[0] = 0xFF;
    multicast_address_[1] = 0x12;
    for (std::size_t pair = 0; pair < 6; ++pair)
    {
        multicast_address_[4U + pair * 2U] = group_hash[2U + pair * 2U];
        multicast_address_[5U + pair * 2U] = group_hash[3U + pair * 2U];
    }

#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE && LWIP_IPV6
    char local_text[INET6_ADDRSTRLEN] = {};
    lwip_inet_ntop(AF_INET6,
                   local_address_,
                   local_text,
                   sizeof(local_text));
    std::array<uint8_t,
               reticulum::kAutoInterfaceGroupMaxLen + INET6_ADDRSTRLEN + 1U>
        material{};
    const std::size_t group_len = std::strlen(group_id_);
    const std::size_t address_len = std::strlen(local_text);
    std::memcpy(material.data(), group_id_, group_len);
    std::memcpy(material.data() + group_len, local_text, address_len);
    reticulum::fullHash(material.data(),
                        group_len + address_len,
                        discovery_token_);
#endif
}

void AutoReticulumInterface::sendPeerAnnounce()
{
#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE && LWIP_IPV6
    if (!sockets_ready_ || data_socket_ < 0)
    {
        return;
    }
    sockaddr_in6 destination{};
    destination.sin6_family = AF_INET6;
    destination.sin6_port = htons(discovery_port_);
    destination.sin6_scope_id = interface_index_;
    std::memcpy(&destination.sin6_addr,
                multicast_address_,
                sizeof(multicast_address_));
    lwip_sendto(data_socket_,
                discovery_token_,
                sizeof(discovery_token_),
                0,
                reinterpret_cast<sockaddr*>(&destination),
                sizeof(destination));
#endif
}

void AutoReticulumInterface::sendReverseAnnounce(Peer& peer)
{
#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE && LWIP_IPV6
    if (!peer.active || data_socket_ < 0)
    {
        return;
    }
    sockaddr_in6 destination{};
    destination.sin6_family = AF_INET6;
    destination.sin6_port = htons(static_cast<uint16_t>(discovery_port_ + 1U));
    destination.sin6_scope_id = peer.scope_id;
    std::memcpy(&destination.sin6_addr,
                peer.address,
                sizeof(peer.address));
    lwip_sendto(data_socket_,
                discovery_token_,
                sizeof(discovery_token_),
                0,
                reinterpret_cast<sockaddr*>(&destination),
                sizeof(destination));
#else
    (void)peer;
#endif
}

void AutoReticulumInterface::receiveDiscovery(int socket_fd)
{
#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE && LWIP_IPV6
    if (socket_fd < 0)
    {
        return;
    }
    uint8_t token[reticulum::kFullHashSize] = {};
    for (uint8_t packet = 0; packet < 6; ++packet)
    {
        sockaddr_in6 source{};
        socklen_t source_len = sizeof(source);
        const int read = lwip_recvfrom(socket_fd,
                                       token,
                                       sizeof(token),
                                       0,
                                       reinterpret_cast<sockaddr*>(&source),
                                       &source_len);
        if (read < 0)
        {
            break;
        }
        if (read != static_cast<int>(sizeof(token)) ||
            std::memcmp(&source.sin6_addr,
                        local_address_,
                        sizeof(local_address_)) == 0)
        {
            continue;
        }

        char source_text[INET6_ADDRSTRLEN] = {};
        if (!lwip_inet_ntop(AF_INET6,
                            &source.sin6_addr,
                            source_text,
                            sizeof(source_text)))
        {
            continue;
        }
        std::array<uint8_t,
                   reticulum::kAutoInterfaceGroupMaxLen + INET6_ADDRSTRLEN + 1U>
            material{};
        const std::size_t group_len = std::strlen(group_id_);
        const std::size_t address_len = std::strlen(source_text);
        std::memcpy(material.data(), group_id_, group_len);
        std::memcpy(material.data() + group_len, source_text, address_len);
        uint8_t expected[reticulum::kFullHashSize] = {};
        reticulum::fullHash(material.data(),
                            group_len + address_len,
                            expected);
        if (std::memcmp(expected, token, sizeof(expected)) != 0)
        {
            continue;
        }
        (void)upsertPeer(reinterpret_cast<const uint8_t*>(&source.sin6_addr),
                         source.sin6_scope_id != 0 ? source.sin6_scope_id
                                                   : interface_index_);
    }
#else
    (void)socket_fd;
#endif
}

void AutoReticulumInterface::receiveData()
{
#if TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE && LWIP_IPV6
    if (data_socket_ < 0)
    {
        return;
    }
    for (uint8_t packet = 0; packet < 6; ++packet)
    {
        sockaddr_in6 source{};
        socklen_t source_len = sizeof(source);
        const int read = lwip_recvfrom(data_socket_,
                                       rx_scratch_.data,
                                       sizeof(rx_scratch_.data),
                                       0,
                                       reinterpret_cast<sockaddr*>(&source),
                                       &source_len);
        if (read < 0)
        {
            break;
        }
        Peer* peer = nullptr;
        for (auto& candidate : peers_)
        {
            if (candidate.active &&
                std::memcmp(candidate.address,
                            &source.sin6_addr,
                            sizeof(candidate.address)) == 0)
            {
                peer = &candidate;
                break;
            }
        }
        if (!peer || read <= 0 ||
            read > static_cast<int>(reticulum::kReticulumMtu))
        {
            continue;
        }
        peer->last_seen_ms = millis();
        rx_scratch_.len = static_cast<std::size_t>(read);
        rx_scratch_.interface_kind = InterfaceKind::Auto;
        rx_scratch_.interface_id = peer->interface_id;
        fillTimestamp(&rx_scratch_.rx_meta);
        rx_scratch_.rx_meta.origin = RxOrigin::WiFi;
        rx_scratch_.rx_meta.direct = true;
        bool dropped = false;
        rx_queue_.append(rx_scratch_, &dropped);
        if (dropped)
        {
            Serial.printf("[Reticulum][IF][Auto][RX] queue_drop id=%u\n",
                          static_cast<unsigned>(peer->interface_id));
        }
    }
#endif
}

void AutoReticulumInterface::cullPeers(uint32_t now_ms)
{
    for (auto& peer : peers_)
    {
        if (peer.active && now_ms - peer.last_seen_ms > kPeerTimeoutMs)
        {
            Serial.printf("[Reticulum][IF][Auto] peer_timeout id=%u\n",
                          static_cast<unsigned>(peer.interface_id));
            peer = Peer{};
        }
    }
}

AutoReticulumInterface::Peer* AutoReticulumInterface::upsertPeer(
    const uint8_t address[16],
    uint32_t scope_id)
{
    if (!address)
    {
        return nullptr;
    }
    for (auto& peer : peers_)
    {
        if (peer.active &&
            std::memcmp(peer.address, address, sizeof(peer.address)) == 0)
        {
            peer.last_seen_ms = millis();
            peer.scope_id = scope_id;
            return &peer;
        }
    }
    for (std::size_t index = 0; index < peers_.size(); ++index)
    {
        auto& peer = peers_[index];
        if (!peer.active)
        {
            std::memcpy(peer.address, address, sizeof(peer.address));
            peer.scope_id = scope_id;
            peer.last_seen_ms = millis();
            peer.last_reverse_announce_ms = 0;
            peer.interface_id = static_cast<InterfaceId>(kAutoInterfaceIdBase + index);
            peer.active = true;
            Serial.printf("[Reticulum][IF][Auto] peer_added id=%u peers=%u\n",
                          static_cast<unsigned>(peer.interface_id),
                          static_cast<unsigned>(peerCount()));
            return &peer;
        }
    }
    return nullptr;
}

AutoReticulumInterface::Peer* AutoReticulumInterface::findPeer(
    InterfaceId interface_id)
{
    for (auto& peer : peers_)
    {
        if (peer.active && peer.interface_id == interface_id)
        {
            return &peer;
        }
    }
    return nullptr;
}

ReticulumInterfaceSet::ReticulumInterfaceSet(LoraBoard& board, bool owns_integrated_radio)
    : lora_(board), owns_integrated_radio_(owns_integrated_radio)
{
}

void ReticulumInterfaceSet::applyConfig(
    const MeshConfig& config,
    const reticulum::ReticulumNetworkConfig& network_config)
{
    config_ = config;
    network_config_ = network_config;

    // Service-local effective policy only: never change persisted user settings.
    // Reapply on every config refresh so a radio interface in the shared network
    // configuration cannot seize the active chat protocol's hardware.
    if (!owns_integrated_radio_)
    {
        config_.reticulum_lora_enabled = false;
        config_.reticulum_wifi_gateway_enabled = true;
        config_.reticulum_interface_policy = ReticulumInterfacePolicy::WifiGatewayOnly;
    }

    bool lora_configured = false;
    const reticulum::NetworkInterfaceConfig* auto_config = nullptr;
    tcp_count_ = 0;
#if TRAIL_MATE_RETICULUM_C6_TCP_AVAILABLE
    constexpr size_t tcp_capacity = 1;
#else
    constexpr size_t tcp_capacity = reticulum::kMaxTcpClientInterfaces;
#endif
    const size_t interface_count = std::min<size_t>(
        network_config_.interface_count,
        reticulum::kMaxNetworkInterfaces);
    for (size_t index = 0; index < interface_count; ++index)
    {
        const auto& interface_config = network_config_.interfaces[index];
        if (!interface_config.enabled)
        {
            continue;
        }
        switch (interface_config.type)
        {
        case reticulum::NetworkInterfaceType::IntegratedLoRa:
            lora_configured = owns_integrated_radio_;
            break;
        case reticulum::NetworkInterfaceType::Auto:
            if (!auto_config)
            {
                auto_config = &interface_config;
            }
            break;
        case reticulum::NetworkInterfaceType::TcpClient:
            if (tcp_count_ < tcp_capacity)
            {
                tcp_[tcp_count_].applyConfig(
                    &interface_config,
                    config_.reticulum_wifi_auto_connect,
                    static_cast<InterfaceId>(kTcpClientInterfaceIdBase +
                                             tcp_count_));
                ++tcp_count_;
            }
            break;
        }
    }
    for (size_t index = tcp_count_; index < tcp_.size(); ++index)
    {
        tcp_[index].applyConfig(nullptr,
                                config_.reticulum_wifi_auto_connect,
                                kInvalidInterfaceId);
    }

    lora_.applyConfig(config_, lora_configured);
    auto_.applyConfig(auto_config, config_.reticulum_wifi_auto_connect);
    maintain();
    syncSharedLoRaRxGate();
    Serial.printf("[Reticulum][IF] configured total=%u lora=%u auto=%u tcp=%u\n",
                  static_cast<unsigned>(interface_count),
                  lora_configured ? 1U : 0U,
                  auto_config ? 1U : 0U,
                  static_cast<unsigned>(tcp_count_));
}

void ReticulumInterfaceSet::setWifiTransportEnabled(bool enabled)
{
    auto_.setTransportEnabled(enabled);
    for (auto& interface : tcp_)
    {
        interface.setTransportEnabled(enabled);
    }
    syncSharedLoRaRxGate();
}

void ReticulumInterfaceSet::maintain()
{
    auto_.maintain();
    for (uint8_t index = 0; index < tcp_count_; ++index)
    {
        tcp_[index].maintain();
    }
    syncSharedLoRaRxGate();
}

bool ReticulumInterfaceSet::hasReadyInterface() const
{
    return (loraSelectedForRuntime() && lora_.isReady()) ||
           (wifiSelectedForRuntime() && hasReadyWifiGateway());
}

bool ReticulumInterfaceSet::hasReadyWifiGateway() const
{
    if (auto_.isReady())
    {
        return true;
    }
    for (uint8_t index = 0; index < tcp_count_; ++index)
    {
        if (tcp_[index].isReady())
        {
            return true;
        }
    }
    return false;
}

bool ReticulumInterfaceSet::wifiGatewayConfigured() const
{
    return hasConfiguredIpInterface();
}

bool ReticulumInterfaceSet::isInterfaceSelected(InterfaceId interface_id) const
{
    if (interface_id == kLoRaInterfaceId)
    {
        return loraSelectedForRuntime();
    }
    if (!wifiSelectedForRuntime())
    {
        return false;
    }
    if (auto_.owns(interface_id))
    {
        return auto_.isReady();
    }
    for (uint8_t index = 0; index < tcp_count_; ++index)
    {
        if (tcp_[index].interfaceId() == interface_id)
        {
            return tcp_[index].isReady();
        }
    }
    return false;
}

bool ReticulumInterfaceSet::sendPacket(const uint8_t* data, size_t len)
{
    last_tx_result_ = {};
    if (!data || len == 0)
    {
        return false;
    }

    maintain();
    last_tx_result_.lora_required = loraSelectedForRuntime();
    last_tx_result_.lora_ready =
        last_tx_result_.lora_required && lora_.isReady();
    last_tx_result_.wifi_required = wifiSelectedForRuntime();
    last_tx_result_.wifi_ready =
        last_tx_result_.wifi_required && hasReadyWifiGateway();

    if (last_tx_result_.lora_ready && lora_.sendPacket(data, len))
    {
        last_tx_result_.lora_ok = true;
        ++last_tx_result_.sent_count;
    }
    if (last_tx_result_.wifi_required && auto_.isReady() &&
        auto_.sendPacket(data, len))
    {
        last_tx_result_.wifi_ok = true;
        ++last_tx_result_.sent_count;
    }
    for (uint8_t index = 0; index < tcp_count_; ++index)
    {
        if (last_tx_result_.wifi_required && tcp_[index].isReady() &&
            tcp_[index].sendPacket(data, len))
        {
            last_tx_result_.wifi_ok = true;
            ++last_tx_result_.sent_count;
        }
    }

    const bool sent = last_tx_result_.sent();
    Serial.printf("[Reticulum][IF][TX] raw_len=%u bearer=%s lora_req=%u lora_ready=%u lora=%u ip_req=%u ip_ready=%u ip=%u sent_count=%u sent=%u complete=%u\n",
                  static_cast<unsigned>(len),
                  txBearerName(last_tx_result_),
                  last_tx_result_.lora_required ? 1U : 0U,
                  last_tx_result_.lora_ready ? 1U : 0U,
                  last_tx_result_.lora_ok ? 1U : 0U,
                  last_tx_result_.wifi_required ? 1U : 0U,
                  last_tx_result_.wifi_ready ? 1U : 0U,
                  last_tx_result_.wifi_ok ? 1U : 0U,
                  static_cast<unsigned>(last_tx_result_.sent_count),
                  sent ? 1U : 0U,
                  last_tx_result_.reachedRequiredInterfaces() ? 1U : 0U);
    return sent;
}

bool ReticulumInterfaceSet::sendPacketOn(InterfaceId interface_id,
                                         const uint8_t* data,
                                         size_t len,
                                         const uint8_t* call_link_id,
                                         bool call_admission_control)
{
    last_tx_result_ = {};
    if (!data || len == 0 || interface_id == kInvalidInterfaceId)
    {
        return false;
    }

    maintain();
    bool sent = false;
    if (interface_id == kLoRaInterfaceId)
    {
        last_tx_result_.lora_required = true;
        last_tx_result_.lora_ready =
            loraSelectedForRuntime() && lora_.isReady();
        sent = last_tx_result_.lora_ready && lora_.sendPacket(data, len);
        last_tx_result_.lora_ok = sent;
    }
    else
    {
        last_tx_result_.wifi_required = true;
        const bool call_wifi_override = call_link_id != nullptr;
        if (!call_wifi_override && !wifiSelectedForRuntime())
        {
            return false;
        }
        if (auto_.owns(interface_id))
        {
            last_tx_result_.wifi_ready = auto_.isReady();
            sent = last_tx_result_.wifi_ready &&
                   auto_.sendPacketOn(interface_id,
                                      data,
                                      len,
                                      call_link_id,
                                      call_admission_control);
        }
        else
        {
            for (uint8_t index = 0; index < tcp_count_; ++index)
            {
                if (tcp_[index].interfaceId() != interface_id)
                {
                    continue;
                }
                last_tx_result_.wifi_ready = tcp_[index].isReady();
                sent = last_tx_result_.wifi_ready &&
                       tcp_[index].sendPacket(data,
                                              len,
                                              call_link_id,
                                              call_admission_control);
                break;
            }
        }
        last_tx_result_.wifi_ok = sent;
    }
    if (sent)
    {
        last_tx_result_.sent_interface = interface_id;
        last_tx_result_.sent_count = 1;
    }
    return sent;
}

bool ReticulumInterfaceSet::sendPacketWifiOnly(
    const uint8_t* data,
    size_t len,
    const uint8_t* call_link_id,
    bool call_admission_control,
    InterfaceId interface_id)
{
    if (interface_id != kInvalidInterfaceId)
    {
        return sendPacketOn(interface_id,
                            data,
                            len,
                            call_link_id,
                            call_admission_control);
    }

    last_tx_result_ = {};
    if (!data || len == 0)
    {
        return false;
    }
    maintain();
    last_tx_result_.wifi_required = hasConfiguredIpInterface();
    last_tx_result_.wifi_ready = hasReadyWifiGateway();

    if (auto_.isReady() &&
        auto_.sendPacket(data, len, call_link_id, call_admission_control))
    {
        last_tx_result_.wifi_ok = true;
        ++last_tx_result_.sent_count;
    }
    for (uint8_t index = 0; index < tcp_count_; ++index)
    {
        if (tcp_[index].isReady() &&
            tcp_[index].sendPacket(data,
                                   len,
                                   call_link_id,
                                   call_admission_control))
        {
            last_tx_result_.wifi_ok = true;
            ++last_tx_result_.sent_count;
        }
    }
    return last_tx_result_.sent();
}

bool ReticulumInterfaceSet::pollIncomingPacket(RxPacket* out)
{
    if (!out)
    {
        return false;
    }

    maintain();
    const bool lora_selected = loraSelectedForRuntime();
    const bool wifi_selected = wifiSelectedForRuntime();
    const uint8_t source_count = static_cast<uint8_t>(2U + tcp_count_);
    for (uint8_t offset = 0; offset < source_count; ++offset)
    {
        const uint8_t index =
            static_cast<uint8_t>((next_poll_index_ + offset) % source_count);
        bool got = false;
        if (index == 0)
        {
            got = lora_selected && lora_.pollPacket(out);
        }
        else if (index == 1)
        {
            got = wifi_selected && auto_.pollPacket(out);
        }
        else
        {
            got = wifi_selected && tcp_[index - 2U].pollPacket(out);
        }
        if (got)
        {
            next_poll_index_ =
                static_cast<uint8_t>((index + 1U) % source_count);
            last_rx_meta_ = out->rx_meta;
            has_last_rx_meta_ = true;
            return true;
        }
    }
    return false;
}

bool ReticulumInterfaceSet::pollLegacyIncomingData(MeshIncomingData* out)
{
    return loraSelectedForRuntime() && lora_.pollLegacyIncomingData(out);
}

void ReticulumInterfaceSet::handleRawPacket(const uint8_t* data, size_t size)
{
    if (loraSelectedForRuntime())
    {
        lora_.handleRawPacket(data, size);
    }
}

void ReticulumInterfaceSet::setLastRxStats(float rssi, float snr)
{
    lora_.setLastRxStats(rssi, snr);
}

float ReticulumInterfaceSet::lastRxRssi() const
{
    if (has_last_rx_meta_)
    {
        return static_cast<float>(last_rx_meta_.rssi_dbm_x10) / 10.0f;
    }
    return lora_.lastRxRssi();
}

float ReticulumInterfaceSet::lastRxSnr() const
{
    if (has_last_rx_meta_)
    {
        return static_cast<float>(last_rx_meta_.snr_db_x10) / 10.0f;
    }
    return lora_.lastRxSnr();
}

bool ReticulumInterfaceSet::loraAllowed() const
{
    if (!owns_integrated_radio_)
    {
        return false;
    }
    if (::platform::ui::reticulum_call::resource_preempt_active())
    {
        return false;
    }
    if (!config_.reticulum_lora_enabled ||
        config_.reticulum_interface_policy ==
            ReticulumInterfacePolicy::WifiGatewayOnly)
    {
        return false;
    }
    const size_t interface_count = std::min<size_t>(
        network_config_.interface_count,
        reticulum::kMaxNetworkInterfaces);
    for (size_t index = 0; index < interface_count; ++index)
    {
        const auto& interface_config = network_config_.interfaces[index];
        if (interface_config.enabled &&
            interface_config.type ==
                reticulum::NetworkInterfaceType::IntegratedLoRa)
        {
            return true;
        }
    }
    return false;
}

bool ReticulumInterfaceSet::wifiAllowed() const
{
    return config_.reticulum_wifi_gateway_enabled &&
           config_.reticulum_interface_policy !=
               ReticulumInterfacePolicy::LoRaOnly &&
           hasConfiguredIpInterface();
}

bool ReticulumInterfaceSet::loraSelectedForRuntime() const
{
    if (!loraAllowed())
    {
        return false;
    }
    return config_.reticulum_interface_policy != ReticulumInterfacePolicy::All ||
           !hasReadyWifiGateway();
}

bool ReticulumInterfaceSet::wifiSelectedForRuntime() const
{
    if (!wifiAllowed())
    {
        return false;
    }
    return config_.reticulum_interface_policy ==
               ReticulumInterfacePolicy::WifiGatewayOnly ||
           hasReadyWifiGateway();
}

bool ReticulumInterfaceSet::hasConfiguredIpInterface() const
{
    if (auto_.isConfigured())
    {
        return true;
    }
    for (uint8_t index = 0; index < tcp_count_; ++index)
    {
        if (tcp_[index].isConfigured())
        {
            return true;
        }
    }
    return false;
}

void ReticulumInterfaceSet::syncSharedLoRaRxGate()
{
    // Background IP services must not suppress Meshtastic/MeshCore reception.
    if (!owns_integrated_radio_)
    {
        return;
    }
    const bool suppress = !loraSelectedForRuntime();
#if defined(ARDUINO)
    if (shared_lora_rx_suppressed_ == suppress &&
        app::AppTasks::isRadioReceiveSuppressed() == suppress)
    {
        return;
    }
    shared_lora_rx_suppressed_ = suppress;
    app::AppTasks::setRadioReceiveSuppressed(suppress);
#else
    shared_lora_rx_suppressed_ = suppress;
#endif
}

} // namespace chat::reticulum::interfaces
