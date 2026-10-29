#pragma once
#include "chat/delivery/chat_delivery_types.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/lxmf/lxmf_delivery_attempt_ledger.h"
#include "chat/infra/lxmf/lxmf_link_manager.h"
#include "chat/ports/i_geocaching_transport.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include <array>

namespace trailmate::linux_app
{
// One mesh-worker owner; callbacks borrow their arguments only for the call.
class LinuxReticulumLinks
{
  public:
    using Sender = bool (*)(void*, const uint8_t*, size_t);
    explicit LinuxReticulumLinks(const platform::linux_runtime::LinuxReticulumIdentity& identity) : identity_(identity) {}
    ~LinuxReticulumLinks();
    LinuxReticulumLinks(const LinuxReticulumLinks&) = delete;
    LinuxReticulumLinks& operator=(const LinuxReticulumLinks&) = delete;
    bool receive(const uint8_t*, size_t, uint32_t now, Sender, void*, const ::chat::RxMeta* = nullptr);
    using TextReceiver = bool (*)(void*, const ::chat::MeshIncomingText&);
    using PeerResolver = bool (*)(void*, const uint8_t destination[16], uint8_t public_key[64], bool* ignored);
    using PeerObserver = void (*)(void*, const uint8_t public_key[64], uint32_t now_s);
    void setIncomingHooks(TextReceiver receiver, PeerResolver resolver, void* context, PeerObserver = nullptr);
    // Worker-owned, synchronous borrowed views. True requires durable business
    // acceptance or a completely processed ephemeral control exchange.
    using CustomReceiver = bool (*)(void*, ::chat::lxmf::ByteSpan type, const ::chat::lxmf::CustomDeliveryView&);
    void setCustomReceiver(CustomReceiver receiver, void* context)
    {
        custom_receiver_ = receiver;
        custom_context_ = context;
    }
    void commitIncoming(const ::chat::MeshIncomingText&, bool durable, uint32_t now, Sender, void*);
    void retryAccepted(uint32_t now, Sender, void*);
    using OutcomeSink = void (*)(void*, ::chat::MessageId, ::chat::MessageStatus, ::chat::delivery::SendFailureKind);
    void setOutcomeSink(OutcomeSink sink, void* context)
    {
        outcome_sink_ = sink;
        outcome_context_ = context;
    }
    void setCustomOutcomeSink(OutcomeSink sink, void* context)
    {
        custom_outcome_sink_ = sink;
        custom_outcome_context_ = context;
    }
    enum class QueueResult
    {
        Queued,
        Pending,
        Invalid,
        Busy,
        TxFailed
    };
    static bool textFitsSinglePacket(const std::string&);
    static bool textFitsTransport(const std::string&);
    QueueResult queueText(const uint8_t destination[16], const uint8_t public_key[64], const std::string& text,
                          ::chat::MessageId, double timestamp, uint32_t now, Sender, void*,
                          uint8_t expected_hops = 1, const uint8_t* next_hop = nullptr);
    QueueResult queueCustom(const uint8_t destination[16], const uint8_t public_key[64], const char* type,
                            ::chat::lxmf::ByteSpan data, ::chat::MessageId, double timestamp, uint32_t now, Sender, void*,
                            uint8_t expected_hops = 1, const uint8_t* next_hop = nullptr, uint8_t accepted_hash[32] = nullptr);
    void processOutbound(uint32_t now, Sender, void*);
    std::vector<::chat::MessageId> pendingOutboundIds() const;
    bool hasOpenDestination(const uint8_t destination[16]);
    bool hasActiveDelivery(const uint8_t destination[16]);
    bool hasLink(const uint8_t link[16]);
    bool closeLink(const uint8_t link[16], uint32_t now, ::chat::lxmf::runtime::LinkCloseReason, Sender, void*);
    enum class OpenResult
    {
        Opening,
        Active,
        Pending,
        Invalid,
        Full,
        TxFailed
    };
    OpenResult open(const uint8_t destination[16], const uint8_t verified_public_key[64],
                    uint32_t now, Sender, void*, uint8_t expected_hops = 1, const uint8_t* next_hop = nullptr);
    OpenResult openPropagation(const uint8_t destination[16], const uint8_t verified_public_key[64],
                               uint32_t now, Sender, void*, uint8_t expected_hops = 1, const uint8_t* next_hop = nullptr);
    enum class ServiceRequestResult
    {
        Queued,
        Duplicate,
        NotReady,
        Busy,
        Invalid,
        TxFailed
    };
    struct ServiceResponse
    {
        ::chat::lxmf::runtime::ResourcePayloadBuffer payload;
        bool data_is_nil = false;
    };
    ServiceRequestResult requestService(const uint8_t destination[16], const uint8_t path_hash[16],
                                        const uint8_t* packed_data, size_t size, bool data_is_nil, double requested_at,
                                        uint32_t now, Sender, void*, uint8_t request_id[16]);
    bool takeServiceResponse(const uint8_t destination[16], const uint8_t request_id[16], ServiceResponse&);
    bool serviceRequestPending(const uint8_t destination[16], const uint8_t request_id[16]) const;
    bool hasPropagationDestination(const uint8_t destination[16]);
    void closePropagation(const uint8_t destination[16], uint32_t now, Sender, void*);
    ServiceRequestResult queuePropagationUpload(const uint8_t destination[16], const uint8_t* batch, size_t,
                                                uint32_t now, Sender, void*, uint8_t resource_hash[32]);
    enum class UploadResult
    {
        TransportAccepted,
        Failed
    };
    bool takePropagationUploadResult(const uint8_t resource_hash[32], UploadResult&);
    bool hasPropagationUpload(const uint8_t resource_hash[32]) const;
    // Decrypts and verifies the original author; does not queue or acknowledge storage.
    bool decodePropagatedText(const uint8_t* message, size_t size, const ::chat::RxMeta*,
                              ::chat::MeshIncomingText&, uint8_t message_hash[32], uint8_t* missing_author = nullptr);
    bool encodePropagatedText(const uint8_t destination[16], const uint8_t verified_public_key[64], const std::string&,
                              double timestamp, ::chat::lxmf::runtime::ResourcePayloadBuffer&, uint8_t message_hash[32], uint8_t transient_id[32]) const;
    struct CallPeer
    {
        uint8_t link[16]{}, destination[16]{}, identity[16]{};
        bool initiator = false;
    };
    // Callbacks run on the mesh worker, borrow arguments and must not reenter
    // this owner. Admission happens only after authenticated identity binding.
    struct CallHooks
    {
        bool (*admit)(void*, const CallPeer&) = nullptr;
        bool (*receive)(void*, const CallPeer&, const uint8_t*, size_t) = nullptr;
        void (*closed)(void*, const CallPeer&) = nullptr;
        void* context = nullptr;
        // Incoming encrypted handshake is ready, but caller identity is not
        // yet proven. Queue control work only; this is not media admission.
        void (*validated)(void*, const CallPeer&) = nullptr;
    };
    void setCallHooks(CallHooks hooks) { call_hooks_ = hooks; }
    OpenResult openCall(const uint8_t destination[16], const uint8_t verified_public_key[64], uint8_t link[16],
                        uint32_t now, Sender, void*, uint8_t expected_hops = 1, const uint8_t* next_hop = nullptr,
                        bool wait_for_route = false, const uint8_t expected_identity[16] = nullptr);
    // Prepared calls own real ephemeral keys/link IDs but emit no handshake.
    // Resolve once on the protocol worker, after selecting a current route.
    bool transmitPendingCall(const uint8_t link[16], uint32_t now, Sender, void*,
                             uint8_t expected_hops, const uint8_t* next_hop = nullptr, const uint8_t verified_public_key[64] = nullptr);
    bool sendCall(const uint8_t link[16], const uint8_t*, size_t, uint32_t now, Sender, void*);
    bool sendCallSignal(const uint8_t link[16], uint16_t signal, uint32_t now, Sender, void*);
    bool identifyCall(const uint8_t link[16], uint32_t now, Sender, void*);
    bool closeCall(const uint8_t link[16], uint32_t now, Sender, void*);
    void tick(uint32_t now);
    void reset();
    struct Statistics
    {
        size_t sessions = 0, active = 0;
        uint64_t proofs = 0, rejected = 0, tx_errors = 0;
        size_t pending_incoming = 0;
        uint64_t incoming_texts = 0, delivery_proofs = 0;
        uint64_t incoming_custom = 0;
        size_t pending_outbound = 0;
    };
    Statistics statistics() const;

