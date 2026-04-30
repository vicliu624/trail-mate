#include "platform/ui/hostlink_runtime.h"

#include "hostlink/hostlink_session.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace platform::ui::hostlink
{
namespace
{

constexpr const char* kBindAddressEnv = "TRAIL_MATE_HOSTLINK_BIND";
constexpr const char* kPortEnv = "TRAIL_MATE_HOSTLINK_PORT";
constexpr const char* kSdRootEnv = "TRAIL_MATE_SD_ROOT";
constexpr const char* kSettingsRootEnv = "TRAIL_MATE_SETTINGS_ROOT";

constexpr uint16_t kDefaultPort = 44042;
constexpr uint16_t kFallbackPortCount = 8;
constexpr uint32_t kHandshakeTimeoutMs = 5000;
constexpr uint32_t kHeartbeatIntervalMs = 2000;
constexpr uint32_t kPollIntervalMs = 100;

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

std::mutex s_mutex;
std::thread s_worker;
std::atomic_bool s_running = false;
::hostlink::SessionRuntime s_runtime{};
SocketHandle s_listen_socket = kInvalidSocket;
SocketHandle s_client_socket = kInvalidSocket;
std::string s_endpoint{};
std::string s_endpoint_file{};

uint32_t now_ms()
{
    using clock = std::chrono::steady_clock;
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
}

std::filesystem::path default_storage_root()
{
    if (const char* configured = std::getenv(kSdRootEnv))
    {
        if (configured[0] != '\0')
        {
            return std::filesystem::path(configured);
        }
    }

    if (const char* configured = std::getenv(kSettingsRootEnv))
    {
        if (configured[0] != '\0')
        {
            return std::filesystem::path(configured) / "sdcard";
        }
    }

#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"))
    {
        if (appdata[0] != '\0')
        {
            return std::filesystem::path(appdata) / "TrailMateCardputerZero" / "sdcard";
        }
    }
#endif

    if (const char* home = std::getenv("HOME"))
    {
        if (home[0] != '\0')
        {
            return std::filesystem::path(home) / ".trailmate_cardputer_zero" / "sdcard";
        }
    }

    return std::filesystem::current_path() / ".trailmate_cardputer_zero" / "sdcard";
}

std::filesystem::path hostlink_dir()
{
    return default_storage_root() / "hostlink";
}

bool ensure_hostlink_dir()
{
    std::error_code ec;
    std::filesystem::create_directories(hostlink_dir(), ec);
    return !ec;
}

std::string env_or_default(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return std::string(fallback);
    }
    return std::string(value);
}

uint16_t configured_port()
{
    const char* value = std::getenv(kPortEnv);
    if (!value || value[0] == '\0')
    {
        return kDefaultPort;
    }

    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || (end && *end != '\0') || parsed <= 0 || parsed > 65535)
    {
        return kDefaultPort;
    }
    return static_cast<uint16_t>(parsed);
}

void write_endpoint_file(const std::string& endpoint)
{
    if (!ensure_hostlink_dir())
    {
        return;
    }

    const std::filesystem::path endpoint_path = hostlink_dir() / "endpoint.txt";
    std::ofstream stream(endpoint_path, std::ios::trunc);
    if (!stream.is_open())
    {
        return;
    }
    stream << endpoint << "\n";
    stream.close();

    std::lock_guard<std::mutex> lock(s_mutex);
    s_endpoint_file = endpoint_path.string();
}

void clear_endpoint_file()
{
    std::error_code ec;
    std::filesystem::remove(hostlink_dir() / "endpoint.txt", ec);
}

void close_socket(SocketHandle& socket_handle)
{
    if (socket_handle == kInvalidSocket)
    {
        return;
    }
#if defined(_WIN32)
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
    socket_handle = kInvalidSocket;
}

