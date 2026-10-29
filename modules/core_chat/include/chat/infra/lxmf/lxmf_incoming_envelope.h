#pragma once
#include "chat/infra/lxmf/lxmf_wire.h"
namespace chat::lxmf
{
// Supports a complete LXMF envelope and the RNS destination-omitted tail.
bool unpackMessageEnvelopeForDestination(const uint8_t expected_destination[16], const uint8_t* payload,
                                         size_t size, DecodedEnvelope*, bool* embedded_destination = nullptr);
} // namespace chat::lxmf
