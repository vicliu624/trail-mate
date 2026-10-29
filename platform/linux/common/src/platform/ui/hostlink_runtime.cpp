#include "platform/ui/hostlink_runtime.h"

#include "hostlink/hostlink_codec.h"
#include "hostlink/hostlink_config_codec.h"
#include "hostlink/hostlink_frame_router.h"
#include "hostlink/hostlink_service_codec.h"
#include "hostlink/hostlink_session.h"
#include "platform/linux/build_identity.h"
#include "platform/linux/hostlink_config_snapshot.h"
#include "platform/linux/hostlink_config_transfer.h"
#include "platform/linux/hostlink_geocaching_drafts.h"
#include "platform/linux/hostlink_identity_backup.h"
#include "platform/linux/hostlink_management.h"
#include "platform/linux/hostlink_package_install.h"
#include "platform/linux/hostlink_radio_identity.h"
#include "platform/linux/hostlink_track_catalog.h"
#include "platform/linux/hostlink_track_confirmation.h"
#include "platform/linux/hostlink_track_directories.h"
#include "platform/linux/hostlink_track_prepare.h"
#include "platform/linux/hostlink_track_publication.h"
#include "platform/linux/hostlink_track_snapshots.h"
#include "platform/linux/hostlink_transmit_queue.h"
#include "platform/linux/hostlink_uploads.h"
#include "platform/linux/track_read_runtime.h"
#include "platform/ui/device_runtime.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

#include "platform/linux/runtime_paths.h"
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
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

std::mutex s_mutex;
std::mutex s_lifecycle_mutex;
std::atomic_bool s_running = false;
::hostlink::SessionRuntime s_runtime{};
SocketHandle s_listen_socket = kInvalidSocket;
SocketHandle s_client_socket = kInvalidSocket;
std::string s_endpoint{};
std::string s_endpoint_file{};
// Single-worker connection state. A slow peer may retain at most eight frames.
::platform::linux_runtime::HostlinkTransmitQueue s_output;
::hostlink::Decoder s_decoder;
std::unique_ptr<::platform::linux_runtime::HostlinkUploads> s_uploads;
std::unique_ptr<::platform::linux_runtime::HostlinkPackageInstall> s_packages;
std::unique_ptr<::platform::linux_runtime::HostlinkConfigSnapshot> s_config_snapshot;
std::unique_ptr<::platform::linux_runtime::HostlinkConfigTransfer> s_config_transfer;
std::unique_ptr<::platform::linux_runtime::HostlinkIdentityBackup> s_identity_backup;
std::unique_ptr<::platform::linux_runtime::HostlinkRadioIdentity> s_radio_identity;
std::unique_ptr<::platform::linux_runtime::HostlinkTrackCatalog> s_track_catalog;
std::unique_ptr<::platform::linux_runtime::HostlinkTrackSnapshots> s_track_snapshots;
std::unique_ptr<::platform::linux_runtime::HostlinkTrackPrepare> s_track_prepare;
std::unique_ptr<::platform::linux_runtime::HostlinkTrackDirectories> s_track_directories;
std::unique_ptr<::platform::linux_runtime::HostlinkTrackPublication> s_track_publication;
std::unique_ptr<::platform::linux_runtime::HostlinkTrackConfirmation> s_track_confirmation;
std::unique_ptr<::platform::linux_runtime::HostlinkTrackRead> s_track_read;
std::unique_ptr<::platform::linux_runtime::HostlinkGeocachingDrafts> s_geocaching_drafts;
// Declared after worker dependencies so it stops before they are destroyed.
struct WorkerOwner
{
    std::thread thread;
    ~WorkerOwner() { platform::ui::hostlink::stop(); }
} s_worker_owner;
std::thread& s_worker = s_worker_owner.thread;

uint32_t now_ms()
{
    using clock = std::chrono::steady_clock;
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
}

uint64_t upload_now_ms()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::filesystem::path hostlink_dir()
{
    return ::platform::linux_runtime::resolve_paths().sd_root / "hostlink";
}

