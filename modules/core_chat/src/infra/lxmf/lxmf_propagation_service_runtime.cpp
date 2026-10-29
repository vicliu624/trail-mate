/**
 * @file lxmf_propagation_service_runtime.cpp
 * @brief LXMF propagation service request planning over propagation state.
 */

#include "chat/infra/lxmf/lxmf_propagation_service_runtime.h"

#include <cstring>
#include <utility>

namespace chat::lxmf::runtime
{
namespace
{

constexpr const char* kPropagationOfferPath = "/offer";
constexpr const char* kPropagationGetPath = "/get";
constexpr uint32_t kPropagationErrorNoIdentity = 0xF0;
constexpr uint32_t kPropagationErrorInvalidKey = 0xF3;
constexpr uint32_t kPropagationErrorInvalidData = 0xF4;

bool hashesEqual(const uint8_t* a, const uint8_t* b, std::size_t len)
{
    if ((!a || !b) && len != 0)
    {
        return false;
    }
    for (std::size_t index = 0; index < len; ++index)
    {
        if (a[index] != b[index])
        {
            return false;
        }
    }
    return true;
}

const char* pathText(PropagationServicePath path)
{
    return path == PropagationServicePath::Get ? kPropagationGetPath
                                               : kPropagationOfferPath;
}

bool packUintResponse(uint32_t value, PropagationServiceResponse* out_response)
{
    if (!out_response)
    {
        return false;
    }

    ResourcePayloadBuffer packed(8, 0);
    std::size_t packed_len = packed.size();
    if (!encodeMsgpackUint(value, packed.data(), &packed_len))
    {
        return false;
    }
    packed.resize(packed_len);
    out_response->send_response = true;
    out_response->response_data_is_nil = false;
    out_response->packed_response = std::move(packed);
    return true;
}

bool packBoolResponse(bool value, PropagationServiceResponse* out_response)
{
    if (!out_response)
    {
        return false;
    }

    ResourcePayloadBuffer packed(4, 0);
    std::size_t packed_len = packed.size();
    if (!encodeMsgpackBool(value, packed.data(), &packed_len))
    {
        return false;
    }
    packed.resize(packed_len);
    out_response->send_response = true;
    out_response->response_data_is_nil = false;
    out_response->packed_response = std::move(packed);
    return true;
}

bool packIdListResponse(const PropagationIdList& items,
                        PropagationServiceResponse* out_response)
{
    if (!out_response)
    {
        return false;
    }

    const std::size_t response_capacity =
        4 + (items.size() * (reticulum::kFullHashSize + 3));
    ResourcePayloadBuffer packed(response_capacity, 0);
    std::size_t packed_len = packed.size();
    const RuntimeByteSpanList spans = makeRuntimeByteSpans(items);
    if (!encodePropagationIdListPayload(viewRuntimeByteSpans(spans),
                                        packed.data(),
                                        &packed_len))
    {
        return false;
    }
    packed.resize(packed_len);
    out_response->send_response = true;
    out_response->response_data_is_nil = false;
    out_response->packed_response = std::move(packed);
    return true;
}

bool packMessageListResponse(const RuntimeByteSpanList& items,
                             PropagationServiceResponse* out_response)
{
    if (!out_response)
    {
        return false;
    }

    std::size_t response_capacity = 4;
    for (const auto& item : items)
    {
        response_capacity += item.size + 3;
    }
    ResourcePayloadBuffer packed(response_capacity, 0);
    std::size_t packed_len = packed.size();
    if (!encodePropagationMessageListPayload(viewRuntimeByteSpans(items),
                                             packed.data(),
                                             &packed_len))
    {
        return false;
    }
    packed.resize(packed_len);
    out_response->send_response = true;
    out_response->response_data_is_nil = false;
    out_response->packed_response = std::move(packed);
    return true;
}

bool planOfferResponse(PropagationRuntime& propagation,
                       const DecodedLinkRequest& request,
                       const PropagationServicePeerContext& peer_context,
                       PropagationServiceResponse* out_response)
{
    if (request.data_is_nil)
    {
        return packUintResponse(kPropagationErrorInvalidData, out_response);
    }

    PropagationIdList transient_ids;
    DecodedPropagationOfferHeader offer{};
    if (!decodePropagationOfferPayload(request.packed_data.data(),
                                       request.packed_data.size(),
                                       appendRuntimeByteBufferCallback,
                                       &transient_ids,
                                       &offer))
    {
        return packUintResponse(kPropagationErrorInvalidData, out_response);
    }

    if (offer.peering_key_is_nil || offer.peering_key.size == 0U)
    {
        return packUintResponse(kPropagationErrorInvalidKey, out_response);
    }

    out_response->offer_validated = true;

    PropagationIdList wanted_ids =
        collectMissingPropagationTransientIds(propagation, transient_ids);

    if (wanted_ids.empty())
    {
        return packBoolResponse(false, out_response);
    }

    if (wanted_ids.size() == transient_ids.size())
    {
        return packBoolResponse(true, out_response);
    }

    (void)peer_context;
    return packIdListResponse(wanted_ids, out_response);
}

bool planGetResponse(PropagationRuntime& propagation,
                     const DecodedLinkRequest& request,
                     const PropagationServicePeerContext& peer_context,
                     uint32_t now_s,
                     const PropagationServiceLimits& limits,
                     PropagationPeerState& peer_state,
                     PropagationServiceResponse* out_response)
{
    if (request.data_is_nil)
    {
        return packUintResponse(kPropagationErrorInvalidData, out_response);
    }

    PropagationIdList wants;
    PropagationIdList haves;
    DecodedPropagationGetRequestHeader get_request{};
    if (!decodePropagationGetRequestPayload(request.packed_data.data(),
                                            request.packed_data.size(),
                                            appendRuntimeByteBufferCallback,
                                            &wants,
                                            appendRuntimeByteBufferCallback,
                                            &haves,
                                            &get_request))
    {
        return packUintResponse(kPropagationErrorInvalidData, out_response);
    }

    if (!get_request.haves_is_nil)
    {
        for (const auto& transient_id : haves)
        {
            if (transient_id.size() != reticulum::kFullHashSize)
            {
                continue;
            }

            (void)removePropagationEntriesForDestination(
                propagation,
                transient_id.data(),
                peer_context.remote_delivery_hash);
            rememberPropagationTransient(propagation,
                                         transient_id.data(),
                                         true,
                                         now_s,
                                         limits.max_transients);
        }
    }

    if (get_request.wants_is_nil && get_request.haves_is_nil)
    {
        PropagationIdList response_items =
            collectPropagationEntryIdsForDestination(
                propagation,
                peer_context.remote_delivery_hash);
        return packIdListResponse(response_items, out_response);
    }

    const std::size_t transfer_limit_bytes =
        get_request.has_transfer_limit
            ? (static_cast<std::size_t>(get_request.transfer_limit_kb) * 1000U)
            : 0U;
    const PropagationMessageSelection selection =
        collectPropagationMessagesForWants(propagation,
                                           wants,
                                           peer_context.remote_delivery_hash,
                                           transfer_limit_bytes,
                                           limits.base_response_size,
                                           limits.per_message_overhead);
    notePropagationPeerServedMessages(peer_state, selection.served_count);
    return packMessageListResponse(selection.messages, out_response);
}

} // namespace

void propagationServicePathHash(
    PropagationServicePath path,
    uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    if (!out_hash)
    {
        return;
    }

#if defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
    std::memset(out_hash, 0, reticulum::kTruncatedHashSize);
    out_hash[0] = path == PropagationServicePath::Get ? 0x47 : 0x4F;
    out_hash[1] = path == PropagationServicePath::Get ? 0x45 : 0x46;
    out_hash[2] = path == PropagationServicePath::Get ? 0x54 : 0x46;
#else
    const char* text = pathText(path);
    reticulum::truncatedHash(reinterpret_cast<const uint8_t*>(text),
                             std::strlen(text),
                             out_hash);
#endif
}

bool planPropagationMessageAcceptance(
    PropagationRuntime& propagation,
    const uint8_t* lxmf_data,
    std::size_t lxmf_len,
    const PropagationMessageContext& context,
    PropagationMessageAcceptance* out_acceptance)
{
    if (!out_acceptance)
    {
        return false;
    }

    PropagationMessageAcceptance acceptance{};
    if (!lxmf_data || lxmf_len <= reticulum::kTruncatedHashSize)
    {
        *out_acceptance = std::move(acceptance);
        return false;
    }

    reticulum::fullHash(lxmf_data, lxmf_len, acceptance.transient_id);
    std::memcpy(acceptance.destination_hash,
                lxmf_data,
                sizeof(acceptance.destination_hash));

    if (findPropagationEntry(propagation, acceptance.transient_id) ||
        hasSeenPropagationTransient(propagation, acceptance.transient_id, nullptr))
    {
        acceptance.action = PropagationMessageAction::Duplicate;
        *out_acceptance = std::move(acceptance);
        return true;
    }

    if (context.local_delivery_hash_known &&
        hashesEqual(acceptance.destination_hash,
                    context.local_delivery_hash,
                    sizeof(acceptance.destination_hash)))
    {
        acceptance.action = PropagationMessageAction::DeliverLocal;
        acceptance.local_delivery_payload.assign(
            lxmf_data + reticulum::kTruncatedHashSize,
            lxmf_data + lxmf_len);
        *out_acceptance = std::move(acceptance);
        return true;
    }

    if (!rememberPropagationEntry(propagation,
                                  acceptance.transient_id,
                                  acceptance.destination_hash,
                                  lxmf_data,
                                  lxmf_len,
                                  context.now_s,
                                  context.max_entries))
    {
        *out_acceptance = std::move(acceptance);
        return false;
    }

    rememberPropagationTransient(propagation,
                                 acceptance.transient_id,
                                 false,
                                 context.now_s,
                                 context.max_transients);

    if (context.remote_propagation_hash_known)
    {
        PropagationPeerState* peer =
            findPropagationPeer(propagation, context.remote_propagation_hash);
        if (peer)
        {
            markPropagationPeerSeen(*peer, context.now_s);
        }
    }

    acceptance.action = PropagationMessageAction::Stored;
    *out_acceptance = std::move(acceptance);
    return true;
}

void notePropagationLocalDeliveryResult(
    PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize],
    bool delivered,
    uint32_t now_s,
    std::size_t max_transients)
{
    if (!delivered)
    {
        return;
    }

    rememberPropagationTransient(propagation,
                                 transient_id,
                                 true,
                                 now_s,
                                 max_transients);
}

