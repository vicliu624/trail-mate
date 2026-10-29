#include "chat/infra/lxmf/lxmf_resource_wire.h"
#include "chat/linux_reticulum_links.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace trailmate::linux_app
{
namespace r = chat::reticulum;
namespace state = chat::lxmf::runtime;
LinuxReticulumLinks::ServiceRequestResult LinuxReticulumLinks::queuePropagationUpload(const uint8_t destination[16], const uint8_t* data,
                                                                                      size_t size, uint32_t now, Sender sender, void* context, uint8_t hash[32])
{
    using Result = ServiceRequestResult;
    if (!destination || !data || !size || size > state::kMaxAdvertisedResourceBytes - 128 || !hash || !sender) return Result::Invalid;
    auto* session = links_.findOpenSessionByDestination(destination, state::LocalDestinationKind::Propagation);
    if (!session || !session->initiator || !session->validated || session->state != state::LinkState::Active) return Result::NotReady;
    auto receipt = std::find_if(upload_receipts_.begin(), upload_receipts_.end(), [](const auto& value)
                                { return !value.used; });
    if (receipt == upload_receipts_.end()) return Result::Busy;
    if (std::none_of(resource_io_.begin(), resource_io_.end(), [](const auto& io)
                     { return !io.used; })) return Result::Busy;
    if (!session->local_identity_sent)
    {
        if (!identify(*session, sender, context)) return Result::TxFailed;
        session->local_identity_sent = true;
    }
    uint8_t resource_hash[32];
    if (!startResource(*session, data, size, 0, now, sender, context, 0, nullptr, resource_hash)) return Result::TxFailed;
    receipt->used = true;
    std::memcpy(receipt->link, session->link_id, 16);
    std::memcpy(receipt->hash, resource_hash, 32);
    std::memcpy(hash, resource_hash, 32);
    return Result::Queued;
}
bool LinuxReticulumLinks::hasPropagationUpload(const uint8_t hash[32]) const
{
    return hash && std::any_of(upload_receipts_.begin(), upload_receipts_.end(), [&](const auto& value)
                               { return value.used && !std::memcmp(value.hash, hash, 32); });
}
bool LinuxReticulumLinks::takePropagationUploadResult(const uint8_t hash[32], UploadResult& result)
{
    if (!hash) return false;
    for (auto& receipt : upload_receipts_)
    {
        if (!receipt.used || std::memcmp(receipt.hash, hash, 32)) continue;
        auto* session = links_.findSession(receipt.link);
        if (!receipt.accepted && session && session->state == state::LinkState::Active && links_.findOutgoingResource(*session, hash)) return false;
        result = receipt.accepted ? UploadResult::TransportAccepted : UploadResult::Failed;
        receipt = {};
        return true;
    }
    return false;
}
bool LinuxReticulumLinks::hasPropagationDestination(const uint8_t destination[16])
{
    return destination && links_.findOpenSessionByDestination(destination, state::LocalDestinationKind::Propagation);
}
void LinuxReticulumLinks::closePropagation(const uint8_t destination[16], uint32_t now, Sender sender, void* context)
{
    if (!destination) return;
    if (auto* session = links_.findOpenSessionByDestination(destination, state::LocalDestinationKind::Propagation))
    {
        uint8_t id[16];
        std::memcpy(id, session->link_id, sizeof(id));
        (void)closeLink(id, now, state::LinkCloseReason::Timeout, sender, context);
    }
}
LinuxReticulumLinks::ServiceRequestResult LinuxReticulumLinks::requestService(
    const uint8_t destination[16], const uint8_t path[16], const uint8_t* data, size_t size, bool nil,
    double requested_at, uint32_t now, Sender sender, void* context, uint8_t output[16])
{
    using Result = ServiceRequestResult;
    if (output) std::memset(output, 0, 16);
    if (!destination || !path || !output || !sender || (!nil && (!data || !size)) ||
        (nil && size) || size > state::kMaxAdvertisedResourceBytes - 128 || !std::isfinite(requested_at) || requested_at < 0) return Result::Invalid;
    auto* session = links_.findOpenSessionByDestination(destination, state::LocalDestinationKind::Propagation);
    if (!session || !session->initiator || !session->validated || session->state != state::LinkState::Active) return Result::NotReady;
    state::ResourcePayloadBuffer payload(size + 64);
    size_t packed_size = payload.size();
    if (!chat::lxmf::encodeLinkRequestPayload(requested_at, path, data, size, nil, payload.data(), &packed_size)) return Result::Invalid;
    payload.resize(packed_size);
    uint8_t id[16]{};
    if (links_.pendingRequestCount(*session) >= 4) return Result::Busy;
    if (!session->local_identity_sent)
    {
        if (!identify(*session, sender, context)) return Result::TxFailed;
        session->local_identity_sent = true;
    }
    const bool packet = payload.size() <= session->mdu;
    if (packet)
    {
        if (!preparePacket(*session, r::PacketContext::Request, payload.data(), payload.size(), true)) return Result::TxFailed;
        r::computeTruncatedPacketHash(packet_.data(), last_packet_size_, id);
    }
    else r::truncatedHash(payload.data(), payload.size(), id);
    if (links_.findPendingRequest(*session, id, sizeof(id)))
    {
        std::memcpy(output, id, 16);
        return Result::Duplicate;
    }
    auto* pending = links_.queuePendingRequest(*session, id, sizeof(id), now, false);
    if (!pending) return Result::Busy;
    const bool sent = packet ? transmitPrepared(sender, context) : startResource(*session, payload.data(), payload.size(), 0, now, sender, context, state::kResourceRequestFlag, id);
    if (!sent)
    {
        links_.erasePendingRequest(*session, *pending);
        return Result::TxFailed;
    }
    std::memcpy(output, id, 16);
    links_.touchOutbound(*session, now);
    return Result::Queued;
}
bool LinuxReticulumLinks::serviceResponse(Session& session, const uint8_t* data, size_t size, const uint8_t* resource_id)
{
    if (session.destination != state::LocalDestinationKind::Propagation || !session.initiator || !session.validated || session.state != state::LinkState::Active) return false;
    chat::lxmf::DecodedLinkResponse response;
    if (!chat::lxmf::decodeLinkResponsePayload(data, size, &response) || response.request_id.size() != 16 ||
        (resource_id && std::memcmp(resource_id, response.request_id.data(), 16))) return false;
    auto* pending = links_.findPendingRequest(session, response.request_id.data(), response.request_id.size());
    if (!pending) return false;
    if (pending->response_ready)
        return response.data_is_nil ? pending->response.empty() : pending->response.size() == response.packed_data.size() && !std::memcmp(pending->response.data(), response.packed_data.data(), response.packed_data.size());
    return links_.markPendingResponseReady(session, response.request_id.data(), response.request_id.size(),
                                           response.packed_data.data(), response.packed_data.size(), response.data_is_nil);
}
bool LinuxReticulumLinks::takeServiceResponse(const uint8_t destination[16], const uint8_t id[16], ServiceResponse& output)
{
    if (!destination || !id) return false;
    auto* session = links_.findOpenSessionByDestination(destination, state::LocalDestinationKind::Propagation);
    auto* pending = session ? links_.findPendingRequest(*session, id, 16) : nullptr;
    if (!pending || !pending->response_ready) return false;
    output.payload = std::move(pending->response);
    output.data_is_nil = output.payload.empty();
    return links_.erasePendingRequest(*session, *pending);
}
bool LinuxReticulumLinks::serviceRequestPending(const uint8_t destination[16], const uint8_t id[16]) const
{
    if (!destination || !id) return false;
    bool pending = false;
    links_.forEachSession([&](const Session& session)
                          {
            if (session.state != state::LinkState::Closed && session.destination == state::LocalDestinationKind::Propagation &&
                !std::memcmp(session.remote_destination_hash, destination, 16))
                pending |= links_.findPendingRequest(session, id, 16) != nullptr; });
    return pending;
}
} // namespace trailmate::linux_app
