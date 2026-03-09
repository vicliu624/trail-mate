/**
 * @file mesh_protocol_utils.h
 * @brief Shared MeshProtocol validation and label helpers
 */

#pragma once

#include "chat/domain/chat_types.h"

namespace chat::infra
{

bool isValidMeshProtocol(MeshProtocol protocol);
bool isValidMeshProtocolValue(uint8_t raw);
MeshProtocol meshProtocolFromRaw(uint8_t raw,
                                 MeshProtocol fallback = MeshProtocol::Meshtastic);
const char* meshProtocolName(MeshProtocol protocol);
const char* meshProtocolShortName(MeshProtocol protocol);
const char* meshProtocolSlug(MeshProtocol protocol);

} // namespace chat::infra
