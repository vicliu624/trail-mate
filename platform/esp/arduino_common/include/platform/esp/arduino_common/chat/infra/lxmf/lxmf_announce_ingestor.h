/**
 * @file lxmf_announce_ingestor.h
 * @brief Verified announce ingestion owner for embedded LXMF.
 */

#pragma once
#include "chat/infra/lxmf/lxmf_announce_projection.h"
#include "chat/infra/reticulum/announce_verifier.h"

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_destination_registry.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_path_manager.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_interfaces.h"

namespace chat::lxmf::runtime
{

using LocalDestinationResolver =
    bool (*)(void* context,
             const uint8_t destination_hash[reticulum::kTruncatedHashSize],
             LocalDestinationKind* out_kind);

struct AnnounceIngestOptions
{
    uint32_t now_ms = 0;
    uint32_t now_s = 0;
    uint32_t path_ttl_ms = 0;
    uint32_t directory_address_refresh_interval_s = 0;
    std::size_t max_paths = 0;
    uint8_t max_transport_hops = 0;
    reticulum::interfaces::InterfaceId ingress_interface_id =
        reticulum::interfaces::kInvalidInterfaceId;
    reticulum::interfaces::InterfaceKind ingress_interface =
        reticulum::interfaces::InterfaceKind::LoRa;
    void* local_destination_context = nullptr;
    LocalDestinationResolver resolve_local_destination = nullptr;
};


class AnnounceIngestor
{
  public:
    bool ingest(const uint8_t* raw_packet,
                std::size_t raw_len,
                const reticulum::ParsedPacket& packet,
                const LxmfIdentity& local_identity,
                DestinationRegistry& destination_registry,
                PathManager& path_manager,
                const AnnounceIngestOptions& options,
                AnnounceIngestResult* out_result);

  private:
    reticulum::AnnounceVerifier verifier_;
};

} // namespace chat::lxmf::runtime
