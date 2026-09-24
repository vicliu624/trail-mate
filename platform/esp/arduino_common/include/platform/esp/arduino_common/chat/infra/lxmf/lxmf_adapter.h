/**
 * @file lxmf_adapter.h
 * @brief Device-side LXMF adapter over the existing RNode raw carrier
 */

#pragma once

#include "board/LoraBoard.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/infra/mesh_incoming_queue.h"
#include "chat/ports/i_geocaching_transport.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/ports/i_mesh_peer_directory.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/geocaching_discovery_budget.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/geocaching_discovery_probe.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_adapter_scratch.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_announce_ingestor.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_announce_scheduler.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_deferred_discovery_queue.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_delivery_attempt_ledger.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_delivery_notifier.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_delivery_planner.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_destination_registry.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_link_manager.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_lxst_telephony_client.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_network_page_client.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_packet_router.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_path_manager.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_peer_directory.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_ping_service.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_client.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_stamp_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_runtime_budget.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_runtime_state.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_rx_telemetry.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_interfaces.h"
#include "platform/ui/reticulum_page_runtime.h"

#include <array>
#include <cstddef>
#include <vector>

namespace chat::lxmf
{

class LxmfAdapter : public IMeshAdapter, private runtime::IPeerProjectionSink
{
  public:
    explicit LxmfAdapter(LoraBoard& board,
                         IMeshPeerDirectory* peer_directory = nullptr,
                         bool owns_integrated_radio = true);

    static void* operator new(std::size_t size);
    static void operator delete(void* ptr) noexcept;
    static void operator delete(void* ptr, std::size_t size) noexcept;

