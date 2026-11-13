/*
 * Reticulum License - RNS/LXMF-derived portions.
 * Derived from https://github.com/markqvist/Reticulum
 * Copyright (c) 2016-2026 Mark Qvist (Reticulum-derived portions).
 * Derived from https://github.com/markqvist/LXMF
 * Copyright (c) 2020-2026 Mark Qvist (LXMF-derived portions).
 * Full license: LICENSES/Reticulum-Derivatives.txt.
 * Original notices: LICENSES/Reticulum-RNS.txt and LICENSES/Reticulum-LXMF.txt.
 * Other contributions retain their applicable terms; see LICENSE.
 * Component scope: docs/licensing/reticulum-sources.csv.
 */

/**
 * @file lxmf_adapter_scratch.h
 * @brief Long-lived packet scratch buffers for the embedded LXMF adapter.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_wire.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_interfaces.h"

#include <cstdint>

namespace chat::lxmf::runtime
{

struct AdapterScratchBuffers
{
    reticulum::interfaces::RxPacket rx_packet{};
    uint8_t announce_tx_signed[reticulum::kReticulumMtu] = {};
    uint8_t announce_tx_payload[reticulum::kReticulumMtu] = {};
    uint8_t announce_tx_packet[reticulum::kReticulumMtu] = {};
    uint8_t nomad_page_request_payload[reticulum::kReticulumMtu] = {};
    uint8_t nomad_page_wire_payload[reticulum::kReticulumMtu] = {};
    uint8_t nomad_page_packet[reticulum::kReticulumMtu] = {};
    uint8_t link_request_payload[reticulum::kReticulumMtu] = {};
    uint8_t link_request_packet[reticulum::kReticulumMtu] = {};
    uint8_t link_request_routed[reticulum::kReticulumMtu] = {};
    uint8_t path_request_packet[reticulum::kReticulumMtu] = {};
    uint8_t proof_packet[reticulum::kReticulumMtu] = {};
    uint8_t routed_packet[reticulum::kReticulumMtu] = {};
    uint8_t forward_packet[reticulum::kReticulumMtu] = {};
    uint8_t lxmf_tx_packet[reticulum::kReticulumMtu] = {};
    uint8_t encrypted_payload[reticulum::kReticulumMtu] = {};
    uint8_t link_wire_payload[reticulum::kReticulumMtu] = {};
    uint8_t link_packet[reticulum::kReticulumMtu] = {};
    uint8_t resource_advertisement[reticulum::kReticulumMtu] = {};
    uint8_t resource_hashmap_update[reticulum::kReticulumMtu] = {};
};

} // namespace chat::lxmf::runtime
