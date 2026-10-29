#pragma once
#include "chat/linux_reticulum_ip_frame.h"
#include <memory>

namespace trailmate::linux_app
{
class LinuxReticulumAuto
{
  public:
    static constexpr size_t kMaxPeers = 16, kMaxLocalInterfaces = 8;
    using Address = std::array<uint8_t, 16>;
    using Frame = LinuxReticulumIpFrame;
    LinuxReticulumAuto();
    ~LinuxReticulumAuto();
    LinuxReticulumAuto(const LinuxReticulumAuto&) = delete;
    LinuxReticulumAuto& operator=(const LinuxReticulumAuto&) = delete;
    bool configure(const chat::reticulum::ReticulumNetworkConfig&);
    bool configuredAs(const chat::reticulum::ReticulumNetworkConfig&) const;
    void tick(uint32_t now_ms);
    bool send(const char* id, uint64_t generation, const uint8_t*, size_t);
    bool receive(Frame&);
    bool online(const char* id, uint64_t generation) const;
    void clear();
    // Wire helpers share the production SHA-256 and canonical IPv6 text rules.
    static bool discoveryAddress(const char* group, Address&);
    static bool discoveryToken(const char* group, const Address&, std::array<uint8_t, 32>&);
    struct Peer
    {
        char id[chat::reticulum::kInterfaceIdMaxLen + 1]{};
        uint64_t generation = 0;
        uint32_t scope = 0;
        Address address{};
    };
    struct Snapshot
    {
        bool supported = false;
        size_t local_interfaces = 0, count = 0;
        uint64_t discovered = 0, expired = 0, received = 0, transmitted = 0;
        uint64_t rejected = 0, duplicate_rx = 0, discarded_rx = 0, peer_limit = 0;
        std::array<Peer, kMaxPeers> peers{};
        char error[96]{};
    };
    Snapshot snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trailmate::linux_app
