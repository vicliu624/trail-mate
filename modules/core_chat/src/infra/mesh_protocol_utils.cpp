/**
 * @file mesh_protocol_utils.cpp
 * @brief Shared MeshProtocol validation and label helpers
 */

#include "chat/infra/mesh_protocol_utils.h"

namespace chat::infra
{

bool isValidMeshProtocol(MeshProtocol protocol)
{
    switch (protocol)
    {
    case MeshProtocol::Meshtastic:
    case MeshProtocol::MeshCore:
    case MeshProtocol::RNode:
    case MeshProtocol::LXMF:
        return true;
    default:
        return false;
    }
}

bool isValidMeshProtocolValue(uint8_t raw)
{
    return isValidMeshProtocol(static_cast<MeshProtocol>(raw));
}

MeshProtocol meshProtocolFromRaw(uint8_t raw, MeshProtocol fallback)
{
    const MeshProtocol protocol = static_cast<MeshProtocol>(raw);
    return isValidMeshProtocol(protocol) ? protocol : fallback;
}

const char* meshProtocolName(MeshProtocol protocol)
{
    switch (protocol)
    {
    case MeshProtocol::MeshCore:
        return "MeshCore";
    case MeshProtocol::RNode:
        return "RNode";
    case MeshProtocol::LXMF:
        return "LXMF";
    case MeshProtocol::Meshtastic:
    default:
        return "Meshtastic";
    }
}

const char* meshProtocolShortName(MeshProtocol protocol)
{
    switch (protocol)
    {
    case MeshProtocol::MeshCore:
        return "MC";
    case MeshProtocol::RNode:
        return "RN";
    case MeshProtocol::LXMF:
        return "LX";
    case MeshProtocol::Meshtastic:
    default:
        return "MT";
    }
}

const char* meshProtocolSlug(MeshProtocol protocol)
{
    switch (protocol)
    {
    case MeshProtocol::MeshCore:
        return "meshcore";
    case MeshProtocol::RNode:
        return "rnode";
    case MeshProtocol::LXMF:
        return "lxmf";
    case MeshProtocol::Meshtastic:
    default:
        return "meshtastic";
    }
}

} // namespace chat::infra
