/**
 * @file lxmf_announce_ingestor.cpp
 * @brief Verified announce ingestion owner for embedded LXMF.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_announce_ingestor.h"

#include "chat/infra/lxmf/lxmf_wire.h"

#include "platform/esp/common/reticulum_runtime_compat.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace chat::lxmf::runtime
{
namespace
{

void formatHashHex(const uint8_t* hash,
                   std::size_t hash_len,
                   char* out,
                   std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!hash || hash_len == 0 || out_len < ((hash_len * 2U) + 1U))
    {
        std::snprintf(out, out_len, "-");
        return;
    }
    std::size_t used = 0;
    for (std::size_t index = 0; index < hash_len && used + 2U < out_len; ++index)
    {
        used += static_cast<std::size_t>(
            std::snprintf(out + used,
                          out_len - used,
                          "%02X",
                          static_cast<unsigned>(hash[index])));
    }
}

const char* pathAnnounceDecisionLabel(PathAnnounceDecision decision)
{
    switch (decision)
    {
    case PathAnnounceDecision::AcceptNew:
        return "new";
    case PathAnnounceDecision::AcceptNewer:
        return "newer";
    case PathAnnounceDecision::AcceptExpired:
        return "expired";
    case PathAnnounceDecision::RejectReplay:
        return "replay";
    case PathAnnounceDecision::RejectStale:
        return "stale";
    }
    return "unknown";
}

} // namespace

bool AnnounceIngestor::ingest(const uint8_t* raw_packet,
                              std::size_t raw_len,
                              const reticulum::ParsedPacket& packet,
                              const LxmfIdentity& local_identity,
                              DestinationRegistry& destination_registry,
                              PathManager& path_manager,
                              const AnnounceIngestOptions& options,
                              AnnounceIngestResult* out_result)
{
    if (!out_result)
    {
        return false;
    }
    *out_result = AnnounceIngestResult{};
    AnnounceIngestResult& result = *out_result;

    if (!raw_packet || raw_len == 0 || !packet.destination_hash ||
        packet.destination_type != reticulum::DestinationType::Single ||
        (packet.context != static_cast<uint8_t>(reticulum::PacketContext::None) &&
         packet.context !=
             static_cast<uint8_t>(reticulum::PacketContext::PathResponse)))
    {
        result.reason = "invalid_packet";
        return false;
    }

    const char* verification_error = verifier_.verify(packet, result.announce,
                                                      result.identity_hash, result.expected_destination_hash, LxmfIdentity::verify);
    if (verification_error)
    {
        result.reason = verification_error;
        Serial.printf("[LXMF][AnnounceRX] drop reason=%s\n", verification_error);
        return false;
    }

    if (options.resolve_local_destination)
    {
        result.local_destination =
            options.resolve_local_destination(options.local_destination_context,
                                              packet.destination_hash,
                                              &result.local_kind);
    }
    if (result.local_destination)
    {
        result.status = AnnounceIngestResult::Status::Ignored;
        result.reason = "local_destination";
        return true;
    }
    if (packet.hops > options.max_transport_hops)
    {
        Serial.printf("[LXMF][AnnounceRX] ignore reason=max_hops hops=%u\n",
                      static_cast<unsigned>(packet.hops));
        result.status = AnnounceIngestResult::Status::Ignored;
        result.reason = "max_hops";
        return true;
    }

    const PathEntry* existing_path =
        path_manager.findAnyPath(packet.destination_hash);
    result.path_decision = evaluatePathAnnounce(existing_path,
                                                packet.hops,
                                                result.announce.random_hash,
                                                options.now_ms,
                                                options.path_ttl_ms);
    if (!pathAnnounceAccepted(result.path_decision))
    {
        char destination_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        formatHashHex(packet.destination_hash,
                      reticulum::kTruncatedHashSize,
                      destination_hex,
                      sizeof(destination_hex));
        Serial.printf("[LXMF][AnnounceRX] ignore reason=path_%s dest=%s hops=%u previous_hops=%u\n",
                      pathAnnounceDecisionLabel(result.path_decision),
                      destination_hex,
                      static_cast<unsigned>(packet.hops),
                      static_cast<unsigned>(existing_path ? existing_path->hops : 0));
        result.status = AnnounceIngestResult::Status::Ignored;
        result.reason = "path_rejected";
        return true;
    }

    result.path = path_manager.observeAnnouncePath(packet.destination_hash,
                                                   packet.hops,
                                                   result.announce.random_hash,
                                                   options.now_ms,
                                                   options.now_s,
                                                   options.ingress_interface_id,
                                                   packet.transport_id,
                                                   raw_packet,
                                                   raw_len,
                                                   options.max_paths);
    if (!result.path)
    {
        result.reason = "path_observe_failed";
        return false;
    }

    VerifiedAnnounceProjectionOptions projection;
    projection.now_s = options.now_s;
    projection.directory_address_refresh_interval_s = options.directory_address_refresh_interval_s;
    projection.wifi_gateway = options.ingress_interface == reticulum::interfaces::InterfaceKind::WifiGateway;
    detail::projectVerifiedAnnounce(packet, result, destination_registry, projection);

    result.status = AnnounceIngestResult::Status::Accepted;
    result.reason = "accepted";
    (void)local_identity;
    return true;
}

} // namespace chat::lxmf::runtime
