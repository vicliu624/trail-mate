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
 * @file lxmf_runtime_budget.h
 * @brief Runtime scheduling policy for the embedded Reticulum/LXMF adapter.
 */

#pragma once

#include <cstdint>

namespace chat::lxmf::runtime
{

struct RuntimeBudget
{
    uint8_t live_packet_limit = 1;
    uint8_t deferred_discovery_limit = 0;
    bool allow_public_discovery = false;
    bool allow_persistence = false;
    bool allow_peer_projection = false;
    bool allow_announce_tx = true;
    bool allow_propagation_client = false;
    bool drop_public_discovery = false;
    const char* phase = "screen";
};

struct RuntimeBudgetInput
{
    uint8_t max_ingress_packets_per_poll = 4;
    uint8_t call_ingress_packets_per_poll = 8;
    bool call_realtime_active = false;
    bool nomad_request_active = false;
    bool screen_sleeping = false;
    bool screen_saver_active = false;
};

RuntimeBudget makeRuntimeBudget(const RuntimeBudgetInput& input);

} // namespace chat::lxmf::runtime
