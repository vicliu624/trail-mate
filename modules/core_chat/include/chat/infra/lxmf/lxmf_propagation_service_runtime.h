/**
 * @file lxmf_propagation_service_runtime.h
 * @brief LXMF propagation service request planning over propagation state.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_memory.h"
#include "chat/infra/lxmf/lxmf_propagation_runtime.h"
#include "chat/infra/lxmf/lxmf_wire.h"

#include <cstddef>
#include <cstdint>

namespace chat::lxmf::runtime
{

enum class PropagationServicePath : uint8_t
{
    Offer = 0,
    Get = 1
};

struct PropagationServicePeerContext
{
    bool remote_identity_known = false;
    uint8_t remote_delivery_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t remote_propagation_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t remote_identity_hash[reticulum::kTruncatedHashSize] = {};
};

struct PropagationServiceLimits
{
    std::size_t max_transients = 0;
    std::size_t max_peers = 0;
    std::size_t base_response_size = 24;
    std::size_t per_message_overhead = 16;
};

struct PropagationServiceResponse
{
    bool send_response = false;
    bool response_data_is_nil = false;
    bool offer_validated = false;
    ResourcePayloadBuffer packed_response;
};

enum class PropagationMessageAction : uint8_t
{
    Rejected = 0,
    Duplicate = 1,
    DeliverLocal = 2,
    Stored = 3
};

struct PropagationMessageContext
{
    bool local_delivery_hash_known = false;
    uint8_t local_delivery_hash[reticulum::kTruncatedHashSize] = {};
    bool remote_propagation_hash_known = false;
    uint8_t remote_propagation_hash[reticulum::kTruncatedHashSize] = {};
    uint32_t now_s = 0;
    std::size_t max_entries = 0;
    std::size_t max_transients = 0;
};

struct PropagationMessageAcceptance
{
    PropagationMessageAction action = PropagationMessageAction::Rejected;
    uint8_t transient_id[reticulum::kFullHashSize] = {};
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    ResourcePayloadBuffer local_delivery_payload;
};

using PropagationMessageAcceptanceList =
    std::vector<PropagationMessageAcceptance,
                PsramAllocator<PropagationMessageAcceptance>>;

struct PropagationBatchContext
{
    bool offer_validated = false;
    bool local_delivery_hash_known = false;
    uint8_t local_delivery_hash[reticulum::kTruncatedHashSize] = {};
    PropagationServicePeerContext peer_context;
    uint32_t now_s = 0;
};

struct PropagationBatchLimits
{
    std::size_t max_entries = 0;
    std::size_t max_transients = 0;
    std::size_t max_peers = 0;
    std::size_t max_messages_without_offer = 1;
};

struct PropagationBatchAcceptance
{
    bool remote_propagation_hash_known = false;
    uint8_t remote_propagation_hash[reticulum::kTruncatedHashSize] = {};
    PropagationMessageAcceptanceList messages;
};

void propagationServicePathHash(
    PropagationServicePath path,
    uint8_t out_hash[reticulum::kTruncatedHashSize]);

bool planPropagationServiceResponse(
    PropagationRuntime& propagation,
    const DecodedLinkRequest& request,
    const PropagationServicePeerContext& peer_context,
    uint32_t now_s,
    const PropagationServiceLimits& limits,
    PropagationServiceResponse* out_response);

bool planPropagationMessageAcceptance(
    PropagationRuntime& propagation,
    const uint8_t* lxmf_data,
    std::size_t lxmf_len,
    const PropagationMessageContext& context,
    PropagationMessageAcceptance* out_acceptance);

void notePropagationLocalDeliveryResult(
    PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize],
    bool delivered,
    uint32_t now_s,
    std::size_t max_transients);

bool planPropagationBatchAcceptance(
    PropagationRuntime& propagation,
    const uint8_t* plaintext,
    std::size_t plaintext_len,
    const PropagationBatchContext& context,
    const PropagationBatchLimits& limits,
    PropagationBatchAcceptance* out_acceptance);

void notePropagationBatchMessageHandled(
    PropagationRuntime& propagation,
    const PropagationBatchAcceptance& acceptance);

} // namespace chat::lxmf::runtime
