/*
 * Reticulum License - RNS/LXMF-derived portions.
 * Derived from https://github.com/markqvist/Reticulum
 * Copyright (c) 2016-2026 Mark Qvist (Reticulum-derived portions).
 * Full license: LICENSES/Reticulum-Derivatives.txt.
 * Original notices: LICENSES/Reticulum-RNS.txt and LICENSES/Reticulum-LXMF.txt.
 * Other contributions retain their applicable terms; see LICENSE.
 * Component scope: docs/licensing/reticulum-sources.csv.
 */

#pragma once

#include "platform/esp/arduino_common/chat/infra/reticulum/native_gateway_discovery.h"

namespace chat::reticulum
{
// PSRAM-owned adapter state. Two bounded records retain the last known usable
// public endpoint across interrupted writes. Never stores IFAC credentials.
class GatewayPersistence
{
  public:
    using Endpoint = NativeGatewayDiscovery::Endpoint;
    void poll(uint32_t now, bool stable_connection);
    void installed(const Endpoint& endpoint) { installed_ = endpoint; }
    void clearInstalled() { installed_ = {}; }
    const Endpoint* restored() const { return loaded_ && saved_.port ? &saved_ : nullptr; }

  private:
    static constexpr size_t kRecordSize = 114;
    uint8_t record_[kRecordSize] = {};
    Endpoint saved_{};
    Endpoint installed_{};
    uint32_t session_ = 0;
    uint32_t sequence_ = 0;
    uint32_t last_attempt_ = 0;
    uint32_t last_write_ = 0;
    uint8_t saved_slot_ = 0;
    bool loaded_ = false;
    bool attempted_ = false;
    bool wrote_ = false;
    bool load();
    bool save();
};
static_assert(sizeof(GatewayPersistence) <= 384);
} // namespace chat::reticulum
