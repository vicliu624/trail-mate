#include "chat/domain/reticulum_network_config.h"
#include "chat/infra/reticulum/tcp_retry.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/interface_access.h"
#include "sys/ringbuf.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

#define TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE 1
#define TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE 1
#define TRAIL_MATE_RETICULUM_C6_TCP_AVAILABLE 0

static uint32_t now = 0;
static bool fail_access_allocation = false;
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    if (fail_access_allocation) return nullptr;
    try
    {
        return ::operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}
namespace reticulum = chat::reticulum;
using InterfaceId = uint8_t;
constexpr InterfaceId kInvalidInterfaceId = 0;
constexpr InterfaceId kDiscoveredTcpInterfaceId = 35;
constexpr size_t kReticulumGatewayHostMaxLen = reticulum::kInterfaceHostMaxLen;
static const char* boolLabel(bool value) { return value ? "true" : "false"; }
static void copyHost(char* out, size_t size, const char* input)
{
    std::snprintf(out, size, "%s", input ? input : "");
}
uint32_t millis() { return now; }
struct Logger
{
    template <typename... T>
    void printf(const char*, T...) {}
} Serial;

namespace platform::ui::wifi
{
struct Status
{
    bool connected = true;
};
static Status current;
Status status() { return current; }
} // namespace platform::ui::wifi
namespace platform::ui::wifi_access
{
enum class Client
{
    ReticulumGateway
};
enum class AccessKind
{
    WifiConnect,
    LongLivedSocket,
    ReticulumGatewayCallControl,
    ReticulumGatewayCallAudio
};
enum class Priority
{
    Messaging
};
struct Request
{
    Client client{};
    AccessKind kind{};
    Priority priority{};
    bool allow_connect = false;
    const char* reason = nullptr;
};
struct ConnectResult
{
    int decision = 0;
};
struct Lease
{
    bool granted = true;
    int decision = 0;
};
static Lease current;
bool ensure_connected(const Request&, ConnectResult*) { return false; }
Lease acquire(const Request&) { return current; }
const char* decision_name(int) { return "fixture"; }
struct Budget
{
    bool allow_read = true, allow_write = true, allow_connect = true;
    uint32_t rx_byte_budget = 256, min_read_interval_ms = 0, tx_byte_budget = 1200;
};
static Budget budget;
Budget traffic_budget(Client, Priority) { return budget; }
Budget traffic_budget(Client, Priority, const uint8_t*, AccessKind) { return budget; }
} // namespace platform::ui::wifi_access
namespace platform::esp::arduino_common::net
{
enum class TcpConnectPhase
{
    Idle,
    Resolving,
    Connecting,
    Connected,
    Failed
};
struct Status
{
    TcpConnectPhase phase = TcpConnectPhase::Idle;
    int failure = 0;
    int detail = 0;
};
const char* tcpConnectFailureName(int) { return "fixture"; }
struct Connector
{
    Status state;
    int starts = 0;
    int polls = 0;
    int cancels = 0;
    bool fail_poll = false;
    bool pending() const { return state.phase == TcpConnectPhase::Resolving || state.phase == TcpConnectPhase::Connecting; }
    Status status() const { return state; }
    bool start(const char*, uint16_t, uint32_t, uint32_t)
    {
        ++starts;
        state.phase = TcpConnectPhase::Resolving;
        return true;
    }
    Status poll(uint32_t)
    {
        ++polls;
        if (fail_poll)
        {
            fail_poll = false;
            state.phase = TcpConnectPhase::Failed;
        }
        return state;
    }
    void cancel()
    {
        ++cancels;
        state = {};
    }
    int takeSocket()
    {
        state = {};
        return 1;
    }
};
} // namespace platform::esp::arduino_common::net
struct WiFiClient
{
    std::vector<uint8_t> written;
    size_t write(const uint8_t* data, size_t size)
    {
        written.assign(data, data + size);
        return size;
    }
    WiFiClient() = default;
    explicit WiFiClient(int) {}
    void stop() {}
    void setNoDelay(bool) {}
    bool connected() const { return true; }
    int available() const { return 0; }
    int read() { return -1; }
};
class WifiGatewayReticulumInterface
{
  public:
    bool transport_enabled_ = true;
    bool enabled_ = true;
    bool selected_ = false;
    bool auto_connect_wifi_ = true;
    char host_[64] = "example.invalid";
    InterfaceId interface_id_ = kInvalidInterfaceId;
    sys::RingBuffer<unsigned, 8> rx_queue_;
    sys::RingBuffer<unsigned, 4> rx_priority_queue_;
    uint16_t port_ = 4242;
    bool socket_online_ = false;
    bool socket_open_pending_ = false;
    bool hdlc_in_frame_ = false;
    bool hdlc_escape_ = false;
    size_t hdlc_frame_len_ = 0;
    static constexpr size_t kMaxWirePacketSize = 564;
    static constexpr uint8_t kHdlcFlag = 0x7e, kHdlcEscape = 0x7d, kHdlcEscapeMask = 0x20;
    reticulum::InterfaceAccess access_;
    uint8_t tx_frame_[1130]{}, hdlc_frame_[564]{};
    std::vector<uint8_t> received;
    unsigned accepted_frames = 0;
    bool sendPacket(const uint8_t*, size_t, const uint8_t* = nullptr, bool = false);
    void enqueueFrame(const uint8_t* data, size_t size)
    {
        received.assign(data, data + size);
        ++accepted_frames;
    }
    uint32_t rx_stats_read_skips_ = 0, last_socket_read_ms_ = 0, rx_stats_bytes_ = 0;
    static constexpr int32_t kSocketConnectTimeoutMs = 5000;
    chat::reticulum::TcpRetry reconnect_;
    platform::esp::arduino_common::net::Connector connector_;
    WiFiClient client_;
    void stop();
    void applyConfig(const reticulum::NetworkInterfaceConfig*, bool, InterfaceId);
    bool ensureSocket();
    void maintain();
    bool isReady() const;
    bool isConfigured() const { return enabled_ && host_[0] != '\0'; }
    bool canAttempt() const;
    const chat::reticulum::TcpRetry& retryState() const { return reconnect_; }
    bool stableConnection() const;
    bool isConnecting() const { return socket_open_pending_; }
    void setSelected(bool);
    void syncSocketState() {}
    bool connected() const { return socket_online_; }
    void readAvailable();
    void feedHdlcByte(uint8_t);
};
class ReticulumInterfaceSet
{
  public:
    struct Auto
    {
        void maintain() {}
    } auto_;
    std::array<WifiGatewayReticulumInterface, 4> tcp_;
    reticulum::NetworkInterfaceConfig discovered_config_;
    reticulum::ReticulumNetworkConfig network_config_;
    bool wifi_allowed_ = true;
    bool wifiAllowed() const { return wifi_allowed_; }
    struct Config
    {
        bool reticulum_wifi_auto_connect = true;
    } config_;
    uint8_t tcp_count_ = 3, active_tcp_ = UINT8_MAX, next_tcp_ = 0;
    void maintain();
    bool canReplaceDiscoveredGateway(const char*, uint16_t) const;
    void replaceDiscoveredGateway(const char*, uint16_t);
    void syncSharedLoRaRxGate() {}
};
#include "gateway_actual.inc"