  private:
    using Session = ::chat::lxmf::runtime::LinkSession;
    OpenResult openDestination(const uint8_t*, const uint8_t*, ::chat::lxmf::runtime::LocalDestinationKind,
                               uint32_t, Sender, void*, uint8_t, const uint8_t*, bool wait_for_route = false,
                               const uint8_t* expected_identity = nullptr);
    bool buildLinkRequest(const Session&, uint8_t, const uint8_t*, size_t&);
    CallPeer callPeer(const Session&) const;
    bool admitCall(Session&);
    bool callAdmitted(const Session&) const;
    CallHooks call_hooks_;
    std::array<std::array<uint8_t, 16>, 8> admitted_calls_{};
    bool request(const uint8_t*, size_t, const ::chat::reticulum::ParsedPacket&, uint32_t, Sender, void*);
    bool proof(Session&, Sender, void*);
    bool acceptProof(Session&, const ::chat::reticulum::ParsedPacket&, uint32_t, Sender, void*);
    bool identify(Session&, Sender, void*);
    bool send(Session&, ::chat::reticulum::PacketContext, const uint8_t*, size_t, bool, Sender, void*);
    bool preparePacket(Session&, ::chat::reticulum::PacketContext, const uint8_t*, size_t, bool);
    bool transmitPrepared(Sender, void*);
    void close(Session&, ::chat::lxmf::runtime::LinkCloseReason, uint32_t);
    bool incomingText(Session&, const uint8_t*, size_t, const uint8_t* raw, size_t raw_size, uint32_t, const ::chat::RxMeta*,
                      const uint8_t* resource_hash = nullptr, const uint8_t* resource_proof = nullptr);
    bool packetProof(Session&, const uint8_t hash[32], Sender, void*);
    bool materialiseIncomingText(const Session*, const uint8_t*, size_t, const ::chat::RxMeta*, ::chat::MeshIncomingText&, uint8_t* missing_author = nullptr);
    struct AuthenticatedDelivery
    {
        ::chat::lxmf::DecodedEnvelope envelope;
        ::chat::lxmf::DecodedTextPayload payload;
        std::array<uint8_t, 16> peer_identity{};
        std::array<uint8_t, 32> message_hash{};
    };
    bool authenticateDelivery(const Session*, const uint8_t*, size_t, AuthenticatedDelivery&, uint8_t* missing_author = nullptr);
    bool materialiseVerifiedText(const AuthenticatedDelivery&, const ::chat::RxMeta*, ::chat::MeshIncomingText&) const;
    QueueResult queueEnvelope(const uint8_t*, const uint8_t*, ::chat::lxmf::runtime::ResourcePayloadBuffer&,
                              ::chat::MessageId, uint32_t, Sender, void*, uint8_t, const uint8_t*);
    bool buildTextEnvelope(const uint8_t destination[16], const std::string&, double timestamp,
                           ::chat::lxmf::runtime::ResourcePayloadBuffer&, uint8_t message_hash[32]) const;
    bool serviceResponse(Session&, const uint8_t*, size_t, const uint8_t* resource_request_id = nullptr);
    void outcome(::chat::MessageId, ::chat::MessageStatus, ::chat::delivery::SendFailureKind = ::chat::delivery::SendFailureKind::None);
    ::chat::lxmf::runtime::DeliveryAttemptLedger attempts_;
    OutcomeSink outcome_sink_ = nullptr;
    void* outcome_context_ = nullptr;
    OutcomeSink custom_outcome_sink_ = nullptr;
    void* custom_outcome_context_ = nullptr;
    std::array<::chat::MessageId, 32> custom_message_ids_{};
    struct PendingIncoming
    {
        bool used = false, durable = false;
        bool resource = false;
        uint8_t link[16]{}, destination[16]{}, identity[16]{}, message_hash[32]{}, packet_hash[32]{};
        uint32_t created = 0, last_try = 0;
        uint8_t resource_hash[32]{};
    };
    struct ResourceIo
    {
        bool used = false, outgoing = false, service = false;
        uint8_t link[16]{}, hash[32]{};
        std::vector<size_t> wanted;
        std::vector<uint8_t> submitted;
        uint32_t last_request = 0, last_advertisement = 0;
        ::chat::RxMeta meta{};
    };
    std::array<ResourceIo, 4> resource_io_{};
    struct UploadReceipt
    {
        bool used = false, accepted = false;
        uint8_t link[16]{}, hash[32]{};
    };
    std::array<UploadReceipt, 4> upload_receipts_{};
    size_t resource_cursor_ = 0;
    bool resource_turn_ = false;
    bool startResource(Session&, const uint8_t*, size_t, ::chat::MessageId, uint32_t, Sender, void*,
                       uint8_t flags = 0, const uint8_t* request_id = nullptr, uint8_t* resource_hash = nullptr);
    bool completedResource(Session&, ::chat::lxmf::runtime::LinkResourceTransfer&, ResourceIo&,
                           uint32_t, Sender, void*, const ::chat::RxMeta*);
    bool advertiseResource(Session&, ::chat::lxmf::runtime::LinkResourceTransfer&, uint32_t, Sender, void*);
    bool requestResource(Session&, ::chat::lxmf::runtime::LinkResourceTransfer&, ResourceIo&, uint32_t, Sender, void*);
    bool resourceControl(Session&, ::chat::reticulum::PacketContext, const uint8_t*, size_t, uint32_t, Sender, void*);
    bool resourcePart(Session&, const ::chat::reticulum::ParsedPacket&, uint32_t, Sender, void*, const ::chat::RxMeta*);
    bool resourceProof(Session&, const uint8_t hash[32], const uint8_t proof[32], Sender, void*);
    bool resourceReceipt(Session&, const uint8_t*, size_t, uint32_t);
    bool processResources(uint32_t, Sender, void*);
    bool acknowledge(Session&, PendingIncoming&, uint32_t, Sender, void*);
    std::array<PendingIncoming, 32> incoming_{};
    TextReceiver text_receiver_ = nullptr;
    CustomReceiver custom_receiver_ = nullptr;
    void* custom_context_ = nullptr;
    PeerResolver peer_resolver_ = nullptr;
    PeerObserver peer_observer_ = nullptr;
    void* incoming_context_ = nullptr;
    const platform::linux_runtime::LinuxReticulumIdentity& identity_;
    ::chat::lxmf::runtime::LinkManager links_;
    Statistics counters_;
    size_t last_packet_size_ = 0;
    std::array<uint8_t, ::chat::reticulum::kReticulumMtu> packet_{}, plain_{}, token_{};
};
} // namespace trailmate::linux_app
