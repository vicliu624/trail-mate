#pragma once
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace tcp_test
{
// Fork before endpoint threads start. The parent only forwards bytes and
// supervises both children; endpoint framing and protocol work remain real.
template <class Endpoint>
bool pair(Endpoint endpoint)
{
    const int listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    assert(listener >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && listen(listener, 2) == 0);
    socklen_t size = sizeof(address);
    assert(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0);
    pid_t children[2];
    for (unsigned side = 0; side < 2; ++side)
    {
        children[side] = fork();
        assert(children[side] >= 0);
        if (!children[side])
        {
            close(listener);
            std::exit(endpoint(side == 0, ntohs(address.sin_port)));
        }
    }
    int peers[2] = {-1, -1};
    bool ended[2]{}, eof[2]{}, shutdown_sent[2]{};
    std::vector<uint8_t> pending[2];
    unsigned accepted = 0;
    bool passed = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!ended[0] || !ended[1])
    {
        if (accepted < 2)
        {
            const int peer = accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (peer >= 0) peers[accepted++] = peer;
            else if (errno != EAGAIN && errno != EWOULDBLOCK) passed = false;
        }
        for (unsigned side = 0; side < accepted; ++side)
        {
            uint8_t bytes[4096];
            const auto read = recv(peers[side], bytes, sizeof(bytes), MSG_DONTWAIT);
            if (read > 0) pending[1 - side].insert(pending[1 - side].end(), bytes, bytes + read);
            else if (read == 0 || (read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) eof[side] = true;
            if (!pending[side].empty())
            {
                const auto sent = ::send(peers[side], pending[side].data(), pending[side].size(), MSG_DONTWAIT | MSG_NOSIGNAL);
                if (sent > 0) pending[side].erase(pending[side].begin(), pending[side].begin() + sent);
                else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    pending[side].clear();
                    eof[side] = true;
                }
            }
            if (eof[1 - side] && pending[side].empty() && !shutdown_sent[side])
            {
                shutdown(peers[side], SHUT_WR);
                shutdown_sent[side] = true;
            }
        }
        for (unsigned side = 0; side < 2; ++side)
            if (!ended[side])
            {
                int status;
                const auto child = waitpid(children[side], &status, WNOHANG);
                if (child == children[side])
                {
                    ended[side] = true;
                    passed &= WIFEXITED(status) && WEXITSTATUS(status) == 0;
                }
            }
        if (std::chrono::steady_clock::now() >= deadline || pending[0].size() > 65536 || pending[1].size() > 65536)
        {
            passed = false;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (unsigned side = 0; side < 2; ++side)
    {
        if (!ended[side])
        {
            kill(children[side], SIGKILL);
            int status;
            (void)waitpid(children[side], &status, 0);
        }
        if (peers[side] >= 0) close(peers[side]);
    }
    close(listener);
    return passed;
}
} // namespace tcp_test
