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
 * @file lxmf_rx_telemetry.h
 * @brief RX budget and summary telemetry for the embedded LXMF runtime.
 */

#pragma once

#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_interfaces.h"

#include <cstdint>

namespace chat::lxmf::runtime
{

class RawRxTelemetry
{
  public:
    bool consumeDiscoveryBudget(
        reticulum::interfaces::InterfaceKind ingress_interface,
        uint32_t now_ms,
        uint32_t sample_interval_ms);

    bool shouldLogLoraDiscoveryDetail(uint32_t now_ms,
                                      uint32_t interval_ms,
                                      const char* phase);

    bool shouldLogLoraAnnounceIgnore(uint32_t now_ms,
                                     uint32_t interval_ms);

    void noteSummary(bool wifi_skipped,
                     bool duplicate,
                     bool parse_failed,
                     bool deferred,
                     bool deferred_dropped,
                     bool throttled_discovery,
                     uint32_t now_ms,
                     uint32_t summary_interval_ms);

  private:
    uint32_t last_lora_discovery_sample_ms_ = 0;
    uint32_t last_wifi_discovery_sample_ms_ = 0;
    uint32_t last_summary_ms_ = 0;
    uint32_t packets_ = 0;
    uint32_t wifi_skipped_ = 0;
    uint32_t duplicates_ = 0;
    uint32_t parse_failed_ = 0;
    uint32_t deferred_ = 0;
    uint32_t deferred_dropped_ = 0;
    uint32_t throttled_discovery_ = 0;
    uint32_t last_lora_discovery_detail_log_ms_ = 0;
    uint32_t suppressed_lora_discovery_detail_logs_ = 0;
    uint32_t last_lora_announce_ignore_log_ms_ = 0;
    uint32_t suppressed_lora_announce_ignore_logs_ = 0;
};

} // namespace chat::lxmf::runtime
