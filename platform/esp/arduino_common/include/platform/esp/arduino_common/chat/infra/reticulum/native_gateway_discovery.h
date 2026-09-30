#pragma once

#include "chat/infra/reticulum/reticulum_wire.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/discovery_stamp_verifier.h"

namespace chat::reticulum
{
// Embedded in the PSRAM-owned adapter. No receive-buffer pointers escape offer.
// Only public endpoints are supported until per-interface IFAC is implemented.
class NativeGatewayDiscovery
{
  public:
    struct Endpoint
    {
        char host[64] = {};
        uint16_t port = 0;
        uint8_t network_identity[16] = {};
        uint8_t transport_identity[16] = {};
    };

    static bool matches(const ParsedPacket& packet);
    bool consume(uint32_t now);
    // Caller must have verified the outer announcement signature and binding.
    bool offerVerified(const uint8_t* data, size_t size, const uint8_t identity[16]);
    bool poll(); // True only when a new endpoint has completed stamp validation.
    const Endpoint& latest() const { return latest_; }
    void reset();

  private:
    DiscoveryStampVerifier verifier_;
    Endpoint pending_{};
    Endpoint latest_{};
    uint32_t last_attempt_ = 0;
    bool attempted_ = false;
};
static_assert(sizeof(NativeGatewayDiscovery) <= 768);
} // namespace chat::reticulum
