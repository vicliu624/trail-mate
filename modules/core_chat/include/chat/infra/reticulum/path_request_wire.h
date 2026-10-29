#pragma once
#include "chat/infra/reticulum/reticulum_wire.h"

namespace chat::reticulum
{
// Public control request: destination hash followed by a fresh random tag.
// The caller owns randomness and transmission; no path state changes here.
constexpr size_t kPathRequestTagSize = 16;
constexpr size_t kPathRequestPacketSize = kPacketHeader1Size + kTruncatedHashSize + kPathRequestTagSize;
bool buildPathRequest(const uint8_t destination[16], const uint8_t tag[16], uint8_t* output, size_t* capacity);
} // namespace chat::reticulum
