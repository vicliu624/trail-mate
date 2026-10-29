/**
 * @file lxmf_destination_registry.h
 * @brief Destination and identity registry owner for the embedded LXMF runtime.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_runtime_state.h"

#include <cstddef>
#include <cstdint>

namespace chat::lxmf::runtime
{

class DestinationRegistry
{
  public:
    DestinationRegistry() = default;
    DestinationRegistry(const DestinationRegistry&) = delete;
    DestinationRegistry& operator=(const DestinationRegistry&) = delete;
    DestinationRegistry(DestinationRegistry&&) = delete;
    DestinationRegistry& operator=(DestinationRegistry&&) = delete;

    std::size_t size() const;
    void clear();

    PeerInfo* findByNodeId(NodeId node_id);
    const PeerInfo* findByNodeId(NodeId node_id) const;
    PeerInfo* findByDestinationHash(
        const uint8_t hash[reticulum::kTruncatedHashSize]);
    const PeerInfo* findByDestinationHash(
        const uint8_t hash[reticulum::kTruncatedHashSize]) const;
    const PeerInfo* findByIdentityHash(
        const uint8_t hash[reticulum::kTruncatedHashSize]) const;

    PeerInfo& upsertDestination(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    PeerInfo& upsertDestinationBounded(const uint8_t destination_hash[reticulum::kTruncatedHashSize], std::size_t maximum);

    template <typename Fn>
    void forEach(Fn&& fn)
    {
        for (auto& peer : peers_)
        {
            fn(peer);
        }
    }

    template <typename Fn>
    void forEach(Fn&& fn) const
    {
        for (const auto& peer : peers_)
        {
            fn(peer);
        }
    }

  private:
    std::vector<PeerInfo> peers_;
};

} // namespace chat::lxmf::runtime
