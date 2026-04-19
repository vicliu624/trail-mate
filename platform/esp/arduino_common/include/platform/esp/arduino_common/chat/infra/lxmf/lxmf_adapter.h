/**
 * @file lxmf_adapter.h
 * @brief Device-side LXMF adapter over the existing RNode raw carrier
 */

#pragma once

#include "board/LoraBoard.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/ports/i_mesh_adapter.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_runtime_state.h"
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
    using PeerInfo = runtime::PeerInfo;
    using PathEntry = runtime::PathEntry;
    using PacketFilterEntry = runtime::PacketFilterEntry;
    using ReverseEntry = runtime::ReverseEntry;
    using PendingPathRequest = runtime::PendingPathRequest;
    using LinkRelayEntry = runtime::LinkRelayEntry;
    using LocalDestinationKind = runtime::LocalDestinationKind;
    using LinkState = runtime::LinkState;
    using LinkCloseReason = runtime::LinkCloseReason;
    using LinkPendingRequest = runtime::LinkPendingRequest;
    using LinkResourceTransfer = runtime::LinkResourceTransfer;
    using LinkResourceAssembly = runtime::LinkResourceAssembly;
    using LinkSession = runtime::LinkSession;
    using PropagationEntry = runtime::PropagationEntry;
    using PropagationTransientEntry = runtime::PropagationTransientEntry;
    using PropagationPeerState = runtime::PropagationPeerState;

    static constexpr uint32_t kAnnounceIntervalMs = 120000;
    static constexpr uint32_t kInitialAnnounceDelayMs = 1500;

    rnode::RNodeAdapter raw_;
    LxmfIdentity identity_;
    MeshConfig config_{};
    std::queue<MeshIncomingText> text_receive_queue_;
    std::queue<MeshIncomingData> data_receive_queue_;
    std::vector<PeerInfo> peers_;
    runtime::TransportRuntime transport_;
    runtime::LinkRuntime links_;
    runtime::PropagationRuntime propagation_;
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
    LinkSession* ensureOutboundLinkSession(PeerInfo& peer,
                                           LocalDestinationKind kind,
                                           bool* out_started = nullptr);
    bool sendLinkRequest(LinkSession& session);
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
    PendingPathRequest* findPendingPathRequest(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    const PendingPathRequest* findPendingPathRequest(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const;
    void notePendingPathRequest(const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                                uint32_t now_ms);
    void resolvePendingPathRequest(const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
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
    bool sendLinkKeepalive(LinkSession& session);
    bool sendLinkKeepaliveAck(LinkSession& session);
    bool sendLinkPacketProof(LinkSession& session,
                             const uint8_t* raw_packet, size_t raw_len);
    void closeLinkSession(LinkSession& session,
                          LinkCloseReason reason = LinkCloseReason::LocalClose);
    void flushDeferredLinkPayloads(LinkSession& session);
    void expirePath(const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
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
    LinkResourceAssembly* findLinkResourceAssembly(
        LinkSession& session,
        const uint8_t original_hash[reticulum::kFullHashSize]);
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
