#pragma once
#include "chat/domain/reticulum_network_config.h"
#include "chat/infra/lxmf/lxmf_propagation_runtime.h"
#include "chat/linux_reticulum_ingress.h"
#include "chat/linux_reticulum_links.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>

namespace trailmate::linux_app
{
// Serialized mesh-worker state shared by the IP and integrated-radio bearers.
class LinuxReticulumPropagation
{
  public:
    explicit LinuxReticulumPropagation(::chat::IMeshPeerDirectory& directory) : directory_(directory) {}
    void reset()
    {
        runtime_ = {};
        sightings_.clear();
        selected_.reset();
        active_.reset();
        sync_configured_ = false;
    }
    void observe(const LinuxReticulumIngress::PropagationAnnounce& event)
    {
        if (!event.data.valid || !permitted(event.delivery.data(), event.identity.data())) return;
        auto found = std::find_if(sightings_.begin(), sightings_.end(), [&](const Sighting& item)
                                  { return item.destination == event.destination; });
        if (found == sightings_.end())
        {
            if (sightings_.size() >= kMaxPeers)
            {
                // Keep the shared state and monotonic freshness table bounded together.
                const auto oldest = sightings_.front().destination;
                runtime_.peers.erase(std::remove_if(runtime_.peers.begin(), runtime_.peers.end(), [&](const auto& peer)
                                                    { return !std::memcmp(peer.propagation_hash, oldest.data(), oldest.size()); }),
                                     runtime_.peers.end());
                sightings_.erase(sightings_.begin());
            }
            sightings_.push_back({event.destination, event.received_ms});
        }
        else found->received_ms = event.received_ms;
        auto& peer = ::chat::lxmf::runtime::upsertPropagationPeer(runtime_, event.destination.data(), event.delivery.data(), event.identity.data(), kMaxPeers);
        std::memcpy(peer.enc_pub, event.public_key.data(), sizeof(peer.enc_pub));
        std::memcpy(peer.sig_pub, event.public_key.data() + sizeof(peer.enc_pub), sizeof(peer.sig_pub));
        peer.node_active = event.data.node_active;
        peer.hops = event.hops;
        peer.last_seen_s = event.received_s;
        peer.announce_timebase_s = event.data.timebase_s;
        peer.transfer_limit_kb = event.data.transfer_limit_kb;
        peer.sync_limit_kb = event.data.sync_limit_kb;
        peer.stamp_cost = event.data.stamp_cost;
        peer.stamp_cost_flexibility = event.data.stamp_cost_flexibility;
        peer.peering_cost = event.data.peering_cost;
        std::snprintf(peer.display_name, sizeof(peer.display_name), "%s", event.data.display_name.c_str());
    }
    const ::chat::lxmf::runtime::PropagationPeerState* select(const ::chat::reticulum::LxmfPropagationClientConfig& config,
                                                              bool permitted_to_transmit, uint32_t now_ms)
    {
        selected_.reset();
        if (!config.enabled || !permitted_to_transmit) return nullptr;
        const ::chat::lxmf::runtime::PropagationPeerState* best = nullptr;
        uint32_t best_age = 0;
        for (const auto& peer : runtime_.peers)
        {
            const auto found = std::find_if(sightings_.begin(), sightings_.end(), [&](const Sighting& item)
                                            { return !std::memcmp(peer.propagation_hash, item.destination.data(), item.destination.size()); });
            if (found == sightings_.end()) continue;
            const uint32_t age = now_ms - found->received_ms;
            if (age > kPeerTtlMs || !peer.node_active || !permitted(peer.delivery_hash, peer.identity_hash)) continue;
            if (!config.automatic_node && std::memcmp(peer.propagation_hash, config.node_hash, sizeof(config.node_hash))) continue;
            if (!best || peer.hops < best->hops || (peer.hops == best->hops && age < best_age))
            {
                best = &peer;
                best_age = age;
            }
        }
        if (best) selected_ = *best;
        return selected_ ? &*selected_ : nullptr;
    }
    const ::chat::lxmf::runtime::PropagationPeerState* selected() const { return selected_ ? &*selected_ : nullptr; }
    class Transport
    {
      public:
        virtual ~Transport() = default;
        virtual LinuxReticulumLinks::OpenResult open(const ::chat::lxmf::runtime::PropagationPeerState&) = 0;
        virtual LinuxReticulumLinks::ServiceRequestResult request(const uint8_t destination[16], const uint8_t path[16],
                                                                  const uint8_t*, size_t, double timestamp, uint8_t request_id[16]) = 0;
        virtual bool take(const uint8_t destination[16], const uint8_t request_id[16], LinuxReticulumLinks::ServiceResponse&) = 0;
        virtual bool pending(const uint8_t destination[16], const uint8_t request_id[16]) = 0;
        virtual void cancel(const uint8_t destination[16]) = 0;
        virtual void discover(const uint8_t destination[16]) {}
        virtual bool decode(const uint8_t*, size_t, ::chat::MeshIncomingText&) = 0;
        virtual bool deliver(const ::chat::MeshIncomingText&) = 0;
    };
    void tick(const ::chat::reticulum::LxmfPropagationClientConfig&, bool allowed, uint32_t now_ms, uint32_t utc, Transport&);
    bool commit(const ::chat::MeshIncomingText&, bool durable, uint32_t utc);
    ::chat::lxmf::runtime::PropagationSyncStage syncStage() const { return runtime_.sync_stage; }

  private:
    struct Sighting
    {
        std::array<uint8_t, 16> destination;
        uint32_t received_ms;
    };
    static constexpr size_t kMaxPeers = 32;
    static constexpr uint32_t kPeerTtlMs = 24U * 60U * 60U * 1000U;
    bool permitted(const uint8_t* delivery, const uint8_t* identity) const
    {
        ::chat::MeshPeerRecord record;
        const auto result = directory_.find(::chat::makeMeshPeerReticulumIdentity(::chat::makeReticulumPeerIdentity(delivery, identity)), record);
        return result.succeeded() ? !record.flags.ignored : result.code == ::chat::MeshPeerDirectoryStatusCode::NotFound;
    }
    ::chat::IMeshPeerDirectory& directory_;
    ::chat::lxmf::runtime::PropagationRuntime runtime_;
    std::vector<Sighting> sightings_;
    std::optional<::chat::lxmf::runtime::PropagationPeerState> selected_;
    std::optional<::chat::lxmf::runtime::PropagationPeerState> active_;
    ::chat::reticulum::LxmfPropagationClientConfig sync_config_;
    bool sync_configured_ = false;
    uint32_t request_started_ms_ = 0;
    bool sendGet(Transport&, ::chat::lxmf::runtime::PropagationSyncStage, uint32_t, uint32_t);
    void finish(Transport&, uint32_t utc, bool success);
};
} // namespace trailmate::linux_app
