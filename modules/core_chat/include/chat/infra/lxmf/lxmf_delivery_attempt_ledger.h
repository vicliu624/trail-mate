/**
 * @file lxmf_delivery_attempt_ledger.h
 * @brief Reticulum/LXMF outbound delivery attempt and proof receipt owner.
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "chat/infra/lxmf/lxmf_memory.h"

#include <cstring>

namespace chat::lxmf::runtime
{

enum class DeliveryAttemptKind : uint8_t
{
    DirectPacket = 0,
    LinkPacket = 1,
    LinkResource = 2,
    Propagation = 3,
};

struct DeliveryAttemptReceipt
{
    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    uint8_t proof_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t link_id[reticulum::kTruncatedHashSize] = {};
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t peer_sig_pub[reticulum::kSigningPublicKeySize] = {};
    MessageId message_id = 0;
    uint32_t created_ms = 0;
    DeliveryAttemptKind kind = DeliveryAttemptKind::DirectPacket;
};

class DeliveryAttemptLedger
{
  public:
    DeliveryAttemptLedger() = default;
    DeliveryAttemptLedger(const DeliveryAttemptLedger&) = delete;
    DeliveryAttemptLedger& operator=(const DeliveryAttemptLedger&) = delete;
    DeliveryAttemptLedger(DeliveryAttemptLedger&&) = delete;
    DeliveryAttemptLedger& operator=(DeliveryAttemptLedger&&) = delete;

    void noteDirectPacketReceipt(
        const uint8_t packet_hash[reticulum::kFullHashSize],
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        const uint8_t peer_sig_pub[reticulum::kSigningPublicKeySize],
        MessageId message_id,
        uint32_t now_ms,
        std::size_t max_receipts);
    void noteLinkPacketReceipt(
        const uint8_t packet_hash[reticulum::kFullHashSize],
        const uint8_t link_id[reticulum::kTruncatedHashSize],
        MessageId message_id,
        uint32_t now_ms,
        std::size_t max_receipts);
    void noteLinkResourceReceipt(
        const uint8_t resource_hash[reticulum::kFullHashSize],
        const uint8_t link_id[reticulum::kTruncatedHashSize],
        MessageId message_id,
        uint32_t now_ms,
        std::size_t max_receipts);
    void notePropagationReceipt(
        const uint8_t transient_id[reticulum::kFullHashSize],
        MessageId message_id,
        uint32_t now_ms,
        std::size_t max_receipts);

    DeliveryAttemptReceipt* findReceiptByProofHash(
        const uint8_t proof_hash[reticulum::kTruncatedHashSize]);
    void removeReceiptByProofHash(
        const uint8_t proof_hash[reticulum::kTruncatedHashSize]);
    DeliveryAttemptReceipt* findLinkPacketReceipt(
        const uint8_t link_id[reticulum::kTruncatedHashSize],
        const uint8_t packet_hash[reticulum::kFullHashSize]);
    void removeLinkPacketReceipt(
        const uint8_t link_id[reticulum::kTruncatedHashSize],
        const uint8_t packet_hash[reticulum::kFullHashSize]);
    DeliveryAttemptReceipt* findLinkResourceReceipt(
        const uint8_t link_id[reticulum::kTruncatedHashSize],
        const uint8_t resource_hash[reticulum::kFullHashSize]);
    void removeLinkResourceReceipt(
        const uint8_t link_id[reticulum::kTruncatedHashSize],
        const uint8_t resource_hash[reticulum::kFullHashSize]);
    DeliveryAttemptReceipt* findPropagationReceipt(
        const uint8_t transient_id[reticulum::kFullHashSize]);
    void removePropagationReceipt(
        const uint8_t transient_id[reticulum::kFullHashSize]);

    template <typename Fn>
    void forEachReceipt(Fn&& fn) const
    {
        for (const auto& receipt : receipts_)
        {
            fn(receipt);
        }
    }

    template <typename Fn>
    void takeReceiptsForLink(
        const uint8_t link_id[reticulum::kTruncatedHashSize],
        Fn&& fn)
    {
        if (!link_id)
        {
            return;
        }
        for (auto it = receipts_.begin(); it != receipts_.end();)
        {
            if (std::memcmp(it->link_id, link_id, sizeof(it->link_id)) == 0)
            {
                const DeliveryAttemptReceipt receipt = *it;
                it = receipts_.erase(it);
                fn(receipt);
            }
            else
            {
                ++it;
            }
        }
    }

    template <typename Fn>
    void takeExpiredReceipts(DeliveryAttemptKind kind,
                             uint32_t now_ms,
                             uint32_t receipt_ttl_ms,
                             Fn&& fn)
    {
        if (receipt_ttl_ms == 0)
        {
            return;
        }
        for (auto it = receipts_.begin(); it != receipts_.end();)
        {
            if (it->kind == kind &&
                (it->created_ms == 0 ||
                 now_ms - it->created_ms > receipt_ttl_ms))
            {
                const DeliveryAttemptReceipt receipt = *it;
                it = receipts_.erase(it);
                fn(receipt);
            }
            else
            {
                ++it;
            }
        }
    }

    void cull(DeliveryAttemptKind kind,
              uint32_t now_ms,
              uint32_t receipt_ttl_ms,
              std::size_t max_receipts);
    void clear();
    std::size_t size() const;

  private:
    using ReceiptVector =
        std::vector<DeliveryAttemptReceipt,
                    PsramAllocator<DeliveryAttemptReceipt>>;

    void trimOldestReceipts(DeliveryAttemptKind kind,
                            std::size_t max_receipts,
                            bool reserve_slot);

    ReceiptVector receipts_;
};

} // namespace chat::lxmf::runtime
