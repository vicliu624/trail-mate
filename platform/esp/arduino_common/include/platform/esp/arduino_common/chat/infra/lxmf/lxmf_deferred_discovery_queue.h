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
 * @file lxmf_deferred_discovery_queue.h
 * @brief Deferred public discovery packet queue for the embedded LXMF runtime.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_wire.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_interfaces.h"
#include "sys/ringbuf.h"

#include <cstddef>
#include <cstdint>

namespace chat::lxmf::runtime
{

struct DeferredDiscoveryPacket
{
    uint8_t data[reticulum::kReticulumMtu] = {};
    std::size_t len = 0;
    RxMeta rx_meta{};
    reticulum::interfaces::InterfaceKind interface_kind =
        reticulum::interfaces::InterfaceKind::LoRa;
    reticulum::interfaces::InterfaceId interface_id =
        reticulum::interfaces::kInvalidInterfaceId;
    uint8_t packet_hash[reticulum::kFullHashSize] = {};
};

class DeferredDiscoveryQueue
{
  public:
    static constexpr std::size_t kDepth = 8;

    void clear();
    bool contains(const uint8_t packet_hash[reticulum::kFullHashSize]) const;
    bool push(const reticulum::interfaces::RxPacket& packet,
              const uint8_t packet_hash[reticulum::kFullHashSize],
              bool* out_dropped);
    bool pop(reticulum::interfaces::RxPacket* out_packet);

  private:
    sys::RingBuffer<DeferredDiscoveryPacket, kDepth> queue_;
    DeferredDiscoveryPacket scratch_{};
};

} // namespace chat::lxmf::runtime
