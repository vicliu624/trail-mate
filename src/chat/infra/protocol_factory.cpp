/**
 * @file protocol_factory.cpp
 * @brief Factory for creating protocol-specific mesh adapters
 */

#include "protocol_factory.h"
#include "meshtastic/mt_adapter.h"
#include "meshcore/meshcore_adapter.h"

namespace chat {

std::unique_ptr<IMeshAdapter> ProtocolFactory::createAdapter(MeshProtocol protocol,
                                                             TLoRaPagerBoard& board) {
    switch (protocol) {
        case MeshProtocol::MeshCore:
            return std::make_unique<chat::meshcore::MeshCoreAdapter>(board);
        case MeshProtocol::Meshtastic:
        default:
            return std::make_unique<chat::meshtastic::MtAdapter>(board);
    }
}

} // namespace chat
