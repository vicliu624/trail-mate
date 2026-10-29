#pragma once
#include "chat/infra/lxmf/lxmf_announce_projection.h"
#include "chat/infra/lxmf/lxmf_packet_router.h"
#include "chat/infra/lxmf/lxmf_path_manager.h"
#include "chat/infra/lxmf/lxmf_peer_directory.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/infra/reticulum/announce_verifier.h"
#include "chat/ports/track_delivery_binding.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include <array>
#include <memory>

namespace trailmate::linux_app
{
// Mesh-worker-owned path control. It learns authenticated announce paths;
// identity/contact projection, links and LXMF delivery remain separate owners.
class LinuxReticulumIngress
{
  public:
    struct Options
    {
        size_t paths = 64, packet_filter = 128;
        size_t peers = 64;
        uint32_t path_ttl_ms = 7UL * 24 * 60 * 60 * 1000, packet_ttl_ms = 30000;
        size_t pending_paths = 32;
        uint32_t request_interval_ms = 20000, request_ttl_ms = 45000;
        uint8_t max_hops = 128, interface_id = 1; // Logical interface handle, not a Linux device index.
    };
    struct Statistics
    {
        uint64_t accepted = 0, rejected = 0, replays = 0, ignored = 0;
        uint64_t requests_sent = 0, request_errors = 0, responses = 0;
        size_t paths = 0;
        size_t peers = 0;
        uint64_t directory_errors = 0, projection_errors = 0;
    };
    explicit LinuxReticulumIngress(const platform::linux_runtime::LinuxReticulumIdentity& identity);
    LinuxReticulumIngress(const platform::linux_runtime::LinuxReticulumIdentity&, Options, ::chat::IMeshPeerDirectory* = nullptr);
    void setPeerProjectionSink(::chat::lxmf::runtime::IPeerProjectionSink* sink) { peer_sink_ = sink; }
    struct PropagationAnnounce
    {
        std::array<uint8_t, 16> destination{}, delivery{}, identity{};
        std::array<uint8_t, 64> public_key{};
        ::chat::lxmf::DecodedPropagationAnnounce data;
        uint32_t received_ms = 0, received_s = 0;
        uint8_t hops = 0, interface_id = 0;
    };
    // Synchronous, worker-owned callback. The event owns all decoded data.
    using PropagationObserver = void (*)(void*, const PropagationAnnounce&);
    void setPropagationObserver(PropagationObserver observer, void* context)
    {
        propagation_observer_ = observer;
        propagation_context_ = context;
    }
    using TrackDirectoryObserver = bool (*)(void*, const ::chat::lxmf::TrackDirectoryAnnouncementView&);
    void setTrackDirectoryObserver(TrackDirectoryObserver observer, void* context)
    {
        track_directory_observer_ = observer;
        track_directory_context_ = context;
    }
    const ::chat::lxmf::runtime::PeerInfo* peer(const uint8_t destination[16]) const;
    // Caller must have authenticated a service announce or LinkIdentify signature.
    bool rememberAuthenticatedPeer(const uint8_t public_key[64], uint32_t now_s);
    bool receive(const uint8_t*, size_t, uint32_t now_ms, uint32_t now_s, uint8_t interface_id = 0);
    void retireInterface(uint8_t interface_id) { paths_.retireInterface(interface_id); }
    template <class Visitor>
    void forEachPath(Visitor&& visitor) const { paths_.forEachPath(visitor); }
    enum class RequestResult
    {
        Sent,
        CoolingDown,
        Invalid,
        NotReady,
        TxFailed
    };
    using PacketSender = bool (*)(void*, const uint8_t*, size_t);
    RequestResult requestPath(const uint8_t destination[16], const uint8_t tag[16], uint32_t now_ms, PacketSender, void*);
    bool pendingPath(const uint8_t destination[16]) const;
    void tick(uint32_t now_ms);
    void reset();
    Statistics statistics() const { return stats_; }
    const ::chat::lxmf::runtime::PathEntry* path(const uint8_t hash[16], uint32_t now_ms) const;

  private:
    void observeTrackDirectory(const ::chat::reticulum::ParsedAnnounce&, const uint8_t identity[16], const uint8_t destination[16], uint32_t now_s);
    const platform::linux_runtime::LinuxReticulumIdentity& identity_;
    Options options_;
    Statistics stats_;
    ::chat::reticulum::AnnounceVerifier verifier_;
    ::chat::lxmf::runtime::PathManager paths_;
    ::chat::lxmf::runtime::ReticulumPacketRouter router_;
    ::chat::lxmf::runtime::DestinationRegistry destinations_;
    ::chat::lxmf::runtime::AnnounceIngestResult projection_;
    ::chat::IMeshPeerDirectory* directory_ = nullptr;
    std::unique_ptr<::chat::lxmf::runtime::PeerDirectoryService> directory_service_;
    ::chat::lxmf::runtime::IPeerProjectionSink* peer_sink_ = nullptr;
    PropagationObserver propagation_observer_ = nullptr;
    TrackDirectoryObserver track_directory_observer_ = nullptr;
    void* track_directory_context_ = nullptr;
    void* propagation_context_ = nullptr;
};
} // namespace trailmate::linux_app