int socket_error_code()
{
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool socket_would_block(int error_code)
{
#if defined(_WIN32)
    return error_code == WSAEWOULDBLOCK;
#else
    return error_code == EWOULDBLOCK || error_code == EAGAIN;
#endif
}

void set_socket_nonblocking(SocketHandle socket_handle)
{
#if defined(_WIN32)
    u_long mode = 1;
    ioctlsocket(socket_handle, FIONBIO, &mode);
#else
    const int current_flags = fcntl(socket_handle, F_GETFL, 0);
    if (current_flags >= 0)
    {
        (void)fcntl(socket_handle, F_SETFL, current_flags | O_NONBLOCK);
    }
#endif
}

bool wait_for_readable(SocketHandle socket_handle, uint32_t timeout_ms)
{
    if (socket_handle == kInvalidSocket)
    {
        return false;
    }

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket_handle, &read_set);

    timeval timeout{};
    timeout.tv_sec = static_cast<long>(timeout_ms / 1000U);
    timeout.tv_usec = static_cast<long>((timeout_ms % 1000U) * 1000U);

#if defined(_WIN32)
    const int ready = select(0, &read_set, nullptr, nullptr, &timeout);
#else
    const int ready = select(socket_handle + 1, &read_set, nullptr, nullptr, &timeout);
#endif
    return ready > 0 && FD_ISSET(socket_handle, &read_set);
}

#if defined(_WIN32)
bool ensure_winsock()
{
    static std::once_flag s_once;
    static bool s_ok = false;
    std::call_once(s_once,
                   []()
                   {
                       WSADATA data{};
                       s_ok = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
                   });
    return s_ok;
}
#endif

void set_error_state(uint32_t error_code)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    ::hostlink::set_link_state(s_runtime, ::hostlink::LinkState::Error);
    ::hostlink::note_error(s_runtime, error_code);
}

SocketHandle open_listen_socket(std::string& endpoint_out)
{
#if defined(_WIN32)
    if (!ensure_winsock())
    {
        set_error_state(10001U);
        return kInvalidSocket;
    }
#endif

    const std::string bind_address = env_or_default(kBindAddressEnv, "0.0.0.0");
    const uint16_t base_port = configured_port();

    for (uint16_t offset = 0; offset < kFallbackPortCount; ++offset)
    {
        const uint16_t port = static_cast<uint16_t>(base_port + offset);
        SocketHandle listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_socket == kInvalidSocket)
        {
            continue;
        }

        int reuse_addr = 1;
        setsockopt(listen_socket,
                   SOL_SOCKET,
                   SO_REUSEADDR,
#if defined(_WIN32)
                   reinterpret_cast<const char*>(&reuse_addr),
#else
                   &reuse_addr,
#endif
                   sizeof(reuse_addr));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (inet_pton(AF_INET, bind_address.c_str(), &address.sin_addr) != 1)
        {
            close_socket(listen_socket);
            set_error_state(10002U);
            return kInvalidSocket;
        }

        if (bind(listen_socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        {
            close_socket(listen_socket);
            continue;
        }

        if (listen(listen_socket, 1) != 0)
        {
            close_socket(listen_socket);
            continue;
        }

        set_socket_nonblocking(listen_socket);
        endpoint_out = "tcp://" + bind_address + ":" + std::to_string(port);
        return listen_socket;
    }

    set_error_state(10003U);
    return kInvalidSocket;
}

void disconnect_client()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    close_socket(s_client_socket);
    ::hostlink::mark_disconnected(s_runtime);
}

void send_status_line(SocketHandle client_socket, const char* line)
{
    if (client_socket == kInvalidSocket || !line)
    {
        return;
    }

    const int sent = send(client_socket, line, static_cast<int>(std::strlen(line)), 0);
    if (sent > 0)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        ::hostlink::note_tx(s_runtime);
    }
}

void accept_client_if_available(SocketHandle listen_socket)
{
    if (!wait_for_readable(listen_socket, kPollIntervalMs))
    {
        return;
    }

    sockaddr_in remote_addr{};
#if defined(_WIN32)
    int remote_len = sizeof(remote_addr);
#else
    socklen_t remote_len = sizeof(remote_addr);
#endif
    SocketHandle client_socket =
        accept(listen_socket, reinterpret_cast<sockaddr*>(&remote_addr), &remote_len);
    if (client_socket == kInvalidSocket)
    {
        return;
    }

    set_socket_nonblocking(client_socket);
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_client_socket = client_socket;
        ::hostlink::set_link_state(s_runtime, ::hostlink::LinkState::Connected);
        ::hostlink::mark_handshake_started(s_runtime, now_ms(), kHandshakeTimeoutMs);
    }

    send_status_line(client_socket, "TRAILMATE_HOSTLINK/1 READY\n");
}

