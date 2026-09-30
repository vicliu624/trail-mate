#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_delivery_planner.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Execute the production dispatcher with deterministic transport/crypto seams.
// Cryptographic wire compatibility is covered separately by the vector tests.
namespace reticulum = chat::reticulum;
namespace runtime = chat::lxmf::runtime;
namespace chat::reticulum
{
inline void computePacketHash(const uint8_t*, size_t, uint8_t*) {}
} // namespace chat::reticulum
namespace chat::lxmf::runtime
{
using RuntimeByteBuffer = std::vector<uint8_t>;
struct DeferredLinkPayload
{
    RuntimeByteBuffer payload;
    uint32_t message_id = 0;
};
} // namespace chat::lxmf::runtime
namespace rtnet
{
struct Config
{
    struct
    {
        bool enabled = false;
        reticulum::LxmfDeliveryPreference delivery = reticulum::LxmfDeliveryPreference::Automatic;
    } propagation;
};
const Config& active()
{
    static Config config;
    return config;
}
} // namespace rtnet
enum class MeshOperationFailure
{
    None,
    EncodeFailed,
    CryptoFailed,
    RadioTxFailed
};
enum class LocalDestinationKind
{
    Delivery
};
enum class LinkState
{
    Active,
    Pending
};
struct PeerInfo
{
    uint8_t destination_hash[16]{};
    uint8_t sig_pub[32]{};
};
struct LinkSession
{
    LinkState state = LinkState::Pending;
};
struct OutboundLxmfDispatch
{
    bool ok = false;
    bool result_event_deferred = false;
    uint32_t message_id = 0;
    MeshOperationFailure failure = MeshOperationFailure::None;
    uint8_t message_hash[32]{};
    const char* path = "none";
};
constexpr size_t kMaxPacketLen = 500;
constexpr size_t kMaxPendingDeliveryReceipts = 8;
uint32_t millis() { return 123; }
uint32_t messageIdFromHash(const uint8_t*) { return 42; }

struct LxmfAdapter
{
    bool transport_ok = true;
    bool ratchet = true;
    bool link_available = false;
    LinkSession link;
    struct Identity
    {
        const uint8_t* destinationHash() { return nullptr; }
        bool sign(const uint8_t*, size_t, uint8_t*) { return true; }
    } identity_;
    struct Ledger
    {
        unsigned receipts = 0;
        void noteDirectPacketReceipt(const uint8_t*, const uint8_t*, const uint8_t*, uint32_t, uint32_t, size_t) { ++receipts; }
    } delivery_attempt_ledger_;
    struct Notifier
    {
        std::vector<uint32_t> sent_ids;
        void sent(uint32_t id) { sent_ids.push_back(id); }
    } delivery_notifier_;
    struct Links
    {
        unsigned queued = 0;
        void appendDeferredPayload(LinkSession&, runtime::DeferredLinkPayload&&) { ++queued; }
    } link_manager_;
    bool shouldRequestPath(PeerInfo&) { return false; }
    bool sendPathRequest(PeerInfo&) { return true; }
    bool buildSignedPart(const uint8_t*, const uint8_t*, const uint8_t*, size_t, uint8_t*, size_t*, uint8_t*) { return true; }
    bool packMessage(const uint8_t*, const uint8_t*, const uint8_t*, const uint8_t*, size_t, uint8_t*, size_t*) { return true; }
    LinkSession* findActiveLinkSessionByDestination(const uint8_t*, LocalDestinationKind) { return nullptr; }
    bool peerHasUsableRatchet(PeerInfo&) { return ratchet; }
    const void* selectActivePropagationPeer() { return nullptr; }
    bool queuePropagationUpload(PeerInfo&, const uint8_t*, size_t, uint32_t, const uint8_t*, bool, OutboundLxmfDispatch*) { return false; }
    void flushDeferredLinkPayloads(LinkSession&) {}
    bool buildSignedMessagePacket(PeerInfo&, const uint8_t*, size_t, uint8_t*, size_t*, uint8_t*) { return true; }
    bool routeAndSendPacket(const uint8_t*, size_t, bool) { return transport_ok; }
    LinkSession* ensureOutboundLinkSession(PeerInfo&, LocalDestinationKind, bool*) { return link_available ? &link : nullptr; }
    bool dispatchLxmfPayload(PeerInfo&, const uint8_t*, size_t, bool, OutboundLxmfDispatch*, bool);
};
#include "dispatch_actual.inc"

int main()
{
    PeerInfo peer;
    uint8_t payload[]{1, 2, 3};
    OutboundLxmfDispatch result;
    LxmfAdapter success;
    assert(success.dispatchLxmfPayload(peer, payload, sizeof(payload), true, &result, false));
    assert(success.delivery_attempt_ledger_.receipts == 1);
    assert(success.delivery_notifier_.sent_ids == std::vector<uint32_t>{42});

    LxmfAdapter custom;
    assert(custom.dispatchLxmfPayload(peer, payload, sizeof(payload), false, &result, false));
    assert(custom.delivery_attempt_ledger_.receipts == 0);
    assert(custom.delivery_notifier_.sent_ids.empty());

    LxmfAdapter failed;
    failed.transport_ok = false;
    assert(!failed.dispatchLxmfPayload(peer, payload, sizeof(payload), true, &result, false));
    assert(failed.delivery_notifier_.sent_ids.empty());

    LxmfAdapter deferred;
    deferred.transport_ok = false;
    deferred.link_available = true;
    assert(deferred.dispatchLxmfPayload(peer, payload, sizeof(payload), true, &result, false));
    assert(result.result_event_deferred);
    assert(deferred.link_manager_.queued == 1);
    assert(deferred.delivery_notifier_.sent_ids.empty());
}
