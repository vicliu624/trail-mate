/**
 * @file lxmf_delivery_attempt_ledger.cpp
 * @brief Reticulum/LXMF outbound delivery attempt and proof receipt owner.
 */

#include "chat/infra/lxmf/lxmf_delivery_attempt_ledger.h"

#include <algorithm>
#include <cstring>

namespace chat::lxmf::runtime
{
namespace
{

bool hashesEqual(const uint8_t* a, const uint8_t* b, std::size_t len)
{
    return a && b && std::memcmp(a, b, len) == 0;
}

void copyHash(uint8_t* out, const uint8_t* in, std::size_t len)
{
    if (!out || !in)
    {
        return;
    }
    std::memcpy(out, in, len);
}

} // namespace

void DeliveryAttemptLedger::noteDirectPacketReceipt(
    const uint8_t packet_hash[reticulum::kFullHashSize],
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const uint8_t peer_sig_pub[reticulum::kSigningPublicKeySize],
    MessageId message_id,
    uint32_t now_ms,
    std::size_t max_receipts)
{
    if (!packet_hash || !destination_hash || !peer_sig_pub || message_id == 0)
    {
        return;
    }

    uint8_t proof_hash[reticulum::kTruncatedHashSize] = {};
    copyHash(proof_hash, packet_hash, sizeof(proof_hash));
    removeReceiptByProofHash(proof_hash);

    DeliveryAttemptReceipt receipt{};
    copyHash(receipt.packet_hash, packet_hash, sizeof(receipt.packet_hash));
    copyHash(receipt.proof_hash, proof_hash, sizeof(receipt.proof_hash));
    copyHash(receipt.destination_hash,
             destination_hash,
             sizeof(receipt.destination_hash));
    copyHash(receipt.peer_sig_pub,
             peer_sig_pub,
             sizeof(receipt.peer_sig_pub));
    receipt.message_id = message_id;
    receipt.created_ms = now_ms == 0 ? 1U : now_ms;
    receipt.kind = DeliveryAttemptKind::DirectPacket;
    trimOldestReceipts(receipt.kind, max_receipts, true);
    receipts_.push_back(receipt);
}

void DeliveryAttemptLedger::noteLinkPacketReceipt(
    const uint8_t packet_hash[reticulum::kFullHashSize],
    const uint8_t link_id[reticulum::kTruncatedHashSize],
    MessageId message_id,
    uint32_t now_ms,
    std::size_t max_receipts)
{
    if (!packet_hash || !link_id || message_id == 0)
    {
        return;
    }

    removeLinkPacketReceipt(link_id, packet_hash);
    DeliveryAttemptReceipt receipt{};
    copyHash(receipt.packet_hash, packet_hash, sizeof(receipt.packet_hash));
    copyHash(receipt.link_id, link_id, sizeof(receipt.link_id));
    receipt.message_id = message_id;
    receipt.created_ms = now_ms == 0 ? 1U : now_ms;
    receipt.kind = DeliveryAttemptKind::LinkPacket;
    trimOldestReceipts(receipt.kind, max_receipts, true);
    receipts_.push_back(receipt);
}

void DeliveryAttemptLedger::noteLinkResourceReceipt(
    const uint8_t resource_hash[reticulum::kFullHashSize],
    const uint8_t link_id[reticulum::kTruncatedHashSize],
    MessageId message_id,
    uint32_t now_ms,
    std::size_t max_receipts)
{
    if (!resource_hash || !link_id || message_id == 0)
    {
        return;
    }

    removeLinkResourceReceipt(link_id, resource_hash);
    DeliveryAttemptReceipt receipt{};
    copyHash(receipt.packet_hash, resource_hash, sizeof(receipt.packet_hash));
    copyHash(receipt.link_id, link_id, sizeof(receipt.link_id));
    receipt.message_id = message_id;
    receipt.created_ms = now_ms == 0 ? 1U : now_ms;
    receipt.kind = DeliveryAttemptKind::LinkResource;
    trimOldestReceipts(receipt.kind, max_receipts, true);
    receipts_.push_back(receipt);
}

void DeliveryAttemptLedger::notePropagationReceipt(
    const uint8_t transient_id[reticulum::kFullHashSize],
    MessageId message_id,
    uint32_t now_ms,
    std::size_t max_receipts)
{
    if (!transient_id || message_id == 0)
    {
        return;
    }

    removePropagationReceipt(transient_id);
    DeliveryAttemptReceipt receipt{};
    copyHash(receipt.packet_hash, transient_id, sizeof(receipt.packet_hash));
    receipt.message_id = message_id;
    receipt.created_ms = now_ms == 0 ? 1U : now_ms;
    receipt.kind = DeliveryAttemptKind::Propagation;
    trimOldestReceipts(receipt.kind, max_receipts, true);
    receipts_.push_back(receipt);
}

DeliveryAttemptReceipt* DeliveryAttemptLedger::findReceiptByProofHash(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    if (!proof_hash)
    {
        return nullptr;
    }
    for (auto& receipt : receipts_)
    {
        if (receipt.created_ms != 0 &&
            hashesEqual(receipt.proof_hash,
                        proof_hash,
                        sizeof(receipt.proof_hash)))
        {
            return &receipt;
        }
    }
    return nullptr;
}

void DeliveryAttemptLedger::removeReceiptByProofHash(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    if (!proof_hash)
    {
        return;
    }
    receipts_.erase(
        std::remove_if(
            receipts_.begin(),
            receipts_.end(),
            [proof_hash](const DeliveryAttemptReceipt& receipt)
            {
                return hashesEqual(receipt.proof_hash,
                                   proof_hash,
                                   sizeof(receipt.proof_hash));
            }),
        receipts_.end());
}

DeliveryAttemptReceipt* DeliveryAttemptLedger::findLinkPacketReceipt(
    const uint8_t link_id[reticulum::kTruncatedHashSize],
    const uint8_t packet_hash[reticulum::kFullHashSize])
{
    if (!link_id || !packet_hash)
    {
        return nullptr;
    }
    for (auto& receipt : receipts_)
    {
        if (receipt.created_ms != 0 &&
            receipt.kind == DeliveryAttemptKind::LinkPacket &&
            hashesEqual(receipt.link_id, link_id, sizeof(receipt.link_id)) &&
            hashesEqual(receipt.packet_hash,
                        packet_hash,
                        sizeof(receipt.packet_hash)))
        {
            return &receipt;
        }
    }
    return nullptr;
}

void DeliveryAttemptLedger::removeLinkPacketReceipt(
    const uint8_t link_id[reticulum::kTruncatedHashSize],
    const uint8_t packet_hash[reticulum::kFullHashSize])
{
    if (!link_id || !packet_hash)
    {
        return;
    }
    receipts_.erase(
        std::remove_if(
            receipts_.begin(),
            receipts_.end(),
            [link_id, packet_hash](const DeliveryAttemptReceipt& receipt)
            {
                return receipt.kind == DeliveryAttemptKind::LinkPacket &&
                       hashesEqual(receipt.link_id,
                                   link_id,
                                   sizeof(receipt.link_id)) &&
                       hashesEqual(receipt.packet_hash,
                                   packet_hash,
                                   sizeof(receipt.packet_hash));
            }),
        receipts_.end());
}

DeliveryAttemptReceipt* DeliveryAttemptLedger::findLinkResourceReceipt(
    const uint8_t link_id[reticulum::kTruncatedHashSize],
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    if (!link_id || !resource_hash)
    {
        return nullptr;
    }
    for (auto& receipt : receipts_)
    {
        if (receipt.created_ms != 0 &&
            receipt.kind == DeliveryAttemptKind::LinkResource &&
            hashesEqual(receipt.link_id, link_id, sizeof(receipt.link_id)) &&
            hashesEqual(receipt.packet_hash,
                        resource_hash,
                        sizeof(receipt.packet_hash)))
        {
            return &receipt;
        }
    }
    return nullptr;
}

void DeliveryAttemptLedger::removeLinkResourceReceipt(
    const uint8_t link_id[reticulum::kTruncatedHashSize],
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    if (!link_id || !resource_hash)
    {
        return;
    }
    receipts_.erase(
        std::remove_if(
            receipts_.begin(),
            receipts_.end(),
            [link_id, resource_hash](const DeliveryAttemptReceipt& receipt)
            {
                return receipt.kind == DeliveryAttemptKind::LinkResource &&
                       hashesEqual(receipt.link_id,
                                   link_id,
                                   sizeof(receipt.link_id)) &&
                       hashesEqual(receipt.packet_hash,
                                   resource_hash,
                                   sizeof(receipt.packet_hash));
            }),
        receipts_.end());
}

DeliveryAttemptReceipt* DeliveryAttemptLedger::findPropagationReceipt(
    const uint8_t transient_id[reticulum::kFullHashSize])
{
    if (!transient_id)
    {
        return nullptr;
    }
    for (auto& receipt : receipts_)
    {
        if (receipt.created_ms != 0 &&
            receipt.kind == DeliveryAttemptKind::Propagation &&
            hashesEqual(receipt.packet_hash,
                        transient_id,
                        sizeof(receipt.packet_hash)))
        {
            return &receipt;
        }
    }
    return nullptr;
}

void DeliveryAttemptLedger::removePropagationReceipt(
    const uint8_t transient_id[reticulum::kFullHashSize])
{
    if (!transient_id)
    {
        return;
    }
    receipts_.erase(
        std::remove_if(
            receipts_.begin(),
            receipts_.end(),
            [transient_id](const DeliveryAttemptReceipt& receipt)
            {
                return receipt.kind == DeliveryAttemptKind::Propagation &&
                       hashesEqual(receipt.packet_hash,
                                   transient_id,
                                   sizeof(receipt.packet_hash));
            }),
        receipts_.end());
}

void DeliveryAttemptLedger::cull(DeliveryAttemptKind kind,
                                 uint32_t now_ms,
                                 uint32_t receipt_ttl_ms,
                                 std::size_t max_receipts)
{
    if (receipt_ttl_ms != 0)
    {
        receipts_.erase(
            std::remove_if(
                receipts_.begin(),
                receipts_.end(),
                [kind, now_ms, receipt_ttl_ms](
                    const DeliveryAttemptReceipt& receipt)
                {
                    return receipt.kind == kind &&
                           receipt.created_ms != 0 &&
                           now_ms - receipt.created_ms > receipt_ttl_ms;
                }),
            receipts_.end());
    }
    trimOldestReceipts(kind, max_receipts, false);
}

void DeliveryAttemptLedger::clear()
{
    receipts_.clear();
}

std::size_t DeliveryAttemptLedger::size() const
{
    return receipts_.size();
}

void DeliveryAttemptLedger::trimOldestReceipts(DeliveryAttemptKind kind,
                                               std::size_t max_receipts,
                                               bool reserve_slot)
{
    if (max_receipts == 0)
    {
        return;
    }

    std::size_t matching = 0;
    for (const auto& receipt : receipts_)
    {
        if (receipt.kind == kind)
        {
            ++matching;
        }
    }

    while (matching != 0 &&
           matching + (reserve_slot ? 1U : 0U) > max_receipts)
    {
        for (auto it = receipts_.begin(); it != receipts_.end(); ++it)
        {
            if (it->kind == kind)
            {
                receipts_.erase(it);
                --matching;
                break;
            }
        }
    }
}

} // namespace chat::lxmf::runtime