bool ensure_hostlink_dir()
{
    return ::platform::linux_runtime::ensure_directory(hostlink_dir());
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

    // Default to loopback for safety.  Use TRAIL_MATE_HOSTLINK_BIND=0.0.0.0
    // to explicitly enable external access on real devices.
    const char* bind_addr = std::getenv(kBindAddressEnv);
    const std::string bind_address = (bind_addr && bind_addr[0] != '\0')
                                         ? std::string(bind_addr)
                                         : std::string("127.0.0.1");
    const uint16_t base_port = configured_port();

    for (uint16_t offset = 0; offset < kFallbackPortCount; ++offset)
    {
        const uint32_t candidate = uint32_t(base_port) + offset;
        if (candidate > 65535) break;
        const uint16_t port = static_cast<uint16_t>(candidate);
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
    if (s_track_publication) s_track_publication->close();
    if (s_track_read) s_track_read->close();
    if (s_geocaching_drafts) s_geocaching_drafts->close();
    if (s_track_directories) s_track_directories->close();
    if (s_track_prepare) s_track_prepare->advance(upload_now_ms(), false);
    if (s_track_confirmation) s_track_confirmation->advance(upload_now_ms(), false);
    if (s_track_snapshots) s_track_snapshots->advance(upload_now_ms(), false);
    if (s_config_snapshot) s_config_snapshot->close();
    if (s_packages) s_packages->advance(upload_now_ms(), false);
    if (s_uploads) s_uploads->advance(upload_now_ms(), false);
    s_decoder.reset();
    s_output.reset();
    std::lock_guard<std::mutex> lock(s_mutex);
    close_socket(s_client_socket);
    ::hostlink::mark_disconnected(s_runtime);
}

bool queue_frame(::hostlink::FrameType type, uint16_t sequence, const uint8_t* payload, size_t size)
{
    return s_output.push(static_cast<uint8_t>(type), sequence, payload, size);
}

bool flush_output(SocketHandle socket)
{
    using Queue = ::platform::linux_runtime::HostlinkTransmitQueue;
    const auto result = s_output.flush([socket](const uint8_t* bytes, size_t size) -> Queue::WriteResult
                                       {
        if (!s_running.load()) return {Queue::WriteState::Failed};
        const int sent = send(socket, reinterpret_cast<const char*>(bytes), static_cast<int>(size), kSendFlags);
        if (sent < 0)
        {
            const int error = socket_error_code();
#if !defined(_WIN32)
            if (error == EINTR) return {Queue::WriteState::Retry};
#endif
            return {socket_would_block(error) ? Queue::WriteState::WouldBlock : Queue::WriteState::Failed};
        }
        return {sent ? Queue::WriteState::Progress : Queue::WriteState::Failed, static_cast<size_t>(sent)}; });
    std::lock_guard<std::mutex> lock(s_mutex);
    for (size_t frame = 0; frame < result.completed; ++frame) ::hostlink::note_tx(s_runtime);
    return result.connected;
}

bool queue_status()
{
    ::hostlink::StatusPayloadSnapshot snapshot;
    uint16_t sequence;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        snapshot.link_state = static_cast<uint8_t>(s_runtime.status.state);
        snapshot.last_error = s_runtime.status.last_error;
        sequence = ::hostlink::next_tx_sequence(s_runtime);
    }
    const auto battery = ::platform::ui::device::battery_info();
    if (battery.available && battery.level >= 0 && battery.level <= 100) snapshot.battery = static_cast<uint8_t>(battery.level);
    snapshot.charging = battery.charging;
    std::vector<uint8_t> encoded, payload;
    if (!::hostlink::encode_status_payload(encoded, snapshot, false)) return false;
    // Do not emit fabricated config/radio values while those ports are absent.
    for (size_t offset = 0; offset + 2 <= encoded.size();)
    {
        const size_t length = encoded[offset + 1] + 2;
        if (length > encoded.size() - offset) return false;
        const auto key = static_cast<::hostlink::StatusKey>(encoded[offset]);
        if (key == ::hostlink::StatusKey::Battery || key == ::hostlink::StatusKey::Charging ||
            key == ::hostlink::StatusKey::LinkState || key == ::hostlink::StatusKey::LastError)
            payload.insert(payload.end(), encoded.begin() + offset, encoded.begin() + offset + length);
        offset += length;
    }
    return queue_frame(::hostlink::FrameType::EvStatus, sequence, payload.data(), payload.size());
}

