#pragma once
#include "chat/linux_reticulum_auto.h"
#include "chat/linux_reticulum_ingress.h"
#include "chat/linux_reticulum_links.h"
#include "chat/linux_reticulum_tcp.h"
#include <memory>

namespace trailmate::linux_app
{
// Serialized production protocol owner across the configured IP endpoints.
// Link bindings pin ciphertext and receipts to their original connection.
class LinuxReticulumNetwork
{
  public:
    struct Hooks
    {
        LinuxReticulumLinks::TextReceiver incoming = nullptr;
        LinuxReticulumLinks::OutcomeSink outcome = nullptr;
        bool (*transmit_allowed)(void*) = nullptr;
        bool (*context_current)(void*) = nullptr;
        ::chat::lxmf::runtime::IPeerProjectionSink* peers = nullptr;
        void* context = nullptr;
        LinuxReticulumIngress::PropagationObserver propagation = nullptr;
        LinuxReticulumIngress::TrackDirectoryObserver track_directory = nullptr;
        LinuxReticulumLinks::CustomReceiver custom = nullptr;
        LinuxReticulumLinks::OutcomeSink custom_outcome = nullptr;
    };
    LinuxReticulumNetwork(const platform::linux_runtime::LinuxReticulumIdentity&, ::chat::IMeshPeerDirectory&, Hooks);
    ~LinuxReticulumNetwork();
    LinuxReticulumNetwork(const LinuxReticulumNetwork&) = delete;
    LinuxReticulumNetwork& operator=(const LinuxReticulumNetwork&) = delete;
    bool configure(const ::chat::reticulum::ReticulumNetworkConfig&, bool anonymous, const char* display_name);
    void tick(uint32_t now_ms, uint32_t utc_seconds);
    void clear();
    bool ready() const;
    bool canCall() const;
    bool hasRoute(const uint8_t destination[16]);
    bool hasActiveDelivery(const uint8_t destination[16]);
    bool requestPath(const uint8_t destination[16]);
    LinuxReticulumLinks::OpenResult openPropagation(const uint8_t destination[16], const uint8_t verified_public_key[64]);
    LinuxReticulumLinks::ServiceRequestResult requestPropagation(const uint8_t destination[16], const uint8_t path_hash[16],
                                                                 const uint8_t* packed_data, size_t size, bool data_is_nil,
                                                                 double requested_at, uint8_t request_id[16]);
    bool takePropagationResponse(const uint8_t destination[16], const uint8_t request_id[16], LinuxReticulumLinks::ServiceResponse&);
    bool propagationRequestPending(const uint8_t destination[16], const uint8_t request_id[16]) const;
    void closePropagation(const uint8_t destination[16]);
    LinuxReticulumLinks::ServiceRequestResult queuePropagationUpload(const uint8_t destination[16], const uint8_t*, size_t, uint8_t resource_hash[32]);
    bool takePropagationUploadResult(const uint8_t resource_hash[32], LinuxReticulumLinks::UploadResult&);
    bool hasPropagationUpload(const uint8_t resource_hash[32]) const;
    LinuxReticulumLinks::QueueResult queueText(const ::chat::ReticulumPeerIdentity&, const std::string&,
                                               ::chat::MessageId, double timestamp);
    LinuxReticulumLinks::QueueResult queueCustom(const ::chat::ReticulumPeerIdentity&, const char* type,
                                                 ::chat::lxmf::ByteSpan, ::chat::MessageId, double timestamp,
                                                 uint8_t accepted_hash[32] = nullptr);
    ::chat::MeshActionResult dial(const ::chat::ReticulumPeerIdentity&);
    void commitIncoming(const ::chat::MeshIncomingText&, bool durable);
    std::vector<::chat::MessageId> pendingOutboundIds() const;
    std::vector<std::array<uint8_t, 16>> pathDestinations() const;
    struct Snapshot
    {
        bool ready = false;
        uint64_t announcements = 0, path_responses = 0, rejected_routes = 0;
        LinuxReticulumTcp::Snapshot tcp;
        LinuxReticulumLinks::Statistics links;
        LinuxReticulumIngress::Statistics ingress;
        LinuxReticulumAuto::Snapshot automatic;
    };
    Snapshot snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trailmate::linux_app
