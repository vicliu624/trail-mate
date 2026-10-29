#pragma once
#include "chat/infra/reticulum/reticulum_wire.h"

namespace chat::reticulum
{
constexpr size_t kLinkSignallingSize = 3;
void buildLinkSignalling(uint16_t mtu, uint8_t out[kLinkSignallingSize]);
uint16_t linkPayloadMdu(uint16_t mtu);
// Signalling bytes do not belong to the link-ID hash domain. Header-2
// transport metadata is normalized by the canonical packet hash helper.
bool computeLinkRequestId(const uint8_t* raw, size_t len, uint8_t out[kTruncatedHashSize]);
} // namespace chat::reticulum