bool handle_binary_frame(const ::hostlink::Frame& frame)
{
    bool ready;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        ::hostlink::note_rx(s_runtime);
        ready = ::hostlink::is_ready(s_runtime);
    }
    const auto decision = ::hostlink::route_frame(frame, ready);
    if (decision.type == ::hostlink::HostlinkFrameDecisionType::CompleteHello)
    {
        ::hostlink::HelloAckPayloadInfo info;
        info.capabilities = ::hostlink::CapStatus | ::hostlink::CapManagement | ::hostlink::CapAutoInterfaceIfac;
        info.model = "Trail Mate Linux";
        info.firmware = ::platform::linux_runtime::runtime_firmware_version();
        std::vector<uint8_t> payload;
        if (!::hostlink::build_hello_ack_payload(payload, info) ||
            !queue_frame(::hostlink::FrameType::HelloAck, frame.seq, payload.data(), payload.size())) return false;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            ::hostlink::mark_handshake_complete(s_runtime, now_ms());
        }
        return queue_status();
    }
    auto error = decision.type == ::hostlink::HostlinkFrameDecisionType::DispatchCommand
                     ? ::hostlink::ErrorCode::Unsupported
                     : decision.error;
    if (decision.type == ::hostlink::HostlinkFrameDecisionType::DispatchCommand &&
        decision.command == ::hostlink::HostlinkCommandId::Management)
    {
        std::vector<uint8_t> response;
        struct ClearResponse
        {
            std::vector<uint8_t>& bytes;
            ~ClearResponse()
            {
                volatile uint8_t* clear = bytes.data();
                for (size_t i = 0; i < bytes.size(); ++i) clear[i] = 0;
            }
        } clear_response{response};
        error = ::platform::linux_runtime::process_hostlink_management(frame.payload.data(), frame.payload.size(), response, s_uploads.get(), s_packages.get(), s_config_snapshot.get(), s_config_transfer.get(), s_identity_backup.get(), s_radio_identity.get(), s_track_catalog.get(), s_track_snapshots.get(), s_track_prepare.get(), s_track_directories.get(), s_track_publication.get(), s_track_confirmation.get(), s_track_read.get(), s_geocaching_drafts.get());
        if (error == ::hostlink::ErrorCode::Ok &&
            !queue_frame(::hostlink::FrameType::ManagementResponse, frame.seq, response.data(), response.size())) return false;
    }
    const uint8_t payload = static_cast<uint8_t>(error);
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (error != ::hostlink::ErrorCode::Ok) ::hostlink::note_error(s_runtime, payload);
    }
    return queue_frame(::hostlink::FrameType::Ack, frame.seq, &payload, 1);
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
    s_decoder.reset();
    s_output.reset();
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_client_socket = client_socket;
        ::hostlink::set_link_state(s_runtime, ::hostlink::LinkState::Connected);
        ::hostlink::mark_handshake_started(s_runtime, now_ms(), kHandshakeTimeoutMs);
    }
}

