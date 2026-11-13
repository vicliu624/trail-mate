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

#pragma once
#include "chat/infra/lxmf/lxmf_wire.h"
namespace chat::lxmf
{
// Supports a complete LXMF envelope and the RNS destination-omitted tail.
bool unpackMessageEnvelopeForDestination(const uint8_t expected_destination[16], const uint8_t* payload,
                                         size_t size, DecodedEnvelope*, bool* embedded_destination = nullptr);
} // namespace chat::lxmf