bool planPropagationBatchAcceptance(
    PropagationRuntime& propagation,
    const uint8_t* plaintext,
    std::size_t plaintext_len,
    const PropagationBatchContext& context,
    const PropagationBatchLimits& limits,
    PropagationBatchAcceptance* out_acceptance)
{
    if (!out_acceptance)
    {
        return false;
    }

    PropagationBatchAcceptance acceptance{};
    PropagationMessageList messages;
    double remote_timebase = 0.0;
    if (!decodePropagationBatch(plaintext,
                                plaintext_len,
                                appendRuntimeByteBufferCallback,
                                &messages,
                                &remote_timebase))
    {
        *out_acceptance = std::move(acceptance);
        return false;
    }

    if (!context.offer_validated &&
        messages.size() > limits.max_messages_without_offer)
    {
        *out_acceptance = std::move(acceptance);
        return false;
    }

    PropagationMessageContext message_context{};
    message_context.local_delivery_hash_known = context.local_delivery_hash_known;
    if (context.local_delivery_hash_known)
    {
        std::memcpy(message_context.local_delivery_hash,
                    context.local_delivery_hash,
                    sizeof(message_context.local_delivery_hash));
    }
    message_context.now_s = context.now_s;
    message_context.max_entries = limits.max_entries;
    message_context.max_transients = limits.max_transients;

    if (context.peer_context.remote_identity_known)
    {
        PropagationPeerState& peer =
            upsertPropagationPeer(propagation,
                                  context.peer_context.remote_propagation_hash,
                                  context.peer_context.remote_delivery_hash,
                                  context.peer_context.remote_identity_hash,
                                  limits.max_peers);
        markPropagationPeerSeen(peer, context.now_s);

        acceptance.remote_propagation_hash_known = true;
        std::memcpy(acceptance.remote_propagation_hash,
                    context.peer_context.remote_propagation_hash,
                    sizeof(acceptance.remote_propagation_hash));
        message_context.remote_propagation_hash_known = true;
        std::memcpy(message_context.remote_propagation_hash,
                    context.peer_context.remote_propagation_hash,
                    sizeof(message_context.remote_propagation_hash));
    }

    (void)remote_timebase;
    acceptance.messages.reserve(messages.size());
    for (const auto& message : messages)
    {
        PropagationMessageAcceptance message_acceptance{};
        (void)planPropagationMessageAcceptance(propagation,
                                               message.data(),
                                               message.size(),
                                               message_context,
                                               &message_acceptance);
        acceptance.messages.push_back(std::move(message_acceptance));
    }

    *out_acceptance = std::move(acceptance);
    return true;
}