    MeshCapabilities getCapabilities() const override;
    bool getGeocachingAuthorKey(uint8_t out[64]) const;
    bool signGeocachingRecord(ByteSpan record, uint8_t* workspace, size_t workspace_capacity,
                              uint8_t* output, size_t output_capacity, size_t& written);
    void setGeocachingAnnouncementHandler(GeocachingAnnouncementHandler handler, void* context)
    {
        if (handler != geocaching_announcement_handler_ || context != geocaching_announcement_context_)
            geocaching_discovery_probe_.reset();
        geocaching_announcement_handler_ = handler;
        geocaching_announcement_context_ = context;
    }
    void setGeocachingDeliveryHandler(CustomDeliveryHandler handler, void* context)
    {
        geocaching_handler_ = handler;
        geocaching_handler_context_ = context;
    }
    MeshSendResult sendCustomDataToDestination(const uint8_t destination_hash[16],
                                               const char* custom_type, ByteSpan data,
                                               bool response, std::array<uint8_t, 32>* accepted_lxmf_hash = nullptr);
    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;
    MeshSendResult sendTextDetailed(ChannelId channel, const std::string& text,
                                    MessageId forced_msg_id = 0,
                                    NodeId peer = 0) override;
    MeshSendResult sendTextToReticulumDestination(
        ChannelId channel,
        const std::string& text,
        MessageId forced_msg_id,
        const ReticulumPeerIdentity& destination) override;
    bool pollIncomingText(MeshIncomingText* out) override;
    void commitIncomingText(const MeshIncomingText& message,
                            bool durably_accepted);
    bool sendAppData(ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     NodeId dest = 0, bool want_ack = false,
                     MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(MeshIncomingData* out) override;
    bool requestNodeInfo(NodeId dest, bool want_response) override;
    bool broadcastSelfIdentity() override;
    NodeId getNodeId() const override;
    /** Derives a domain-separated VMP contact secret from the peer LXMF identity. */
    bool deriveVmpContactSecret(NodeId peer_id, uint8_t out_secret[32]);
    bool getReticulumLocalIdentityInfo(ReticulumLocalIdentityInfo* out) const override;
    MeshActionResult startReticulumAudioCall(
        const ReticulumPeerIdentity& destination) override;
    MeshActionResult pingReticulumDestination(
        const ReticulumPeerIdentity& destination) override;
    MeshActionResult persistReticulumPeer(
        const ReticulumPeerIdentity& destination,
        bool favorite) override;
    MeshActionResult requestNomadPage(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        const char* path,
        const uint8_t* request_data = nullptr,
        std::size_t request_data_len = 0);
    bool cancelNomadPage(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        const char* path);
    bool cancelIncomingResource(
        const uint8_t link_id[reticulum::kTruncatedHashSize],
        const uint8_t resource_hash[reticulum::kFullHashSize]);
    void applyConfig(const MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    bool setWifiTransportEnabled(bool enabled) override;
    bool isReady() const override;
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;
    void processSendQueue() override;

  private:
    CustomDeliveryHandler geocaching_handler_ = nullptr;
    GeocachingAnnouncementHandler geocaching_announcement_handler_ = nullptr;
    void* geocaching_announcement_context_ = nullptr;
    void* geocaching_handler_context_ = nullptr;
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
    using PendingPropagationUpload = runtime::PendingPropagationUpload;
    using PropagationSyncStage = runtime::PropagationSyncStage;
    using PendingNomadPageRequest = runtime::PendingNomadPageRequest;
    using RuntimeBudget = runtime::RuntimeBudget;

    static bool resolveLocalDestinationForAnnounce(
        void* context,
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        LocalDestinationKind* out_kind);

    static constexpr uint32_t kAnnounceIntervalMs = 120000;
    static constexpr uint32_t kInitialAnnounceDelayMs = 1500;
    static constexpr uint32_t kPendingAnnounceRetryMs = 30000;
    static constexpr uint8_t kMaxIngressPacketsPerPoll = 4;
    static constexpr uint8_t kCallIngressPacketsPerPoll = 8;
    static constexpr uint32_t kDiscoverySampleIntervalMs = 10000;
    static constexpr uint32_t kRxSummaryIntervalMs = 5000;
    static constexpr uint32_t kAnnounceRebroadcastIntervalMs = 60000;
    static constexpr uint32_t kPeerProjectionScreenIntervalMs = 2000;
    static constexpr uint32_t kPeerProjectionSleepIntervalMs = 250;
    static constexpr std::size_t kMaxPendingPingRequests = 4;
    static constexpr std::size_t kMaxPendingNomadPageRequests = 4;
    static constexpr std::size_t kNomadPagePathMaxLen = 64;
    static constexpr uint32_t kNomadPageRequestTtlMs = 90000;
    static constexpr uint32_t kNomadPageSendRetryMs = 1500;

    struct OutboundLxmfDispatch
    {
        bool ok = false;
        bool result_event_deferred = false;
        MessageId message_id = 0;
        MeshOperationFailure failure = MeshOperationFailure::None;
        uint8_t message_hash[reticulum::kFullHashSize] = {};
        const char* path = "none";
    };

    reticulum::interfaces::ReticulumInterfaceSet interfaces_;
    uint32_t network_config_generation_ = 0;
    runtime::AdapterScratchBuffers scratch_{};
    runtime::DeferredDiscoveryQueue deferred_discovery_;
    LxmfIdentity identity_;
    MeshConfig config_{};
    static constexpr std::size_t kIncomingQueueDepth = 12;
    ::chat::infra::IncomingTextQueue<kIncomingQueueDepth, reticulum::kReticulumMtu> text_receive_queue_;
    ::chat::infra::IncomingDataQueue<kIncomingQueueDepth, reticulum::kReticulumMtu> data_receive_queue_;
    runtime::DestinationRegistry destination_registry_;
    runtime::PathManager path_manager_;
    runtime::DeliveryAttemptLedger delivery_attempt_ledger_;
    runtime::LinkManager link_manager_;
    runtime::AnnounceIngestor announce_ingestor_;
    runtime::ReticulumPacketRouter packet_router_;
    runtime::PingService ping_service_;
    runtime::NetworkPageClient network_page_client_;
    runtime::PropagationClient propagation_client_;
    runtime::LxstTelephonyClient lxst_telephony_client_;
    runtime::PeerDirectoryService peer_directory_service_;
    const bool geocaching_only_;
    runtime::LxmfDeliveryNotifier delivery_notifier_;
    std::string user_long_name_;
    std::string user_short_name_;
    runtime::AnnounceScheduler announce_scheduler_;
    runtime::RawRxTelemetry rx_telemetry_;
    runtime::GeocachingDiscoveryBudget geocaching_discovery_budget_;
    runtime::GeocachingDiscoveryProbe geocaching_discovery_probe_;
    std::size_t link_request_packet_len_ = 0;
    uint32_t next_app_packet_id_ = 1;
    bool peers_loaded_ = false;
    RxMeta active_rx_meta_{};
    bool has_active_rx_meta_ = false;
    reticulum::interfaces::InterfaceId active_ingress_interface_id_ =
        reticulum::interfaces::kInvalidInterfaceId;

    RuntimeBudget makeRuntimeBudget() const;
    void processRuntime();
    void processRadioPackets(const RuntimeBudget& budget);
    bool processOneRadioPacket(const reticulum::interfaces::RxPacket& packet,
                               const RuntimeBudget& budget,
                               bool deferred_replay);
    bool shouldDeferDiscoveryPacket(
        const reticulum::ParsedPacket& packet,
        reticulum::interfaces::InterfaceKind ingress_interface,
        const RuntimeBudget& budget);
    bool isPublicDiscoveryPacket(const reticulum::ParsedPacket& packet) const;
    bool enqueueDeferredDiscoveryPacket(
        const reticulum::interfaces::RxPacket& packet,
        const uint8_t packet_hash[reticulum::kFullHashSize]);
    bool hasDeferredDiscoveryPacket(
        const uint8_t packet_hash[reticulum::kFullHashSize]) const;
    void processDeferredDiscoveryPackets(const RuntimeBudget& budget);
    void maybeAnnounce();
    bool sendAnnounce(LocalDestinationKind kind = LocalDestinationKind::Delivery,
                      reticulum::PacketContext context = reticulum::PacketContext::None);
    bool lastAnnounceTxReachedRequiredInterfaces(bool sent) const;
    bool handleAnnouncePacket(const uint8_t* raw_packet, size_t raw_len,
                              const reticulum::ParsedPacket& packet,
                              reticulum::interfaces::InterfaceKind ingress_interface,
                              bool allow_persistence);
    bool handleDataPacket(const uint8_t* raw_packet, size_t raw_len,
                          const reticulum::ParsedPacket& packet);
    bool handleProofPacket(const uint8_t* raw_packet, size_t raw_len,
                           const reticulum::ParsedPacket& packet,
                           reticulum::interfaces::InterfaceKind ingress_interface);
    bool handleLinkRequestPacket(
        const uint8_t* raw_packet, size_t raw_len,
        const reticulum::ParsedPacket& packet,
        reticulum::interfaces::InterfaceKind ingress_interface);
    bool handlePathRequestPacket(const reticulum::ParsedPacket& packet);
    bool handleCacheRequestPacket(const reticulum::ParsedPacket& packet);
    bool maybeForwardTransportPacket(const uint8_t* raw_packet, size_t raw_len,
                                     const reticulum::ParsedPacket& packet);
    bool maybeForwardLinkPacket(const uint8_t* raw_packet, size_t raw_len,
                                const reticulum::ParsedPacket& packet);
    bool sendForwardPlan(const reticulum::ParsedPacket& packet,
                         const runtime::PacketForwardPlan& plan);
    bool handleLocalLinkPacket(
        const uint8_t* raw_packet, size_t raw_len,
        const reticulum::ParsedPacket& packet,
        reticulum::interfaces::InterfaceKind ingress_interface);
    bool sendProofForPacket(const uint8_t* raw_packet, size_t raw_len);
    bool sendPathRequest(PeerInfo& peer);
    bool sendPathRequestForDestination(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    bool shouldRequestPath(const PeerInfo& peer) const;
    LinkSession* ensureOutboundLinkSession(PeerInfo& peer,
                                           LocalDestinationKind kind,
                                           bool* out_started = nullptr);
    bool prepareLinkRequest(LinkSession& session);
    bool sendLinkRequest(LinkSession& session);
    bool buildSignedMessagePacket(const PeerInfo& peer,
                                  const uint8_t* packed_payload, size_t packed_payload_len,
                                  uint8_t* out_packet, size_t* inout_len,
                                  uint8_t out_message_hash[reticulum::kFullHashSize]);
    bool buildGroupMessagePacket(
        const ReticulumPeerIdentity& destination,
        const uint8_t* packed_payload, size_t packed_payload_len,
        uint8_t* out_packet, size_t* inout_len,
        uint8_t out_message_hash[reticulum::kFullHashSize]);
    bool dispatchLxmfPayload(PeerInfo& peer,
                             const uint8_t* packed_payload,
                             size_t packed_payload_len,
                             bool track_user_message,
                             OutboundLxmfDispatch* out_dispatch,
                             bool allow_propagation = true);
    bool queuePropagationUpload(PeerInfo& recipient,
                                const uint8_t* lxmf_message,
                                size_t lxmf_message_len,
                                MessageId message_id,
                                const uint8_t message_hash[reticulum::kFullHashSize],
                                bool track_user_message,
                                OutboundLxmfDispatch* out_dispatch);
    void processPropagationClient();
    const PropagationPeerState* selectActivePropagationPeer();
    bool preparePropagationPeer(const PropagationPeerState& source,
                                PeerInfo* out_peer) const;
    bool encryptForPeer(const PeerInfo& peer,
                        const uint8_t* plaintext,
                        size_t plaintext_len,
                        uint8_t* out_payload,
                        size_t* inout_len);
    bool queueReadyPropagationUpload(PendingPropagationUpload& upload,
                                     const PropagationPeerState& node);
    MessageId propagationUploadMessageId(
        const PendingPropagationUpload& upload);
    MessageId takePropagationUploadMessageId(
        const PendingPropagationUpload& upload);
    bool sendPropagationSyncRequest(LinkSession& session,
                                    PropagationSyncStage next_stage,
                                    const runtime::PropagationIdList* wants,
                                    const runtime::PropagationIdList* haves,
                                    bool include_transfer_limit);
    void processPropagationSyncResponse(LinkSession& session);
    bool respondToSidebandTelemetryRequest(
        PeerInfo& peer,
        const SidebandTelemetryRequest& request);
    bool buildEncryptedPacketForPeer(const PeerInfo& peer,
                                     const uint8_t* plaintext, size_t plaintext_len,
                                     uint8_t* out_packet, size_t* inout_len);
    bool routeAndSendPacket(const uint8_t* raw_packet, size_t raw_len,
                            bool allow_transport,
                            bool wifi_only = false);
    bool sendCachedAnnounceResponse(const PathEntry& path,
                                    reticulum::PacketContext context);
    bool sendCachedPacketReplay(const uint8_t packet_hash[reticulum::kFullHashSize]);
    bool shouldProcessWifiIngressPacket(const reticulum::ParsedPacket& packet,
                                        const RuntimeBudget& budget);
    bool shouldLogRxDetail(const reticulum::ParsedPacket& packet,
                           reticulum::interfaces::InterfaceKind ingress_interface,
                           const RuntimeBudget& budget);
    bool consumeDiscoveryBudget(reticulum::interfaces::InterfaceKind ingress_interface);
    bool isForegroundDiscoveryDestination(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const;
    void noteRxSummary(bool wifi_skipped = false,
                       bool duplicate = false,
                       bool parse_failed = false,
                       bool deferred = false,
                       bool deferred_dropped = false,
                       bool throttled_discovery = false);
    bool shouldRebroadcastAnnounce(
        const reticulum::ParsedPacket& packet,
        reticulum::interfaces::InterfaceKind ingress_interface) const;
    bool rebroadcastAnnounce(const PathEntry& path, const reticulum::ParsedPacket& packet);
    void cullTransportState();
    void cullLinkSessions();
    LinkSession* findLinkSession(const uint8_t link_id[reticulum::kTruncatedHashSize]);
    LinkSession* findActiveLinkSessionByDestination(const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                                                    LocalDestinationKind kind);
    const ReticulumGroupDestinationConfig* findConfiguredGroupDestination(
        const uint8_t hash[reticulum::kTruncatedHashSize]) const;
    bool isConfiguredGroupDestination(
        const ReticulumPeerIdentity& destination) const;
    PeerInfo* findOrLoadPeerByNodeId(NodeId node_id);
    PeerInfo* findOrLoadPeerByDestinationHash(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    MeshActionResult persistPeerAddressNow(const PeerInfo& peer, bool favorite) const;
    bool recordPeerInDirectory(const PeerInfo& peer,
                               MeshPeerSource source,
                               bool update_favorite,
                               bool favorite) const;
    PeerInfo* rememberPeerIdentity(const uint8_t combined_pub[reticulum::kCombinedPublicKeySize],
                                   const char* display_name = nullptr,
                                   bool publish_contact = true);
    void pumpPendingPeerUpdates();
    void publishPeerUpdate(const PeerInfo& peer) override;
    void loadPersistedPeers();
    void loadDirectoryPeers();
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
                            runtime::ResourcePayloadBuffer* out_plaintext) const;
    bool sendLinkPacket(LinkSession& session,
                        reticulum::PacketType packet_type,
                        reticulum::PacketContext context,
                        const uint8_t* payload, size_t payload_len,
                        bool encrypt_payload,
                        bool call_admission_control = false,
                        uint8_t out_packet_hash[reticulum::kFullHashSize] = nullptr);
    bool sendNomadPageRequestPacket(LinkSession& session,
                                    PendingNomadPageRequest& request);
    MeshActionResult sendReticulumPingToPeer(PeerInfo& peer,
                                             uint32_t operation_started_ms);
    MeshActionResult queuePendingReticulumPing(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    void pumpPendingPingRequests();
    void pumpNomadPageRequests();
    void completeNomadPageRequest(PendingNomadPageRequest& request,
                                  const runtime::ResourcePayloadBuffer& packed_response);
    PendingNomadPageRequest* findPendingNomadPageRequestById(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        const uint8_t* request_id,
        std::size_t request_id_len);
    void updateNomadPageProgress(const PendingNomadPageRequest& request,
                                 int progress_percent,
                                 const char* message,
                                 const char* detail,
                                 bool active,
                                 bool complete,
                                 platform::ui::reticulum_page::RequestProgress::
                                     FailureKind failure);
    void updateNomadPageProgressForDestination(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        int progress_percent,
        const char* message,
        const char* detail,
        bool active,
        bool complete,
        platform::ui::reticulum_page::RequestProgress::FailureKind failure);
    bool sendLinkHandshakeProof(LinkSession& session,
                                bool call_admission_control = false);
    bool sendLinkRtt(LinkSession& session);
    bool sendLinkKeepalive(LinkSession& session);
    bool sendLinkKeepaliveAck(LinkSession& session);
    bool sendLinkIdentify(LinkSession& session);
    bool sendLinkPacketProof(LinkSession& session,
                             const uint8_t* raw_packet, size_t raw_len);
    void updateCallRuntimePeer(LinkSession& session,
                               const PeerInfo* peer = nullptr);
    bool beginIncomingCallRuntime(LinkSession& session,
                                  const PeerInfo& peer);
    bool sendLxstSignal(LinkSession& session,
                        uint16_t signal,
                        bool call_admission_control = false);
    bool sendLxstSignals(LinkSession& session,
                         const uint16_t* signals,
                         std::size_t signal_count,
                         bool call_admission_control = false);
    bool dispatchLxstCallEvent(
        LinkSession& session,
        const reticulum::lxst::call::Event& event);
    bool handleLxstPacket(LinkSession& session,
                          const uint8_t* payload,
                          size_t payload_len);
    bool sendCallAudioPacket(LinkSession& session,
                             const uint8_t* payload,
                             size_t payload_len);
    void pumpReticulumAudioCall();
    void closeLinkSession(LinkSession& session,
                          LinkCloseReason reason = LinkCloseReason::LocalClose);
    void flushDeferredLinkPayloads(LinkSession& session);
    bool handleLinkDataPacket(LinkSession& session,
                              const uint8_t* raw_packet, size_t raw_len,
                              const reticulum::ParsedPacket& packet);
    bool handleLinkProofPacket(LinkSession& session,
                               const uint8_t* raw_packet, size_t raw_len,
                               const reticulum::ParsedPacket& packet);
    bool acceptVerifiedEnvelope(const uint8_t* plaintext, size_t plaintext_len,
                                const uint8_t* raw_packet, size_t raw_len,
                                uint8_t* out_message_hash = nullptr,
                                bool* out_awaiting_commit = nullptr,
                                LinkSession* incoming_delivery_session = nullptr);
    bool acceptVerifiedEnvelopeForDestination(
        const uint8_t expected_destination_hash[reticulum::kTruncatedHashSize],
        const ReticulumPeerIdentity& conversation_identity,
        bool destination_is_group,
        bool encrypted,
        const uint8_t* plaintext, size_t plaintext_len,
        const uint8_t* raw_packet, size_t raw_len,
        uint8_t* out_message_hash = nullptr,
        bool* out_awaiting_commit = nullptr,
        LinkSession* incoming_delivery_session = nullptr);
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
    bool acceptPropagatedDelivery(const uint8_t* propagated_payload,
                                  size_t propagated_payload_len,
                                  uint8_t* out_message_hash = nullptr,
                                  bool* out_awaiting_commit = nullptr);
    bool requestNextResourceWindow(LinkSession& session,
                                   LinkResourceTransfer& resource);
    bool advertiseLinkResource(LinkSession& session,
                               LinkResourceTransfer& resource,
                               uint32_t hashmap_segment = 0);
    bool queueOutgoingResource(LinkSession& session,
                               const uint8_t* data, size_t len,
                               uint8_t flags,
                               const uint8_t* request_id,
                               size_t request_id_len,
                               uint32_t message_id = 0);
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
