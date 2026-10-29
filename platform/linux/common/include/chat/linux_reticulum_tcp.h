#pragma once
#include "chat/domain/reticulum_network_config.h"
#include "chat/linux_reticulum_ip_frame.h"
#include <array>
#include <memory>

namespace trailmate::linux_app
{
// Serialized protocol-worker port. No UI calls, interface-name assumptions or
// implicit endpoint selection. A connection generation pins queued packets to
// the connection which authenticated/received them.
class LinuxReticulumTcp
{
  public:
    struct Options
    {
        uint32_t connect_timeout_ms = 5000, max_pending_ms = 5000;
        size_t read_byte_budget = 4096, write_byte_budget = 4096;
    };
    LinuxReticulumTcp();
    explicit LinuxReticulumTcp(Options);
    ~LinuxReticulumTcp();
    LinuxReticulumTcp(const LinuxReticulumTcp&) = delete;
    LinuxReticulumTcp& operator=(const LinuxReticulumTcp&) = delete;
    // Validates the complete update before retiring changed connections.
    // Non-TCP entries belong to other interface owners.
    bool configure(const ::chat::reticulum::ReticulumNetworkConfig&);
    void tick(uint32_t now_ms);
    bool send(const char* interface_id, uint64_t connection_generation, const uint8_t*, size_t);
    using Frame = LinuxReticulumIpFrame;
    bool receive(Frame&);
    bool online(const char* interface_id, uint64_t connection_generation) const;
    void clear();
    enum class State : uint8_t
    {
        Disabled,
        Waiting,
        Connecting,
        Connected
    };
    struct Endpoint
    {
        char id[::chat::reticulum::kInterfaceIdMaxLen + 1]{};
        State state = State::Disabled;
        uint64_t generation = 0, connections = 0, failures = 0;
        uint64_t received = 0, transmitted = 0, rejected = 0, discarded_tx = 0;
        size_t pending_tx = 0;
        char error[96]{};
    };
    struct Snapshot
    {
        bool supported = false;
        size_t count = 0;
        uint64_t discarded_rx = 0;
        std::array<Endpoint, ::chat::reticulum::kMaxTcpClientInterfaces> endpoints{};
        char error[96]{};
    };
    Snapshot snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trailmate::linux_app