int main()
{
    // Real send/framing/receive code with the real IFAC codec. Protect maximum
    // packets, escape expansion, credential changes and public/private isolation.
    WifiGatewayReticulumInterface protected_gateway, public_gateway;
    reticulum::NetworkInterfaceConfig access_config;
    access_config.type = reticulum::NetworkInterfaceType::TcpClient;
    access_config.enabled = true;
    std::strcpy(access_config.target_host, "protected.example");
    public_gateway.applyConfig(&access_config, true, 33);
    std::strcpy(access_config.access.network_name, "trail");
    std::strcpy(access_config.access.passphrase, "example-passphrase");
    access_config.access.ifac_size_bits = 512;
    protected_gateway.applyConfig(&access_config, true, 32);
    protected_gateway.selected_ = protected_gateway.socket_online_ = true;
    std::array<uint8_t, 500> packet;
    packet.fill(0x7e);
    packet[0] = 0x14;
    assert(protected_gateway.sendPacket(packet.data(), packet.size()));
    for (auto byte : protected_gateway.client_.written)
    {
        protected_gateway.feedHdlcByte(byte);
        public_gateway.feedHdlcByte(byte);
    }
    assert(protected_gateway.received == std::vector<uint8_t>(packet.begin(), packet.end()));
    assert(public_gateway.accepted_frames == 0);
    protected_gateway.rx_priority_queue_.append(1);
    protected_gateway.applyConfig(&access_config, true, 32);
    assert(protected_gateway.socket_online_ && protected_gateway.rx_priority_queue_.size() == 1);
    const auto old_wire = protected_gateway.client_.written;
    std::strcpy(access_config.access.passphrase, "new-passphrase");
    protected_gateway.applyConfig(&access_config, true, 32);
    assert(!protected_gateway.socket_online_ && protected_gateway.rx_priority_queue_.size() == 0);
    for (auto byte : old_wire) protected_gateway.feedHdlcByte(byte);
    assert(protected_gateway.accepted_frames == 1);
    public_gateway.selected_ = public_gateway.socket_online_ = true;
    assert(public_gateway.sendPacket(packet.data(), packet.size()));
    assert(public_gateway.client_.written.size() == 1001); // All but byte zero escaped.
    for (auto byte : public_gateway.client_.written)
    {
        public_gateway.feedHdlcByte(byte);
        protected_gateway.feedHdlcByte(byte);
    }
    assert(public_gateway.received == std::vector<uint8_t>(packet.begin(), packet.end()));
    assert(protected_gateway.accepted_frames == 1);
    access_config.access.ifac_size_bits = 7;
    protected_gateway.applyConfig(&access_config, true, 32);
    assert(!protected_gateway.enabled_); // Invalid access never enables public traffic.
    access_config.access.ifac_size_bits = 128;
    fail_access_allocation = true;
    protected_gateway.applyConfig(&access_config, true, 32);
    fail_access_allocation = false;
    assert(!protected_gateway.enabled_);
    for (auto byte : public_gateway.client_.written) protected_gateway.feedHdlcByte(byte);
    assert(protected_gateway.accepted_frames == 1);
    protected_gateway.applyConfig(&access_config, true, 32);
    assert(protected_gateway.enabled_); // Retry the same config once PSRAM is available.
    using chat::reticulum::TcpRetry;
    using platform::esp::arduino_common::net::TcpConnectPhase;
    TcpRetry stability;
    stability.connected(0);
    assert(!stability.stable(59999) && stability.stable(60000));
    stability.disconnected();
    assert(!stability.stable(60001));
    stability.connected(60002);
    assert(!stability.stable(120001) && stability.stable(120002));
    // Exercise the production reconfiguration function with both real queue
    // types. Old priority frames must not acquire the replacement interface ID.
    WifiGatewayReticulumInterface configured;
    reticulum::NetworkInterfaceConfig endpoint;
    endpoint.type = reticulum::NetworkInterfaceType::TcpClient;
    endpoint.enabled = true;
    std::strcpy(endpoint.target_host, "first.example.org");
    configured.applyConfig(&endpoint, true, 32);
    configured.rx_queue_.append(1);
    configured.rx_priority_queue_.append(2);
    configured.applyConfig(&endpoint, true, 32);
    assert(configured.rx_queue_.size() == 1 && configured.rx_priority_queue_.size() == 1);
    std::strcpy(endpoint.target_host, "second.example.org");
    configured.applyConfig(&endpoint, true, 32);
    assert(configured.rx_queue_.size() == 0 && configured.rx_priority_queue_.size() == 0);
    configured.rx_priority_queue_.append(3);
    configured.applyConfig(&endpoint, true, 33);
    assert(configured.interface_id_ == 33 && configured.rx_priority_queue_.size() == 0);
    configured.rx_priority_queue_.append(4);
    ++endpoint.target_port;
    configured.applyConfig(&endpoint, true, 33);
    assert(configured.rx_priority_queue_.size() == 0);
    configured.rx_priority_queue_.append(5);
    configured.applyConfig(nullptr, true, 33);
    assert(configured.interface_id_ == kInvalidInterfaceId && configured.rx_priority_queue_.size() == 0);
    ReticulumInterfaceSet discovered;
    discovered.tcp_[3].applyConfig(nullptr, true, 35);
    discovered.network_config_.interface_count = 1;
    discovered.network_config_.interfaces[0] = endpoint;
    assert(!discovered.canReplaceDiscoveredGateway(endpoint.target_host, endpoint.target_port));
    assert(discovered.canReplaceDiscoveredGateway("learned.example.org", 4242));
    discovered.wifi_allowed_ = false;
    assert(!discovered.canReplaceDiscoveredGateway("learned.example.org", 4242));
    discovered.wifi_allowed_ = true;
    discovered.tcp_[0].selected_ = true;
    discovered.tcp_[0].socket_online_ = true;
    discovered.active_tcp_ = 0;
    discovered.replaceDiscoveredGateway("learned.example.org", 4242);
    assert(discovered.tcp_count_ == 4 && discovered.active_tcp_ == 0);
    assert(discovered.tcp_[0].isReady()); // Adding a candidate leaves healthy uplink intact.
    assert(discovered.tcp_[3].interface_id_ == 35);
    assert(std::strcmp(discovered.tcp_[3].host_, "learned.example.org") == 0);
    assert(!discovered.canReplaceDiscoveredGateway("learned.example.org", 4242));
    discovered.tcp_[3].socket_open_pending_ = true;
    assert(!discovered.canReplaceDiscoveredGateway("other.example.org", 4242));
    discovered.tcp_[3].socket_open_pending_ = false;
    discovered.tcp_[3].selected_ = true;
    discovered.tcp_[3].socket_online_ = true;
    assert(!discovered.canReplaceDiscoveredGateway("other.example.org", 4242));
    discovered.tcp_[3].socket_online_ = false;
    discovered.tcp_[3].selected_ = false;
    for (unsigned i = 0; i < 3; ++i)
    {
        discovered.tcp_[i].socket_online_ = false;
        discovered.tcp_[i].reconnect_.failed(now);
    }
    discovered.maintain();
    assert(discovered.active_tcp_ == 3); // Cooling configured entries yield to discovery.
    ReticulumInterfaceSet empty;
    empty.tcp_count_ = 0;
    assert(!empty.canReplaceDiscoveredGateway("learned.example.org", 4242));
    ReticulumInterfaceSet single;
    single.tcp_count_ = 1;
    for (unsigned i = 1; i < 4; ++i) single.tcp_[i].applyConfig(nullptr, true, 0);
    single.replaceDiscoveredGateway("learned.example.org", 4242);
    single.tcp_[0].reconnect_.failed(now);
    single.maintain();
    assert(single.active_tcp_ == 3); // Disabled manual slots do not hide discovery.
    // Bounded failures, tick wrap, and stable recovery. A short-lived TCP
    // handshake must not reset a failing endpoint's penalty.
    TcpRetry retry;
    uint32_t tick = UINT32_MAX - 100;
    for (uint32_t delay : {10000U, 20000U, 40000U, 80000U, 160000U, 300000U, 300000U})
    {
        assert(retry.ready(tick));
        retry.failed(tick);
        assert(!retry.ready(tick + delay - 1));
        tick += delay;
        assert(retry.ready(tick));
        retry.connected(tick);
        retry.connected(tick + 5);
        tick += 10;
    }
    retry.connected(tick + TcpRetry::kStableConnectionMs);
    retry.failed(tick + TcpRetry::kStableConnectionMs);
    assert(retry.ready(tick + TcpRetry::kStableConnectionMs + 10000));

    WifiGatewayReticulumInterface gateway;
    now = 0;
    assert(gateway.ensureSocket());
    now = 1;
    assert(gateway.ensureSocket());
    assert(gateway.connector_.starts == 1 && gateway.connector_.polls == 2);
    // Revoking permission cancels pending DNS/TCP rather than leaving it alive
    // to bypass the retry gate every tick.
    platform::ui::wifi_access::current.granted = false;
    assert(!gateway.ensureSocket());
    assert(!gateway.connector_.pending() && gateway.connector_.cancels == 1);
    platform::ui::wifi_access::current.granted = true;
    now = 10000;
    assert(!gateway.ensureSocket());
    now = 10001;
    assert(gateway.ensureSocket());
    gateway.connector_.fail_poll = true;
    ++now;
    assert(!gateway.ensureSocket());
    const auto failure = now;
    now = failure + 9999;
    assert(!gateway.ensureSocket());
    ++now;
    assert(gateway.ensureSocket());
    // Station loss also cancels the in-flight connection.
    platform::ui::wifi::current.connected = false;
    ++now;
    assert(!gateway.ensureSocket());
    assert(!gateway.connector_.pending());
    platform::ui::wifi::current.connected = true;
    now += 10000;
    assert(gateway.ensureSocket());
    gateway.connector_.state.phase = TcpConnectPhase::Connected;
    ++now;
    assert(gateway.ensureSocket());
    assert(gateway.socket_online_ && !gateway.socket_open_pending_);
    platform::ui::wifi_access::budget = {false, false, false, 0, 0};
    gateway.readAvailable();
    assert(!gateway.socket_online_ && !gateway.canAttempt());
    now += 10000;
    assert(gateway.canAttempt());
    platform::ui::wifi_access::budget = {};

    ReticulumInterfaceSet pool;
    now = 100000;
    pool.maintain();
    assert(pool.active_tcp_ == 0 && pool.tcp_[0].isConnecting());
    // Send/poll paths call individual maintenance too. Standby entries must
    // remain closed even when those callers tick all configured interfaces.
    for (auto& candidate : pool.tcp_) candidate.maintain();
    assert(pool.tcp_[0].connector_.starts == 1);
    assert(pool.tcp_[1].connector_.starts == 0 && pool.tcp_[2].connector_.starts == 0);
    for (unsigned failed = 0; failed < 3; ++failed)
    {
        pool.tcp_[failed].connector_.fail_poll = true;
        pool.maintain();
        assert(!pool.tcp_[failed].selected_ && !pool.tcp_[failed].canAttempt());
        assert(pool.active_tcp_ == (failed == 2 ? UINT8_MAX : failed + 1));
    }
    for (unsigned tick = 0; tick < 20; ++tick) pool.maintain();
    for (unsigned i = 0; i < pool.tcp_count_; ++i) assert(pool.tcp_[i].connector_.starts == 1);
    assert(pool.tcp_[3].connector_.starts == 0); // No discovery candidate was installed.
    now += 10000;
    pool.maintain();
    assert(pool.active_tcp_ == 0 && pool.tcp_[0].connector_.starts == 2);
    pool.tcp_[0].connector_.state.phase = TcpConnectPhase::Connected;
    pool.maintain();
    now += 60000;
    for (unsigned tick = 0; tick < 20; ++tick) pool.maintain();
    assert(pool.active_tcp_ == 0 && pool.tcp_[0].isReady());
    assert(pool.tcp_[1].connector_.starts == 1 && pool.tcp_[2].connector_.starts == 1);
    pool.tcp_count_ = 0;
    pool.maintain();
    assert(pool.active_tcp_ == UINT8_MAX && !pool.tcp_[0].selected_);
    ReticulumInterfaceSet ranked;
    ranked.tcp_[0].reconnect_.failed(0);
    ranked.tcp_[0].reconnect_.failed(10000);
    ranked.tcp_[2].reconnect_.failed(0);
    now = 200000;
    ranked.maintain();
    assert(ranked.active_tcp_ == 1 && ranked.tcp_[1].isConnecting());
    assert(ranked.tcp_[0].connector_.starts == 0 && ranked.tcp_[2].connector_.starts == 0);
}
