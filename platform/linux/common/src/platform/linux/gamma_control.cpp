#include "platform/linux/gamma_control.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace platform::linux_runtime
{
namespace
{
using Bytes = std::vector<uint8_t>;
using Clock = std::chrono::steady_clock;
void append(Bytes& bytes, uint32_t value)
{
    const auto* data = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), data, data + sizeof(value));
}
uint32_t number(const Bytes& bytes, size_t offset)
{
    if (offset + 4 > bytes.size()) throw std::runtime_error("Truncated Wayland event");
    uint32_t value;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
std::string string(const Bytes& bytes, size_t offset)
{
    const auto length = number(bytes, offset);
    if (!length || length > 1024 || offset + 4 + length > bytes.size() || bytes[offset + 3 + length])
        throw std::runtime_error("Invalid Wayland string");
    return {reinterpret_cast<const char*>(bytes.data() + offset + 4), length - 1};
}
} // namespace

struct GammaControl::Impl
{
    struct Output
    {
        uint32_t global, version, object;
        std::string name;
    };
    int fd = -1;
    uint32_t manager = 0, size = 0, next_id = 4;
    bool failed = false, claimed = false;
    std::vector<Output> outputs;
    Bytes buffer;
    Clock::time_point deadline;
    const std::atomic_bool* cancel = nullptr;

