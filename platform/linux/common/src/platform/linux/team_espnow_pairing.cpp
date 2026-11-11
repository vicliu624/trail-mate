#include "platform/linux/team_espnow_pairing.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace platform::linux_runtime
{
namespace
{
constexpr const char* kSocketPath = "/run/trailmate-espnow/control";
constexpr size_t kMaxPayload = 250;
} // namespace

LinuxEspNowPairing::LinuxEspNowPairing(team::ITeamRuntime& runtime, team::ITeamPairingEventSink& events)
    : core_(runtime, events, *this)
{
}

LinuxEspNowPairing::~LinuxEspNowPairing()
{
    end();
}

bool LinuxEspNowPairing::available()
{
    return ::access(kSocketPath, R_OK | W_OK) == 0;
}

bool LinuxEspNowPairing::begin(team::ITeamPairingTransport::Receiver&)
{
    if (socket_ >= 0) return true;
    socket_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (socket_ < 0) return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, kSocketPath, sizeof(address.sun_path) - 1);
    if (::connect(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        end();
        return false;
    }
    std::array<uint8_t, 3> ready{};
    size_t count = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (count < ready.size() && std::chrono::steady_clock::now() < deadline)
    {
        pollfd item{socket_, POLLIN, 0};
        if (::poll(&item, 1, 100) < 0 || item.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (!(item.revents & POLLIN)) continue;
        const auto size = ::recv(socket_, ready.data() + count, ready.size() - count, 0);
        if (size <= 0) break;
        count += static_cast<size_t>(size);
    }
    if (count != 3 || ready != std::array<uint8_t, 3>{1, 0, 'R'})
    {
        end();
        return false;
    }
    return true;
}

void LinuxEspNowPairing::end()
{
    if (socket_ >= 0)
    {
        ::shutdown(socket_, SHUT_RDWR);
        ::close(socket_);
        socket_ = -1;
    }
    pending_size_ = 0;
}

bool LinuxEspNowPairing::ensurePeer(const uint8_t* mac)
{
    return socket_ >= 0 && mac != nullptr;
}

bool LinuxEspNowPairing::send(const uint8_t* mac, const uint8_t* data, size_t len)
{
    if (socket_ < 0 || !mac || !data || !len || len > kMaxPayload) return false;
    std::array<uint8_t, kMaxPayload + 9> frame{};
    const size_t body_size = 7 + len;
    frame[0] = static_cast<uint8_t>(body_size);
    frame[1] = static_cast<uint8_t>(body_size >> 8);
    frame[2] = 'D';
    std::copy(mac, mac + 6, frame.begin() + 3);
    std::copy(data, data + len, frame.begin() + 9);
    const auto size = ::send(socket_, frame.data(), len + 9, MSG_NOSIGNAL);
    if (size != static_cast<ssize_t>(len + 9))
    {
        end();
        return false;
    }
    return true;
}

bool LinuxEspNowPairing::startLeader(const team::TeamId& id, uint32_t key_id, const uint8_t* psk,
                                     size_t psk_len, uint32_t leader_id, const char* name)
{
    if (!begin(*this)) return false;
    if (core_.startLeader(id, key_id, psk, psk_len, leader_id, name)) return true;
    end();
    return false;
}

bool LinuxEspNowPairing::startMember(uint32_t self_id)
{
    if (!begin(*this)) return false;
    if (core_.startMember(self_id)) return true;
    end();
    return false;
}

void LinuxEspNowPairing::stop()
{
    core_.stop();
    end();
}

team::TeamPairingStatus LinuxEspNowPairing::getStatus() const
{
    return core_.getStatus();
}

void LinuxEspNowPairing::onPairingReceive(const uint8_t* mac, const uint8_t* data, size_t len)
{
    core_.handleIncomingPacket(mac, data, len);
}

void LinuxEspNowPairing::update()
{
    if (socket_ < 0)
    {
        if (core_.getStatus().role != team::TeamPairingRole::None) core_.stop();
        return;
    }
    for (unsigned received = 0; received < 32 && socket_ >= 0; ++received)
    {
        pollfd item{socket_, POLLIN, 0};
        if (::poll(&item, 1, 0) <= 0) break;
        if (item.revents & (POLLERR | POLLNVAL) || pending_size_ == pending_.size())
        {
            stop();
            return;
        }
        const auto size = ::recv(socket_, pending_.data() + pending_size_, pending_.size() - pending_size_, 0);
        if (size <= 0)
        {
            stop();
            return;
        }
        pending_size_ += static_cast<size_t>(size);
        while (pending_size_ >= 2)
        {
            const size_t body_size = pending_[0] | (static_cast<size_t>(pending_[1]) << 8);
            if (body_size < 8 || body_size > kMaxPayload + 7)
            {
                stop();
                return;
            }
            const size_t frame_size = body_size + 2;
            if (pending_size_ < frame_size) break;
            if (pending_[2] != 'D')
            {
                stop();
                return;
            }
            onPairingReceive(pending_.data() + 3, pending_.data() + 9, body_size - 7);
            // The shared coordinator can finish on receipt of Key. It does
            // not close this socket; consume the packet before ending below.
            pending_size_ -= frame_size;
            std::memmove(pending_.data(), pending_.data() + frame_size, pending_size_);
        }
    }
    core_.update();
    if (core_.getStatus().state == team::TeamPairingState::Idle) end();
}

} // namespace platform::linux_runtime
