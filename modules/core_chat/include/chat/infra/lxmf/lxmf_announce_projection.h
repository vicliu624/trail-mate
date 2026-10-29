#pragma once
#include "chat/infra/lxmf/lxmf_destination_registry.h"
#include "chat/infra/lxmf/lxmf_transport_runtime.h"

namespace chat::lxmf::runtime
{
struct AnnounceIngestResult
{
    enum class Status
    {
        Rejected,
        Ignored,
        Accepted,
    };

    Status status = Status::Rejected;
    const char* reason = "invalid";
    reticulum::ParsedAnnounce announce{};
    uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t expected_destination_hash[reticulum::kTruncatedHashSize] = {};
    bool local_destination = false;
    LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
    PathAnnounceDecision path_decision = PathAnnounceDecision::RejectReplay;
    PathEntry* path = nullptr;
    bool delivery_announce = false;
    bool propagation_announce = false;
    bool call_audio_announce = false;
    bool lxst_telephony_announce = false;
    bool nomad_node_announce = false;
    bool contact_announce = false;
    bool packet_has_ratchet = false;
    char display_name[32] = {};
    PeerInfo* learned_peer = nullptr;
    bool identity_changed = false;
    bool ratchet_changed = false;
    bool display_changed = false;
    bool address_refresh_due = false;
    bool should_store_address = false;
};
struct VerifiedAnnounceProjectionOptions
{
    uint32_t now_s = 0;
    uint32_t directory_address_refresh_interval_s = 0;
    bool wifi_gateway = false;
    std::size_t max_destinations = 0; // Zero preserves the legacy registry policy.
};
namespace detail
{
// Internal stage: caller must have verified the signature/destination binding
// and admitted its path. This function does not perform I/O or grant trust.
void projectVerifiedAnnounce(const reticulum::ParsedPacket&, AnnounceIngestResult&, DestinationRegistry&, const VerifiedAnnounceProjectionOptions&);
} // namespace detail
} // namespace chat::lxmf::runtime