    ~Impl() { disconnect(); }
    void disconnect()
    {
        // Wayland destroys all client-owned resources on disconnect, including
        // gamma control. This also covers failed/partial handshakes.
        if (fd >= 0) ::close(fd);
        fd = -1;
        outputs.clear();
        buffer.clear();
        manager = size = 0;
        next_id = 4;
        failed = claimed = false;
    }
    void begin(const std::atomic_bool& stopping)
    {
        cancel = &stopping;
        deadline = Clock::now() + std::chrono::seconds(2);
    }
    void ready(short events)
    {
        while (true)
        {
            if (cancel->load() || Clock::now() >= deadline) throw std::runtime_error("Wayland operation cancelled or timed out");
            pollfd descriptor{fd, events, 0};
            const int result = poll(&descriptor, 1, 25);
            if (result < 0 && errno == EINTR) continue;
            if (result < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) throw std::runtime_error("Wayland disconnected");
            if (result > 0 && (descriptor.revents & events)) return;
        }
    }
    void send(uint32_t object, uint16_t opcode, const Bytes& payload = {}, int passed_fd = -1)
    {
        Bytes bytes;
        append(bytes, object);
        append(bytes, (static_cast<uint32_t>(8 + payload.size()) << 16) | opcode);
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        size_t offset = 0;
        while (offset < bytes.size())
        {
            ready(POLLOUT);
            iovec part{bytes.data() + offset, bytes.size() - offset};
            msghdr message{};
            message.msg_iov = &part;
            message.msg_iovlen = 1;
            alignas(cmsghdr) std::array<char, CMSG_SPACE(sizeof(int))> control{};
            if (passed_fd >= 0)
            {
                message.msg_control = control.data();
                message.msg_controllen = control.size();
                auto* header = CMSG_FIRSTHDR(&message);
                header->cmsg_level = SOL_SOCKET;
                header->cmsg_type = SCM_RIGHTS;
                header->cmsg_len = CMSG_LEN(sizeof(int));
                std::memcpy(CMSG_DATA(header), &passed_fd, sizeof(int));
            }
            const auto count = sendmsg(fd, &message, MSG_NOSIGNAL);
            if (count < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            if (count <= 0) throw std::runtime_error("Wayland write failed");
            passed_fd = -1; // SCM_RIGHTS must only accompany the first bytes.
            offset += static_cast<size_t>(count);
        }
    }
    void bind(uint32_t global, const std::string& interface, uint32_t version, uint32_t object)
    {
        Bytes payload;
        append(payload, global);
        append(payload, static_cast<uint32_t>(interface.size() + 1));
        payload.insert(payload.end(), interface.begin(), interface.end());
        payload.push_back(0);
        while (payload.size() % 4) payload.push_back(0);
        append(payload, version);
        append(payload, object);
        send(2, 0, payload);
    }
    void event(uint32_t object, uint16_t opcode, const Bytes& payload)
    {
        if (object == 1 && opcode == 0) throw std::runtime_error("Wayland protocol error");
        if (object == 2 && opcode == 0)
        {
            const auto interface = string(payload, 4);
            const auto version = number(payload, 8 + ((interface.size() + 4) / 4) * 4);
            if (interface == "zwlr_gamma_control_manager_v1")
            {
                if (manager) throw std::runtime_error("Ambiguous gamma manager");
                manager = number(payload, 0);
            }
            if (interface == "wl_output")
            {
                if (claimed) throw std::runtime_error("Wayland output topology changed");
                if (outputs.size() >= 64) throw std::runtime_error("Too many Wayland outputs");
                outputs.push_back({number(payload, 0), std::min(version, 4U), next_id++, {}});
            }
        }
        if (object == 2 && opcode == 1)
        {
            const auto removed = number(payload, 0);
            if (removed == manager || std::any_of(outputs.begin(), outputs.end(), [removed](const auto& output)
                                                  { return output.global == removed; }))
                throw std::runtime_error("Wayland output topology changed");
        }
        if (object == 3)
        {
            if (opcode == 0) size = number(payload, 0);
            if (opcode == 1) failed = true;
        }
        for (auto& output : outputs)
            if (object == output.object && opcode == 4) output.name = string(payload, 0);
    }
    void roundtrip()
    {
        const auto callback = next_id++;
        Bytes payload;
        append(payload, callback);
        send(1, 0, payload);
        while (true)
        {
            while (buffer.size() >= 8)
            {
                const auto object = number(buffer, 0), header = number(buffer, 4);
                const auto length = header >> 16;
                if (length < 8 || length % 4) throw std::runtime_error("Malformed Wayland event");
                if (buffer.size() < length) break;
                Bytes body(buffer.begin() + 8, buffer.begin() + length);
                buffer.erase(buffer.begin(), buffer.begin() + length);
                event(object, header & 65535, body);
                if (object == callback && (header & 65535) == 0) return;
            }
            ready(POLLIN);
            std::array<uint8_t, 4096> incoming{};
            const auto count = recv(fd, incoming.data(), incoming.size(), 0);
            if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            if (count <= 0 || buffer.size() + count > 131072) throw std::runtime_error("Wayland read failed");
            buffer.insert(buffer.end(), incoming.begin(), incoming.begin() + count);
        }
    }
};

GammaControl::GammaControl() : impl_(std::make_unique<Impl>()) {}
GammaControl::~GammaControl() = default;
void GammaControl::close() { impl_->disconnect(); }
GammaControlStatus GammaControl::connect(const GammaControlOptions& options, const std::atomic_bool& cancel)
{
    close();
    auto& state = *impl_;
    try
    {
        state.begin(cancel);
        auto path = options.socket;
        if (path.empty())
        {
            const char* display = std::getenv("WAYLAND_DISPLAY");
            const char* runtime = std::getenv("XDG_RUNTIME_DIR");
            if (!display || !*display) return GammaControlStatus::Unavailable;
            path = display;
            if (!path.is_absolute())
            {
                if (!runtime || !std::filesystem::path(runtime).is_absolute()) return GammaControlStatus::Unavailable;
                path = std::filesystem::path(runtime) / path;
            }
        }
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const auto native = path.string();
        if (!path.is_absolute() || native.size() >= sizeof(address.sun_path)) return GammaControlStatus::Unavailable;
        std::memcpy(address.sun_path, native.c_str(), native.size() + 1);
        state.fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (state.fd < 0) throw std::runtime_error("Cannot create Wayland socket");
        if (::connect(state.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
        {
            if (errno != EINPROGRESS) throw std::runtime_error("Cannot connect Wayland socket");
            state.ready(POLLOUT);
            int error = 0;
            socklen_t length = sizeof(error);
            if (getsockopt(state.fd, SOL_SOCKET, SO_ERROR, &error, &length) || error) throw std::runtime_error("Wayland connect failed");
        }
        Bytes registry;
        append(registry, 2);
        state.send(1, 1, registry);
        state.roundtrip();
        if (!state.manager || state.outputs.empty())
        {
            close();
            return GammaControlStatus::Unavailable;
        }
        // The initial sync can precede the registry events. Bind first, then
        // round-trip again to collect wl_output.name (introduced in version 4).
        const auto manager_object = state.next_id++;
        state.bind(state.manager, "zwlr_gamma_control_manager_v1", 1, manager_object);
        for (const auto& output : state.outputs) state.bind(output.global, "wl_output", output.version, output.object);
        state.roundtrip();
        std::vector<uint32_t> matches;
        for (const auto& output : state.outputs)
            if (options.output.empty() || output.name == options.output) matches.push_back(output.object);
        if (matches.size() != 1)
        {
            close();
            return GammaControlStatus::SelectionRequired;
        }
        Bytes claim;
        append(claim, 3);
        append(claim, matches.front());
        state.send(manager_object, 0, claim);
        state.claimed = true;
        state.roundtrip();
        if (state.failed || state.size < 2 || state.size > 65536)
        {
            close();
            return GammaControlStatus::Unavailable;
        }
        return GammaControlStatus::Ready;
    }
    catch (...)
    {
        close();
        return GammaControlStatus::Failed;
    }
}
bool GammaControl::apply(uint8_t level, const std::atomic_bool& cancel)
{
    auto& state = *impl_;
    int ramp_fd = -1;
    try
    {
        if (state.fd < 0 || !state.claimed || state.failed || !state.size) return false;
        state.begin(cancel);
        std::vector<uint16_t> ramp(state.size * 3);
        for (uint32_t i = 0; i < state.size; ++i)
        {
            const uint64_t denominator = uint64_t(state.size - 1) * 255;
            const auto value = static_cast<uint16_t>((uint64_t(i) * 65535 * level + denominator / 2) / denominator);
            for (uint32_t channel = 0; channel < 3; ++channel) ramp[channel * state.size + i] = value;
        }
        ramp_fd = memfd_create("trailmate-gamma", MFD_CLOEXEC);
        if (ramp_fd < 0) throw std::runtime_error("Cannot allocate gamma ramp");
        const auto bytes = ramp.size() * sizeof(uint16_t);
        size_t written = 0;
        while (written < bytes)
        {
            const auto count = write(ramp_fd, reinterpret_cast<const uint8_t*>(ramp.data()) + written, bytes - written);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) throw std::runtime_error("Cannot write gamma ramp");
            written += static_cast<size_t>(count);
        }
        if (lseek(ramp_fd, 0, SEEK_SET) < 0) throw std::runtime_error("Cannot rewind gamma ramp");
        state.send(3, 0, {}, ramp_fd);
        ::close(ramp_fd);
        ramp_fd = -1;
        state.roundtrip();
        if (state.failed) throw std::runtime_error("Compositor rejected gamma ramp");
        return true;
    }
    catch (...)
    {
        if (ramp_fd >= 0) ::close(ramp_fd);
        close();
        return false;
    }
}
bool GammaControl::healthy(const std::atomic_bool& cancel)
{
    try
    {
        if (impl_->fd < 0 || !impl_->claimed) return false;
        impl_->begin(cancel);
        impl_->roundtrip();
        if (impl_->failed) throw std::runtime_error("Gamma control lost");
        return true;
    }
    catch (...)
    {
        close();
        return false;
    }
}
} // namespace platform::linux_runtime