void service_client()
{
    if (s_track_prepare) s_track_prepare->advance(upload_now_ms(), true);
    if (s_track_confirmation) s_track_confirmation->advance(upload_now_ms(), true);
    if (s_geocaching_drafts) s_geocaching_drafts->advance(upload_now_ms(), true);
    if (s_track_snapshots) s_track_snapshots->advance(upload_now_ms(), true);
    if (s_uploads) s_uploads->advance(upload_now_ms(), true);
    if (s_packages) s_packages->advance(upload_now_ms(), true);
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

    if (!flush_output(client_socket))
    {
        disconnect_client();
        return;
    }
    if (!wait_for_readable(client_socket, kPollIntervalMs))
    {
        const uint32_t tick_ms = now_ms();
        bool emit = false;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            emit = s_output.empty() && ::hostlink::should_emit_status(s_runtime, tick_ms, kHeartbeatIntervalMs);
            if (emit)
                ::hostlink::mark_status_emitted(s_runtime, tick_ms);
        }
        if (emit && (!queue_status() || !flush_output(client_socket))) disconnect_client();
        return;
    }

    char buffer[256];
    struct ClearInput
    {
        char* bytes;
        ~ClearInput()
        {
            volatile char* clear = bytes;
            for (size_t i = 0; i < 256; ++i) clear[i] = 0;
        }
    } clear_input{buffer};
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

    s_decoder.push(reinterpret_cast<const uint8_t*>(buffer), static_cast<size_t>(received));
    ::hostlink::Frame frame;
    while (s_decoder.next(frame))
    {
        struct ClearFrame
        {
            ::hostlink::Frame& frame;
            ~ClearFrame()
            {
                volatile uint8_t* clear = frame.payload.data();
                for (size_t i = 0; i < frame.payload.size(); ++i) clear[i] = 0;
            }
        } clear_frame{frame};
        if (!handle_binary_frame(frame) || !flush_output(client_socket))
        {
            disconnect_client();
            return;
        }
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
    s_config_snapshot = std::make_unique<::platform::linux_runtime::HostlinkConfigSnapshot>();
    s_config_transfer = std::make_unique<::platform::linux_runtime::HostlinkConfigTransfer>();
    s_identity_backup = std::make_unique<::platform::linux_runtime::HostlinkIdentityBackup>(::platform::linux_runtime::sqlite_database_path());
    s_radio_identity = std::make_unique<::platform::linux_runtime::HostlinkRadioIdentity>(::platform::linux_runtime::sqlite_database_path());
    s_track_directories = std::make_unique<::platform::linux_runtime::HostlinkTrackDirectories>(::platform::linux_runtime::sqlite_database_path());
    s_track_publication = std::make_unique<::platform::linux_runtime::HostlinkTrackPublication>(::platform::linux_runtime::sqlite_database_path());
    s_track_read = std::make_unique<::platform::linux_runtime::HostlinkTrackRead>(::platform::linux_runtime::sqlite_database_path());
    // Retained unfinished work is opt-in, not part of device/map delivery.
    if (const auto* enabled = std::getenv("TRAIL_MATE_EXPERIMENTAL_GEOCACHING_DRAFTS"); enabled && std::string(enabled) == "1")
        s_geocaching_drafts = std::make_unique<::platform::linux_runtime::HostlinkGeocachingDrafts>(::platform::linux_runtime::sqlite_database_path());
    s_track_catalog = std::make_unique<::platform::linux_runtime::HostlinkTrackCatalog>(::platform::linux_runtime::resolve_paths().sd_root);
    s_track_snapshots = std::make_unique<::platform::linux_runtime::HostlinkTrackSnapshots>(::platform::linux_runtime::resolve_paths().sd_root);
    s_uploads = std::make_unique<::platform::linux_runtime::HostlinkUploads>(
        ::platform::linux_runtime::resolve_paths().sd_root, ::platform::linux_runtime::sqlite_database_path());
    s_track_prepare = std::make_unique<::platform::linux_runtime::HostlinkTrackPrepare>(
        ::platform::linux_runtime::resolve_paths().sd_root, ::platform::linux_runtime::sqlite_database_path(), *s_uploads);
    s_track_confirmation = std::make_unique<::platform::linux_runtime::HostlinkTrackConfirmation>(
        ::platform::linux_runtime::resolve_paths().sd_root, ::platform::linux_runtime::sqlite_database_path(), *s_uploads);
    s_packages = std::make_unique<::platform::linux_runtime::HostlinkPackageInstall>(
        ::platform::linux_runtime::resolve_paths().sd_root, ::platform::linux_runtime::sqlite_database_path(), *s_uploads);

    while (s_running.load())
    {
        SocketHandle active_client = kInvalidSocket;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            active_client = s_client_socket;
        }

        if (active_client == kInvalidSocket)
        {
            if (s_track_prepare) s_track_prepare->advance(upload_now_ms(), false);
            if (s_track_confirmation) s_track_confirmation->advance(upload_now_ms(), false);
            if (s_track_snapshots) s_track_snapshots->advance(upload_now_ms(), false);
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
    if (s_track_prepare) s_track_prepare->advance(upload_now_ms(), false);
    s_track_prepare.reset();
    if (s_track_confirmation) s_track_confirmation->advance(upload_now_ms(), false);
    s_track_confirmation.reset();
    if (s_packages) s_packages->advance(upload_now_ms(), false);
    s_packages.reset();
    if (s_uploads) s_uploads->advance(upload_now_ms(), false);
    s_uploads.reset();
    if (s_config_snapshot) s_config_snapshot->close();
    s_config_snapshot.reset();
    s_config_transfer.reset();
    s_identity_backup.reset();
    s_radio_identity.reset();
    if (s_track_directories) s_track_directories->close();
    s_track_directories.reset();
    if (s_track_publication) s_track_publication->close();
    s_track_publication.reset();
    if (s_track_read) s_track_read->close();
    s_track_read.reset();
    if (s_geocaching_drafts) s_geocaching_drafts->close();
    s_geocaching_drafts.reset();
    s_track_catalog.reset();
    if (s_track_snapshots) s_track_snapshots->advance(upload_now_ms(), false);
    s_track_snapshots.reset();
    clear_endpoint_file();
}

} // namespace

bool is_supported()
{
    return ensure_hostlink_dir();
}

void start()
{
    std::lock_guard<std::mutex> lifecycle(s_lifecycle_mutex);
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

    try
    {
        s_worker = std::thread(worker_main);
    }
    catch (...)
    {
        s_running = false;
        set_error_state(10004U);
    }
}

void stop()
{
    std::lock_guard<std::mutex> lifecycle(s_lifecycle_mutex);
    s_running = false;
    // The worker owns its descriptors. Its bounded poll observes cancellation;
    // closing them here could race select/accept and reuse an unrelated fd.
    if (s_worker.joinable())
    {
        s_worker.join();
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    ::hostlink::stop_session(s_runtime);
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