void service_client()
{
    SocketHandle client_socket = kInvalidSocket;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        client_socket = s_client_socket;
        if (client_socket == kInvalidSocket)
        {
            return;
        }

        if (::hostlink::handshake_expired(s_runtime, now_ms()))
        {
            close_socket(s_client_socket);
            return;
        }
    }

    if (!wait_for_readable(client_socket, kPollIntervalMs))
    {
        const uint32_t tick_ms = now_ms();
        std::lock_guard<std::mutex> lock(s_mutex);
        if (::hostlink::should_emit_status(s_runtime, tick_ms, kHeartbeatIntervalMs) &&
            s_client_socket != kInvalidSocket)
        {
            const Status snapshot = s_runtime.status;
            std::string heartbeat = "STATUS rx=" + std::to_string(snapshot.rx_count) +
                                    " tx=" + std::to_string(snapshot.tx_count) + "\n";
            const int sent = send(s_client_socket,
                                  heartbeat.c_str(),
                                  static_cast<int>(heartbeat.size()),
                                  0);
            if (sent > 0)
            {
                ::hostlink::note_tx(s_runtime);
                ::hostlink::mark_status_emitted(s_runtime, tick_ms);
            }
        }
        return;
    }

    char buffer[256];
    const int received = recv(client_socket, buffer, sizeof(buffer), 0);
    if (received == 0)
    {
        disconnect_client();
        return;
    }

    if (received < 0)
    {
        const int error_code = socket_error_code();
        if (!socket_would_block(error_code))
        {
            set_error_state(static_cast<uint32_t>(error_code));
            disconnect_client();
        }
        return;
    }

    bool handshake_completed = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        ::hostlink::note_rx(s_runtime);
        if (::hostlink::is_handshaking(s_runtime))
        {
            ::hostlink::mark_handshake_complete(s_runtime, now_ms());
            handshake_completed = true;
        }
    }

    if (handshake_completed)
    {
        send_status_line(client_socket, "TRAILMATE_HOSTLINK/1 OK\n");
        return;
    }

    const int sent = send(client_socket, buffer, received, 0);
    if (sent > 0)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        ::hostlink::note_tx(s_runtime);
    }
}

void worker_main()
{
    std::string endpoint;
    SocketHandle listen_socket = open_listen_socket(endpoint);
    if (listen_socket == kInvalidSocket)
    {
        s_running.store(false);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_listen_socket = listen_socket;
        s_endpoint = endpoint;
    }
    write_endpoint_file(endpoint);

    while (s_running.load())
    {
        SocketHandle active_client = kInvalidSocket;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            active_client = s_client_socket;
        }

        if (active_client == kInvalidSocket)
        {
            accept_client_if_available(listen_socket);
            continue;
        }

        service_client();
    }

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        close_socket(s_client_socket);
        close_socket(s_listen_socket);
        ::hostlink::stop_session(s_runtime);
        s_endpoint.clear();
    }
    clear_endpoint_file();
}

} // namespace

bool is_supported()
{
    return ensure_hostlink_dir();
}

void start()
{
    if (s_running.exchange(true))
    {
        return;
    }

    if (s_worker.joinable())
    {
        s_worker.join();
    }

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        ::hostlink::reset_session(s_runtime, now_ms());
        s_runtime.status.state = ::hostlink::LinkState::Waiting;
        s_endpoint.clear();
        s_endpoint_file.clear();
    }

    s_worker = std::thread(worker_main);
}

void stop()
{
    if (!s_running.exchange(false))
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        ::hostlink::stop_session(s_runtime);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        close_socket(s_client_socket);
        close_socket(s_listen_socket);
    }

    if (s_worker.joinable())
    {
        s_worker.join();
    }
}

bool is_active()
{
    return s_running.load();
}

Status get_status()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_runtime.status;
}

} // namespace platform::ui::hostlink
