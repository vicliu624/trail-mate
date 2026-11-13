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
#include "chat/infra/reticulum/reticulum_wire.h"

namespace chat::reticulum
{
// Public control request: destination hash followed by a fresh random tag.
// The caller owns randomness and transmission; no path state changes here.
constexpr size_t kPathRequestTagSize = 16;
constexpr size_t kPathRequestPacketSize = kPacketHeader1Size + kTruncatedHashSize + kPathRequestTagSize;
bool buildPathRequest(const uint8_t destination[16], const uint8_t tag[16], uint8_t* output, size_t* capacity);
} // namespace chat::reticulum