void notePropagationBatchMessageHandled(
    PropagationRuntime& propagation,
    const PropagationBatchAcceptance& acceptance)
{
    if (!acceptance.remote_propagation_hash_known)
    {
        return;
    }

    PropagationPeerState* peer =
        findPropagationPeer(propagation, acceptance.remote_propagation_hash);
    if (peer)
    {
        notePropagationPeerIncomingMessage(*peer);
    }
}

bool planPropagationServiceResponse(
    PropagationRuntime& propagation,
    const DecodedLinkRequest& request,
    const PropagationServicePeerContext& peer_context,
    uint32_t now_s,
    const PropagationServiceLimits& limits,
    PropagationServiceResponse* out_response)
{
    if (!out_response)
    {
        return false;
    }

    PropagationServiceResponse response{};

    if (!peer_context.remote_identity_known)
    {
        return packUintResponse(kPropagationErrorNoIdentity, out_response);
    }

    PropagationPeerState& peer_state =
        upsertPropagationPeer(propagation,
                              peer_context.remote_propagation_hash,
                              peer_context.remote_delivery_hash,
                              peer_context.remote_identity_hash,
                              limits.max_peers);
    markPropagationPeerSeen(peer_state, now_s);

    uint8_t offer_path_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t get_path_hash[reticulum::kTruncatedHashSize] = {};
    propagationServicePathHash(PropagationServicePath::Offer, offer_path_hash);
    propagationServicePathHash(PropagationServicePath::Get, get_path_hash);

    if (hashesEqual(request.path_hash, offer_path_hash, sizeof(offer_path_hash)))
    {
        if (!planOfferResponse(propagation, request, peer_context, &response))
        {
            return false;
        }
        *out_response = std::move(response);
        return true;
    }

    if (!hashesEqual(request.path_hash, get_path_hash, sizeof(get_path_hash)))
    {
        return packUintResponse(kPropagationErrorInvalidData, out_response);
    }

    if (!planGetResponse(propagation,
                         request,
                         peer_context,
                         now_s,
                         limits,
                         peer_state,
                         &response))
    {
        return false;
    }
    *out_response = std::move(response);
    return true;
}

} // namespace chat::lxmf::runtime
