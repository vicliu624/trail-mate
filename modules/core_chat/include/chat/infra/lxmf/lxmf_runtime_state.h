/**
 * @file lxmf_runtime_state.h
 * @brief Shared runtime state models for the embedded Reticulum/LXMF runtime
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/infra/reticulum/lxst_call_state_machine.h"
#include "chat/infra/lxmf/lxmf_memory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat::lxmf::runtime
{

// X25519 raw private bytes and the existing expanded Ed25519 runtime state.
constexpr std::size_t kEncryptionPrivateKeySize = 32;
constexpr std::size_t kExpandedSigningPrivateKeySize = 64;
constexpr std::size_t kAnnounceRandomBlobSize = 10;
constexpr std::size_t kPathRandomBlobHistory = 4;

struct PeerInfo
{
    uint32_t node_id = 0;
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t enc_pub[reticulum::kEncryptionPublicKeySize] = {};
    uint8_t sig_pub[reticulum::kSigningPublicKeySize] = {};
    uint8_t ratchet_pub[reticulum::kRatchetSize] = {};
    char display_name[32] = {};
    uint32_t last_seen_s = 0;
    uint32_t last_path_request_ms = 0;
    uint32_t ratchet_seen_s = 0;
    bool has_ratchet = false;
};

struct PathEntry
{
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t next_hop_transport[reticulum::kTruncatedHashSize] = {};
    uint8_t cached_packet_hash[reticulum::kFullHashSize] = {};
    uint8_t cached_announce[reticulum::kReticulumMtu] = {};
    size_t cached_announce_len = 0;
    uint8_t announce_random_blobs[kPathRandomBlobHistory][kAnnounceRandomBlobSize] = {};
    uint8_t announce_random_blob_count = 0;
    uint8_t interface_id = 0;
    uint8_t hops = 0;
    uint32_t last_seen_s = 0;
    uint32_t updated_ms = 0;
    uint64_t announce_timebase = 0;
    bool direct = false;
};

using PathEntryList = std::vector<PathEntry, PsramAllocator<PathEntry>>;

struct PacketFilterEntry
{
    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    uint32_t seen_ms = 0;
};

using PacketFilterEntryList =
    std::vector<PacketFilterEntry, PsramAllocator<PacketFilterEntry>>;

struct ReverseEntry
{
    uint8_t proof_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t interface_id = 0;
    uint8_t expected_hops = 0;
    uint32_t created_ms = 0;
};

using ReverseEntryList =
    std::vector<ReverseEntry, PsramAllocator<ReverseEntry>>;

struct PendingPathRequest
{
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint32_t created_ms = 0;
    uint32_t last_attempt_ms = 0;
    uint8_t attempts = 0;
    bool resolved = false;
};

using PendingPathRequestList =
    std::vector<PendingPathRequest, PsramAllocator<PendingPathRequest>>;

struct PendingPingReceipt
{
    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    uint8_t proof_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t peer_sig_pub[reticulum::kSigningPublicKeySize] = {};
    uint32_t created_ms = 0;
};

using PendingPingReceiptList =
    std::vector<PendingPingReceipt, PsramAllocator<PendingPingReceipt>>;

struct LinkRelayEntry
{
    uint8_t link_id[reticulum::kTruncatedHashSize] = {};
    uint8_t initiator_interface_id = 0;
    uint8_t responder_interface_id = 0;
    uint8_t initiator_hops = 0;
    uint8_t responder_hops = 0;
    uint32_t last_seen_ms = 0;
};

using LinkRelayEntryList =
    std::vector<LinkRelayEntry, PsramAllocator<LinkRelayEntry>>;

enum class LocalDestinationKind : uint8_t
{
    Delivery = 0,
    Propagation = 1,
    CallAudio = 2,
    NomadPage = 3
};

enum class LinkState : uint8_t
{
    Pending = 0,
    Handshake = 1,
    Active = 2,
    Stale = 3,
    Closed = 4
};

enum class LinkCloseReason : uint8_t
{
    None = 0,
    LocalClose = 1,
    RemoteClose = 2,
    Timeout = 3,
    Error = 4
};

struct LinkPendingRequest
{
    ResourceMetadataBuffer request_id;
    uint32_t created_ms = 0;
    bool awaiting_resource = false;
    bool response_ready = false;
    ResourcePayloadBuffer response;
};

using LinkPendingRequestList =
    std::vector<LinkPendingRequest, PsramAllocator<LinkPendingRequest>>;

struct DeferredLinkPayload
{
    ResourcePayloadBuffer payload;
    ResourceMetadataBuffer request_id;
    uint32_t message_id = 0;
    uint8_t resource_flags = 0;
};

using DeferredLinkPayloadList =
    std::vector<DeferredLinkPayload, PsramAllocator<DeferredLinkPayload>>;

struct LinkResourceTransfer
{
    uint8_t resource_hash[reticulum::kFullHashSize] = {};
    uint8_t random_hash[4] = {};
    uint8_t original_hash[reticulum::kFullHashSize] = {};
    uint8_t expected_proof[reticulum::kFullHashSize] = {};
    ResourceMetadataBuffer request_id;
    ResourceMetadataBuffer hashmap;
    ResourceMapHashList map_hashes;
    ResourceBitmapBuffer map_hash_known;
    ResourcePayloadList parts;
    ResourceBitmapBuffer received_bitmap;
    uint32_t data_size = 0;
    uint32_t transfer_size = 0;
    uint32_t part_count = 0;
    uint32_t hashmap_height = 0;
    uint32_t next_request_index = 0;
    uint32_t window_size = 4;
    uint32_t segment_index = 1;
    uint32_t total_segments = 1;
    uint32_t created_ms = 0;
    uint32_t last_activity_ms = 0;
    uint8_t flags = 0;
    bool incoming = true;
    bool encrypted = false;
    bool compressed = false;
    bool has_metadata = false;
    bool split = false;
    bool waiting_for_hashmap = false;
    bool complete = false;
    bool waiting_for_proof = false;
    int32_t consecutive_complete_index = -1;
};

using LinkResourceTransferList =
    std::vector<LinkResourceTransfer, PsramAllocator<LinkResourceTransfer>>;

struct LinkResourceAssembly
{
    uint8_t original_hash[reticulum::kFullHashSize] = {};
    ResourceMetadataBuffer request_id;
    ResourcePayloadBuffer payload;
    uint32_t next_segment_index = 1;
    uint32_t total_segments = 1;
    uint32_t last_activity_ms = 0;
    uint8_t flags = 0;
};

using LinkResourceAssemblyList =
    std::vector<LinkResourceAssembly, PsramAllocator<LinkResourceAssembly>>;

struct LinkSession
{
    uint8_t link_id[reticulum::kTruncatedHashSize] = {};
    uint8_t remote_destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t remote_identity_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t local_enc_pub[reticulum::kEncryptionPublicKeySize] = {};
    uint8_t local_enc_priv[kEncryptionPrivateKeySize] = {};
    uint8_t local_sig_pub[reticulum::kSigningPublicKeySize] = {};
    uint8_t local_sig_priv[kExpandedSigningPrivateKeySize] = {};
    uint8_t peer_enc_pub[reticulum::kEncryptionPublicKeySize] = {};
    uint8_t peer_link_sig_pub[reticulum::kSigningPublicKeySize] = {};
    uint8_t peer_identity_sig_pub[reticulum::kSigningPublicKeySize] = {};
    uint8_t derived_key[reticulum::kDerivedTokenKeySize] = {};
    uint32_t created_ms = 0;
    uint32_t request_ms = 0;
    uint32_t last_inbound_ms = 0;
    uint32_t last_outbound_ms = 0;
    uint16_t mtu = reticulum::kReticulumMtu;
    uint16_t mdu = reticulum::kReticulumMdu;
    float rtt_s = 0.0f;
    uint32_t keepalive_interval_ms = 15000;
    uint32_t stale_timeout_ms = 30000;
    uint32_t last_keepalive_ms = 0;
    uint8_t interface_id = 0;
    uint8_t expected_hops = 0;
    bool initiator = false;
    bool local_identity_sent = false;
    bool remote_identity_known = false;
    bool validated = false;
    ReticulumCallWireProfile call_wire_profile =
        ReticulumCallWireProfile::SidebandLxst;
    reticulum::lxst::call::State lxst_call{};
    bool call_runtime_started = false;
    LocalDestinationKind destination = LocalDestinationKind::Delivery;
    LinkState state = LinkState::Pending;
    LinkCloseReason close_reason = LinkCloseReason::None;
    bool propagation_offer_validated = false;
    LinkPendingRequestList pending_requests;
    DeferredLinkPayloadList deferred_payloads;
    LinkResourceTransferList incoming_resources;
    LinkResourceAssemblyList incoming_resource_assemblies;
    LinkResourceTransferList outgoing_resources;
};

using LinkSessionList = std::vector<LinkSession, PsramAllocator<LinkSession>>;

struct PropagationEntry
{
    uint8_t transient_id[reticulum::kFullHashSize] = {};
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    ResourcePayloadBuffer lxmf_data;
    uint32_t created_s = 0;
    uint32_t served_count = 0;
};

using PropagationEntryList =
    std::vector<PropagationEntry, PsramAllocator<PropagationEntry>>;

struct PropagationTransientEntry
{
    uint8_t transient_id[reticulum::kFullHashSize] = {};
    uint32_t seen_s = 0;
    bool delivered = false;
};

using PropagationTransientEntryList =
    std::vector<PropagationTransientEntry,
                PsramAllocator<PropagationTransientEntry>>;

enum class PropagationUploadState : uint8_t
{
    WaitingNode = 0,
    NeedsStamp = 1,
    Stamping = 2,
    Ready = 3,
    QueuedToLink = 4,
    Failed = 5,
};

struct PendingPropagationUpload
{
    uint8_t node_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t message_hash[reticulum::kFullHashSize] = {};
    uint8_t transient_id[reticulum::kFullHashSize] = {};
    ResourcePayloadBuffer transient_data;
    uint32_t created_ms = 0;
    uint8_t stamp_cost = 0;
    PropagationUploadState state = PropagationUploadState::WaitingNode;
};

using PendingPropagationUploadList =
    std::vector<PendingPropagationUpload,
                PsramAllocator<PendingPropagationUpload>>;

enum class PropagationDeliveryCommitState : uint8_t
{
    AwaitingPersistence = 0,
    Accepted = 1,
    Rejected = 2,
};

struct PendingPropagationDelivery
{
    uint8_t transient_id[reticulum::kFullHashSize] = {};
    uint8_t message_hash[reticulum::kFullHashSize] = {};
    PropagationDeliveryCommitState state =
        PropagationDeliveryCommitState::AwaitingPersistence;
};

using PendingPropagationDeliveryList =
    std::vector<PendingPropagationDelivery,
                PsramAllocator<PendingPropagationDelivery>>;

enum class PropagationSyncStage : uint8_t
{
    Idle = 0,
    NeedList = 1,
    Listing = 2,
    NeedMessages = 3,
    Downloading = 4,
    AwaitingPersistence = 5,
    NeedAcknowledge = 6,
    Acknowledging = 7,
    Complete = 8,
    Failed = 9,
};

struct PropagationPeerState
{
    uint8_t propagation_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t enc_pub[reticulum::kEncryptionPublicKeySize] = {};
    uint8_t sig_pub[reticulum::kSigningPublicKeySize] = {};
    char display_name[32] = {};
    uint32_t announce_timebase_s = 0;
    uint32_t last_seen_s = 0;
    uint32_t transfer_limit_kb = 0;
    uint32_t sync_limit_kb = 0;
    uint32_t incoming_messages = 0;
    uint32_t served_messages = 0;
    uint8_t hops = 0;
    uint8_t stamp_cost = 0;
    uint8_t stamp_cost_flexibility = 0;
    uint8_t peering_cost = 0;
    bool node_active = false;
};

using PropagationPeerStateList =
    std::vector<PropagationPeerState, PsramAllocator<PropagationPeerState>>;

struct TransportRuntime
{
    PathEntryList paths;
    PacketFilterEntryList packet_filter;
    ReverseEntryList reverse_table;
    PendingPathRequestList pending_path_requests;
    PendingPingReceiptList pending_ping_receipts;
    LinkRelayEntryList link_relays;
};

struct LinkRuntime
{
    LinkSessionList sessions;
};

struct PropagationRuntime
{
    PropagationEntryList entries;
    PropagationTransientEntryList transients;
    PropagationPeerStateList peers;
    PendingPropagationUploadList pending_uploads;
    PendingPropagationDeliveryList pending_deliveries;
    PropagationIdList sync_wants;
    PropagationIdList sync_haves;
    uint8_t active_node_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t sync_request_id[reticulum::kTruncatedHashSize] = {};
    uint32_t last_sync_s = 0;
    uint32_t sync_started_ms = 0;
    uint32_t persistence_started_ms = 0;
    PropagationSyncStage sync_stage = PropagationSyncStage::Idle;
    bool has_active_node = false;
    bool initial_sync_pending = true;
};

} // namespace chat::lxmf::runtime
