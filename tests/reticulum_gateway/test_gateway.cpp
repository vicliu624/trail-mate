#include "chat/domain/reticulum_network_config.h"
#include "chat/infra/reticulum/tcp_retry.h"
#include "sys/ringbuf.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#define TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE 1
#define TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE 1
#define TRAIL_MATE_RETICULUM_C6_TCP_AVAILABLE 0

static uint32_t now = 0;
namespace reticulum = chat::reticulum;
using InterfaceId = uint8_t;
constexpr InterfaceId kInvalidInterfaceId = 0;
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
    LongLivedSocket
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
    uint32_t rx_byte_budget = 256, min_read_interval_ms = 0;
};
static Budget budget;
Budget traffic_budget(Client, Priority) { return budget; }
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
    bool canAttempt() const;
    bool isConnecting() const { return socket_open_pending_; }
    void setSelected(bool);
    void syncSocketState() {}
    bool connected() const { return socket_online_; }
    void readAvailable();
    void feedHdlcByte(uint8_t) {}
};
class ReticulumInterfaceSet
{
  public:
    struct Auto
    {
        void maintain() {}
    } auto_;
    std::array<WifiGatewayReticulumInterface, 3> tcp_;
    uint8_t tcp_count_ = 3, active_tcp_ = UINT8_MAX, next_tcp_ = 0;
    void maintain();
    void syncSharedLoRaRxGate() {}
};
#include "gateway_actual.inc"

int main()
{
    using chat::reticulum::TcpRetry;
    using platform::esp::arduino_common::net::TcpConnectPhase;
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
    for (const auto& candidate : pool.tcp_) assert(candidate.connector_.starts == 1);
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
}
