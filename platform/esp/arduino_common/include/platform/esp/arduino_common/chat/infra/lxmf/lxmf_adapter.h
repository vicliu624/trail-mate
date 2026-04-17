/**
 * @file lxmf_adapter.h
 * @brief Device-side LXMF adapter over the existing RNode raw carrier
 */

#pragma once

#include "board/LoraBoard.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/ports/i_mesh_adapter.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"
#include "platform/esp/arduino_common/chat/infra/rnode/rnode_adapter.h"

#include <array>
#include <queue>
#include <vector>

namespace chat::lxmf
{

class LxmfAdapter : public IMeshAdapter
{
  public:
    explicit LxmfAdapter(LoraBoard& board);

    MeshCapabilities getCapabilities() const override;
    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;
    bool pollIncomingText(MeshIncomingText* out) override;
    bool sendAppData(ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     NodeId dest = 0, bool want_ack = false,
                     MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(MeshIncomingData* out) override;
    bool requestNodeInfo(NodeId dest, bool want_response) override;
    NodeId getNodeId() const override;
    void applyConfig(const MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    bool isReady() const override;
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;

  private:
    struct PeerInfo
    {
        uint32_t node_id = 0;
        uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t enc_pub[LxmfIdentity::kEncPubKeySize] = {};
        uint8_t sig_pub[LxmfIdentity::kSigPubKeySize] = {};
        char display_name[32] = {};
        uint32_t last_seen_s = 0;
        uint32_t last_path_request_ms = 0;
    };

    struct PathEntry
    {
        uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t next_hop_transport[reticulum::kTruncatedHashSize] = {};
        uint8_t cached_packet_hash[reticulum::kFullHashSize] = {};
        uint8_t cached_announce[reticulum::kReticulumMtu] = {};
        size_t cached_announce_len = 0;
        uint8_t hops = 0;
        uint32_t last_seen_s = 0;
        bool direct = false;
    };

    struct PacketFilterEntry
    {
        uint8_t packet_hash[reticulum::kFullHashSize] = {};
        uint32_t seen_ms = 0;
    };

    struct ReverseEntry
    {
        uint8_t proof_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t expected_hops = 0;
        uint32_t created_ms = 0;
    };

    struct LinkRelayEntry
    {
        uint8_t link_id[reticulum::kTruncatedHashSize] = {};
        uint8_t initiator_hops = 0;
        uint8_t responder_hops = 0;
        uint32_t last_seen_ms = 0;
    };

    enum class LocalDestinationKind : uint8_t
    {
        Delivery = 0,
        Propagation = 1
    };

    enum class LinkState : uint8_t
    {
        Pending = 0,
        Handshake = 1,
        Active = 2,
        Closed = 3
    };

    struct LinkPendingRequest
    {
        std::vector<uint8_t> request_id;
        uint32_t created_ms = 0;
        bool awaiting_resource = false;
        bool response_ready = false;
        std::vector<uint8_t> response;
    };

    struct LinkResourceTransfer
    {
        uint8_t resource_hash[reticulum::kFullHashSize] = {};
        uint8_t random_hash[4] = {};
        uint8_t original_hash[reticulum::kFullHashSize] = {};
        uint8_t expected_proof[reticulum::kFullHashSize] = {};
        std::vector<uint8_t> request_id;
        std::vector<uint8_t> hashmap;
        std::vector<std::array<uint8_t, 4>> map_hashes;
        std::vector<uint8_t> map_hash_known;
        std::vector<std::vector<uint8_t>> parts;
        std::vector<uint8_t> received_bitmap;
        uint32_t data_size = 0;
        uint32_t transfer_size = 0;
        uint32_t part_count = 0;
        uint32_t hashmap_height = 0;
        uint32_t next_request_index = 0;
        uint32_t window_size = 4;
        uint32_t created_ms = 0;
        uint32_t last_activity_ms = 0;
        uint8_t flags = 0;
        bool incoming = true;
        bool encrypted = false;
        bool compressed = false;
        bool has_metadata = false;
        bool waiting_for_hashmap = false;
        bool complete = false;
        bool waiting_for_proof = false;
        int32_t consecutive_complete_index = -1;
    };

    struct LinkSession
    {
        uint8_t link_id[reticulum::kTruncatedHashSize] = {};
        uint8_t remote_destination_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t remote_identity_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t local_enc_pub[LxmfIdentity::kEncPubKeySize] = {};
        uint8_t local_enc_priv[LxmfIdentity::kEncPrivKeySize] = {};
        uint8_t local_sig_pub[LxmfIdentity::kSigPubKeySize] = {};
        uint8_t local_sig_priv[LxmfIdentity::kSigPrivKeySize] = {};
        uint8_t peer_enc_pub[LxmfIdentity::kEncPubKeySize] = {};
        uint8_t peer_link_sig_pub[LxmfIdentity::kSigPubKeySize] = {};
        uint8_t peer_identity_sig_pub[LxmfIdentity::kSigPubKeySize] = {};
        uint8_t derived_key[reticulum::kDerivedTokenKeySize] = {};
        uint32_t created_ms = 0;
        uint32_t request_ms = 0;
        uint32_t last_inbound_ms = 0;
        uint32_t last_outbound_ms = 0;
        uint16_t mtu = reticulum::kReticulumMtu;
        uint16_t mdu = reticulum::kReticulumMdu;
        float rtt_s = 0.0f;
        bool initiator = false;
        bool remote_identity_known = false;
        LocalDestinationKind destination = LocalDestinationKind::Delivery;
        LinkState state = LinkState::Pending;
        bool propagation_offer_validated = false;
        std::vector<LinkPendingRequest> pending_requests;
        std::vector<LinkResourceTransfer> incoming_resources;
        std::vector<LinkResourceTransfer> outgoing_resources;
    };

    struct PropagationEntry
    {
        uint8_t transient_id[reticulum::kFullHashSize] = {};
        uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
        std::vector<uint8_t> lxmf_data;
        uint32_t created_s = 0;
        uint32_t served_count = 0;
    };

    struct PropagationTransientEntry
    {
        uint8_t transient_id[reticulum::kFullHashSize] = {};
        uint32_t seen_s = 0;
        bool delivered = false;
    };

    struct PropagationPeerState
    {
        uint8_t propagation_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
        uint32_t last_seen_s = 0;
        uint32_t incoming_messages = 0;
        uint32_t served_messages = 0;
    };

    static constexpr uint32_t kAnnounceIntervalMs = 120000;
    static constexpr uint32_t kInitialAnnounceDelayMs = 1500;

    rnode::RNodeAdapter raw_;
    LxmfIdentity identity_;
    MeshConfig config_{};
    std::queue<MeshIncomingText> text_receive_queue_;
    std::queue<MeshIncomingData> data_receive_queue_;
    std::vector<PeerInfo> peers_;
    std::vector<PathEntry> paths_;
    std::vector<PacketFilterEntry> packet_filter_;
    std::vector<ReverseEntry> reverse_table_;
    std::vector<LinkRelayEntry> link_relays_;
    std::vector<LinkSession> link_sessions_;
    std::vector<PropagationEntry> propagation_entries_;
    std::vector<PropagationTransientEntry> propagation_transients_;
    std::vector<PropagationPeerState> propagation_peers_;
    std::string user_long_name_;
    std::string user_short_name_;
    uint32_t last_announce_ms_ = 0;
    uint32_t next_app_packet_id_ = 1;
    bool announce_pending_ = true;
    bool peers_loaded_ = false;

    void processRadioPackets();
    void maybeAnnounce();
    bool sendAnnounce(LocalDestinationKind kind = LocalDestinationKind::Delivery,
                      reticulum::PacketContext context = reticulum::PacketContext::None);
    bool handleAnnouncePacket(const uint8_t* raw_packet, size_t raw_len,
                              const reticulum::ParsedPacket& packet);
    bool handleDataPacket(const uint8_t* raw_packet, size_t raw_len,
                          const reticulum::ParsedPacket& packet);
    bool handleProofPacket(const uint8_t* raw_packet, size_t raw_len,
                           const reticulum::ParsedPacket& packet);
    bool handleLinkRequestPacket(const uint8_t* raw_packet, size_t raw_len,
                                 const reticulum::ParsedPacket& packet);
    bool handlePathRequestPacket(const reticulum::ParsedPacket& packet);
    bool handleCacheRequestPacket(const reticulum::ParsedPacket& packet);
    bool maybeForwardTransportPacket(const uint8_t* raw_packet, size_t raw_len,
                                     const reticulum::ParsedPacket& packet);
    bool maybeForwardLinkPacket(const uint8_t* raw_packet, size_t raw_len,
                                const reticulum::ParsedPacket& packet);
    bool handleLocalLinkPacket(const uint8_t* raw_packet, size_t raw_len,
                               const reticulum::ParsedPacket& packet);
    bool sendProofForPacket(const uint8_t* raw_packet, size_t raw_len);
    bool sendPathRequest(PeerInfo& peer);
    bool shouldRequestPath(const PeerInfo& peer) const;
    bool buildSignedMessagePacket(const PeerInfo& peer,
                                  const uint8_t* packed_payload, size_t packed_payload_len,
                                  uint8_t* out_packet, size_t* inout_len,
                                  uint8_t out_message_hash[reticulum::kFullHashSize]);
    bool buildEncryptedPacketForPeer(const PeerInfo& peer,
                                     const uint8_t* plaintext, size_t plaintext_len,
                                     uint8_t* out_packet, size_t* inout_len);
    bool routeAndSendPacket(const uint8_t* raw_packet, size_t raw_len,
                            bool allow_transport);
    bool sendCachedAnnounceResponse(const PathEntry& path,
                                    reticulum::PacketContext context);
    bool sendCachedPacketReplay(const uint8_t packet_hash[reticulum::kFullHashSize]);
    bool shouldRebroadcastAnnounce(const reticulum::ParsedPacket& packet) const;
    bool rebroadcastAnnounce(const PathEntry& path, const reticulum::ParsedPacket& packet);
    bool isDuplicatePacket(const uint8_t packet_hash[reticulum::kFullHashSize]);
    void rememberPacket(const uint8_t packet_hash[reticulum::kFullHashSize]);
    void rememberReversePath(const uint8_t proof_hash[reticulum::kTruncatedHashSize],
                             uint8_t expected_hops);
    ReverseEntry* findReversePath(const uint8_t proof_hash[reticulum::kTruncatedHashSize]);
    void cullTransportState();
    void cullLinkSessions();
    PathEntry& upsertPath(const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    const PathEntry* findPath(const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const;
    LinkRelayEntry& upsertLinkRelay(const uint8_t link_id[reticulum::kTruncatedHashSize]);
    LinkRelayEntry* findLinkRelay(const uint8_t link_id[reticulum::kTruncatedHashSize]);
    LinkSession* findLinkSession(const uint8_t link_id[reticulum::kTruncatedHashSize]);
    LinkSession* findActiveLinkSessionByDestination(const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                                                    LocalDestinationKind kind);
    PeerInfo* findPeerByNodeId(NodeId node_id);
    const PeerInfo* findPeerByDestinationHash(const uint8_t hash[reticulum::kTruncatedHashSize]) const;
    PeerInfo& upsertPeer(const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    PeerInfo* rememberPeerIdentity(const uint8_t combined_pub[reticulum::kCombinedPublicKeySize],
                                   const char* display_name = nullptr);
    void publishPeerUpdate(const PeerInfo& peer) const;
    void loadPersistedPeers();
    bool persistPeers() const;
    uint32_t currentTimestampSeconds() const;
    const char* effectiveDisplayName() const;
    void populateRxMeta(RxMeta* out) const;
    void localDestinationHash(LocalDestinationKind kind,
                              uint8_t out_hash[reticulum::kTruncatedHashSize]) const;
    bool isLocalDestinationHash(const uint8_t hash[reticulum::kTruncatedHashSize],
                                LocalDestinationKind* out_kind) const;
    static uint16_t linkMduForMtu(uint16_t mtu);
    static bool generateLinkSigningKey(uint8_t out_pub[LxmfIdentity::kSigPubKeySize],
                                       uint8_t out_priv[LxmfIdentity::kSigPrivKeySize]);
    static bool signWithKey(const uint8_t sign_pub[LxmfIdentity::kSigPubKeySize],
                            const uint8_t sign_priv[LxmfIdentity::kSigPrivKeySize],
                            const uint8_t* message,
                            size_t message_len,
                            uint8_t out_signature[reticulum::kSignatureSize]);
    bool deriveLinkKey(LinkSession& session);
    bool encryptLinkPayload(const LinkSession& session,
                            const uint8_t* plaintext, size_t plaintext_len,
                            uint8_t* out_payload, size_t* inout_len) const;
    bool decryptLinkPayload(const LinkSession& session,
                            const uint8_t* payload, size_t payload_len,
                            std::vector<uint8_t>* out_plaintext) const;
    bool sendLinkPacket(LinkSession& session,
                        reticulum::PacketType packet_type,
                        reticulum::PacketContext context,
                        const uint8_t* payload, size_t payload_len,
                        bool encrypt_payload);
    bool sendLinkHandshakeProof(LinkSession& session);
    bool sendLinkRtt(LinkSession& session);
    bool sendLinkKeepaliveAck(LinkSession& session);
    bool sendLinkPacketProof(LinkSession& session,
                             const uint8_t* raw_packet, size_t raw_len);
    void closeLinkSession(LinkSession& session);
    bool handleLinkDataPacket(LinkSession& session,
                              const uint8_t* raw_packet, size_t raw_len,
                              const reticulum::ParsedPacket& packet);
    bool handleLinkProofPacket(LinkSession& session,
                               const uint8_t* raw_packet, size_t raw_len,
                               const reticulum::ParsedPacket& packet);
    bool acceptVerifiedEnvelope(const uint8_t* plaintext, size_t plaintext_len,
                                const uint8_t* raw_packet, size_t raw_len);
    LinkResourceTransfer* findLinkResource(std::vector<LinkResourceTransfer>& resources,
                                           const uint8_t resource_hash[reticulum::kFullHashSize]);
    bool handleLinkResourceAdvertisement(LinkSession& session,
                                         const uint8_t* plaintext, size_t plaintext_len);
    bool handleLinkResourceRequest(LinkSession& session,
                                   const uint8_t* plaintext, size_t plaintext_len);
    bool handleLinkResourceHashmapUpdate(LinkSession& session,
                                         const uint8_t* plaintext, size_t plaintext_len);
    bool handleLinkResourcePart(LinkSession& session,
                                const reticulum::ParsedPacket& packet);
    bool handleLinkResourceProof(LinkSession& session,
                                 const reticulum::ParsedPacket& packet);
    bool handlePropagationBatch(LinkSession& session,
                                const uint8_t* plaintext, size_t plaintext_len);
    bool handlePropagationRequest(LinkSession& session,
                                  const DecodedLinkRequest& request,
                                  const uint8_t* request_id,
                                  size_t request_id_len);
    bool acceptPropagatedMessage(const uint8_t* lxmf_data, size_t lxmf_len,
                                 const uint8_t* remote_propagation_hash);
    bool acceptPropagatedDelivery(const uint8_t* propagated_payload,
                                  size_t propagated_payload_len);
    PropagationEntry* findPropagationEntry(
        const uint8_t transient_id[reticulum::kFullHashSize]);
    const PropagationEntry* findPropagationEntry(
        const uint8_t transient_id[reticulum::kFullHashSize]) const;
    PropagationPeerState& upsertPropagationPeer(
        const uint8_t propagation_hash[reticulum::kTruncatedHashSize],
        const uint8_t delivery_hash[reticulum::kTruncatedHashSize],
        const uint8_t identity_hash[reticulum::kTruncatedHashSize]);
    bool hasSeenPropagationTransient(
        const uint8_t transient_id[reticulum::kFullHashSize],
        bool* out_delivered = nullptr) const;
    void rememberPropagationTransient(
        const uint8_t transient_id[reticulum::kFullHashSize],
        bool delivered);
    void cullPropagationState();
    bool requestNextResourceWindow(LinkSession& session,
                                   LinkResourceTransfer& resource);
    bool advertiseLinkResource(LinkSession& session,
                               LinkResourceTransfer& resource,
                               uint32_t hashmap_segment = 0);
    bool queueOutgoingResource(LinkSession& session,
                               const uint8_t* data, size_t len,
                               uint8_t flags,
                               const uint8_t* request_id,
                               size_t request_id_len);
    bool sendLinkResponse(LinkSession& session,
                          const uint8_t* request_id,
                          size_t request_id_len,
                          const uint8_t* packed_data,
                          size_t packed_data_len,
                          bool data_is_nil);
    static uint32_t messageIdFromHash(const uint8_t hash[reticulum::kFullHashSize]);
    static void pathRequestDestinationHash(uint8_t out_hash[reticulum::kTruncatedHashSize]);
};

} // namespace chat::lxmf
