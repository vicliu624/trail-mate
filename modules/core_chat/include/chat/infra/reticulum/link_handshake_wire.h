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
constexpr size_t kLinkSignallingSize = 3;
void buildLinkSignalling(uint16_t mtu, uint8_t out[kLinkSignallingSize]);
uint16_t linkPayloadMdu(uint16_t mtu);
// Signalling bytes do not belong to the link-ID hash domain. Header-2
// transport metadata is normalized by the canonical packet hash helper.
bool computeLinkRequestId(const uint8_t* raw, size_t len, uint8_t out[kTruncatedHashSize]);
} // namespace chat::reticulum
